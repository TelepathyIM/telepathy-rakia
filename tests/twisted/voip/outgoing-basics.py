"""
Test basic outgoing call handling, using CreateChannel and all three variations
of RequestChannel.
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

CREATE = 0 # CreateChannel({TargetHandleType: Contact, TargetHandle: h});
           # RequestStreams()

def create(q, bus, conn, sip_proxy, peer='foo@bar.com'):
    worker(q, bus, conn, sip_proxy, CREATE, peer)

def worker(q, bus, conn, sip_proxy, variant, peer):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.GetSelfHandle()
    context = VoipTestContext(q, conn, bus, sip_proxy, 'sip:testacc@127.0.0.1', peer)

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(1, [context.peer])[0]

    path = conn.Requests.CreateChannel({
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CALL,
            cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
            cs.TARGET_HANDLE: remote_handle,
            cs.INITIAL_AUDIO: True,
            cs.INITIAL_AUDIO_NAME: "audiocontent",
            })[0]

    old_sig, new_sig = q.expect_many(
        EventPattern('dbus-signal', signal='NewChannel',
            predicate=lambda e: cs.CHANNEL_TYPE_CONTACT_LIST not in e.args),
        EventPattern('dbus-signal', signal='NewChannels',
            predicate=lambda e:
                cs.CHANNEL_TYPE_CONTACT_LIST not in e.args[0][0][1].values()),
        )

    assertEquals( [path, cs.CHANNEL_TYPE_CALL, cs.HT_CONTACT,
                   remote_handle, True], old_sig.args)

    assertLength(1, new_sig.args)
    assertLength(1, new_sig.args[0])       # one channel
    assertLength(2, new_sig.args[0][0])    # two struct members
    emitted_props = new_sig.args[0][0][1]

    assertEquals(cs.CHANNEL_TYPE_CALL, emitted_props[cs.CHANNEL_TYPE])

    assertEquals(remote_handle, emitted_props[cs.TARGET_HANDLE])
    assertEquals(cs.HT_CONTACT, emitted_props[cs.TARGET_HANDLE_TYPE])
    assertEquals(context.peer_id, emitted_props[cs.TARGET_ID])

    assertEquals(True, emitted_props[cs.REQUESTED])
    assertEquals(self_handle, emitted_props[cs.INITIATOR_HANDLE])
    assertEquals('sip:testacc@127.0.0.1', emitted_props[cs.INITIATOR_ID])

    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Call1')

    # Exercise basic Channel Properties
    channel_props = chan.Properties.GetAll(cs.CHANNEL)

    assertEquals(cs.CHANNEL_TYPE_CALL,
        channel_props.get('ChannelType'))

    assertEquals(remote_handle, channel_props['TargetHandle'])
    assertEquals(cs.HT_CONTACT, channel_props['TargetHandleType'])
    assertEquals(context.peer_id, channel_props['TargetID'])
    assertEquals((cs.HT_CONTACT, remote_handle), chan.GetHandle())


    for interface in [cs.CHANNEL_IFACE_HOLD, cs.CHANNEL_IFACE_DTMF]:
        assertContains(interface, channel_props['Interfaces'])

    assertEquals(True, channel_props['Requested'])
    assertEquals('sip:testacc@127.0.0.1', channel_props['InitiatorID'])
    assertEquals(conn.GetSelfHandle(), channel_props['InitiatorHandle'])

    call_props = chan.Properties.GetAll(cs.CHANNEL_TYPE_CALL)
    assertEquals(cs.CALL_STATE_PENDING_INITIATOR, call_props['CallState'])
    assertEquals(0, call_props['CallFlags'])
    assertEquals(False, call_props['HardwareStreaming'])
    assertEquals(True, call_props['MutableContents'])
    assertEquals(True, call_props['InitialAudio'])
    assertEquals("audiocontent", call_props['InitialAudioName'])
    assertEquals(False, call_props['InitialVideo'])
    assertEquals("", call_props['InitialVideoName'])
    assertEquals(cs.CALL_STREAM_TRANSPORT_RAW_UDP,
                 call_props['InitialTransport'])
    assertEquals({remote_handle: 0}, call_props['CallMembers'])

    assertLength(1, call_props['Contents'])

    content = bus.get_object (conn.bus_name, call_props['Contents'][0])

    content_props = content.GetAll(cs.CALL_CONTENT)
    assertEquals(cs.CALL_DISPOSITION_INITIAL, content_props['Disposition'])
    assertEquals("audiocontent", content_props['Name'])
    assertLength(1, content_props['Streams'])

    cmedia_props = content.GetAll(cs.CALL_CONTENT_IFACE_MEDIA)
    assertLength(0, cmedia_props['RemoteMediaDescriptions'])
    assertLength(0, cmedia_props['LocalMediaDescriptions'])
    assertNotEquals('/', cmedia_props['MediaDescriptionOffer'][0])
    assertEquals(cs.CALL_CONTENT_PACKETIZATION_RTP,
                 cmedia_props['Packetization'])
    assertEquals(cs.CALL_SENDING_STATE_NONE, cmedia_props['CurrentDTMFState'])
    
    
    stream = bus.get_object (conn.bus_name, content_props['Streams'][0])

    stream = ProxyWrapper (stream, cs.CALL_STREAM,
                           {'Media': cs.CALL_STREAM_IFACE_MEDIA})

    stream_props = stream.Properties.GetAll(cs.CALL_STREAM)
    assertEquals(True, stream_props['CanRequestReceiving'])
    assertEquals(cs.CALL_SENDING_STATE_PENDING_SEND,
                 stream_props['LocalSendingState'])

    smedia_props = stream.Properties.GetAll(cs.CALL_STREAM_IFACE_MEDIA)
    assertEquals(cs.CALL_SENDING_STATE_NONE, smedia_props['SendingState'])
    assertEquals(cs.CALL_SENDING_STATE_NONE, smedia_props['ReceivingState'])
    assertEquals(cs.CALL_STREAM_TRANSPORT_RAW_UDP, smedia_props['Transport'])
    assertEquals([], smedia_props['LocalCandidates'])
    assertEquals(("",""), smedia_props['LocalCredentials'])
    assertEquals([], smedia_props['STUNServers'])
    assertEquals([], smedia_props['RelayInfo'])
    assertEquals(True, smedia_props['HasServerInfo'])
    assertEquals([], smedia_props['Endpoints'])        
    assertEquals(False, smedia_props['ICERestartPending'])

    chan.Call1.Accept()

    q.expect ('dbus-signal', signal='ReceivingStateChanged',
              args=[cs.CALL_STREAM_FLOW_STATE_PENDING_START])

    stream.Media.CompleteReceivingStateChange(cs.CALL_STREAM_FLOW_STATE_STARTED)

    md = bus.get_object (conn.bus_name, cmedia_props['MediaDescriptionOffer'][0])
    md.Accept(context.get_audio_md_dbus(remote_handle))
    q.expect_many(
        EventPattern('dbus-signal', signal='MediaDescriptionOfferDone'),
        EventPattern('dbus-signal', signal='LocalMediaDescriptionChanged'),
        EventPattern('dbus-signal', signal='RemoteMediaDescriptionsChanged'))

    mdo = content.Get(cs.CALL_CONTENT_IFACE_MEDIA, 'MediaDescriptionOffer')
    assertEquals(('/', {}), mdo)
    
    
    stream.Media.AddCandidates(context.get_remote_candidates_dbus())
    stream.Media.FinishInitialCandidates()
    
    q.expect('dbus-signal', signal='LocalCandidatesAdded')

    invite_event = q.expect('sip-invite')

    
    # Send Ringing
    context.pr_respond(invite_event, 180)
    o = q.expect('dbus-signal', signal='CallMembersChanged')
    assertEquals(cs.CALL_MEMBER_FLAG_RINGING, o.args[0][remote_handle])

    context.check_call_sdp(invite_event.sip_message.body)
    context.accept(invite_event.sip_message)

    ack_cseq = "%s ACK" % invite_event.cseq.split()[0]

    o = q.expect_many(
        EventPattern('sip-ack', cseq=ack_cseq),
        # Call accepted
        EventPattern('dbus-signal', signal='NewMediaDescriptionOffer'))

    md = bus.get_object (conn.bus_name, o[1].args[0])
    md.Accept(context.get_audio_md_dbus(remote_handle))

    o = q.expect_many(
        # Call accepted
        EventPattern('dbus-signal', signal='CallStateChanged'),
        EventPattern('dbus-signal', signal='EndpointsChanged'),
        EventPattern('dbus-signal', signal='MediaDescriptionOfferDone'),
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args=[cs.CALL_STREAM_FLOW_STATE_PENDING_START]),
        EventPattern('dbus-signal', signal='LocalMediaDescriptionChanged'),
        EventPattern('dbus-signal', signal='RemoteMediaDescriptionsChanged'))

    mdo = content.Get(cs.CALL_CONTENT_IFACE_MEDIA, 'MediaDescriptionOffer')
    assertEquals(('/', {}), mdo)

    assertEquals(cs.CALL_STATE_ACCEPTED, o[0].args[0])
    assertLength(0, o[1].args[1])

    endpoint = bus.get_object(conn.bus_name, o[1].args[0][0])
    endpoint_props = endpoint.GetAll(cs.CALL_STREAM_ENDPOINT)
    assertEquals(('',''), endpoint_props['RemoteCredentials'])
    assertEquals(context.get_remote_candidates_dbus(),
                 endpoint_props['RemoteCandidates'])
    assertLength(0, endpoint_props['EndpointState'])
    assertEquals(cs.CALL_STREAM_TRANSPORT_RAW_UDP,
                 endpoint_props['Transport'])
    assertEquals(False, endpoint_props['IsICELite'])

    endpoint.SetEndpointState(1,
                              cs.CALL_STREAM_ENDPOINT_STATE_FULLY_CONNECTED,
                              dbus_interface=cs.CALL_STREAM_ENDPOINT)
    endpoint.SetEndpointState(2,
                              cs.CALL_STREAM_ENDPOINT_STATE_FULLY_CONNECTED,
                              dbus_interface=cs.CALL_STREAM_ENDPOINT)

    assertEquals({1: cs.CALL_STREAM_ENDPOINT_STATE_FULLY_CONNECTED,
                  2: cs.CALL_STREAM_ENDPOINT_STATE_FULLY_CONNECTED},
                 endpoint.Get(cs.CALL_STREAM_ENDPOINT, 'EndpointState'))

    o = q.expect('dbus-signal', signal='CallStateChanged')
    assertEquals(cs.CALL_STATE_ACTIVE, o.args[0])

    # Time passes ... afterwards we close the chan

    chan.Call1.Hangup(cs.CALL_SCR_USER_REQUESTED, "", "User hangs up")
    ended_event, bye_event = q.expect_many(
        EventPattern('dbus-signal', signal='CallStateChanged'),
        EventPattern('sip-bye', call_id=context.call_id))
    # Check that we're the actor
    assertEquals(cs.CALL_STATE_ENDED, ended_event.args[0])
    assertEquals(0, ended_event.args[1])
    assertEquals((self_handle, cs.CALL_SCR_USER_REQUESTED, "",
                  "User hangs up"), ended_event.args[2])
    
    # For completeness, reply to the BYE.
    bye_response = sip_proxy.responseFromRequest(200, bye_event.sip_message)
    sip_proxy.deliverResponse(bye_response)

    chan.Close()

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

    rccs = conn.Properties.Get(cs.CONN_IFACE_REQUESTS,
        'RequestableChannelClasses')

    # Test Channel.Type.StreamedMedia
    media_classes = [ rcc for rcc in rccs
        if rcc[0][cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_CALL ]

    assertLength(2, media_classes)

    for media_class in media_classes:
        fixed, allowed = media_class

        assertEquals(cs.HT_CONTACT, fixed[cs.TARGET_HANDLE_TYPE])
        assert fixed.has_key(cs.INITIAL_AUDIO) or fixed.has_key(cs.INITIAL_VIDEO)

        expected_allowed = [
            cs.TARGET_ID, cs.TARGET_HANDLE,
            cs.INITIAL_VIDEO, cs.INITIAL_AUDIO,
            cs.INITIAL_VIDEO_NAME, cs.INITIAL_AUDIO_NAME,
            cs.INITIAL_TRANSPORT,
            cs.DTMF_INITIAL_TONES,
            ]

        allowed.sort()
        expected_allowed.sort()
        assertSameSets(expected_allowed, allowed)

if __name__ == '__main__':
    
    exec_test(rccs)
    exec_test(create)
    exec_test(lambda q, b, c, s:
            create(q, b, c, s, peer='foo@gw.bar.com'))
