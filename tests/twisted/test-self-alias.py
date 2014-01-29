#
# Test alias setting for the self handle
#

from sofiatest import exec_test
import constants as cs

import dbus

def test(q, bus, conn, sip_proxy):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.Get(cs.CONN, 'SelfHandle', dbus_interface=cs.PROPERTIES_IFACE)

    attrs = conn.Contacts.GetContactAttributes([self_handle], [cs.CONN_IFACE_ALIASING])
    default_alias = attrs[self_handle][cs.CONN_IFACE_ALIASING + "/alias"]

    conn.Aliasing.SetAliases({self_handle: 'foo@bar.baz'})

    event = q.expect('dbus-signal', signal='AliasesChanged',
            args=[{self_handle: u'foo@bar.baz'}])

    handle = conn.get_contact_handle_sync('sip:user@somewhere.com')

    attrs = conn.Contacts.GetContactAttributes([self_handle, handle],
	[cs.CONN_IFACE_ALIASING])
    assert cs.CONN_IFACE_ALIASING + "/alias" in attrs[self_handle]
    assert attrs[self_handle][cs.CONN_IFACE_ALIASING + "/alias"] == u'foo@bar.baz'

    conn.Requests.CreateChannel(
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: handle })

    event = q.expect('dbus-signal', signal='NewChannel')
    path, props = event.args

    text_iface = dbus.Interface(bus.get_object(conn.bus_name, path),
            cs.CHANNEL_TYPE_TEXT)

    def send_msg(text_iface, txt):
        msg = [ {'message-type': cs.MT_NORMAL },
                {'content-type': 'text/plain',
                 'content': txt }]

        text_iface.SendMessage(msg, 0)

    send_msg(text_iface, 'Check the display name in From')

    event = q.expect('sip-message')

    self_uri = conn.inspect_contact_sync(self_handle)

    from_header = event.sip_message.headers['from'][0]
    assert from_header.startswith('"foo@bar.baz" <' + self_uri + '>'), from_header

    # Test setting of the default alias
    conn.Aliasing.SetAliases({self_handle: default_alias})
    send_msg(text_iface, 'The display name should be missing in From')
    event = q.expect('sip-message')
    from_header = event.sip_message.headers['from'][0]
    assert from_header.startswith('<' + self_uri + '>'), from_header

    # Test if escaping and whitespace normalization works
    conn.Aliasing.SetAliases({self_handle: 'foo " bar \\\r\n baz\t'})
    send_msg(text_iface, 'Check display name escaping in From')
    event = q.expect('sip-message')
    from_header = event.sip_message.headers['from'][0]
    assert from_header.startswith(r'"foo \" bar \\ baz " <' + self_uri + '>'), from_header

if __name__ == '__main__':
    exec_test(test)
