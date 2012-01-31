"""
Test incoming call handling.
"""

import dbus

from sofiatest import exec_test
from servicetest import (
    make_channel_proxy, wrap_channel,
    EventPattern, call_async, ProxyWrapper,
    assertEquals, assertNotEquals, assertContains, assertLength,
    )
import constants as cs
from voip_test import VoipTestContext

def test(q, bus, conn, sip_proxy, peer='foo@bar.com'):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    context = VoipTestContext(q, conn, bus, sip_proxy, 'sip:testacc@127.0.0.1', peer)

    self_handle = conn.GetSelfHandle()
    remote_handle = conn.RequestHandles(cs.HT_CONTACT, [context.peer])[0]

    # Try making a call to ourself. StreamedMedia should refuse this because
    # the API doesn't support it.
    context.incoming_call_from_self()
    acc = q.expect('sip-response', code=501)

    # Remote end calls us
    context.incoming_call()

    nc = q.expect('dbus-signal', signal='NewChannels')

    path, props = nc.args[0][0]
    assertEquals(cs.CHANNEL_TYPE_CALL, props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_CONTACT, props[cs.CHANNEL + '.TargetHandleType'])
    assertEquals(remote_handle, props[cs.CHANNEL + '.TargetHandle'])
    assertEquals(remote_handle, props[cs.CHANNEL + '.InitiatorHandle'])
    assertEquals(True, props[cs.CHANNEL_TYPE_CALL + '.InitialAudio'])
    assertEquals(True, props[cs.CHANNEL_TYPE_CALL + '.MutableContents'])
    assertEquals(False, props[cs.CHANNEL_TYPE_CALL + '.HardwareStreaming'])

    chan = wrap_channel(bus.get_object(conn.bus_name, path), 'Call1')

    call_props = chan.Properties.GetAll(cs.CHANNEL_TYPE_CALL)
    assertEquals(cs.CALL_STATE_INITIALISING, call_props['CallState'])
    assertEquals(0, call_props['CallFlags'])
    assertEquals(False, call_props['HardwareStreaming'])
    assertEquals(True, call_props['MutableContents'])
    assertEquals(True, call_props['InitialAudio'])
    assertEquals("initial_audio_1", call_props['InitialAudioName'])
    assertEquals(False, call_props['InitialVideo'])
    assertEquals("", call_props['InitialVideoName'])
    assertEquals(cs.CALL_STREAM_TRANSPORT_RAW_UDP,
                 call_props['InitialTransport'])
    assertEquals({remote_handle: 0}, call_props['CallMembers'])


    assertLength(1, call_props['Contents'])

    content = bus.get_object (conn.bus_name, call_props['Contents'][0])

    content_props = content.GetAll(cs.CALL_CONTENT)
    assertEquals(cs.CALL_DISPOSITION_INITIAL, content_props['Disposition'])
    assertEquals("initial_audio_1", content_props['Name'])
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

    md = bus.get_object (conn.bus_name,
                         cmedia_props['MediaDescriptionOffer'][0])
    md.Accept(context.get_audio_md_dbus(remote_handle))

    o = q.expect_many(
        EventPattern('dbus-signal', signal='EndpointsChanged'),
        EventPattern('dbus-signal', signal='MediaDescriptionOfferDone'),
        EventPattern('dbus-signal', signal='LocalMediaDescriptionChanged'),
        EventPattern('dbus-signal', signal='RemoteMediaDescriptionsChanged'))

    assertLength(1, o[0].args[0])
    assertEquals([], o[0].args[1])

    endpoint = bus.get_object(conn.bus_name, o[0].args[0][0])
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
    assertEquals(cs.CALL_STATE_INITIALISED, o.args[0])

    chan.Call1.SetQueued()

    o = q.expect_many(
        EventPattern('sip-response', call_id=context.call_id, code=182),
        EventPattern('dbus-signal', signal='CallStateChanged'))
    assertEquals(cs.CALL_STATE_INITIALISED, o[1].args[0])
    assertEquals(cs.CALL_FLAG_LOCALLY_QUEUED, o[1].args[1])

    chan.Call1.SetRinging()

    o = q.expect_many(
        EventPattern('sip-response', call_id=context.call_id, code=180),
        EventPattern('dbus-signal', signal='CallStateChanged'))
    assertEquals(cs.CALL_STATE_INITIALISED, o[1].args[0])
    assertEquals(cs.CALL_FLAG_LOCALLY_RINGING, o[1].args[1])


    chan.Call1.Accept()

    o = q.expect_many(
        EventPattern('dbus-signal', signal='CallStateChanged'),
        EventPattern('dbus-signal', signal='CallStateChanged'),
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args=[cs.CALL_STREAM_FLOW_STATE_PENDING_START]),
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args=[cs.CALL_STREAM_FLOW_STATE_PENDING_START]))
    assertEquals(cs.CALL_STATE_ACCEPTED, o[0].args[0])
    assertEquals(cs.CALL_STATE_ACTIVE, o[1].args[0])


    stream.Media.CompleteReceivingStateChange(
        cs.CALL_STREAM_FLOW_STATE_STARTED)
    stream.Media.CompleteSendingStateChange(cs.CALL_STREAM_FLOW_STATE_STARTED)

    o = q.expect_many(
        EventPattern('dbus-signal', signal='ReceivingStateChanged',
                     args=[cs.CALL_STREAM_FLOW_STATE_STARTED]),
        EventPattern('dbus-signal', signal='SendingStateChanged',
                     args=[cs.CALL_STREAM_FLOW_STATE_STARTED]),
        EventPattern('dbus-signal', signal='LocalSendingStateChanged'))
    assertEquals(cs.CALL_SENDING_STATE_SENDING, o[2].args[0])

    stream.Media.AddCandidates(context.get_remote_candidates_dbus())
    stream.Media.FinishInitialCandidates()

    acc = q.expect('sip-response', call_id=context.call_id, code=200)

    context.check_call_sdp(acc.sip_message.body)
    context.ack(acc.sip_message)
    

    # Connected! Blah, blah, ...

    # 'Nuff said
    bye_msg = context.terminate()
    
    o = q.expect_many(
        EventPattern('dbus-signal', signal='CallStateChanged', path=path),
        EventPattern('sip-response', cseq=bye_msg.headers['cseq'][0]))
    assertEquals(cs.CALL_STATE_ENDED, o[0].args[0])

if __name__ == '__main__':
    exec_test(test)
    exec_test(lambda q, bus, conn, stream:
                  test(q, bus, conn, stream, 'foo@sip.bar.com'))
