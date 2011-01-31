#
# Test alias setting for the self handle
#

from sofiatest import exec_test
from servicetest import tp_name_prefix

import dbus

TEXT_TYPE = tp_name_prefix + '.Channel.Type.Text'
ALIASING_INTERFACE = tp_name_prefix + '.Connection.Interface.Aliasing'
CONTACTS_INTERFACE = tp_name_prefix + '.Connection.Interface.Contacts'

def test(q, bus, conn, sip_proxy):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.GetSelfHandle()

    default_alias = conn.Aliasing.GetAliases([self_handle])[self_handle]

    conn.Aliasing.SetAliases({self_handle: 'foo@bar.baz'})

    event = q.expect('dbus-signal', signal='AliasesChanged',
        args=[[(self_handle, u'foo@bar.baz')]])

    handle = conn.RequestHandles(1, ['sip:user@somewhere.com'])[0]

    assert ALIASING_INTERFACE in \
        conn.Properties.Get(CONTACTS_INTERFACE, "ContactAttributeInterfaces")
    attrs = conn.Contacts.GetContactAttributes([self_handle, handle],
	[ALIASING_INTERFACE], False)
    assert ALIASING_INTERFACE + "/alias" in attrs[self_handle]
    assert attrs[self_handle][ALIASING_INTERFACE + "/alias"] == u'foo@bar.baz'

    conn.RequestChannel(TEXT_TYPE, 1, handle, True)

    event = q.expect('dbus-signal', signal='NewChannel')

    text_iface = dbus.Interface(bus.get_object(conn.bus_name, event.args[0]),
                               TEXT_TYPE)
    text_iface.Send(0, 'Check the display name in From')

    event = q.expect('sip-message')

    self_uri = conn.InspectHandles(1, [self_handle])[0]

    from_header = event.sip_message.headers['from'][0]
    assert from_header.startswith('"foo@bar.baz" <' + self_uri + '>'), from_header

    # Test setting of the default alias
    conn.Aliasing.SetAliases({self_handle: default_alias})
    text_iface.Send(0, 'The display name should be missing in From')
    event = q.expect('sip-message')
    from_header = event.sip_message.headers['from'][0]
    assert from_header.startswith('<' + self_uri + '>'), from_header

    # Test if escaping and whitespace normalization works
    conn.Aliasing.SetAliases({self_handle: 'foo " bar \\\r\n baz\t'})
    text_iface.Send(0, 'Check display name escaping in From')
    event = q.expect('sip-message')
    from_header = event.sip_message.headers['from'][0]
    assert from_header.startswith(r'"foo \" bar \\ baz " <' + self_uri + '>'), from_header

if __name__ == '__main__':
    exec_test(test)
