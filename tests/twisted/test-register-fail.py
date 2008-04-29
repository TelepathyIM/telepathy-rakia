"""
Test SIP registration failure.
"""

from sofiatest import exec_test

def test(q, bus, conn, sip):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[1, 1])
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 3])
    return True

if __name__ == '__main__':
    exec_test(test, register_cb=lambda *args: False)

