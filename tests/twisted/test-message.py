# -*- coding: utf-8 -*-

from servicetest import tp_name_prefix
from sofiatest import go, exec_test

import twisted.protocols.sip

import dbus

# Test outgoing message handling

def test(q, bus, conn, sip):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    contact = 'sip:user@somewhere.com'
    handle = conn.RequestHandles(1, [contact])[0]
    chan = conn.RequestChannel(tp_name_prefix + '.Channel.Type.Text',
        1, handle, True)

    event = q.expect('dbus-signal', signal='NewChannel')
    assert event.args[1] == tp_name_prefix + '.Channel.Type.Text'

    obj = bus.get_object(conn._named_service, chan)
    iface = dbus.Interface(obj,
        u'org.freedesktop.Telepathy.Channel.Type.Text')

    iface.Send(0, 'Hello')

    event = q.expect('sip-message', uri='sip:user@somewhere.com', body='Hello')
    sip.deliverResponse(sip.responseFromRequest(404, event.sip_message))

    q.expect('dbus-signal', signal='SendError')

    iface.Send(0, 'Hello Again')

    event = q.expect('sip-message', uri='sip:user@somewhere.com',
        body='Hello Again')
    sip.deliverResponse(sip.responseFromRequest(200, event.sip_message))

    prevhdr = event.sip_message.headers

    q.expect('dbus-signal', signal='Sent')
    url = twisted.protocols.sip.parseURL('sip:testacc@127.0.0.1')
    msg = twisted.protocols.sip.Request('MESSAGE', url)
    send_message(sip, prevhdr, 'Hi')

    event = q.expect('dbus-signal', signal='NewChannel')
    assert (event.args[1] == tp_name_prefix + '.Channel.Type.Text' and
        event.args[2] == 1)

    handle = event.args[3]
    name = conn.InspectHandles(1, [handle])[0]

    assert name == 'sip:other.user@somewhere.else.com'

    event = q.expect('dbus-signal', signal='Received')
    assert event.args[5] == 'Hi'

    # Test conversion from an 8-bit encoding.
    # Due to limited set of encodings available in some environments,
    # try with US ASCII and ISO 8859-1.

    send_message(sip, prevhdr, u'straight ASCII'.encode('us-ascii'), 'us-ascii')
    event = q.expect('dbus-signal', signal='Received')
    assert event.args[5] == 'straight ASCII'

    send_message(sip, prevhdr, u'Hyv\xe4!'.encode('iso-8859-1'), 'iso-8859-1')
    event = q.expect('dbus-signal', signal='Received')
    assert event.args[5] == u'Hyv\xe4!'

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2,1])

cseq_num = 1
def send_message(sip, prevhdr, body, encoding=None):
    global cseq_num
    cseq_num += 1
    url = twisted.protocols.sip.parseURL('sip:testacc@127.0.0.1')
    msg = twisted.protocols.sip.Request('MESSAGE', url)
    msg.body = body
    msg.addHeader('from', '<sip:other.user@somewhere.else.com>;tag=XYZ')
    msg.addHeader('to', '<sip:user@127.0.0.1>')
    msg.addHeader('cseq', '%d MESSAGE' % cseq_num)
    msg.addHeader('allow', 'INVITE ACK BYE MESSAGE')
    if encoding is None:
        msg.addHeader('content-type', 'text/plain')
    else:
        msg.addHeader('content-type', 'text/plain; charset=%s' % encoding)
    msg.addHeader('content-length', '%d' % len(msg.body))
    msg.addHeader('call-id', prevhdr['call-id'][0])
    msg.addHeader('via', 'SIP/2.0/UDP 127.0.0.1;branch=ABCXYZ')

    destVia = twisted.protocols.sip.parseViaHeader(prevhdr['via'][0])
    host = destVia.received or destVia.host
    port = destVia.rport or destVia.port or self.PORT
    destAddr = twisted.protocols.sip.URL(host=host, port=port)
    sip.sendMessage(destAddr, msg)
    return True

if __name__ == '__main__':
    exec_test(test)


