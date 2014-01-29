"""
Test SIP registration failure.
"""

import dbus

from sofiatest import exec_test
from servicetest import assertEquals
import constants as cs

def test(q, bus, conn, sip):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[1, 1])
    
    q.expect('sip-register')
    
    nc = q.expect('dbus-signal', signal='NewChannel')
    path, props = nc.args
    assertEquals(cs.CHANNEL_TYPE_SERVER_AUTHENTICATION, props[cs.CHANNEL_TYPE])
    assertEquals(['X-TELEPATHY-PASSWORD'], props[cs.SASL_AVAILABLE_MECHANISMS])
    
    chan = dbus.Interface(bus.get_object(conn._named_service, path), cs.CHANNEL_IFACE_SASL_AUTH)
    
    chan.StartMechanismWithData('X-TELEPATHY-PASSWORD', 'wrong password')
    chan.AcceptSASL()
    
    q.expect('sip-register')
    
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 3])
    return True

if __name__ == '__main__':
    exec_test(test, register_cb=lambda *args: False,
              params={"password": None})

