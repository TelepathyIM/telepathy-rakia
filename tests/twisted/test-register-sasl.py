"""
Test SIP registration failure.
"""

import dbus

from sofiatest import exec_test

def test(q, bus, conn, sip):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[1, 1])
    
    q.expect('sip-register')
    
    nc = q.expect('dbus-signal', signal='NewChannels')
    (((path, props),),) = nc.args
    assert props['org.freedesktop.Telepathy.Channel.ChannelType'] == \
            'org.freedesktop.Telepathy.Channel.Type.ServerAuthentication'
    assert props['org.freedesktop.Telepathy.Channel.Interface.SASLAuthentication.AvailableMechanisms'] == \
            ['X-TELEPATHY-PASSWORD']
    
    chan = dbus.Interface(bus.get_object(conn._named_service, path),
                          "org.freedesktop.Telepathy.Channel.Interface.SASLAuthentication")
    
    chan.StartMechanismWithData('X-TELEPATHY-PASSWORD', 'wrong password')
    chan.AcceptSASL()
    
    q.expect('sip-register')
    
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 3])
    return True

if __name__ == '__main__':
    exec_test(test, register_cb=lambda *args: False,
              params={"password": None})

