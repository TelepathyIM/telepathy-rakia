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

    orig = [ 'test', '+123 / 45-67-89', 'gt:someone@gmail.com',
        'sip:user:pass@HoSt;something=something',
        'weird\t\n\1\2user' ]
        
    expected = [ 'sip:test@127.0.0.1', 'sip:+12345-67-89@127.0.0.1', 'gt:someone@gmail.com',
        'sip:user:pass@host;something=something', 'sip:weird%09%0A%01%02user@127.0.0.1' ]

    handles = data['conn_iface'].RequestHandles(1, orig)
    names = data['conn_iface'].InspectHandles(1, handles)

    assert expected == map(lambda x: str(unwrap(x)), names)

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


