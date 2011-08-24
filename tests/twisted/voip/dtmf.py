"""
Test DTMF dialstring playback and signalling.
"""

from sofiatest import exec_test
from servicetest import (
    wrap_channel,
    assertEquals, assertContains, assertLength, assertSameSets
    )

import constants as cs

def request_initial_tones(q, bus, conn, sip_proxy, peer='foo@bar.com'):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    path = conn.Requests.CreateChannel({
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
            cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
            cs.TARGET_ID: peer,
            cs.INITIAL_AUDIO: True,
            cs.DTMF_INITIAL_TONES: '1234',
        })[0]

    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'StreamedMedia',
        ['DTMF'])

    channel_props = chan.Properties.GetAll(cs.CHANNEL)

    assertContains(cs.CHANNEL_IFACE_DTMF, channel_props['Interfaces'])

    dtmf_props = chan.Properties.GetAll(cs.CHANNEL_IFACE_DTMF)

    assertEquals('1234', dtmf_props['InitialTones'])

if __name__ == '__main__':
    exec_test(request_initial_tones)
