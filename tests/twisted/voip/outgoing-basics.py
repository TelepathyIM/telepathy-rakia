"""
Test basic outgoing call handling, using CreateChannel and all three variations
of RequestChannel.
"""

import dbus
from twisted.words.xish import xpath

from sofiatest import exec_test
from servicetest import (
    make_channel_proxy, wrap_channel,
    EventPattern, call_async,
    assertEquals, assertContains, assertLength,
    )
import constants as cs
from voip_test import VoipTestContext

# There are various deprecated APIs for requesting calls, documented at
# <http://telepathy.freedesktop.org/wiki/Requesting StreamedMedia channels>.
# These are ordered from most recent to most deprecated.
CREATE = 0 # CreateChannel({TargetHandleType: Contact, TargetHandle: h});
           # RequestStreams()
REQUEST_ANONYMOUS = 1 # RequestChannel(HandleTypeNone, 0); RequestStreams()
REQUEST_ANONYMOUS_AND_ADD = 2 # RequestChannel(HandleTypeNone, 0);
                              # AddMembers([h], ...); RequestStreams(h,...)
REQUEST_NONYMOUS = 3 # RequestChannel(HandleTypeContact, h);
                     # RequestStreams(h, ...)

def create(q, bus, conn, sip_proxy, peer='foo@bar.com'):
    worker(q, bus, conn, sip_proxy, CREATE, peer)

def request_anonymous(q, bus, conn, sip_proxy, peer='publish@foo.com'):
    worker(q, bus, conn, sip_proxy, REQUEST_ANONYMOUS, peer)

def request_anonymous_and_add(q, bus, conn, sip_proxy,
        peer='publish-subscribe@foo.com/Res'):
    worker(q, bus, conn, sip_proxy, REQUEST_ANONYMOUS_AND_ADD, peer)

def request_nonymous(q, bus, conn, sip_proxy, peer='subscribe@foo.com'):
    worker(q, bus, conn, sip_proxy, REQUEST_NONYMOUS, peer)

