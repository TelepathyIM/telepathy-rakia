"""
Test incoming call handling.
"""

import dbus

from sofiatest import exec_test
from servicetest import (
    make_channel_proxy, wrap_channel,
    EventPattern, call_async,
    assertEquals, assertContains, assertLength,
    )
import constants as cs
from voip_test import VoipTestContext

def test(q, bus, conn, sip_proxy, peer='foo@bar.com'):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    context = VoipTestContext(q, conn, bus, sip_proxy, 'sip:testacc@127.0.0.1', peer)

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(cs.HT_CONTACT, [context.peer])[0]

    # Remote end calls us
    context.incoming_call()

    nc, e = q.expect_many(
        EventPattern('dbus-signal', signal='NewChannels'),
        EventPattern('dbus-signal', signal='NewSessionHandler'),
        )[0:2]

    path, props = nc.args[0][0]
    ct = props[cs.CHANNEL_TYPE]
    ht = props[cs.CHANNEL + '.TargetHandleType']
    h = props[cs.CHANNEL + '.TargetHandle']

    assert ct == cs.CHANNEL_TYPE_STREAMED_MEDIA, ct
    assert ht == cs.HT_CONTACT, ht
    assert h == remote_handle, h

    media_chan = make_channel_proxy(conn, path, 'Channel.Interface.Group')
    media_iface = make_channel_proxy(conn, path, 'Channel.Type.StreamedMedia')

    # S-E was notified about new session handler, and calls Ready on it
    assert e.args[1] == 'rtp'
    session_handler = make_channel_proxy(conn, e.args[0], 'Media.SessionHandler')
    session_handler.Ready()

    nsh_event = q.expect('dbus-signal', signal='NewStreamHandler')

    # S-E gets notified about a newly-created stream
    stream_handler = make_channel_proxy(conn, nsh_event.args[0],
        'Media.StreamHandler')

    streams = media_iface.ListStreams()
    assertLength(1, streams)

    stream_id, stream_handle, stream_type, _, stream_direction, pending_flags =\
        streams[0]
    assertEquals(remote_handle, stream_handle)
    assertEquals(cs.MEDIA_STREAM_TYPE_AUDIO, stream_type)
    assertEquals(cs.MEDIA_STREAM_DIRECTION_RECEIVE, stream_direction)
    assertEquals(cs.MEDIA_STREAM_PENDING_LOCAL_SEND, pending_flags)

    # Exercise channel properties
    channel_props = media_chan.GetAll(
        cs.CHANNEL, dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals(remote_handle, channel_props['TargetHandle'])
    assertEquals(cs.HT_CONTACT, channel_props['TargetHandleType'])
    assertEquals((cs.HT_CONTACT, remote_handle),
            media_chan.GetHandle(dbus_interface=cs.CHANNEL))
    assertEquals(context.peer_id, channel_props['TargetID'])
    assertEquals(context.peer_id, channel_props['InitiatorID'])
    assertEquals(remote_handle, channel_props['InitiatorHandle'])
    assertEquals(False, channel_props['Requested'])

    group_props = media_chan.GetAll(
        cs.CHANNEL_IFACE_GROUP, dbus_interface=dbus.PROPERTIES_IFACE)

    assert group_props['SelfHandle'] == self_handle, \
        (group_props['SelfHandle'], self_handle)

    flags = group_props['GroupFlags']
    assert flags & cs.GF_PROPERTIES, flags
    # Changing members in any way other than adding or removing yourself is
    # meaningless for incoming calls, and the flags need not be sent to change
    # your own membership.
    assert not flags & cs.GF_CAN_ADD, flags
    assert not flags & cs.GF_CAN_REMOVE, flags
    assert not flags & cs.GF_CAN_RESCIND, flags

    assert group_props['Members'] == [remote_handle], group_props['Members']
    assert group_props['RemotePendingMembers'] == [], \
        group_props['RemotePendingMembers']
    # We're local pending because remote_handle invited us.
    assert group_props['LocalPendingMembers'] == \
        [(self_handle, remote_handle, cs.GC_REASON_INVITED, '')], \
        unwrap(group_props['LocalPendingMembers'])

    streams = media_chan.ListStreams(
            dbus_interface=cs.CHANNEL_TYPE_STREAMED_MEDIA)
    assert len(streams) == 1, streams
    assert len(streams[0]) == 6, streams[0]
    # streams[0][0] is the stream identifier, which in principle we can't
    # make any assertion about (although in practice it's probably 1)
    assert streams[0][1] == remote_handle, (streams[0], remote_handle)
    assert streams[0][2] == cs.MEDIA_STREAM_TYPE_AUDIO, streams[0]
    # We haven't connected yet
    assert streams[0][3] == cs.MEDIA_STREAM_STATE_DISCONNECTED, streams[0]
    # In Gabble, incoming streams start off with remote send enabled, and
    # local send requested
    assert streams[0][4] == cs.MEDIA_STREAM_DIRECTION_RECEIVE, streams[0]
    assert streams[0][5] == cs.MEDIA_STREAM_PENDING_LOCAL_SEND, streams[0]

    # Connectivity checks happen before we have accepted the call
    stream_handler.NewNativeCandidate("fake", context.get_remote_transports_dbus())
    stream_handler.NativeCandidatesPrepared()
    stream_handler.Ready(context.get_audio_codecs_dbus())
    stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)
    stream_handler.SupportedCodecs(context.get_audio_codecs_dbus())

    # At last, accept the call
    media_chan.AddMembers([self_handle], 'accepted')

    # Call is accepted, we become a member, and the stream that was pending
    # local send is now sending.
    memb, acc, _, _, _ = q.expect_many(
        EventPattern('dbus-signal', signal='MembersChanged',
            args=[u'', [self_handle], [], [], [], self_handle,
                  cs.GC_REASON_NONE]),
        EventPattern('sip-response', call_id=context.call_id, code=200),
        EventPattern('dbus-signal', signal='SetStreamSending', args=[True]),
        EventPattern('dbus-signal', signal='SetStreamPlaying', args=[True]),
        EventPattern('dbus-signal', signal='StreamDirectionChanged',
            args=[stream_id, cs.MEDIA_STREAM_DIRECTION_BIDIRECTIONAL, 0]),
        )
    
    context.check_call_sdp(acc.sip_message.body)

    context.ack(acc.sip_message)

    # we are now both in members
    members = media_chan.GetMembers()
    assert set(members) == set([self_handle, remote_handle]), members

    # Connected! Blah, blah, ...

    # 'Nuff said
    bye_msg = context.terminate()
    
    q.expect_many(EventPattern('dbus-signal', signal='Closed', path=path),
                  EventPattern('sip-response', cseq=bye_msg.headers['cseq'][0]))

if __name__ == '__main__':
    exec_test(test)
    exec_test(lambda q, bus, conn, stream:
            test(q, bus, conn, stream, 'foo@sip.bar.com'))
