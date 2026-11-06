import struct
import unittest
from harness.bridge.whale_v2 import Workspace, Link, CAPS, pack, fragments, Receiver

class CompanionCues(unittest.TestCase):
    def setUp(self):
        self.m=Workspace()
        self.m.set_roster({f's{i}' for i in range(12)})
        for i in range(12):self.m.update(dict(sessionId=f's{i}',runId='run',state='idle',title=f'Task {i}'))
        self.l=Link(self.m);self.l.receive(2,pack('BBIHHB',2,2,CAPS,20,20,0));self.l.snapshot()
    def update(self,index,**kw):
        sid=self.m.keys[f's{index}'];row=self.m.sessions[sid]
        self.m.update({**row,**kw});return sid
    def test_global_busy_and_attention_outside_page(self):
        sid=self.update(11,state='thinking')
        data=self.l.changes();s=[b for k,b in data if k==0x50][0]
        self.assertEqual(struct.unpack('<QIHHHHIB',s)[2:5],(12,1,0))
        self.assertFalse(any(k==0x51 for k,b in data))
        self.update(11,state='done',replyId='reply1');events=[b for k,b in self.l.changes() if k==0x51]
        self.assertEqual(len(events),1);self.assertEqual(events[0][-2:],bytes((3,3)))
        self.assertEqual(int.from_bytes(events[0][12:16],'little'),sid)
        self.assertFalse(any(k==0x51 for k,b in self.l.changes()))
        self.m.read_mark(sid,self.m.sessions[sid]['_reply'],1)
        self.assertEqual([b for k,b in self.l.changes() if k==0x51][0][-2:],bytes((0,0)))
    def test_history_reconnect_and_old_capability_silent(self):
        self.update(0,state='done',replyId='old');self.l.changes()
        l=Link(self.m);l.receive(2,pack('BBIHHB',2,2,CAPS,20,20,0));l.snapshot()
        self.assertFalse(any(k==0x51 for k,b in l.changes()))
        old=Link(self.m);old.receive(2,pack('BBIHHB',2,2,2047,20,20,0));old.snapshot()
        self.assertFalse(any(k in (0x50,0x51) for k,b in old.changes()))
    def test_stop_and_deletion_invalidate_without_sound(self):
        self.l.changes();sid=self.update(10,state='error');self.l.changes()
        self.update(10,state='stopped')
        self.assertEqual([b for k,b in self.l.changes() if k==0x51][0][-2:],bytes((0,0)))
        self.update(10,state='thinking');self.l.changes();self.m.evict('s10')
        event=[b for k,b in self.l.changes() if k==0x51][0]
        self.assertEqual(event[-2:],bytes((0,0)));self.assertEqual(int.from_bytes(event[12:16],'little'),sid)
    def test_question_priority_and_targeted_page(self):
        self.l.changes();sid=self.m.keys['s11']
        self.m.question(dict(sessionId='s11',runId='run',requestId='q',question='Pick',options=['A','B']),('127.0.0.1',1))
        data=self.l.changes();self.assertEqual([b for k,b in data if k==0x51][0][-2:],bytes((1,1)))
        self.l.receive(0x52,pack('QI',self.m.epoch,sid));self.assertIn(sid,self.l.rows)
        self.m.resolve('q',0,1)
        self.assertEqual([b for k,b in self.l.changes() if k==0x51][0][-2:],bytes((0,0)))
    def test_new_messages_fragment_roundtrip(self):
        self.l.changes();self.update(10,state='error');rx=Receiver()
        for serial,(kind,body) in enumerate(self.l.changes(),1):
            if kind not in (0x50,0x51):continue
            result=None
            for frame in fragments(kind,serial,body):result=rx.accept(frame)
            self.assertEqual(result,(kind,body))
    def test_completion_without_reply_is_once_and_network_wait_is_silent(self):
        self.l.changes();self.update(0,state='thinking');self.l.changes()
        self.update(0,state='waiting',hint=0)
        self.assertFalse(any(b[-1] for k,b in self.l.changes() if k==0x51))
        self.update(0,state='done')
        self.assertEqual([b[-1] for k,b in self.l.changes() if k==0x51],[3])
        self.update(0,replyId='late-final')
        self.assertEqual([b[-1] for k,b in self.l.changes() if k==0x51],[0])
        self.update(0,state='stopped',replyId='stopped-final')
        self.assertEqual([b[-1] for k,b in self.l.changes() if k==0x51],[0])
    def test_clearing_error_does_not_announce_an_old_reply(self):
        self.update(0,replyId='old',state='thinking');self.l.changes()
        self.update(0,state='error');self.l.changes();sid=self.m.keys['s0']
        self.m.read_mark(sid,self.m.sessions[sid]['_rid'],2)
        self.assertFalse(any(b[-1] for k,b in self.l.changes() if k==0x51))
    def test_repeat_approvals_and_resolved_question_in_same_run(self):
        self.l.changes()
        for i in range(2):
            self.update(0,state='waiting',hint=2)
            self.assertEqual([b[-1] for k,b in self.l.changes() if k==0x51],[1])
            self.update(0,state='thinking',hint=0);self.l.changes()
        self.update(0,state='question')
        self.m.question(dict(sessionId='s0',runId='run',requestId='resolve',question='Choose',options=['A','B']),('127.0.0.1',1))
        self.l.changes();self.m.resolve('resolve',0,0)
        events=[b for k,b in self.l.changes() if k==0x51]
        self.assertEqual(events[0][-2:],bytes((0,0)))
    def test_question_metadata_attachment_preserves_pending_identity(self):
        self.l.changes();self.update(0,state='question')
        first=[b for k,b in self.l.changes() if k==0x51][0]
        self.assertEqual(first[-1],1)
        self.m.question(dict(sessionId='s0',runId='run',requestId='attach',question='Choose',options=['A','B']),('127.0.0.1',1))
        attached=[b for k,b in self.l.changes() if k==0x51][0]
        self.assertEqual(attached[20:24],first[20:24])
        self.assertEqual(attached[-2:],bytes((1,0)))
    def test_incomplete_roster_cannot_claim_idle(self):
        self.m.set_roster({f's{i}' for i in range(13)})
        summary=[b for k,b in self.l.changes() if k==0x50][0]
        self.assertEqual(summary[-1],0)

if __name__=='__main__':unittest.main()