def worker(q, bus, conn, sip_proxy, variant, peer):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.GetSelfHandle()
    context = VoipTestContext(q, conn, bus, sip_proxy, 'sip:testacc@127.0.0.1', peer)

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(1, [context.peer])[0]

    if variant == REQUEST_NONYMOUS:
        path = conn.RequestChannel(cs.CHANNEL_TYPE_STREAMED_MEDIA,
            cs.HT_CONTACT, remote_handle, True)
    elif variant == CREATE:
        path = conn.Requests.CreateChannel({
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_STREAMED_MEDIA,
            cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
            cs.TARGET_HANDLE: remote_handle,
            })[0]
    else:
        path = conn.RequestChannel(cs.CHANNEL_TYPE_STREAMED_MEDIA,
            cs.HT_NONE, 0, True)

    old_sig, new_sig = q.expect_many(
        EventPattern('dbus-signal', signal='NewChannel',
            predicate=lambda e: cs.CHANNEL_TYPE_CONTACT_LIST not in e.args),
        EventPattern('dbus-signal', signal='NewChannels',
            predicate=lambda e:
                cs.CHANNEL_TYPE_CONTACT_LIST not in e.args[0][0][1].values()),
        )

    if variant == REQUEST_NONYMOUS or variant == CREATE:
        assertEquals( [path, cs.CHANNEL_TYPE_STREAMED_MEDIA, cs.HT_CONTACT,
            remote_handle, True], old_sig.args)
    else:
        assertEquals( [path, cs.CHANNEL_TYPE_STREAMED_MEDIA, cs.HT_NONE, 0,
            True], old_sig.args)

    assertLength(1, new_sig.args)
    assertLength(1, new_sig.args[0])       # one channel
    assertLength(2, new_sig.args[0][0])    # two struct members
    emitted_props = new_sig.args[0][0][1]

    assertEquals(
        cs.CHANNEL_TYPE_STREAMED_MEDIA, emitted_props[cs.CHANNEL_TYPE])

    if variant == REQUEST_NONYMOUS or variant == CREATE:
        assertEquals(remote_handle, emitted_props[cs.TARGET_HANDLE])
        assertEquals(cs.HT_CONTACT, emitted_props[cs.TARGET_HANDLE_TYPE])
        assertEquals(context.peer_id, emitted_props[cs.TARGET_ID])
    else:
        assertEquals(0, emitted_props[cs.TARGET_HANDLE])
        assertEquals(cs.HT_NONE, emitted_props[cs.TARGET_HANDLE_TYPE])
        assertEquals('', emitted_props[cs.TARGET_ID])

    assertEquals(True, emitted_props[cs.REQUESTED])
    assertEquals(self_handle, emitted_props[cs.INITIATOR_HANDLE])
    assertEquals('sip:testacc@127.0.0.1', emitted_props[cs.INITIATOR_ID])

    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'StreamedMedia',
        ['MediaSignalling'])

    # Exercise basic Channel Properties
    channel_props = chan.Properties.GetAll(cs.CHANNEL)

    assertEquals(cs.CHANNEL_TYPE_STREAMED_MEDIA,
        channel_props.get('ChannelType'))

    if variant == REQUEST_NONYMOUS or variant == CREATE:
        assertEquals(remote_handle, channel_props['TargetHandle'])
        assertEquals(cs.HT_CONTACT, channel_props['TargetHandleType'])
        assertEquals(context.peer_id, channel_props['TargetID'])
        assertEquals((cs.HT_CONTACT, remote_handle), chan.GetHandle())
    else:
        assertEquals(0, channel_props['TargetHandle'])
        assertEquals(cs.HT_NONE, channel_props['TargetHandleType'])
        assertEquals('', channel_props['TargetID'])
        assertEquals((cs.HT_NONE, 0), chan.GetHandle())

    for interface in [
            cs.CHANNEL_IFACE_GROUP, cs.CHANNEL_IFACE_MEDIA_SIGNALLING,
            cs.TP_AWKWARD_PROPERTIES, cs.CHANNEL_IFACE_HOLD]:
        assertContains(interface, channel_props['Interfaces'])

    assertEquals(True, channel_props['Requested'])
    assertEquals('sip:testacc@127.0.0.1', channel_props['InitiatorID'])
    assertEquals(conn.GetSelfHandle(), channel_props['InitiatorHandle'])

    # Exercise Group Properties
    group_props = chan.Properties.GetAll(cs.CHANNEL_IFACE_GROUP)

    assertEquals([self_handle], group_props['Members'])
    assertEquals([], group_props['LocalPendingMembers'])

    if variant == REQUEST_NONYMOUS:
        # In this variant, they're meant to be in RP even though we've sent
        # nothing
        assertEquals([remote_handle], group_props['RemotePendingMembers'])
    else:
        # For an anonymous channel, the peer isn't yet known; for a Create-d
        # channel, the peer only appears in RP when we actually send them the
        # session-initiate
        assertEquals([], group_props['RemotePendingMembers'])

        if variant == REQUEST_ANONYMOUS_AND_ADD:
            # but we should be allowed to add the peer.
            chan.Group.AddMembers([remote_handle], 'I love backwards compat')

    base_flags = cs.GF_PROPERTIES | cs.GF_MESSAGE_REMOVE \
               | cs.GF_MESSAGE_REJECT | cs.GF_MESSAGE_RESCIND

    if variant == REQUEST_ANONYMOUS_AND_ADD or variant == REQUEST_ANONYMOUS:
        expected_flags = base_flags | cs.GF_CAN_ADD
    else:
        expected_flags = base_flags
    # FIXME: 32189: group flags are borked.
    #assertEquals(expected_flags, group_props['GroupFlags'])
    assertEquals({}, group_props['HandleOwners'])

    assertEquals([], chan.StreamedMedia.ListStreams())
    streams = chan.StreamedMedia.RequestStreams(remote_handle,
        [cs.MEDIA_STREAM_TYPE_AUDIO])
    assertEquals(streams, chan.StreamedMedia.ListStreams())
    assertLength(1, streams)

    # streams[0][0] is the stream identifier, which in principle we can't
    # make any assertion about (although in practice it's probably 1)

    assertEquals((
        remote_handle,
        cs.MEDIA_STREAM_TYPE_AUDIO,
        # We haven't connected yet
        cs.MEDIA_STREAM_STATE_DISCONNECTED,
        # In Gabble, requested streams start off bidirectional
        cs.MEDIA_STREAM_DIRECTION_BIDIRECTIONAL,
        cs.MEDIA_STREAM_PENDING_REMOTE_SEND),
        streams[0][1:])

    # S-E does state recovery to get the session handler, and calls Ready on it
    session_handlers = chan.MediaSignalling.GetSessionHandlers()
    sh_path, sh_type = session_handlers[0]

    assert sh_type == 'rtp'

    session_handler = make_channel_proxy(conn, sh_path, 'Media.SessionHandler')
    session_handler.Ready()

    e = q.expect('dbus-signal', signal='NewStreamHandler')

    stream_handler = make_channel_proxy(conn, e.args[0], 'Media.StreamHandler')

    stream_handler.NewNativeCandidate("fake", context.get_remote_transports_dbus())
    stream_handler.NativeCandidatesPrepared()
    stream_handler.Ready(context.get_audio_codecs_dbus())
    stream_handler.StreamState(cs.MEDIA_STREAM_STATE_CONNECTED)

    sh_props = stream_handler.GetAll(
        cs.STREAM_HANDLER, dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals('none', sh_props['NATTraversal'])
    assertEquals(True, sh_props['CreatedLocally'])

    if variant == CREATE:
        # When we actually send INVITE to the peer, they should pop up in remote
        # pending.
        invite_event, _ = q.expect_many(
            EventPattern('sip-invite'),
            EventPattern('dbus-signal', signal='MembersChanged',
                args=["", [], [], [], [remote_handle], self_handle,
                      cs.GC_REASON_INVITED]),
            )
    else:
        invite_event = q.expect('sip-invite')

    # Check the Group interface's properties again. Regardless of the call
    # requesting API in use, the state should be the same here:
    group_props = chan.Properties.GetAll(cs.CHANNEL_IFACE_GROUP)
    assertContains('HandleOwners', group_props)
    assertEquals([self_handle], group_props['Members'])
    assertEquals([], group_props['LocalPendingMembers'])
    assertEquals([remote_handle], group_props['RemotePendingMembers'])

    context.check_call_sdp(invite_event.sip_message.body)
    context.accept(invite_event.sip_message)

    ack_cseq = "%s ACK" % invite_event.cseq.split()[0]
    q.expect_many(
        EventPattern('sip-ack', cseq=ack_cseq),
        # Call accepted
        EventPattern('dbus-signal', signal='MembersChanged',
            args=['', [remote_handle], [], [], [], remote_handle,
                  cs.GC_REASON_NONE]),
        )

    # Time passes ... afterwards we close the chan

    chan.Group.RemoveMembers([self_handle], 'closed')


    mc_event, _, bye_event = q.expect_many(
        EventPattern('dbus-signal', signal='MembersChanged'),
        EventPattern('dbus-signal', signal='Close'),
        EventPattern('sip-bye', call_id=context.call_id),
        )
    # Check that we're the actor
    assertEquals(self_handle, mc_event.args[5])
    
    # For completeness, reply to the BYE.
    bye_response = sip_proxy.responseFromRequest(200, bye_event.sip_message)
    sip_proxy.deliverResponse(bye_response)

def rccs(q, bus, conn, stream):
    """
    Tests that the connection's RequestableChannelClasses for StreamedMedia are
    sane.
    """
    conn.Connect()

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    rccs = conn.Properties.Get(cs.CONN_IFACE_REQUESTS,
        'RequestableChannelClasses')

    # Test Channel.Type.StreamedMedia
    media_classes = [ rcc for rcc in rccs
        if rcc[0][cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_STREAMED_MEDIA ]

    assertLength(1, media_classes)

    fixed, allowed = media_classes[0]

    assertEquals(cs.HT_CONTACT, fixed[cs.TARGET_HANDLE_TYPE])

    expected_allowed = [
        cs.TARGET_ID, cs.TARGET_HANDLE,
        cs.INITIAL_VIDEO, cs.INITIAL_AUDIO
    ]

    allowed.sort()
    expected_allowed.sort()
    assertEquals(expected_allowed, allowed)

if __name__ == '__main__':
    
    exec_test(rccs)
    exec_test(create)
    exec_test(request_anonymous)
    exec_test(request_anonymous_and_add)
    exec_test(request_nonymous)
    exec_test(lambda q, b, c, s:
            create(q, b, c, s, peer='foo@gw.bar.com'))
    exec_test(lambda q, b, c, s:
            request_anonymous(q, b, c, s, peer='foo@gw.bar.com'))
    exec_test(lambda q, b, c, s:
            request_anonymous_and_add(q, b, c, s, peer='foo@gw.bar.com'))
    exec_test(lambda q, b, c, s:
            request_nonymous(q, b, c, s, peer='foo@gw.bar.com'))
