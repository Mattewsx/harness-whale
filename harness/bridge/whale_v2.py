"""AI Passport v2.2/2.3 wire projection. Integers are little endian throughout.

Protocol 2.3 keeps the 2.2 hello and adds capability bit 12: when both peers and the
ATT MTU allow it, fragments after the handshake use header version 3 and the
negotiated frame size instead of 20-byte frames with a 12-byte payload.
"""

import asyncio
from collections import OrderedDict, deque
from dataclasses import dataclass
import json
import secrets
import hashlib
import struct
import time
import zlib

try:
    from .harness_ble_protocol import snapshot_from_mapping
except ImportError:
    from harness_ble_protocol import snapshot_from_mapping

SERVICE = "13b50200-663a-48c5-a224-37c1c2569080"
WRITE = "13b50201-663a-48c5-a224-37c1c2569080"
NOTIFY = "13b50202-663a-48c5-a224-37c1c2569080"
HEADER = struct.Struct("<BBHBBH")
IDENTITY = struct.Struct("<QIII")
ROW = struct.Struct("<IIBBBIHHIB")
QUESTION_READ = 8192  # optional per-question unread marker and acknowledgement
CACHE_INVALIDATION = 1 << 14
CAPS = 4095 | QUESTION_READ | CACHE_INVALIDATION
LARGE_FRAMES = 4096  # bit 12: protocol 2.3 negotiated large frames
LEGACY_FRAME = 20
MAX_FRAME = 244  # ATT MTU 247 minus the 3-byte ATT header
CONTROL = {0x40, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x4A, 0x1A, 1, 2, 0x10, 0x16, 0x22, 0x23, 0x24, 0x25, 0x30, 0x31, 0x32}
CONTROL |= {0x50, 0x51, 0x52}
END = 0xFFFFFFFF
CLOCK_RESYNC_SECONDS = 1800


def pack(fmt, *values):
    return struct.pack("<" + fmt, *values)


def frame_capacity(frame):
    """Payload bytes per fragment: legacy version 2 or negotiated version 3."""
    return frame - HEADER.size


