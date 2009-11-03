#
# Test alias setting for the self handle
#

from sofiatest import exec_test
from servicetest import tp_name_prefix

import dbus

TEXT_TYPE = tp_name_prefix + '.Channel.Type.Text'

def test(q, bus, conn, sip_proxy):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.GetSelfHandle()

    conn.Aliasing.SetAliases({self_handle: 'foo@bar.baz'})

    event = q.expect('dbus-signal', signal='AliasesChanged',
        args=[[(self_handle, u'foo@bar.baz')]])

    handle = conn.RequestHandles(1, ['sip:user@somewhere.com'])[0]
    conn.RequestChannel(TEXT_TYPE, 1, handle, True)

    event = q.expect('dbus-signal', signal='NewChannel')

    text_iface = dbus.Interface(bus.get_object(conn.bus_name, event.args[0]),
                               TEXT_TYPE)
    text_iface.Send(0, 'Hello')

    event = q.expect('sip-message')

    self_uri = conn.InspectHandles(1, [self_handle])[0]

    from_header = event.sip_message.headers['from'][0]
    assert from_header.startswith('"foo@bar.baz" <' + self_uri + '>'), from_header

    # test if escaping and whitespace normalization works
    conn.Aliasing.SetAliases({self_handle: 'foo " bar \\\r\n baz\t'})
    text_iface.Send(0, 'Hello again')
    event = q.expect('sip-message')
    from_header = event.sip_message.headers['from'][0]
    assert from_header.startswith(r'"foo \" bar \\ baz " <' + self_uri + '>'), from_header

if __name__ == '__main__':
    exec_test(test)
