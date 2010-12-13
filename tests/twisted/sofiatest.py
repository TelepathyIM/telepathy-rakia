"""
SofiaSIP testing framework
"""

import servicetest

from twisted.protocols import sip
from twisted.internet import reactor

import os
import sys
import dbus
import dbus.glib

class SipProxy(sip.RegisterProxy):
    registry = sip.InMemoryRegistry("127.0.0.1")

    def __init__(self, *args, **kw):
        sip.RegisterProxy.__init__(self, *args, **kw)

    def register(self, message, host, port):
        if hasattr(self, 'registrar_handler'):
            self.event_func(servicetest.Event('sip-register',
                uri=str(message.uri), headers=message.headers, body=message.body,
                sip_message=message, host=host, port=port))
            if self.registrar_handler(message, host, port):
                sip.RegisterProxy.register(self, message, host, port)
            else:
                self.unauthorized(message, host, port)

    def handle_request(self, message, addr):
        if message.method == 'REGISTER':
            return sip.RegisterProxy.handle_request(self, message, addr)
        if message.method == 'MESSAGE':
            self.event_func(servicetest.Event('sip-message',
                uri=str(message.uri), headers=message.headers, body=message.body,
                sip_message=message))

    def handle_response(self, message, addr):
        self.event_func(servicetest.Event('sip-response',
            code=message.code, headers=message.headers, body=message.body,
            sip_message=message))

def prepare_test(event_func, register_cb, params=None):
    actual_params = {
        'account': 'testacc@127.0.0.1',
        'password': 'testpwd',
        'proxy-host': '127.0.0.1',
        'port': dbus.UInt16(9090),
        'local-ip-address': '127.0.0.1'
    }

    if params is not None:
        for k, v in params.items():
            if v is None:
                actual_params.pop(k, None)
            else:
                actual_params[k] = v

    bus = dbus.SessionBus()
    conn = servicetest.make_connection(bus, event_func,
        'sofiasip', 'sip', actual_params)

    port = int(actual_params['port'])
    sip = SipProxy(host=actual_params['proxy-host'], port=port)
    sip.event_func = event_func
    sip.registrar_handler = register_cb
    reactor.listenUDP(port, sip)
    return bus, conn, sip

def default_register_cb(message, host, port):
    return True

def go(params=None, register_cb=default_register_cb, start=None):
    handler = servicetest.EventTest()
    bus, conn, sip = \
        prepare_test(handler.handle_event, register_cb, params)
    handler.data = {
        'bus': bus,
        'conn': conn,
        'conn_iface': dbus.Interface(conn,
            'org.freedesktop.Telepathy.Connection'),
        'sip': sip}
    handler.data['test'] = handler
    handler.data['sip'].test_handler = handler
    handler.verbose = (os.environ.get('CHECK_TWISTED_VERBOSE', '') != '')
    map(handler.expect, servicetest.load_event_handlers())

    if '-v' in sys.argv:
        handler.verbose = True

    if start is None:
        handler.data['conn'].Connect()
    else:
        start(handler.data)

    reactor.run()

def exec_test(fun, params=None, register_cb=default_register_cb, timeout=None):
    queue = servicetest.IteratingEventQueue(timeout)

    queue.verbose = (os.environ.get('CHECK_TWISTED_VERBOSE', '') != '')
    if '-v' in sys.argv:
        queue.verbose = True

    bus, conn, sip = prepare_test(queue.append,
        params=params, register_cb=register_cb)

    if sys.stdout.isatty():
        def red(s):
            return '\x1b[31m%s\x1b[0m' % s

        def green(s):
            return '\x1b[32m%s\x1b[0m' % s

        patterns = {
            'handled': green,
            'not handled': red,
            }

        class Colourer:
            def __init__(self, fh, patterns):
                self.fh = fh
                self.patterns = patterns

            def write(self, s):
                f = self.patterns.get(s, lambda x: x)
                self.fh.write(f(s))

        sys.stdout = Colourer(sys.stdout, patterns)

    try:
        fun(queue, bus, conn, sip)
    finally:
        try:
            conn.Disconnect()
            # second call destroys object
            conn.Disconnect()
        except dbus.DBusException, e:
            pass

