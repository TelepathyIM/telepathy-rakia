# -*- coding: utf-8 -*-

from servicetest import tp_name_prefix
from sofiatest import go, exec_test

import twisted.protocols.sip

import dbus
import uuid

# Test message channels

FROM_URL = 'sip:other.user@somewhere.else.com'

def test(q, bus, conn, sip):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    TEXT_TYPE = tp_name_prefix + '.Channel.Type.Text'

    self_handle = conn.GetSelfHandle()
    self_uri = conn.InspectHandles(1, [self_handle])[0]

    contact = 'sip:user@somewhere.com'
    handle = conn.RequestHandles(1, [contact])[0]
    chan = conn.RequestChannel(TEXT_TYPE, 1, handle, True)

    event = q.expect('dbus-signal', signal='NewChannel')
    assert event.args[1] == TEXT_TYPE, event.args[1]

    requested_obj = bus.get_object(conn._named_service, chan)
    iface = dbus.Interface(requested_obj, TEXT_TYPE)

    text_props = requested_obj.GetAll(tp_name_prefix + '.Channel',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert text_props['ChannelType'] == TEXT_TYPE, text_props
    assert 'Interfaces' in text_props, text_props
    assert text_props['Interfaces'] == [], text_props
    assert 'TargetHandle' in text_props, text_props
    assert text_props['TargetHandle'] == handle, \
            (text_props, handle)
    assert 'TargetHandleType' in text_props, text_props
    assert text_props['TargetHandleType'] == 1, text_props
    assert text_props['TargetID'] == contact, text_props
    assert text_props['InitiatorHandle'] == self_handle, \
            (text_props, self_handle)
    assert text_props['InitiatorID'] == self_uri, \
            (text_props, self_uri)
    assert text_props['Requested'], text_props

    iface.Send(0, 'Hello')

    event = q.expect('sip-message', uri='sip:user@somewhere.com', body='Hello')
    sip.deliverResponse(sip.responseFromRequest(404, event.sip_message))

    q.expect('dbus-signal', signal='SendError')

    iface.Send(0, 'Hello Again')

    event = q.expect('sip-message', uri='sip:user@somewhere.com',
        body='Hello Again')
    sip.deliverResponse(sip.responseFromRequest(200, event.sip_message))

    prevhdr = event.headers

    q.expect('dbus-signal', signal='Sent')

    conn.ReleaseHandles(1, [handle])

    url = twisted.protocols.sip.parseURL(self_uri)
    msg = twisted.protocols.sip.Request('MESSAGE', url)
    send_message(sip, prevhdr, 'Hi')

    event = q.expect('dbus-signal', signal='NewChannel')
    assert (event.args[1] == TEXT_TYPE and event.args[2] == 1)
    handle = event.args[3]

    # start using the new channel object
    incoming_obj = bus.get_object(conn._named_service, event.args[0])
    iface = dbus.Interface(incoming_obj, TEXT_TYPE)

    text_props = incoming_obj.GetAll(tp_name_prefix + '.Channel',
            dbus_interface='org.freedesktop.DBus.Properties')
    assert text_props['ChannelType'] == TEXT_TYPE, text_props
    assert 'Interfaces' in text_props, text_props
    assert text_props['Interfaces'] == [], text_props
    assert 'TargetHandle' in text_props, text_props
    assert text_props['TargetHandle'] == handle, \
            (text_props, handle)
    assert 'TargetHandleType' in text_props, text_props
    assert text_props['TargetHandleType'] == 1, text_props
    assert text_props['TargetID'] == FROM_URL, text_props
    assert text_props['InitiatorHandle'] == handle, \
            (text_props, self_handle)
    assert text_props['InitiatorID'] == FROM_URL, \
            (text_props, self_uri)
    assert 'Requested' in text_props, text_props
    assert not text_props['Requested'], text_props

    name = conn.InspectHandles(1, [handle])[0]
    assert name == FROM_URL

    event = q.expect('dbus-signal', signal='Received')
    assert event.args[5] == 'Hi'

    iface.AcknowledgePendingMessages([event.args[0]])
    event = q.expect('sip-response', code=200)

    # TODO: close the old channel

    # Test conversion from an 8-bit encoding.
    # Due to limited set of encodings available in some environments,
    # try with US ASCII and ISO 8859-1.

    send_message(sip, prevhdr, u'straight ASCII'.encode('us-ascii'), encoding='us-ascii')
    event = q.expect('dbus-signal', signal='Received')
    assert event.args[5] == 'straight ASCII'

    iface.AcknowledgePendingMessages([event.args[0]])
    event = q.expect('sip-response', code=200)

    send_message(sip, prevhdr, u'Hyv\xe4!'.encode('iso-8859-1'), encoding='iso-8859-1')
    event = q.expect('dbus-signal', signal='Received')
    assert event.args[5] == u'Hyv\xe4!'

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2,1])

cseq_num = 1
def send_message(sip, prevhdr, body, encoding=None, sender=FROM_URL):
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

    destVia = twisted.protocols.sip.parseViaHeader(prevhdr['via'][0])
    host = destVia.received or destVia.host
    port = destVia.rport or destVia.port
    destAddr = twisted.protocols.sip.URL(host=host, port=port)
    sip.sendMessage(destAddr, msg)
    return True

if __name__ == '__main__':
    exec_test(test)


