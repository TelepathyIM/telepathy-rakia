
"""
Test the debug message interface.
"""

import dbus

from servicetest import assertEquals, sync_dbus
from sofiatest import exec_test
import constants as cs
from config import DEBUGGING

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

    channel_path = conn.RequestChannel(
        cs.CHANNEL_TYPE_TEXT, cs.HT_CONTACT, conn.GetSelfHandle(), True)
    q.expect('dbus-signal', signal='NewChannel')

    if DEBUGGING:
        assert len(messages) > 0
    else:
        assertEquals([], messages)

    # Turn signalling off and check we don't get any more messages.

    props_iface.Set(cs.DEBUG_IFACE, 'Enabled', False)
    sync_dbus(bus, q, conn)
    snapshot = list(messages)

    channel = bus.get_object(conn.bus_name, channel_path)
    channel.Close(dbus_interface=cs.CHANNEL)
    q.expect('dbus-signal', signal='Closed')

    conn.RequestChannel(
        cs.CHANNEL_TYPE_TEXT, cs.HT_CONTACT, conn.GetSelfHandle(), True)
    q.expect('dbus-signal', signal='NewChannel')

    assertEquals (snapshot, messages)

if __name__ == '__main__':
    exec_test(test)
