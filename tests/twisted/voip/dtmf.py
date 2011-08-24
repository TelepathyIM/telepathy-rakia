"""
Test DTMF dialstring playback and signalling.
"""

from sofiatest import exec_test
from servicetest import (
    wrap_channel, EventPattern,
    assertEquals, assertContains, assertLength, assertSameSets
    )
from voip_test import VoipTestContext
import constants as cs

def setup_dtmf_channel(context, initial_tones=None):
    q = context.q
    bus = context.bus
    conn = context.conn

    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    request_params = {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
            cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
            cs.TARGET_ID: context.peer,
            cs.INITIAL_AUDIO: True,
        }
    if initial_tones:
        request_params[cs.DTMF_INITIAL_TONES] = initial_tones

    path = conn.Requests.CreateChannel(request_params)[0]

    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'StreamedMedia',
        ['MediaSignalling', 'DTMF'])

    channel_props = chan.Properties.GetAll(cs.CHANNEL)

    assertContains(cs.CHANNEL_IFACE_DTMF, channel_props['Interfaces'])

    dtmf_props = chan.Properties.GetAll(cs.CHANNEL_IFACE_DTMF)

    if initial_tones:
        assertEquals(initial_tones, dtmf_props['InitialTones'])
    else:
        assertEquals('', dtmf_props['InitialTones'])
    assertEquals(False, dtmf_props['CurrentlySendingTones'])

    stream_handler = context.handle_audio_session(chan)

    invite_event = q.expect('sip-invite')

    context.accept(invite_event.sip_message)

    q.expect('dbus-signal', signal='SetRemoteCodecs')

    stream_handler.SupportedCodecs(context.get_audio_codecs_dbus())
    stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    return chan

def request_initial_tones(q, bus, conn, sip_proxy, peer='foo@bar.com'):
    context = VoipTestContext(q, conn, bus, sip_proxy, 'sip:testacc@127.0.0.1', peer)

    tones = '123'

    chan = setup_dtmf_channel(context, tones)

    q.expect_many(
            EventPattern('dbus-signal', signal='SendingTones', args=[tones]),
            EventPattern('dbus-signal', signal='StartTelephonyEvent', args=[int(tones[0])]))

    assertEquals(True, chan.Properties.Get(cs.CHANNEL_IFACE_DTMF, 'CurrentlySendingTones'))

    q.expect('dbus-signal', signal='StopTelephonyEvent')

    for i in range(1, len(tones) - 1):
        q.expect('dbus-signal', signal='StartTelephonyEvent', args=[int(tones[i])])
        q.expect('dbus-signal', signal='StopTelephonyEvent')

    q.expect('dbus-signal', signal='StoppedTones')

    assertEquals(False, chan.Properties.Get(cs.CHANNEL_IFACE_DTMF, 'CurrentlySendingTones'))

def multiple_tones(q, bus, conn, sip_proxy, peer='foo@bar.com'):

    context = VoipTestContext(q, conn, bus, sip_proxy, 'sip:testacc@127.0.0.1', peer)

    chan = setup_dtmf_channel(context)

    tones_deferred = '78'
    tones = '56w' + tones_deferred

    chan.DTMF.MultipleTones(tones)

    q.expect_many(
            EventPattern('dbus-signal', signal='SendingTones', args=[tones]),
            EventPattern('dbus-signal', signal='StartTelephonyEvent', args=[int(tones[0])]))

    dtmf_props = chan.Properties.GetAll(cs.CHANNEL_IFACE_DTMF)
    assertEquals(True, dtmf_props['CurrentlySendingTones'])
    assertEquals('', dtmf_props['DeferredTones'])

    q.expect('dbus-signal', signal='StopTelephonyEvent')

    q.expect('dbus-signal', signal='StartTelephonyEvent', args=[int(tones[1])])
    q.expect('dbus-signal', signal='StopTelephonyEvent')

    q.expect('dbus-signal', signal='TonesDeferred', args=[tones_deferred])

    dtmf_props = chan.Properties.GetAll(cs.CHANNEL_IFACE_DTMF)

    assertEquals(False, dtmf_props['CurrentlySendingTones'])
    assertEquals(tones_deferred, dtmf_props['DeferredTones'])

    chan.DTMF.MultipleTones(tones_deferred)

    q.expect_many(
            EventPattern('dbus-signal', signal='SendingTones', args=[tones_deferred]),
            EventPattern('dbus-signal', signal='StartTelephonyEvent', args=[int(tones_deferred[0])]))

    dtmf_props = chan.Properties.GetAll(cs.CHANNEL_IFACE_DTMF)

    assertEquals(True, dtmf_props['CurrentlySendingTones'])
    assertEquals('', dtmf_props['DeferredTones'])

    q.expect('dbus-signal', signal='StopTelephonyEvent')

    for i in range(1, len(tones_deferred) - 1):
        q.expect('dbus-signal', signal='StartTelephonyEvent', args=[int(tones_deferred[i])])
        q.expect('dbus-signal', signal='StopTelephonyEvent')

    q.expect('dbus-signal', signal='StoppedTones')

if __name__ == '__main__':
    exec_test(request_initial_tones)
    exec_test(multiple_tones)
