# -*- coding: utf-8 -*-

from servicetest import (unwrap, assertSameSets, assertEquals, assertContains)
from sofiatest import exec_test
import constants as cs

import twisted.protocols.sip

import dbus
import email.utils
import time
import uuid

# Test message channels

FROM_URL = 'sip:other.user@somewhere.else.com'

def test_new_channel(q, bus, conn, target_uri, initiator_uri, requested):
    event = q.expect('dbus-signal', signal='NewChannel')
    path, props = event.args
    assertEquals(cs.CHANNEL_TYPE_TEXT, props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_CONTACT, props[cs.TARGET_HANDLE_TYPE])
    handle = props[cs.TARGET_HANDLE]
    obj = bus.get_object(conn._named_service, path)

    initiator_handle = conn.get_contact_handle_sync(initiator_uri)

    text_props = obj.GetAll(cs.CHANNEL,
            dbus_interface='org.freedesktop.DBus.Properties')
    assert text_props['ChannelType'] == cs.CHANNEL_TYPE_TEXT, text_props
    assert 'Interfaces' in text_props, text_props
    assertContains(cs.CHANNEL_IFACE_DESTROYABLE, text_props['Interfaces'])
    assert 'TargetHandle' in text_props, text_props
    assert text_props['TargetHandle'] == handle, \
            (text_props, handle)
    assert 'TargetEntityType' in text_props, text_props
    assert text_props['TargetEntityType'] == 1, text_props
    assert text_props['TargetID'] == target_uri, text_props
    assert text_props['InitiatorHandle'] == initiator_handle, \
            (text_props, initiator_handle)
    assert text_props['InitiatorID'] == initiator_uri, \
            (text_props, initiator_uri)
    assert 'Requested' in text_props, text_props
    assert text_props['Requested'] == requested, text_props

    return obj, handle

