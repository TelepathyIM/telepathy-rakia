from servicetest import match
from sofiatest import go
import re

@match('dbus-signal', signal='StatusChanged', args=[1, 1])
def expect_connecting(event, data):
    return True

@match('dbus-signal', signal='StatusChanged', args=[0, 1])
def expect_connected(event, data):
    data['conn_iface'].Disconnect()
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 1])
def expect_disconnected(event, data):    
    return True

def register_cb(message, host, port):
    if 'authorization' not in message.headers:
        return False

    r = re.match('.*username="([^"]+)".*', message.headers['authorization'][0])
    assert r is not None

    if r.group(1) == 'authusername':
        return True

    return False

if __name__ == '__main__':
    go(register_cb, params={'auth-user': 'authusername'})

