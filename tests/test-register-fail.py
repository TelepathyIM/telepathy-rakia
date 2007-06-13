from servicetest import match
from sofiatest import go

@match('dbus-signal', signal='StatusChanged', args=[1, 1])
def expect_connecting(event, data):
    return True

@match('dbus-signal', signal='StatusChanged', args=[2, 3])
def expect_disconnected(event, data):    
    return True

def register_cb(message, host, port):
    return False

if __name__ == '__main__':
    go(register_cb)


