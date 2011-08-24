"""
Test DTMF dialstring playback and signalling.
"""

from sofiatest import exec_test
from servicetest import (
    wrap_channel,
    assertEquals, assertContains, assertLength, assertSameSets
    )
from voip_test import VoipTestContext
import constants as cs

def request_initial_tones(q, bus, conn, sip_proxy, peer='foo@bar.com'):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    context = VoipTestContext(q, conn, bus, sip_proxy, 'sip:testacc@127.0.0.1', peer)

    path = conn.Requests.CreateChannel({
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
            cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
            cs.TARGET_ID: peer,
            cs.INITIAL_AUDIO: True,
            cs.DTMF_INITIAL_TONES: '1234',
        })[0]

    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'StreamedMedia',
        ['MediaSignalling', 'DTMF'])

    channel_props = chan.Properties.GetAll(cs.CHANNEL)

    assertContains(cs.CHANNEL_IFACE_DTMF, channel_props['Interfaces'])

    dtmf_props = chan.Properties.GetAll(cs.CHANNEL_IFACE_DTMF)

    assertEquals('1234', dtmf_props['InitialTones'])
    assertEquals(False, dtmf_props['CurrentlySendingTones'])

    stream_handler = context.handle_audio_session(chan)

    invite_event = q.expect('sip-invite')

    context.accept(invite_event.sip_message)

    stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    q.expect('dbus-signal', signal='SendingTones', args=['1234'])

    dtmf_props = chan.Properties.GetAll(cs.CHANNEL_IFACE_DTMF)

    assertEquals(True, dtmf_props['CurrentlySendingTones'])

if __name__ == '__main__':
    exec_test(request_initial_tones)
