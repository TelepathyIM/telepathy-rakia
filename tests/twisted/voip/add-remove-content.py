import calltest
import constants as cs
from servicetest import (
    EventPattern, call_async,
    assertEquals, assertNotEquals, assertContains, assertLength,
    assertDoesNotContain
    )

class AddRemoveContent(calltest.CallTest):

    def __init__(self, *params):
        calltest.CallTest.__init__(self, *params)



    def during_call(self):
        content = self.contents[0]

        o = self.chan.Call1.AddContent("new audio", cs.MEDIA_STREAM_TYPE_AUDIO,
                             cs.MEDIA_STREAM_DIRECTION_BIDIRECTIONAL)


        self.q.expect('dbus-signal', signal='ContentAdded',
                      args=[o], path=self.chan.__dbus_object_path__)

        content = self.add_content(o, initial=False, incoming=False)

        self.add_candidates(content.stream)

        md_path, _ = content.Get(cs.CALL_CONTENT_IFACE_MEDIA,
                                 'MediaDescriptionOffer')
        md = self.bus.get_object (self.conn.bus_name, md_path)
        md.Accept(self.context.get_audio_md_dbus(self.remote_handle))
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

        medias = self.medias + [('audio', None)]
        
        self.context.check_call_sdp(reinvite_event.sip_message.body,
                                   medias)
        self.context.accept(reinvite_event.sip_message)

        return calltest.CallTest.during_call(self)




if __name__ == '__main__':
    calltest.run(klass=AddRemoveContent)
    
