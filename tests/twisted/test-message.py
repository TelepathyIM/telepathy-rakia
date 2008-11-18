# -*- coding: utf-8 -*-

from servicetest import tp_name_prefix, tp_path_prefix, unwrap
from sofiatest import go, exec_test

import twisted.protocols.sip

import dbus
import uuid

# Test message channels

CHANNEL = tp_name_prefix + '.Channel'
TEXT_TYPE = tp_name_prefix + '.Channel.Type.Text'
DESTROYABLE_IFACE = tp_name_prefix + '.Channel.Interface.Destroyable'

FROM_URL = 'sip:other.user@somewhere.else.com'

def test_new_channel(q, bus, conn, target_uri, initiator_uri, requested):
    event = q.expect('dbus-signal', signal='NewChannel')
    assert (event.args[1] == TEXT_TYPE and event.args[2] == 1)
    handle = event.args[3]
    obj = bus.get_object(conn._named_service, event.args[0])

    initiator_handle = conn.RequestHandles(1, [initiator_uri])[0]

    text_props = obj.GetAll(CHANNEL,
            dbus_interface='org.freedesktop.DBus.Properties')
    assert text_props['ChannelType'] == TEXT_TYPE, text_props
    assert 'Interfaces' in text_props, text_props
    assert text_props['Interfaces'] == [], text_props
    assert 'TargetHandle' in text_props, text_props
    assert text_props['TargetHandle'] == handle, \
            (text_props, handle)
    assert 'TargetHandleType' in text_props, text_props
    assert text_props['TargetHandleType'] == 1, text_props
    assert text_props['TargetID'] == target_uri, text_props
    assert text_props['InitiatorHandle'] == initiator_handle, \
            (text_props, initiator_handle)
    assert text_props['InitiatorID'] == initiator_uri, \
            (text_props, initiator_uri)
    assert 'Requested' in text_props, text_props
    assert text_props['Requested'] == requested, text_props

    if initiator_handle != handle:
        conn.ReleaseHandles(1, [initiator_handle])

    return obj, handle

def test(q, bus, conn, sip):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.GetSelfHandle()
    self_uri = conn.InspectHandles(1, [self_handle])[0]

    contact = 'sip:user@somewhere.com'
    handle = conn.RequestHandles(1, [contact])[0]
    chan = conn.RequestChannel(TEXT_TYPE, 1, handle, True)

    requested_obj, target_handle = test_new_channel (q, bus, conn,
        target_uri=contact,
        initiator_uri=self_uri,
        requested=True)

    assert requested_obj.object_path == chan, requested_obj.object_path
    assert target_handle == handle, (target_handle, handle)

    iface = dbus.Interface(requested_obj, TEXT_TYPE)

    iface.Send(0, 'Hello')

    event = q.expect('sip-message', uri='sip:user@somewhere.com', body='Hello')
    sip.deliverResponse(sip.responseFromRequest(404, event.sip_message))

    q.expect('dbus-signal', signal='SendError')

    iface.Send(0, 'Hello Again')

    event = q.expect('sip-message', uri='sip:user@somewhere.com',
        body='Hello Again')
    sip.deliverResponse(sip.responseFromRequest(200, event.sip_message))

    ua_via = twisted.protocols.sip.parseViaHeader(event.headers['via'][0])

    q.expect('dbus-signal', signal='Sent')

    conn.ReleaseHandles(1, [handle])

    url = twisted.protocols.sip.parseURL(self_uri)
    msg = twisted.protocols.sip.Request('MESSAGE', url)
    send_message(sip, ua_via, 'Hi')

    incoming_obj, handle = test_new_channel (q, bus, conn,
        target_uri=FROM_URL,
        initiator_uri=FROM_URL,
        requested=False)

    iface = dbus.Interface(incoming_obj, TEXT_TYPE)

    name = conn.InspectHandles(1, [handle])[0]
    assert name == FROM_URL

    event = q.expect('dbus-signal', signal='Received')
    assert event.args[5] == 'Hi'

    iface.AcknowledgePendingMessages([event.args[0]])
    event = q.expect('sip-response', code=200)

    # Test conversion from an 8-bit encoding.
    # Due to limited set of encodings available in some environments,
    # try with US ASCII and ISO 8859-1.

    send_message(sip, ua_via, u'straight ASCII'.encode('us-ascii'), encoding='us-ascii')
    event = q.expect('dbus-signal', signal='Received')
    assert event.args[5] == 'straight ASCII'

    iface.AcknowledgePendingMessages([event.args[0]])
    event = q.expect('sip-response', code=200)

    send_message(sip, ua_via, u'Hyv\xe4!'.encode('iso-8859-1'), encoding='iso-8859-1')
    event = q.expect('dbus-signal', signal='Received')
    assert event.args[5] == u'Hyv\xe4!'

    conn.ReleaseHandles(1, [handle])

    iface = dbus.Interface(incoming_obj, DESTROYABLE_IFACE)
    iface.Destroy()
    del iface
    event = q.expect('dbus-signal', signal='Closed')
    del incoming_obj

    # Sending the message to appear on the requested channel
    pending_msgs = []

    send_message(sip, ua_via, 'How are you doing now, old pal?',
                 sender=contact)
    event = q.expect('dbus-signal', signal='Received')
    assert tp_path_prefix + event.path == chan, (event.path, chan)
    assert event.args[5] == 'How are you doing now, old pal?'
    pending_msgs.append(tuple(event.args))

    send_message(sip, ua_via, 'I hope you can receive it',
                 sender=contact)
    event = q.expect('dbus-signal', signal='Received')
    assert event.args[5] == 'I hope you can receive it'
    pending_msgs.append(tuple(event.args))

    # Don't acknowledge the last messages, close the channel so that it's reopened
    dbus.Interface(requested_obj, CHANNEL).Close()
    del requested_obj

    event = q.expect('dbus-signal', signal='Closed')
    assert tp_path_prefix + event.path == chan, (event.path, chan)

    requested_obj, handle = test_new_channel (q, bus, conn,
        target_uri=contact,
        initiator_uri=contact,
        requested=False)

    # Expect Channel_Text_Message_Flag_Resqued to be set
    pending_msgs = [message_with_resqued(msg) for msg in pending_msgs]

    iface = dbus.Interface(requested_obj, TEXT_TYPE)

    pending_res = iface.ListPendingMessages(False)
    assert pending_msgs == pending_res, (pending_msgs, unwrap(pending_res))

    pending_res = iface.ListPendingMessages(True)
    assert pending_msgs == pending_res, (pending_msgs, unwrap(pending_res))

    # TODO: match the CSeq
    q.expect('sip-response', code=200)
    q.expect('sip-response', code=200)

    # There should be no pending messages any more
    pending_res = iface.ListPendingMessages(False)
    assert pending_res == [], pending_res

    del iface

    # Hit also the code path for closing the channel with no pending messages
    dbus.Interface(requested_obj, CHANNEL).Close()
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
def send_message(sip, destVia, body, encoding=None, sender=FROM_URL):
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
    msg.addHeader('call-id', uuid.uuid4().hex)
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
    l[4] = 8
    return tuple(l)

if __name__ == '__main__':
    exec_test(test)