def fragments(kind, serial, body, limit=LEGACY_FRAME):
    if not LEGACY_FRAME <= limit <= MAX_FRAME:
        raise ValueError("invalid frame size")
    capacity = frame_capacity(limit)
    version = 3 if limit > LEGACY_FRAME else 2
    count = max(1, (len(body) + capacity - 1) // capacity)
    if len(body) > (96 if kind in CONTROL else 2048) or count > 255:
        raise ValueError("invalid message size")
    for i in range(count):
        yield HEADER.pack(version, kind, serial, i, count, len(body)) + body[
            i * capacity : (i + 1) * capacity
        ]


def offered_frame(mtu):
    """The largest frame one ATT write or notification can carry at this MTU."""
    try:
        mtu = int(mtu or 0)
    except (TypeError, ValueError):
        mtu = 0
    return max(LEGACY_FRAME, min(MAX_FRAME, mtu - 3))


def hello(frame=LEGACY_FRAME):
    """A 2.2-compatible hello; bit 12 offers 2.3 large frames when the MTU allows."""
    caps = CAPS | (LARGE_FRAMES if frame > LEGACY_FRAME else 0)
    return (1, pack("BBIHH", 2, 2, caps, frame, frame))


class Receiver:
    def __init__(self):
        self.active = {}
        self.last = {}
        self.frame = 0  # negotiated version-3 frame size; 0 accepts legacy frames only

    def accept(self, frame):
        if len(frame) < 8:
            return None
        version, kind, serial, index, count, size = HEADER.unpack_from(frame)
        lane = kind in CONTROL
        if version == 2 and len(frame) <= LEGACY_FRAME:
            capacity = frame_capacity(LEGACY_FRAME)
        elif version == 3 and LEGACY_FRAME < self.frame and len(frame) <= self.frame:
            capacity = frame_capacity(self.frame)
        else:
            return None
        if (
            not size
            or size > (96 if lane else 2048)
            or count != (size + capacity - 1) // capacity
            or index >= count
            or len(frame) != 8 + min(capacity, size - index * capacity)
        ):
            return None
        now = time.monotonic()
        if lane in self.active and now - self.active[lane][5][0] >= 2:
            del self.active[lane]
        if not index and serial != self.last.get(lane):
            old = self.last.get(lane)
            if old is not None and not 0 < (serial - old) % 65536 < 32768:
                return None
            priority = lambda t: (
                3 if t in (0x20, 0x21, 0x17) else 2 if t in (0x11, 0x12, 0x13) else 1
            )
            old_slot = self.active.get(lane)
            if old_slot and (lane or priority(kind) <= priority(old_slot[1])):
                return None
            self.last[lane] = serial
            self.active[lane] = (serial, kind, count, size, [], [now], version)
        active = self.active.get(lane)
        if not active or active[:4] != (serial, kind, count, size) or active[6] != version:
            return None
        pieces = active[4]
        chunk = bytes(frame[8:])
        if index < len(pieces) and pieces[index] == chunk:
            return None
        if index != len(pieces) or sum(map(len, pieces)) + len(chunk) > size:
            del self.active[lane]
            return None
        pieces.append(chunk)
        active[5][0] = now
        if len(pieces) == count:
            del self.active[lane]
            body = b"".join(pieces)
            return (kind, body) if len(body) == size else None
        return None


@dataclass
class Question:
    identity: bytes
    request: str
    labels: list
    prompt: bytes
    addr: tuple
    status: int = 4
    choice: int = 65535
    submitted: bool = False
    submitted_option: int = 65535


class Workspace:
    def __init__(self):
        self.epoch = secrets.randbits(64)
        self.sessions = OrderedDict()
        self.keys = {}
        self.roster = None
        self.delivered = None
        self.visibility = {}
        self.reply_cache = OrderedDict()
        self.cache_sequence = 0
        self.cache_invalidations = deque(maxlen=32)
        self.history_pages = {}
        self.questions = OrderedDict()
        self.by_request = {}
        self.next_id = 0
        self.revision = 0
        self.transport = None
        self.balance = None
        self.balance_old = False
        self.read_marks = {}
        self.marks_path = None
        self.voice = None
        self.frame = None  # negotiated BLE frame size of the current link, for diagnostics

    def load_marks(self, path):
        self.marks_path = path
        try:
            value = json.loads(path.read_text(encoding='utf-8'))
            if isinstance(value, dict):
                self.read_marks = {k:v for k,v in list(value.items())[-512:] if isinstance(v,dict)}
        except (OSError, ValueError): pass

    def read_mark(self, sid, revision, kind):
        row = self.sessions.get(sid, {})
        field = 'reply' if kind == 1 else 'error'
        current = row.get('_reply') if kind == 1 else row.get('_rid')
        value = row.get('replyId') if kind == 1 else row.get('runId')
        if kind == 3 and row:
            question = self.questions.get(IDENTITY.pack(self.epoch, sid, row['_rid'], revision))
            if not question or question.status != 4:
                return
            field, value, current = 'question', question.request, revision
        if kind == 4 and row.get('state') == 'done' and not row.get('replyId'):
            field = 'done'
        elif kind == 4:
            return
        if kind == 5:
            if row.get('state') != 'question' and row.get('hint', 0) < 2:
                return
            field = 'hostQuestion'
        if kind not in (1, 2, 3, 4, 5) or not value or current != revision: return
        key = hashlib.sha256(row['sessionId'].encode()).hexdigest()
        self.read_marks.setdefault(key, {})[field] = value
        self.revision += 1
        if len(self.read_marks) > 512: del self.read_marks[next(iter(self.read_marks))]
        if self.marks_path:
            try:
                self.marks_path.parent.mkdir(parents=True, exist_ok=True)
                temp = self.marks_path.with_suffix('.tmp')
                temp.write_text(json.dumps(self.read_marks), encoding='utf-8')
                temp.replace(self.marks_path)
            except OSError: pass

    def allocate(self):
        self.next_id += 1
        return self.next_id

    def is_hidden(self, key):
        return not isinstance(key, str) or (self.roster is not None and key not in self.roster) or self.visibility.get(key, (0, False))[1]

    def set_roster(self, ids):
        if self.roster is not None:
            for key in ids - self.roster:
                stamp, hidden = self.visibility.get(key, (0, False))
                if hidden:
                    self.visibility[key] = (stamp, False)
        self.roster = frozenset(ids)
        for key in self.keys.keys() - self.roster:
            self.evict(key)

    def set_visibility(self, key, hidden, stamp):
        if not isinstance(key, str) or type(hidden) is not bool or type(stamp) is not int:
            return
        if stamp <= self.visibility.get(key, (0, False))[0]:
            return
        sid = self.keys.get(key)
        current = self.sessions.get(sid, {})
        if hidden and stamp < current.get("visibleAt", 0):
            return
        self.visibility[key] = (stamp, hidden)
        if hidden:
            self.evict(key)

    def evict(self, key):
        sid = self.keys.pop(key, None)
        if sid is None:
            return
        old = self.sessions.pop(sid)
        self.invalidate_reply(sid, old.get("_reply", 0))
        self.history_pages.pop(sid, None)
        for ref, question in list(self.questions.items()):
            if IDENTITY.unpack(ref)[1] == sid:
                if question.status == 4 and self.transport:
                    msg = {"kind":"whale-detach","requestId":question.request}
                    self.transport.sendto(json.dumps(msg).encode(), question.addr)
                if question.status == 4:
                    self.resolve(question.request, 2)
                question.labels.clear()
                question.prompt = b""
        self.revision += 1

    def invalidate_reply(self, sid, revision):
        if revision:
            self.cache_sequence += 1
            self.cache_invalidations.append((self.cache_sequence, sid, revision))
            for key in list(self.reply_cache):
                if key[:2] == (sid, revision):
                    del self.reply_cache[key]

    def restore_history(self, key, history, reply_id, pages):
        if self.is_hidden(key):
            return
        sid = self.keys.get(key)
        if sid is None:
            self.update({"sessionId": key, "runId": "history", "title": history.title,
                         "state": history.state, "elapsed": history.elapsed,
                         "visibleAt": self.visibility.get(key, (0, False))[0] + 1})
            sid = self.keys[key]
        row = self.sessions[sid]
        # A live event can arrive while the worker is decompressing the journal.
        if row.get("runId") == "history":
            row.update(state=history.state, elapsed=history.elapsed, title=history.title)
            self.revision += 1
        elif not row.get("title") and history.title:
            row["title"] = history.title
            self.revision += 1
        if pages and row.get("replyId") != reply_id and (not row.get("replyId") or sid in self.history_pages):
            self.invalidate_reply(sid, row.get("_reply", 0))
            row["replyId"] = reply_id
            row["replyPreview"] = " ".join(history.reply.split())[:20]
            row["_reply"] = self.allocate()
            self.history_pages[sid] = pages
            self.revision += 1

    def reply_page(self, event, addr):
        sid=self.keys.get(event.get("sessionId"))
        row=self.sessions.get(sid,{})
        if not row or row.get("_addr")!=addr or row.get("replyId")!=event.get("replyId"):
            return
        page=event.get("page"); count=event.get("count"); text=event.get("text")
        if not isinstance(page,int) or not isinstance(count,int) or not 0<=page<count<=0xffffffff or not isinstance(text,str) or not 0<len(text.encode())<=384:
            return
        key=(sid,row["_reply"],page)
        self.reply_cache[key]=(count,text.encode())
        if len(self.reply_cache)>32:self.reply_cache.popitem(last=False)

    def update(self, event, addr=None):
        key = event.get("sessionId")
        run = event.get("runId")
        if not isinstance(key, str) or not key or not isinstance(run, str) or not run:
            return
        if self.roster is not None and key not in self.roster:
            return
        cutoff, hidden = self.visibility.get(key, (0, False))
        visible_at = event.get("visibleAt", 0)
        if cutoff and (type(visible_at) is not int or visible_at <= cutoff):
            return
        if hidden:
            # A newer affirmative registry check restores a session across plugin restarts.
            self.set_visibility(key, False, visible_at)
        sid = self.keys.get(key)
        if sid is None:
            sid = self.keys[key] = self.allocate()
        previous = self.sessions.get(sid, {})
        stamp = event.get("sentAt", 0)
        if stamp < previous.get("sentAt", 0):
            return
        if sid in self.history_pages:
            if event.get("replyId"):
                self.history_pages.pop(sid)
            else:
                event = {**event, "replyId": previous["replyId"], "replyPreview": previous["replyPreview"]}
        if previous.get("title") and not event.get("title"):
            event = {**event, "title": previous["title"]}
        rid = previous.get("_rid") if previous.get("runId") == run else self.allocate()
        if previous and rid != previous["_rid"]:
            for q in self.questions.values():
                if (
                    IDENTITY.unpack(q.identity)[1:3] == (sid, previous["_rid"])
                    and q.status == 4
                ):
                    q.status = 2
        revision=previous.get("_reply",0)
        if event.get("replyId")!=previous.get("replyId"):
            self.invalidate_reply(sid, revision)
            revision=self.allocate() if event.get("replyId") else 0
        state = {**event, "_rid": rid,"_reply":revision,"_addr":addr}
        self.sessions[sid] = state
        # Elapsed is extrapolated by the device. A heartbeat alone need not cause radio traffic.
        keys = ("state", "title", "tool", "todoDone", "todoTotal", "runId", "hint", "replyId")
        if any(state.get(k) != previous.get(k) for k in keys):
            self.revision += 1

    def question(self, event, addr):
        req = event.get("requestId")
        if not isinstance(req, str) or req in self.by_request:
            return
        sid = self.keys.get(event.get("sessionId"))
        row = self.sessions.get(sid)
        if not row or row.get("runId") != event.get("runId"):
            return
        labels = event.get("options")
        if (
            not isinstance(labels, list)
            or not 2 <= len(labels) <= 64
            or not all(
                isinstance(v, str) and 0 < len(v.encode()) <= 256 for v in labels
            )
        ):
            return
        prompt = event.get("question")
        if (
            not isinstance(prompt, str)
            or not 0 < len(prompt.encode()) <= 512
            or len(set(labels)) != len(labels)
        ):
            return
        qid = self.allocate()
        p = IDENTITY.pack(self.epoch, sid, row["_rid"], qid)
        self.questions[p] = Question(p, req, labels, prompt.encode(), addr)
        self.by_request[req] = p
        self.revision += 1

    def resolve(self, request, status, option=65535):
        q = self.questions.get(self.by_request.get(request))
        if status not in (0, 1, 2, 3):
            return
        if q and q.status == 4:
            q.status = status
            q.choice = option if status == 0 else 65535
            self.revision += 1
        # Keep a bounded result cache, including accepted answers for idempotent queries.
        if len(self.questions) > 256:
            for k, old in list(self.questions.items()):
                if old.status != 4:
                    del self.by_request[old.request]
                    del self.questions[k]
                    if len(self.questions) <= 256:
                        break

    def needs_answer(self, sid, state, qid):
        if qid:
            return True
        data = self.sessions[sid]
        if state != 8 and not (state == 4 and data.get("hint", 0) >= 2):
            return False
        return not any(q.status != 4 and IDENTITY.unpack(q.identity)[1:3] == (sid, data["_rid"])
                       for q in self.questions.values())

    def row(self, sid, attention=False, question_read=False):
        data = self.sessions[sid]
        snap = snapshot_from_mapping(data, seq=0)
        title = str(data.get("title") or "").encode()
        short = title[:72].decode("utf8", "ignore").encode()
        rid = data["_rid"]
        qid = next(
            (
                IDENTITY.unpack(q.identity)[3]
                for q in self.questions.values()
                if q.status == 4 and IDENTITY.unpack(q.identity)[1:3] == (sid, rid)
            ),
            0,
        )
        flags = bool(snap.todo_total) | (bool(qid) << 1) | ((short != title) << 2)
        if attention:
            seen = self.read_marks.get(hashlib.sha256(data['sessionId'].encode()).hexdigest(), {})
            if question_read and not qid:
                if int(snap.state) == 5 and not data.get('replyId') and data.get('runId') != seen.get('done'):
                    flags |= 64
                if data.get('runId') != seen.get('hostQuestion') and self.needs_answer(sid, int(snap.state), qid):
                    flags |= 32
            if qid and question_read:
                question = self.questions[IDENTITY.pack(self.epoch, sid, rid, qid)]
                if question.request != seen.get('question'):
                    flags |= 32
            if data.get('replyId') and data['replyId'] != seen.get('reply'): flags |= 8
            if int(snap.state) == 6 and data.get('runId') != seen.get('error'): flags |= 16
        return (
            ROW.pack(
                sid,
                rid,
                8 if qid else int(snap.state),
                min(7, int(snap.tool)),
                flags,
                snap.elapsed_s,
                snap.todo_done,
                snap.todo_total,
                qid,
                len(short),
            )
            + short
        )


class Link:
    def __init__(self, model):
        self.model = model
        self.ready = False
        self.seq = 0
        self.snap = 0
        self.cursor = 0
        self.rows = {}
        self.known = set()
        self.results = {}
        self.balance = None
        self.detail = False
        self.question_read = False
        self.cache_invalidation = False
        self.cache_sequence = 0
        self.attention = False
        self.details = {}
        self.readable=False
        self.previews={}
        self.pending_page=None
        self.limit = 8
        self.revision = -1
        self.order = []
        self.global_cap = False
        self.global_rows = None
        self.global_seq = 0
        self.global_summary = None
        self.global_at = 0
        self.announced = set()
        self.global_initialized = False
        self.clock_at = None
        self.offered = LEGACY_FRAME
        self.frame = LEGACY_FRAME

    def clock(self, now=None, tz_minutes=None):
        """0x53 desk-display wall clock: epoch, Unix seconds, local UTC offset.

        Sent after the first snapshot (the device only accepts it once ready)
        and then every CLOCK_RESYNC_SECONDS to correct the chip's drift.
        Firmware before v1.4.0 ignores the unknown message type.
        """
        if not self.ready or not self.snap:
            return []
        tick = time.monotonic()
        if self.clock_at is not None and tick - self.clock_at < CLOCK_RESYNC_SECONDS:
            return []
        self.clock_at = tick
        now = time.time() if now is None else now
        if tz_minutes is None:
            tz_minutes = (time.localtime(now).tm_gmtoff or 0) // 60
        return [(0x53, pack('QqH', self.model.epoch, int(now), tz_minutes & 0xFFFF))]

    def global_changes(self):
        """Independent of the eight-row window; reconnect establishes a silent baseline.

        0x51 carries the current valid cue as well as an optional live trigger.
        A zero cue invalidates deferred audio after reads, new runs and deletion.
        """
        if not self.ready or not self.global_cap:
            return []
        m = self.model
        current = {}
        running = waiting = unread = 0
        focus = 0
        for sid, data in m.sessions.items():
            row = m.row(sid, True, self.question_read)
            state, flags = row[8], row[10]
            busy = state in (2, 3, 4, 8)
            qid = int.from_bytes(row[19:23], 'little')
            old_value = (self.global_rows or {}).get(sid)
            pending = m.needs_answer(sid, state, qid)
            running += busy
            waiting += pending
            unread += bool(flags & 120)
            if busy and not focus:
                focus = sid
            cue = 1 if pending else 2 if flags & 16 else 3 if flags & 72 or (not self.question_read and state == 5 and not data.get("replyId")) else 0
            if state == 7: cue = 0  # user-initiated stop is always silent
            if cue == 1:
                token = (old_value[3] if old_value and old_value[0] == data['_rid'] and old_value[2] == 1 and old_value[5] in (0, qid) else qid or m.allocate())
            else:
                token = data['_rid'] if cue == 2 else data.get('_reply', 0) or data['_rid']
            current[sid] = (data['_rid'], state, cue, token, data.get('_reply', 0), qid)
        complete = m.roster is not None and m.roster <= m.keys.keys()
        baseline = self.global_rows is None or not self.global_initialized
        self.global_initialized = complete
        out = []
        previous = self.global_rows or {}
        for sid in previous.keys() | current.keys():
            value = current.get(sid, (0, 0, 0, 0, 0, 0))
            if baseline or previous.get(sid) == value:
                continue
            run, state, cue, token, reply, qid = value
            old = previous.get(sid, (0, 0, 0, 0, 0, 0))
            trigger = 0
            live = m.sessions.get(sid, {}).get('runId') not in (None, 'history')
            identity = (sid, cue, run if cue == 3 else token)
            new_cue = (cue, token) != old[2:4]
            if cue == 3:
                new_cue = bool(sid in previous and (reply != old[4] or (state == 5 and old[1] in (2, 3, 4, 8))))
            if live and cue and new_cue and identity not in self.announced:
                trigger = cue
                self.announced.add(identity)
            elif live and state in (2, 3) and (run != old[0] or old[1] not in (2, 3, 4, 8)):
                trigger = 5  # work started; visual only
            self.global_seq += 1
            out.append((0x51, pack('QIIIIBB', m.epoch, self.global_seq, sid, run, token, cue, trigger)))
        # Bound lifetime memory by retaining only current identities. The model's
        # run/reply/question IDs never repeat, and ordinary heartbeat is unchanged.
        if len(self.announced) > max(512, len(current) * 4):
            self.announced.intersection_update((sid, v[2], v[0] if v[2] == 3 else v[3]) for sid, v in current.items())
        self.global_rows = current
        summary = (len(current), running, waiting, unread, focus, int(complete))
        now = time.monotonic()
        if baseline or summary != self.global_summary or now - self.global_at >= 10:
            # Summary follows events so all invalidations reach the device first.
            out.append((0x50, pack('QIHHHHIB', m.epoch, self.global_seq, *summary)))
            self.global_summary, self.global_at = summary, now
        return out

    def snapshot(self, cursor=0, limit=8, refresh_order=True):
        m = self.model
        keys = list(m.sessions)
        if refresh_order and self.attention:
            def priority(sid):
                row = m.row(sid, True, self.question_read)
                state, flags = row[8], row[10]
                question = m.needs_answer(sid, state, int.from_bytes(row[19:23], 'little'))
                if flags & 32 or (question and not self.question_read):
                    return 0
                if flags & 16:
                    return 1
                if flags & (8 | 64):
                    return 2
                if question:
                    return 3
                return 4 if state in (2, 3, 4) else 5
            keys.sort(key=priority)  # stable within a priority; no heartbeat jitter
        elif not refresh_order:
            keys = [sid for sid in self.order if sid in m.sessions] + [sid for sid in keys if sid not in self.order]
        self.order = keys
        self.limit = max(1, min(8, limit))
        last_start = max(0, (len(keys)-1)//self.limit) * self.limit
        self.cursor = cursor if 0 <= cursor < len(keys) else last_start
        ids = keys[self.cursor : self.cursor + self.limit]
        nxt = self.cursor + len(ids)
        self.snap += 1
        begin = pack(
            "QIIIIHB",
            m.epoch,
            self.snap,
            self.seq,
            self.cursor,
            nxt if nxt < len(keys) else END,
            len(keys),
            len(ids),
        )
        self.snapshot_receipt = {
            "total": len(keys), "window": len(ids),
            "digest": hashlib.sha256("\n".join(sorted(m.sessions[i]["sessionId"] for i in ids)).encode()).hexdigest()
        }
        self.rows = {i: m.row(i, self.attention, self.question_read) for i in ids}
        self.known = set(keys)
        self.details.clear()
        self.previews.clear()
        self.revision = m.revision
        out = [(0x11, begin)]
        crc = zlib.crc32(begin)
        for i, row in enumerate(self.rows.values()):
            out.append((0x12, pack("QIB", m.epoch, self.snap, i) + row))
            crc = zlib.crc32(row, crc)
        return out + [(0x13, pack("QIBI", m.epoch, self.snap, len(ids), crc))]

    def notices(self):
        if not self.ready:
            return []
        m = self.model
        out = []
        for p, q in m.questions.items():
            if q.status != 4 and self.results.get(p) != q.status:
                out.append((0x23, p + pack("BH", q.status, q.choice)))
                self.results[p] = q.status
        return out

    def changes(self):
        if not self.ready:
            return []
        m = self.model
        out = []
        if m.revision != self.revision:
            if set(m.sessions) != self.known:
                out.extend(self.snapshot(self.cursor, self.limit, refresh_order=False))
            else:
                for sid, old in list(self.rows.items()):
                    row = m.row(sid, self.attention, self.question_read)
                    if row != old:
                        self.seq += 1
                        out.append((0x14, pack("QIB", m.epoch, self.seq, 0) + row))
                        self.rows[sid] = row
                self.revision = m.revision
        if self.detail:
            for sid, row in self.rows.items():
                event = m.sessions[sid]
                hint = event.get("hint") or 0
                pending = (event["_rid"], hint, row)
                if self.details.get(sid) != pending:
                    if hint or self.details.get(sid):
                        out.append(
                            (0x18, pack("QIIB", m.epoch, sid, event["_rid"], hint))
                        )
                    self.details[sid] = pending
        if self.readable:
            for sid in self.rows:
                event=m.sessions[sid]
                rev=event["_reply"]
                if self.previews.get(sid)!=rev:
                    raw=str(event.get("replyPreview") or "").encode()[:80].decode("utf8","ignore").encode()
                    out.append((0x19,pack("QIIB",m.epoch,sid,rev,len(raw))+raw))
                    self.previews[sid]=rev
            if self.pending_page:
                key=self.pending_page
                if m.sessions.get(key[0], {}).get("_reply") != key[1]:
                    self.pending_page=None
                elif key in m.reply_cache:
                    count,txt=m.reply_cache[key]
                    out.append((0x1B,pack("QIIIIH",m.epoch,*key,count,len(txt))+txt))
                    self.pending_page=None
        if self.cache_invalidation and self.snap and self.cache_sequence != m.cache_sequence:
            for seq, sid, rev in m.cache_invalidations:
                if seq > self.cache_sequence:
                    out.append((0x1C, pack("QQII", m.epoch, seq, sid, rev)))
            self.cache_sequence = m.cache_sequence
        return out + self.global_changes()

    def receive(self, kind, body):
        m = self.model
        if kind == 2 and len(body) == 11:
            major, minor, caps, rx, tx, status = struct.unpack("<BBIHHB", body)
            self.detail = bool(caps & 128)
            self.readable=bool(caps & 256)
            self.question_read = bool(caps & QUESTION_READ)
            self.cache_invalidation = bool(caps & CACHE_INVALIDATION)
            self.attention=bool(caps & 1024)
            self.global_cap=bool(caps & 2048)
            self.clock_at = None
            self.ready = (
                (major, status) == (2, 0)
                and minor in (2, 3)
                and caps & 63 == 63
                and min(rx, tx) >= LEGACY_FRAME
            )
            # 2.3 grants one symmetric frame no larger than this bridge offered.
            large = (self.ready and minor == 3 and caps & LARGE_FRAMES and rx == tx
                     and LEGACY_FRAME < rx <= self.offered)
            self.frame = rx if large else LEGACY_FRAME
            return []
        if not self.ready:
            return []
        if kind == 0x52 and self.global_cap and len(body) == 12:
            epoch, sid = struct.unpack('<QI', body)
            if epoch != m.epoch or sid not in m.sessions:
                return []
            first = self.snapshot(0)
            cursor = self.order.index(sid) // self.limit * self.limit
            return self.snapshot(cursor, self.limit) if cursor else first
        if kind == 0x4a and self.attention and len(body) == 17:
            epoch,sid,revision,which = struct.unpack('<QIIB',body)
            if epoch == m.epoch and (which <= 2 or self.question_read): m.read_mark(sid,revision,which)
            return []
        if kind == 0x10 and len(body) == 14:
            _, cursor, limit, sort = struct.unpack("<QIBB", body)
            return self.snapshot(cursor, limit, refresh_order=not self.cache_invalidation or sort == 2 or not self.order)
        if kind == 0x1A and self.readable and len(body)==20:
            epoch,sid,rev,page=struct.unpack("<QIII",body)
            row=m.sessions.get(sid,{})
            if epoch!=m.epoch or not rev or row.get("_reply")!=rev:
                return [(0x32,pack("BHBB",kind,0,4,0))]
            key=(sid,rev,page)
            if sid in m.history_pages:
                self.pending_page = None
                pages = m.history_pages[sid]
                if page >= len(pages):
                    return [(0x32,pack("BHBB",kind,0,4,0))]
                txt = pages[page]
                return [(0x1B,pack("QIIIIH",m.epoch,sid,rev,page,len(pages),len(txt))+txt)]
            self.pending_page=key
            if key not in m.reply_cache and row["_addr"] and m.transport:
                request={"kind":"whale-read-reply","sessionId":row["sessionId"],"replyId":row["replyId"],"page":page}
                m.transport.sendto(json.dumps(request).encode(),row["_addr"])
            return []
        if kind == 0x16 and len(body) == 16:
            epoch, sid, rid = struct.unpack("<QII", body)
            row = m.sessions.get(sid, {})
            if epoch == m.epoch and row.get("_rid") == rid:
                txt = str(row.get("title") or "").encode()
                if len(txt) <= 512:
                    return [(0x17, body + pack("H", len(txt)) + txt)]
            return [(0x32, pack("BHBB", 0x16, 0, 2, 0))]
        if (
            kind == 0x30
            and len(body) == 12
            and int.from_bytes(body[:8], "little") == m.epoch
        ):
            for q in m.questions.values():
                if q.status == 4 and not q.submitted and m.transport:
                    m.transport.sendto(
                        json.dumps(
                            {"kind": "whale-detach", "requestId": q.request}
                        ).encode(),
                        q.addr,
                    )
                    m.resolve(q.request, 2)
            return [(0x31, body)]
        if (
            kind not in (0x22, 0x24, 0x25)
            or len(body) != {0x22: 22, 0x24: 21, 0x25: 20}[kind]
        ):
            return []
        p = body[:20]
        q = m.questions.get(p)
        if not q:
            return [(0x23, p + pack("BH", 3, 65535))]
        if q.status == 4 and kind == 0x24:
            page = body[20]
            count = len(q.labels)
            pages = (count + 2) // 3
            if page == 255:
                return [(0x20, p + pack("BBH", count, pages, len(q.prompt)) + q.prompt)]
            if page >= pages:
                return []
            labels = q.labels[page * 3 : page * 3 + 3]
            payload = b"".join(
                pack("HH", page * 3 + i, len(t.encode())) + t.encode()
                for i, t in enumerate(labels)
            )
            return [(0x21, p + bytes((page, len(labels))) + payload)]
        if q.status == 4 and (
            kind == 0x22 and not q.submitted or kind == 0x25 and q.submitted
        ):
            idx = (
                int.from_bytes(body[20:], "little")
                if kind == 0x22
                else q.submitted_option
            )
            if idx < len(q.labels) and m.transport:
                msg = {
                    "v": 1,
                    "kind": "harness-answer",
                    "requestId": q.request,
                    "selected": idx,
                }
                m.transport.sendto(json.dumps(msg).encode(), q.addr)
                q.submitted = True
                q.submitted_option = idx
        return [(0x23, p + pack("BH", q.status, q.choice))]


HELLO_TIMEOUT_SECONDS = 8.0


async def serve(client, model, on_write=None):
    """Write one ATT value at a time; service inbound control between bulk fragments."""
    try:
        from .whale_voice import VoiceBridge, AUDIO
    except ImportError:
        from whale_voice import VoiceBridge, AUDIO
    import os
    from pathlib import Path
    if model.marks_path is None:
        model.load_marks(Path(os.environ.get('LOCALAPPDATA') or Path.home()/'.local/share') / 'HarnessWhale' / 'read-marks.json')
    if model.voice is None: model.voice = VoiceBridge(model)
    voice = model.voice
    wire = Link(model)
    model.delivered = None
    sending_snapshot = None
    rx = Receiver()
    inbox = asyncio.Queue(maxsize=64)
    overflow = False

    def notify(_, data):
        nonlocal overflow
        if inbox.full():
            overflow = True
        else:
            inbox.put_nowait(bytes(data))

    await client.start_notify(NOTIFY, notify)
    try:
        audio_char = client.services.get_characteristic(AUDIO)
        if audio_char is not None:
            await client.start_notify(AUDIO, lambda _, data: voice.feed(bytes(data)))
            voice.audio_supported = True
    except (AttributeError, KeyError): pass
    queues = [[], []]
    serial = [0, 0]
    inflight = [None, None]

    def queue(messages):
        for kind, body in messages:
            lane = 0 if kind in CONTROL else 1
            queues[lane].append((kind, body))

    wire.offered = offered_frame(getattr(client, "mtu_size", 0))
    model.frame = None
    queue([hello(wire.offered)])
    bulk_type = None
    refresh_after_page = False
    hello_deadline = time.monotonic() + HELLO_TIMEOUT_SECONDS
    try:
        while client.is_connected:
            if not wire.ready and inbox.empty() and time.monotonic() >= hello_deadline:
                raise ConnectionError("R06+ handshake timed out; reconnecting")
            if overflow:
                raise ConnectionError("notification queue overflow")
            while not inbox.empty():
                msg = rx.accept(inbox.get_nowait())
                if msg:
                    replies = [] if wire.ready and voice.receive(*msg) else wire.receive(*msg)
                    if msg[0] == 2:
                        # Later fragments in both directions use the negotiated frame.
                        rx.frame = wire.frame if wire.frame > LEGACY_FRAME else 0
                        model.frame = wire.frame if wire.ready else None
                    if (
                        msg[0] in (0x24, 0x16)
                        and replies
                        and replies[0][0] in (0x20, 0x21, 0x17)
                    ):
                        if bulk_type in (0x11, 0x12, 0x13, 0x14, 0x15):
                            refresh_after_page = True
                            inflight[1] = None
                            # Invalidations span windows and cannot be rebuilt by a page refresh.
                            queues[1][:] = [item for item in queues[1] if item[0] == 0x1C]
                        queues[1][0:0] = replies
                    else:
                        queue(replies)
            queue(wire.notices())
            if wire.ready: queue(voice.poll())
            if refresh_after_page and not queues[1] and inflight[1] is None:
                queue(wire.snapshot(wire.cursor, wire.limit, refresh_order=False))
                refresh_after_page = False
            if not queues[1] and inflight[1] is None:
                queue(wire.changes())
            if not queues[1] and inflight[1] is None:
                queue(wire.clock())
            for lane in (0, 1):
                if inflight[lane] is None and queues[lane]:
                    kind, body = queues[lane].pop(0)
                    if kind == 0x13:
                        sending_snapshot = wire.snapshot_receipt if int.from_bytes(body[8:12], "little") == wire.snap else None
                    if lane:
                        bulk_type = kind
                    serial[lane] = (serial[lane] + 1) & 65535
                    inflight[lane] = iter(fragments(kind, serial[lane], body, wire.frame))
                if inflight[lane] is not None:
                    value = next(inflight[lane], None)
                    if value is None:
                        inflight[lane] = None
                    else:
                        await client.write_gatt_char(WRITE, value, response=True)
                        if value[1] == 0x13 and value[4]+1 == value[5] and sending_snapshot is not None:
                            model.delivered = {**sending_snapshot, "time": time.time()}
                        if on_write is not None:
                            on_write(wire.ready)
                        break
            else:
                await asyncio.sleep(0.035)
    finally:
        model.frame = None
        voice.disconnected()
