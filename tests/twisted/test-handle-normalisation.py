from servicetest import assertEquals
from sofiatest import exec_test

import dbus
import constants as cs

def test(q, bus, conn, sip):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    tests = [ ('sip:test@localhost', 'sip:test@localhost'),
        ('test@localhost', 'sip:test@localhost'),
        ('test', 'sip:test@127.0.0.1'),
        ('123456789', 'sip:123456789@127.0.0.1'),
        ('+123 45-67-89', 'sip:+123456789@127.0.0.1'),
        ('(123)\t45.67.89', 'sip:123456789@127.0.0.1'),
        ('gt:someone@gmail.com', 'gt:someone@gmail.com'),
        ('SIP:User:PaSS@HoSt;Param=CaseMattersHere',
         'sip:User:PaSS@host;Param=CaseMattersHere'),
        ('weird\t\n\1\2user', 'sip:weird%09%0A%01%02user@127.0.0.1'),
        ('sip:%61%61%61%61@127.0.0.1', 'sip:aaaa@127.0.0.1'),
        ("-.!~*'()&=+$,?;/\1", "sip:-.!~*'()&=+$,?;/%01@127.0.0.1"),
        ('sip:0x0weir-d0.example.com', 'sip:0x0weir-d0.example.com'),
        ('sip:user@123.45.67.8', 'sip:user@123.45.67.8')]

    # TODO: test the wrong strings too

    orig = [ x[0] for x in tests ]
    expect = [ x[1] for x in tests ]

    handles = conn.get_contact_handles_sync(orig)
    names = conn.inspect_contacts_sync(handles)

    for a,b in zip(expect, names):
        assertEquals(a, b)

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_REQUESTED])

if __name__ == '__main__':
    exec_test(test)