def test(q, bus, conn, sip):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.Get(cs.CONN, 'SelfHandle', dbus_interface=cs.PROPERTIES_IFACE)
    self_uri = conn.inspect_contact_sync(self_handle)

    contact = 'sip:user@somewhere.com'
    handle = conn.get_contact_handle_sync(contact)

    chan, _ = conn.Requests.CreateChannel(
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
                cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
                cs.TARGET_HANDLE: handle })

    requested_obj, target_handle = test_new_channel (q, bus, conn,
        target_uri=contact,
        initiator_uri=self_uri,
        requested=True)

    assert requested_obj.object_path == chan, requested_obj.object_path
    assert target_handle == handle, (target_handle, handle)

    iface = dbus.Interface(requested_obj, cs.CHANNEL_TYPE_TEXT)

    msg = [ {'message-type': cs.MT_NORMAL },
            {'content-type': 'text/plain',
             'content': 'Hello' }]

    iface.SendMessage(msg, 0)

    event = q.expect('sip-message', uri='sip:user@somewhere.com', body='Hello')
    sip.deliverResponse(sip.responseFromRequest(404, event.sip_message))

    event = q.expect('dbus-signal', signal='MessageReceived')
    msg = event.args[0]
    assert msg[0]['message-type'] == cs.MT_DELIVERY_REPORT
    assert msg[0]['delivery-status'] == cs.DELIVERY_STATUS_PERMANENTLY_FAILED

    msg = [ {'message-type': cs.MT_NORMAL },
            {'content-type': 'text/plain',
             'content': 'Hello Again' }]

    iface.SendMessage(msg, 0)

    event = q.expect('sip-message', uri='sip:user@somewhere.com',
        body='Hello Again')
    sip.deliverResponse(sip.responseFromRequest(200, event.sip_message))

    ua_via = twisted.protocols.sip.parseViaHeader(event.headers['via'][0])

    call_id = 'XYZ@localhost'
    send_message(sip, ua_via, 'Hi', call_id=call_id, time=1234567890)

    incoming_obj, handle = test_new_channel (q, bus, conn,
        target_uri=FROM_URL,
        initiator_uri=FROM_URL,
        requested=False)

    iface = dbus.Interface(incoming_obj, cs.CHANNEL_TYPE_TEXT)

    name = conn.inspect_contact_sync(handle)
    assert name == FROM_URL

    event = q.expect('dbus-signal', signal='MessageReceived')
    msg = event.args[0]
    now = time.time()
    assert msg[0]['message-token'] == "%s;cseq=%u" % (call_id, cseq_num)
    assert now - 10 < msg[0]['message-received'] < now + 10
    assert msg[0]['message-sent'] == 1234567890
    assert msg[1]['content-type'] == 'text/plain'
    assert msg[1]['content'] == 'Hi'

    # FIXME: times out for some reason, the response is in fact sent;
    # race condition with the earlier wait for 'dbus-signal'?
    #event = q.expect('sip-response', code=200)

    iface.AcknowledgePendingMessages([msg[0]['pending-message-id']])

    # Test conversion from an 8-bit encoding.
    # Due to limited set of encodings available in some environments,
    # try with US ASCII and ISO 8859-1.

    send_message(sip, ua_via, u'straight ASCII'.encode('us-ascii'), encoding='us-ascii')
    event = q.expect('dbus-signal', signal='MessageReceived')
    assert event.args[0][1]['content'] == 'straight ASCII'

    iface.AcknowledgePendingMessages([event.args[0][0]['pending-message-id']])

    send_message(sip, ua_via, u'Hyv\xe4!'.encode('iso-8859-1'), encoding='iso-8859-1')
    event = q.expect('dbus-signal', signal='MessageReceived')
    assert event.args[0][1]['content'] == u'Hyv\xe4!'

    iface = dbus.Interface(incoming_obj, cs.CHANNEL_IFACE_DESTROYABLE)
    iface.Destroy()
    del iface
    event = q.expect('dbus-signal', signal='Closed')
    del incoming_obj

    # Sending the message to appear on the requested channel
    pending_msgs = []

    send_message(sip, ua_via, 'How are you doing now, old pal?',
                 sender=contact)
    event = q.expect('dbus-signal', signal='MessageReceived', path=chan)
    assert event.args[0][1]['content'] == 'How are you doing now, old pal?'
    pending_msgs.append(event.args[0])

    send_message(sip, ua_via, 'I hope you can receive it',
                 sender=contact)
    event = q.expect('dbus-signal', signal='MessageReceived')
    assert event.args[0][1]['content'] == 'I hope you can receive it'
    pending_msgs.append(event.args[0])

    # Don't acknowledge the last messages, close the channel so that it's reopened
    dbus.Interface(requested_obj, cs.CHANNEL).Close()
    del requested_obj

    event = q.expect('dbus-signal', signal='Closed', path=chan)

    requested_obj, handle = test_new_channel (q, bus, conn,
        target_uri=contact,
        initiator_uri=contact,
        requested=False)

    # Expect Channel_Text_Message_Flag_Resqued to be set
    pending_msgs = [message_with_resqued(msg) for msg in pending_msgs]
    # The first message is the delivery report of the message we failed to
    # send so we'll skip it.

    iface = dbus.Interface(requested_obj, cs.CHANNEL_TYPE_TEXT)

    pending_res = requested_obj.Get(cs.CHANNEL_TYPE_TEXT, 'PendingMessages', dbus_interface=cs.PROPERTIES_IFACE)
    assert pending_msgs == pending_res[1:], (pending_msgs, unwrap(pending_res)[1:])

    # ack them all
    ids = []
    for msg in pending_res:
        ids.append(msg[0]['pending-message-id'])
    iface.AcknowledgePendingMessages(ids)

    # There should be no pending messages any more
    pending_res = requested_obj.Get(cs.CHANNEL_TYPE_TEXT, 'PendingMessages', dbus_interface=cs.PROPERTIES_IFACE)
    assert pending_res == [], pending_res

    del iface

    # Hit also the code path for closing the channel with no pending messages
    dbus.Interface(requested_obj, cs.CHANNEL).Close()
    event = q.expect('dbus-signal', signal='Closed')
    del requested_obj

    # Hit the message zapping path when the connection is disconnected
    send_message(sip, ua_via, 'Will you leave this unacknowledged?')
    test_new_channel (q, bus, conn,
        target_uri=FROM_URL,
        initiator_uri=FROM_URL,
        requested=False)

    conn.Disconnect()

    # Check the last channel with an unacknowledged message 
    event = q.expect('dbus-signal', signal='Closed')

    q.expect('dbus-signal', signal='StatusChanged', args=[2,1])

cseq_num = 1
def send_message(sip, destVia, body,
                 encoding=None, sender=FROM_URL, call_id=None, time=None):
    global cseq_num
    cseq_num += 1
    url = twisted.protocols.sip.parseURL('sip:testacc@127.0.0.1')
    msg = twisted.protocols.sip.Request('MESSAGE', url)
    msg.body = body
    msg.addHeader('from', '<%s>;tag=XYZ' % sender)
    msg.addHeader('to', '<sip:testacc@127.0.0.1>')
    msg.addHeader('cseq', '%d MESSAGE' % cseq_num)
    msg.addHeader('allow', 'INVITE ACK BYE MESSAGE')
    if encoding is None:
        msg.addHeader('content-type', 'text/plain')
    else:
        msg.addHeader('content-type', 'text/plain; charset=%s' % encoding)
    msg.addHeader('content-length', '%d' % len(msg.body))
    msg.addHeader('call-id', call_id or uuid.uuid4().hex)
    if time is not None:
        msg.addHeader('date', email.utils.formatdate(time, False, True))
    via = sip.getVia()
    via.branch = 'z9hG4bKXYZ'
    msg.addHeader('via', via.toString())

    host = destVia.received or destVia.host
    port = destVia.rport or destVia.port
    destAddr = twisted.protocols.sip.URL(host=host, port=port)
    sip.sendMessage(destAddr, msg)
    return True

def message_with_resqued(msg):
    l = list(msg)
    l[0]['rescued'] = True
    return list(l)

if __name__ == '__main__':
    exec_test(test)


