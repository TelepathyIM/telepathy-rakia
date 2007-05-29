from sofiatest import go

def expect_connecting(event, data):
    if event[2] != 'StatusChanged':
        return False

    assert event[3][0] == 1
    return True

def expect_disconnected(event, data):    
    if event[2] != 'StatusChanged':
        return False
        
    assert event[3][0] == 2
    return True

def register_cb(message, host, port):
    return False

if __name__ == '__main__':
    go(register_cb)


