import calltest
import constants as cs
import re
from servicetest import (
    EventPattern, call_async,
    assertEquals, assertNotEquals, assertContains, assertLength,
    assertDoesNotContain
    )

class AddRemoveContent(calltest.CallTest):

    def __init__(self, *params):
        calltest.CallTest.__init__(self, *params)

    def add_content_succesful(self):
        o = self.chan.Call1.AddContent("new audio", cs.MEDIA_STREAM_TYPE_AUDIO,
                             cs.MEDIA_STREAM_DIRECTION_BIDIRECTIONAL)


        self.q.expect('dbus-signal', signal='ContentAdded',
                      args=[o], path=self.chan.__dbus_object_path__)

        content = self.add_content(o, initial=False, incoming=False)

        self.add_candidates(content.stream)

        md_path, _ = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                                 'MediaDescriptionOffer',
                                 dbus_interface=cs.PROPERTIES_IFACE)
        md = self.bus.get_object (self.conn.bus_name, md_path)
        md.Accept(self.context.get_audio_md_dbus(self.remote_handle),
                dbus_interface=cs.CALL_CONTENT_MEDIA_DESCRIPTION)
        self.q.expect_many(
            EventPattern('dbus-signal', signal='MediaDescriptionOfferDone'),
            EventPattern('dbus-signal', signal='LocalMediaDescriptionChanged'),
            EventPattern('dbus-signal', signal='RemoteMediaDescriptionsChanged'))

        content.stream.Media.CompleteReceivingStateChange(
            cs.CALL_STREAM_FLOW_STATE_STARTED)

        reinvite_event, _ = self.q.expect_many(
            EventPattern('sip-invite'),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                         args=[cs.CALL_STREAM_FLOW_STATE_STARTED],
                         path=content.stream.__dbus_object_path__))

        self.add_to_medias('audio')

        self.context.check_call_sdp(reinvite_event.sip_message.body,
                                   self.medias)

        self.context.accept(reinvite_event.sip_message)

        ack_cseq = "%s ACK" % reinvite_event.cseq.split()[0]

        self.q.expect_many(
            EventPattern('sip-ack', cseq=ack_cseq),
            EventPattern('dbus-signal', signal='SendingStateChanged',
                         args=[cs.CALL_STREAM_FLOW_STATE_PENDING_START],
                         path=content.stream.__dbus_object_path__))


        content.stream.Media.CompleteSendingStateChange(
            cs.CALL_STREAM_FLOW_STATE_STARTED)

        self.q.expect('dbus-signal',  signal='SendingStateChanged',
                      args=[cs.CALL_STREAM_FLOW_STATE_STARTED],
                      path=content.stream.__dbus_object_path__)

        return content
       

    def add_content_rejected(self):
        o = self.chan.Call1.AddContent("new audio", cs.MEDIA_STREAM_TYPE_AUDIO,
                             cs.MEDIA_STREAM_DIRECTION_BIDIRECTIONAL)


        self.q.expect('dbus-signal', signal='ContentAdded',
                      args=[o], path=self.chan.__dbus_object_path__)

        content = self.add_content(o, initial=False, incoming=False)

        self.add_candidates(content.stream)

        md_path, _ = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                                 'MediaDescriptionOffer',
                                 dbus_interface=cs.PROPERTIES_IFACE)
        md = self.bus.get_object (self.conn.bus_name, md_path)
        md.Accept(self.context.get_audio_md_dbus(self.remote_handle),
                dbus_interface=cs.CALL_CONTENT_MEDIA_DESCRIPTION)
        self.q.expect_many(
            EventPattern('dbus-signal', signal='MediaDescriptionOfferDone'),
            EventPattern('dbus-signal', signal='LocalMediaDescriptionChanged'),
            EventPattern('dbus-signal', signal='RemoteMediaDescriptionsChanged'))

        content.stream.Media.CompleteReceivingStateChange(
            cs.CALL_STREAM_FLOW_STATE_STARTED)

        reinvite_event, _ = self.q.expect_many(
            EventPattern('sip-invite'),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                         args=[cs.CALL_STREAM_FLOW_STATE_STARTED],
                         path=content.stream.__dbus_object_path__))


        self.context.check_call_sdp(reinvite_event.sip_message.body,
                                   self.medias + [('audio', None, None)])
        res = re.match('(.*)(m=.*)', reinvite_event.sip_message.body,
                       re.MULTILINE | re.DOTALL)

        body = res.group(1) + 'm=audio 0 RTP/AVP 0'

        self.add_to_medias(None)

        self.context.accept(reinvite_event.sip_message, body)

        ack_cseq = "%s ACK" % reinvite_event.cseq.split()[0]

        o = self.q.expect_many(
            EventPattern('sip-ack', cseq=ack_cseq),
            EventPattern('dbus-signal', signal='ContentRemoved',
                         path=self.chan_path))

        assertEquals(content.__dbus_object_path__, o[1].args[0])
        assertEquals(self.remote_handle, o[1].args[1][0])       

        self.contents.remove(content)
    

    def remove_content_successful(self, x):
        content = self.contents[x]
        self.contents.remove(content)
        content.Remove(dbus_interface=cs.CALL_CONTENT)

        reinvite_event, content_removed = self.q.expect_many(
            EventPattern('sip-invite'),
            EventPattern('dbus-signal', signal='ContentRemoved',
                         path=self.chan.__dbus_object_path__))
        assertEquals(content.__dbus_object_path__, content_removed.args[0])
        assertEquals(self.self_handle, content_removed.args[1][0])
        assertEquals(cs.CALL_STATE_CHANGE_REASON_USER_REQUESTED, content_removed.args[1][1])

        self.medias[x] = (None, None)
        
        self.context.check_call_sdp(reinvite_event.sip_message.body,
                                    self.medias)

        self.context.accept(reinvite_event.sip_message)

        ack_cseq = "%s ACK" % reinvite_event.cseq.split()[0]

        self.q.expect('sip-ack', cseq=ack_cseq)

    def remote_add(self):
        self.add_to_medias('audio')

        self.context.reinvite(self.medias)

        ca = self.q.expect('dbus-signal', signal='ContentAdded')

        content = self.add_content(ca.args[0], incoming=True)

        self.add_candidates(content.stream)

        content.stream.Media.CompleteReceivingStateChange(
            cs.CALL_STREAM_FLOW_STATE_STARTED)

        o = self.q.expect_many(
            EventPattern('sip-response', code=200),
            EventPattern('dbus-signal', signal='ReceivingStateChanged',
                         args=[cs.CALL_STREAM_FLOW_STATE_STARTED],
                         path=content.stream.__dbus_object_path__))

        acc = o[0]
        self.context.check_call_sdp(acc.sip_message.body, self.medias)
        self.context.ack(acc.sip_message)

        return content
       

    def remote_remove(self, index):
        self.medias[index] = (None, None)

        self.context.reinvite(self.medias)

        ca = self.q.expect('dbus-signal', signal='ContentRemoved')

        o = self.q.expect_many(
            EventPattern('sip-response', code=200))


        acc = o[0]
        self.context.check_call_sdp(acc.sip_message.body, self.medias)
        self.context.ack(acc.sip_message)       


    def during_call(self):
        self.add_content_succesful()
        self.add_content_succesful()
        self.add_content_rejected()
        self.add_content_succesful()
        self.remove_content_successful(1)
        self.add_content_succesful()
        self.remote_add()
        self.remote_remove(-1)
        self.add_content_succesful()
        self.remote_remove(-3)


        return calltest.CallTest.during_call(self)




if __name__ == '__main__':
    calltest.run(klass=AddRemoveContent)
