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

    self_handle = conn.GetSelfHandle()
    self_uri = conn.InspectHandles(1, [self_handle])[0]

    uri = 'sip:foo@bar.com'
    call_async(q, conn, 'RequestHandles', 1, [uri])

    event = q.expect('dbus-return', method='RequestHandles')
    foo_handle = event.value[0][0]

    properties = conn.GetAll(cs.CONN_IFACE_REQUESTS,
            dbus_interface=cs.PROPERTIES_IFACE)
    assertEquals([], properties.get('Channels'))

    assertContains(
            ({ cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
               cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT },
             [ cs.TARGET_HANDLE, cs.TARGET_ID ]), properties['RequestableChannelClasses'])

    requestotron = dbus.Interface(conn, cs.CONN_IFACE_REQUESTS)
    call_async(q, requestotron, 'CreateChannel',
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: foo_handle })

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
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

    assert old_sig.args[0] == ret.value[0]
    assert old_sig.args[1] == cs.CHANNEL_TYPE_TEXT
    assert old_sig.args[2] == cs.HT_CONTACT
    assert old_sig.args[3] == foo_handle
    assert old_sig.args[4] == True      # suppress handler

    assert len(new_sig.args) == 1
    assert len(new_sig.args[0]) == 1        # one channel
    assert len(new_sig.args[0][0]) == 2     # two struct members
    assert new_sig.args[0][0][0] == ret.value[0]
    assert new_sig.args[0][0][1] == ret.value[1]

    properties = conn.GetAll(cs.CONN_IFACE_REQUESTS, dbus_interface=cs.PROPERTIES_IFACE)

    assert new_sig.args[0][0] in properties['Channels'], \
            (new_sig.args[0][0], properties['Channels'])

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

