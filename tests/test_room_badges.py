import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'harness/bridge'))
import whale_v2 as protocol


class RoomBadges(unittest.TestCase):
    def test_unread_first_then_stable_pagination(self):
        m = protocol.Workspace()
        for i in range(8):
            m.update(dict(sessionId=f'net-{i}', runId='r', state='waiting'))
        for key, state in [('old', 'question'), ('fault', 'error'), ('done', 'done'),
                           ('approval', 'waiting'), ('new', 'question')]:
            m.update(dict(sessionId=key, runId='r', state=state, hint=2 if key == 'approval' else 0))
        m.read_mark(m.keys['old'], m.sessions[m.keys['old']]['_rid'], 5)
        link = protocol.Link(m)
        link.receive(2, protocol.pack('BBIHHB', 2, 2, protocol.CAPS, 20, 20, 0))
        link.receive(0x10, protocol.pack('QIBB', m.epoch, 0, 8, 2))
        self.assertEqual(list(link.rows)[:5], [m.keys[k] for k in ('approval', 'new', 'fault', 'done', 'old')])
        initial = list(link.order)
        m.read_mark(m.keys['fault'], m.sessions[m.keys['fault']]['_rid'], 2)
        link.receive(0x10, protocol.pack('QIBB', m.epoch, 8, 8, 0))
        self.assertEqual(list(link.rows), initial[8:])
        link.receive(0x10, protocol.pack('QIBB', m.epoch, 0, 8, 2))
        self.assertNotIn(m.keys['fault'], list(link.rows)[:4])

    def test_offpage_revision_and_archive_replayed_after_reconnect(self):
        m = protocol.Workspace()
        for i in range(9):
            m.update(dict(sessionId=str(i), runId='r', state='idle', replyId='one'))
        def connect(caps):
            link = protocol.Link(m)
            link.receive(2, protocol.pack('BBIHHB', 2, 2, caps, 20, 20, 0))
            link.snapshot(8)
            return link
        def changes(link):
            return [struct.unpack('<QQII', b)[1:] for kind, b in link.changes() if kind == 0x1c]
        link = connect(protocol.CAPS)
        expected = [(n, m.keys[k], m.sessions[m.keys[k]]['_reply']) for n, k in enumerate(('0','1'), 1)]
        m.update(dict(sessionId='0', runId='r', state='idle', replyId='two'))
        m.evict('1')
        self.assertEqual(changes(link), expected)
        self.assertEqual(changes(link), [])
        self.assertEqual(changes(connect(protocol.CAPS)), expected)
        self.assertEqual(changes(connect(protocol.CAPS & ~protocol.CACHE_INVALIDATION)), [])

    def test_distinct_unread_sessions_and_deferred_question(self):
        model = protocol.Workspace()
        keys = {str(i) for i in range(12)}
        model.set_roster(keys)
        for i in range(12):
            model.update(dict(sessionId=str(i), runId='turn', title='same', state='idle'))
        model.update(dict(sessionId='10', runId='turn', state='error', replyId='reply'))
        model.update(dict(sessionId='11', runId='turn', state='done'))
        link = protocol.Link(model)
        link.receive(2, protocol.pack('BBIHHB', 2, 2, protocol.CAPS, 20, 20, 0))
        link.snapshot(0)
        sid = model.keys['10']
        model.question(dict(sessionId='10', runId='turn', requestId='one',
                            question='Choose?', options=['A', 'B']), None)
        question = model.by_request['one']
        _, _, run, qid = protocol.IDENTITY.unpack(question)

        def count():
            link.global_at = 0
            summary = dict(link.global_changes())[0x50]
            return struct.unpack('<QIHHHHIB', summary)[5]

        def flags():
            return model.row(sid, True, True)[10]

        def mark(rev, kind):
            link.receive(0x4a, protocol.pack('QIIB', model.epoch, sid, rev, kind))

        self.assertEqual(count(), 2)
        self.assertEqual(flags() & 56, 56)
        self.assertFalse(model.row(sid, True, False)[10] & 32)
        link.snapshot(8)
        self.assertEqual(count(), 2)
        mark(qid + 1, 3)
        self.assertTrue(flags() & 32)
        mark(qid, 3)
        self.assertEqual(flags() & 56, 24)
        self.assertEqual(model.questions[question].status, 4)
        mark(run, 2)
        mark(model.sessions[sid]['_reply'], 1)
        self.assertEqual(count(), 1)
        model.resolve('one', 0, 0)
        model.question(dict(sessionId='10', runId='turn', requestId='two',
                            question='Again?', options=['A', 'B']), None)
        mark(qid, 3)
        self.assertTrue(flags() & 32)
        self.assertEqual(count(), 2)
        model.set_visibility('10', True, 1)
        self.assertEqual(count(), 1)
        last = model.keys['11']
        link.receive(0x4a, protocol.pack('QIIB', model.epoch, last, model.sessions[last]['_rid'], 4))
        self.assertEqual(count(), 0)

    def test_resolved_question_does_not_become_an_unread_computer_prompt(self):
        m = protocol.Workspace()
        m.update(dict(sessionId='a', runId='turn', state='question'))
        sid = m.keys['a']
        self.assertTrue(m.row(sid, True, True)[10] & 32)
        m.question(dict(sessionId='a', runId='turn', requestId='choice', question='Pick',
                        options=['A', 'B']), None)
        m.resolve('choice', 0, 0)
        self.assertFalse(m.row(sid, True, True)[10] & 32)
