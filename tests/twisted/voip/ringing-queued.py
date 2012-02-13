import calltest
import constants as cs
from servicetest import (
    EventPattern, call_async,
    assertEquals, assertNotEquals, assertContains, assertLength,
    )

class RingingQueued(calltest.CallTest):

    def accept(self):
        if self.incoming:
            
            self.chan.Call1.SetQueued()

            o = self.q.expect_many(
                EventPattern('sip-response', call_id=self.context.call_id,
                             code=182),
                EventPattern('dbus-signal', signal='CallStateChanged'))
            assertEquals(cs.CALL_STATE_INITIALISED, o[1].args[0])
            assertEquals(cs.CALL_FLAG_LOCALLY_QUEUED, o[1].args[1])

            self.chan.Call1.SetRinging()

            o = self.q.expect_many(
                EventPattern('sip-response', call_id=self.context.call_id,
                             code=180),
                EventPattern('dbus-signal', signal='CallStateChanged'))
            assertEquals(cs.CALL_STATE_INITIALISED, o[1].args[0])
            assertEquals(cs.CALL_FLAG_LOCALLY_RINGING, o[1].args[1])
        else:
            # Send Ringing
            self.context.pr_respond(self.invite_event, 180)
            o = self.q.expect('dbus-signal', signal='CallMembersChanged')
            assertEquals(cs.CALL_MEMBER_FLAG_RINGING,
                         o.args[0][self.remote_handle])

        return calltest.CallTest.accept(self)




if __name__ == '__main__':
    calltest.run(klass=RingingQueued)

