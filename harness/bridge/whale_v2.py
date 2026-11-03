"""AI Passport v2.2 wire projection. Integers are little endian throughout."""

import asyncio
from collections import OrderedDict
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
CAPS = 511  # bit 7 optionally carries per-run display detail
CONTROL = {0x1A, 1, 2, 0x10, 0x16, 0x22, 0x23, 0x24, 0x25, 0x30, 0x31, 0x32}
END = 0xFFFFFFFF


def pack(fmt, *values):
    return struct.pack("<" + fmt, *values)


def fragments(kind, serial, body, limit=20):
    capacity = limit - HEADER.size
    count = max(1, (len(body) + capacity - 1) // capacity)
    if capacity < 12 or len(body) > (96 if kind in CONTROL else 2048) or count > 255:
        raise ValueError("invalid message size")
    for i in range(count):
        yield HEADER.pack(2, kind, serial, i, count, len(body)) + body[
            i * capacity : (i + 1) * capacity
        ]


class Receiver:
    def __init__(self):
        self.active = {}
        self.last = {}

    def accept(self, frame):
        if len(frame) < 8:
            return None
        version, kind, serial, index, count, size = HEADER.unpack_from(frame)
        lane = kind in CONTROL
        if (
            version != 2
            or not size
            or size > (96 if lane else 2048)
            or count != (size + 11) // 12
            or index >= count
            or len(frame) != 8 + min(12, size - index * 12)
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
            self.active[lane] = (serial, kind, count, size, [], [now])
        active = self.active.get(lane)
        if not active or active[:4] != (serial, kind, count, size):
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
        self.history_pages = {}
        self.questions = OrderedDict()
        self.by_request = {}
        self.next_id = 0
        self.revision = 0
        self.transport = None
        self.balance = None
        self.balance_old = False

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
        self.sessions.pop(sid, None)
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
        for ref in list(self.reply_cache):
            if ref[0] == sid:
                del self.reply_cache[ref]
        self.revision += 1

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

    def row(self, sid):
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
        self.details = {}
        self.readable=False
        self.previews={}
        self.pending_page=None
        self.limit = 8
        self.revision = -1

    def snapshot(self, cursor=0, limit=8):
        m = self.model
        keys = list(m.sessions)
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
        self.rows = {i: m.row(i) for i in ids}
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
                out.extend(self.snapshot(self.cursor, self.limit))
            else:
                for sid, old in list(self.rows.items()):
                    row = m.row(sid)
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
                if key in m.reply_cache:
                    count,txt=m.reply_cache[key]
                    out.append((0x1B,pack("QIIIIH",m.epoch,*key,count,len(txt))+txt))
                    self.pending_page=None
        return out

    def receive(self, kind, body):
        m = self.model
        if kind == 2 and len(body) == 11:
            major, minor, caps, rx, tx, status = struct.unpack("<BBIHHB", body)
            self.detail = bool(caps & 128)
            self.readable=bool(caps & 256)
            self.ready = (
                (major, minor, status) == (2, 2, 0)
                and caps & 63 == 63
                and min(rx, tx) >= 20
            )
            return []
        if not self.ready:
            return []
        if kind == 0x10 and len(body) == 14:
            _, cursor, limit, _ = struct.unpack("<QIBB", body)
            return self.snapshot(cursor, limit)
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
    queues = [[], []]
    serial = [0, 0]
    inflight = [None, None]

    def queue(messages):
        for kind, body in messages:
            lane = 0 if kind in CONTROL else 1
            queues[lane].append((kind, body))

    queue([(1, pack("BBIHH", 2, 2, CAPS, 20, 20))])
    bulk_type = None
    refresh_after_page = False
    hello_deadline = time.monotonic() + HELLO_TIMEOUT_SECONDS
    while client.is_connected:
        if not wire.ready and inbox.empty() and time.monotonic() >= hello_deadline:
            raise ConnectionError("R06+ handshake timed out; reconnecting")
        if overflow:
            raise ConnectionError("notification queue overflow")
        while not inbox.empty():
            msg = rx.accept(inbox.get_nowait())
            if msg:
                replies = wire.receive(*msg)
                if (
                    msg[0] in (0x24, 0x16)
                    and replies
                    and replies[0][0] in (0x20, 0x21, 0x17)
                ):
                    if bulk_type in (0x11, 0x12, 0x13, 0x14, 0x15):
                        refresh_after_page = True
                        inflight[1] = None
                        queues[1].clear()
                    queues[1][0:0] = replies
                else:
                    queue(replies)
        queue(wire.notices())
        if refresh_after_page and not queues[1] and inflight[1] is None:
            queue(wire.snapshot(wire.cursor, wire.limit))
            refresh_after_page = False
        if not queues[1] and inflight[1] is None:
            queue(wire.changes())
        for lane in (0, 1):
            if inflight[lane] is None and queues[lane]:
                kind, body = queues[lane].pop(0)
                if kind == 0x13:
                    sending_snapshot = wire.snapshot_receipt if int.from_bytes(body[8:12], "little") == wire.snap else None
                if lane:
                    bulk_type = kind
                serial[lane] = (serial[lane] + 1) & 65535
                inflight[lane] = iter(fragments(kind, serial[lane], body))
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
