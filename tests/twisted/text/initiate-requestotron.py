"""
Test text channel initiated by me, using Requests.
"""

import dbus

from sofiatest import exec_test
from servicetest import call_async, EventPattern, assertEquals, assertContains
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.Get(cs.CONN, 'SelfHandle', dbus_interface=cs.PROPERTIES_IFACE)
    self_uri = conn.inspect_contact_sync(self_handle)

    uri = 'sip:foo@bar.com'
    foo_handle = conn.get_contact_handle_sync(uri)

    properties = conn.GetAll(cs.CONN_IFACE_REQUESTS,
            dbus_interface=cs.PROPERTIES_IFACE)
    assertEquals([], properties.get('Channels'))

    properties = conn.GetAll(cs.CONN, dbus_interface=cs.PROPERTIES_IFACE)
    assertContains(
            ({ cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
               cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT },
             [ cs.TARGET_HANDLE, cs.TARGET_ID ]), properties['RequestableChannelClasses'])

    requestotron = dbus.Interface(conn, cs.CONN_IFACE_REQUESTS)
    call_async(q, requestotron, 'CreateChannel',
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: foo_handle })

    ret, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        )

    assert len(ret.value) == 2
    emitted_props = ret.value[1]
    assertEquals(cs.CHANNEL_TYPE_TEXT, emitted_props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_CONTACT, emitted_props[cs.TARGET_HANDLE_TYPE])
    assertEquals(foo_handle, emitted_props[cs.TARGET_HANDLE])
    assertEquals(uri, emitted_props[cs.TARGET_ID])
    assertEquals(True, emitted_props[cs.REQUESTED])
    assertEquals(self_handle, emitted_props[cs.INITIATOR_HANDLE])
    assertEquals(self_uri, emitted_props[cs.INITIATOR_ID])

    assert new_sig.args[0] == ret.value[0]
    assert new_sig.args[1] == ret.value[1]

    properties = conn.GetAll(cs.CONN_IFACE_REQUESTS, dbus_interface=cs.PROPERTIES_IFACE)

    assert (new_sig.args[0], new_sig.args[1]) in properties['Channels'], \
            (new_sig.args, properties['Channels'])

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

