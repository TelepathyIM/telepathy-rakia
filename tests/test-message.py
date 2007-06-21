# -*- coding: utf-8 -*-

from servicetest import unwrap, match, tp_name_prefix
from sofiatest import go

import twisted.protocols.sip
import dbus

# Test outgoing message handling

@match('dbus-signal', signal='StatusChanged', args=[1, 1])
def expect_connecting(event, data):
    return True

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):
    contact = 'sip:user@somewhere.com'

    handle = data['conn_iface'].RequestHandles(1, [contact])[0]

    chan = data['conn_iface'].RequestChannel(tp_name_prefix + '.Channel.Type.Text',
        1, handle, True)
    return True

@match('dbus-signal', signal='NewChannel')
def expect_text_chan(event, data):
    if event.args[1] != tp_name_prefix + '.Channel.Type.Text':
        return False

    bus = data['conn']._bus
    data['text_chan'] = bus.get_object(
        data['conn']._named_service, event.args[0])

    dbus.Interface(data['text_chan'],
        u'org.freedesktop.Telepathy.Channel.Type.Text').Send(0, 'Hello')

    return True

@match('sip-message', uri='sip:user@somewhere.com', body='Hello')
def expect_message(event, data):
    data['sip'].deliverResponse(data['sip'].responseFromRequest(404, event.sip_message))
    return True

@match('dbus-signal', signal='SendError')
def expect_send_error(event, data):
    dbus.Interface(data['text_chan'],
        u'org.freedesktop.Telepathy.Channel.Type.Text').Send(0, 'Hello Again')
    return True

@match('sip-message', uri='sip:user@somewhere.com', body='Hello Again')
def expect_message_again(event, data):
    data['sip'].deliverResponse(data['sip'].responseFromRequest(200, event.sip_message))
    data['prevhdr'] = event.sip_message.headers
    return True

# Test incoming message handling

@match('dbus-signal', signal='Sent')
def expect_sent(event, data):
    url = twisted.protocols.sip.parseURL('sip:testacc@127.0.0.1')
    msg = twisted.protocols.sip.Request('MESSAGE', url)
    send_message(data, 'Hi')
    return True

@match('dbus-signal', signal='NewChannel')
def expect_other_text_chan(event, data):
    if event.args[1] != tp_name_prefix + '.Channel.Type.Text' or event.args[2] != 1:
        return False

    handle = event.args[3]
    name = data['conn_iface'].InspectHandles(1, [handle])[0]

    assert name == 'sip:other.user@somewhere.else.com'
    return True

@match('dbus-signal', signal='Received')
def expect_msg_recv(event, data):
    if event.args[5] != 'Hi':
        return False

    send_message(data, 'ŠĐČĆŽ'.decode('utf-8').encode('windows-1250'), 'windows-1250')
    return True

# Test conversion from different encodings

@match('dbus-signal', signal='Received')
def expect_msg_recv_cp1250(event, data):
    if event.args[5] != 'ŠĐČĆŽ'.decode('utf-8'):
        return False

    send_message(data, 'こんにちは'.decode('utf-8').encode('EUC-JP'), 'EUC-JP')
    return True

@match('dbus-signal', signal='Received')
def expect_msg_recv_euc_jp(event, data):
    if event.args[5] != 'こんにちは'.decode('utf-8'):
        return False

    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):
    return True

def register_cb(message, host, port):
    return True

cseq_num = 1
def send_message(data, body, encoding=None):
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
    msg.addHeader('call-id', data['prevhdr']['call-id'][0])
    msg.addHeader('via', 'SIP/2.0/UDP 127.0.0.1;branch=ABCXYZ')

    destVia = twisted.protocols.sip.parseViaHeader(data['prevhdr']['via'][0])
    host = destVia.received or destVia.host
    port = destVia.rport or destVia.port or self.PORT
    destAddr = twisted.protocols.sip.URL(host=host, port=port)
    data['sip'].sendMessage(destAddr, msg)
    return True

if __name__ == '__main__':
    go(register_cb)


