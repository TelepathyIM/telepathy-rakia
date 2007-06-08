from servicetest import unwrap
from sofiatest import go

import dbus

def expect_connecting(event, data):
    if event[2] != 'StatusChanged':
        return False

    assert event[3][0] == 1
    return True

def expect_connected(event, data):
    if event[2] != 'StatusChanged':
        return False

    assert event[3][0] == 0

    tests = [
        ('test', 'sip:test@127.0.0.1'),
        ('+123 / 45-67-89', 'sip:+123/45-67-89@127.0.0.1'),
        ('gt:someone@gmail.com', 'gt:someone@gmail.com'),
        ('sip:user:pass@HoSt;something=something', 'sip:user:pass@host;something=something'),
        ('weird\t\n\1\2user', 'sip:weird%09%0A%01%02user@127.0.0.1'),
        ('sip:%61%61%61%61@127.0.0.1', 'sip:aaaa@127.0.0.1'),
        ("-.!~*'()&=+$,?;/\1", "sip:-.!~*'()&=+$,?;/%01@127.0.0.1") ]

    orig = [ x[0] for x in tests ]
    expect = [ x[1] for x in tests ]

    handles = data['conn_iface'].RequestHandles(1, orig)
    names = data['conn_iface'].InspectHandles(1, handles)

    for a,b in zip(expect, map(lambda x: str(unwrap(x)), names)):
        if a != b:
            print a, b
            raise Exception("test failed")

    data['conn'].Disconnect()
    return True

def expect_disconnected(event, data):    
    if event[2] != 'StatusChanged':
        return False
        
    assert event[3][0] == 2
    return True

def register_cb(message, host, port):
    return True

if __name__ == '__main__':
    go(register_cb)


