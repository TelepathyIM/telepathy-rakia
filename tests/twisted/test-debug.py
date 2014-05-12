
"""
Test the debug message interface.
"""

import dbus

from servicetest import assertEquals, sync_dbus, call_async
from sofiatest import exec_test
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    messages = []

    def new_message(timestamp, domain, level, string):
        messages.append((timestamp, domain, level, string))

    debug = bus.get_object(conn.bus_name, cs.DEBUG_PATH)
    debug_iface = dbus.Interface(debug, cs.DEBUG_IFACE)
    debug_iface.connect_to_signal('NewDebugMessage', new_message)
    props_iface = dbus.Interface(debug, cs.PROPERTIES_IFACE)

    assert len(debug_iface.GetMessages()) > 0

    # Turn signalling on and generate some messages.

    assert len(messages) == 0
    assert props_iface.Get(cs.DEBUG_IFACE, 'Enabled') == False
    props_iface.Set(cs.DEBUG_IFACE, 'Enabled', True)

    self_handle = conn.Get(cs.CONN, 'SelfHandle', dbus_interface=cs.PROPERTIES_IFACE)

    channel_path, _ = conn.Requests.CreateChannel(
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
                cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
                cs.TARGET_HANDLE: self_handle })

    q.expect('dbus-signal', signal='NewChannel')

    assert len(messages) > 0

    # Turn signalling off and check we don't get any more messages.

    props_iface.Set(cs.DEBUG_IFACE, 'Enabled', False)
    sync_dbus(bus, q, conn)
    snapshot = list(messages)

    channel = bus.get_object(conn.bus_name, channel_path)
    channel.Close(dbus_interface=cs.CHANNEL)
    q.expect('dbus-signal', signal='Closed')

    channel_path, _ = conn.Requests.CreateChannel(
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
                cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
                cs.TARGET_HANDLE: self_handle })

    q.expect('dbus-signal', signal='NewChannel')

    assertEquals (snapshot, messages)

if __name__ == '__main__':
    exec_test(test)
