"""
Test Requestable channels classes
"""

import dbus

from sofiatest import exec_test
from servicetest import (
    wrap_channel, EventPattern, call_async, ProxyWrapper,
    assertEquals, assertContains, assertLength, assertSameSets,
    assertNotEquals
    )
import constants as cs
from voip_test import VoipTestContext

def rccs(q, bus, conn, stream):
    """
    Tests that the connection's RequestableChannelClasses for StreamedMedia are
    sane.
    """
    conn.Connect()


    a = q.expect('dbus-signal', signal='StatusChanged',
                 args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    a = q.expect('dbus-signal', signal='StatusChanged',
                 args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    rccs = conn.Properties.Get(cs.CONN, 'RequestableChannelClasses')

    # Test Channel.Type.StreamedMedia
    media_classes = [ rcc for rcc in rccs
        if rcc[0][cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_CALL ]

    assertLength(2, media_classes)

    for media_class in media_classes:
        fixed, allowed = media_class

        assertEquals(cs.HT_CONTACT, fixed[cs.TARGET_HANDLE_TYPE])
        assert fixed.has_key(cs.CALL_INITIAL_AUDIO) or fixed.has_key(cs.CALL_INITIAL_VIDEO)

        expected_allowed = [
            cs.TARGET_ID, cs.TARGET_HANDLE,
            cs.CALL_INITIAL_VIDEO, cs.CALL_INITIAL_AUDIO,
            cs.CALL_INITIAL_VIDEO_NAME, cs.CALL_INITIAL_AUDIO_NAME,
            cs.CALL_INITIAL_TRANSPORT,
            cs.CALL_INITIAL_TONES,
            ]

        allowed.sort()
        expected_allowed.sort()
        assertSameSets(expected_allowed, allowed)

if __name__ == '__main__':
    
    exec_test(rccs)
