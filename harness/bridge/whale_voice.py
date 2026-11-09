"""Bounded, authenticated BLE voice upload and local DSH transcription adapter.

Audio never leaves the computer. The old 20-byte control protocol remains intact;
raw Opus packets arrive on a separate authenticated notification characteristic.
"""
import asyncio
import io
import json
import os
import struct
import subprocess
import time
import urllib.request
import wave

try:
    from .harness_ble_protocol import _truncate_utf8, sanitize_display_text
except ImportError:
    from harness_ble_protocol import _truncate_utf8, sanitize_display_text

AUDIO = '13b50203-663a-48c5-a224-37c1c2569080'
HEADER = struct.Struct('<IIHBB')
READY, DRAFT, SENT, QUEUED, FAILED, UNKNOWN, UNSUPPORTED, ASR_ERROR, ARCHIVED, QUESTION, EXPIRED, EMPTY, BROKEN = range(1, 14)
# Spoken new task (capability bit 16 only): no folder on the computer to open it in.
NO_TARGET = 14
# 0x48 starts a new-task capture; 0x49 tells the device which folder it will open in.
TASK_START, TASK_TARGET = 0x48, 0x49
MAX_LABEL = 48
MAX_TEXT = 1800
STATUS_NAMES = {READY: 'ready', DRAFT: 'draft', SENT: 'sent', QUEUED: 'queued', FAILED: 'failed',
                UNKNOWN: 'unknown', UNSUPPORTED: 'unsupported', ASR_ERROR: 'asr_error',
                ARCHIVED: 'archived', QUESTION: 'question', EXPIRED: 'expired', EMPTY: 'empty',
                BROKEN: 'broken', NO_TARGET: 'no_target'}


def ogg_page(data, sequence, granule, flags=0):
    lacing = bytes([255] * (len(data) // 255) + [len(data) % 255])
    page = bytearray(b'OggS\0' + bytes([flags]) + struct.pack('<QII', granule, 1, sequence) + b'\0'*4 + bytes([len(lacing)]) + lacing + data)
    crc = 0
    for b in page:
        crc ^= b << 24
        for _ in range(8):
            crc = ((crc << 1) ^ (0x04C11DB7 if crc & 0x80000000 else 0)) & 0xffffffff
    page[22:26] = struct.pack('<I', crc)
    return bytes(page)


def decode_opus(head, packets, samples):
    import imageio_ffmpeg
    if len(head) != 19 or head[:8] != b'OpusHead' or head[8:10] != b'\x01\x01' or head[18] != 0:
        raise ValueError('invalid mono Opus header')
    skip = struct.unpack_from('<H', head, 10)[0]
    if skip > 960 or not 0 < samples <= 960000 or not packets or len(packets) > 3001:
        raise ValueError('invalid duration')
    raw = ogg_page(head, 0, 0, 2) + ogg_page(b'OpusTags' + struct.pack('<II', 0, 0), 1, 0)
    raw += b''.join(ogg_page(packet, i+2, samples*3+skip if i == len(packets)-1 else (i+1)*960,
                             4 if i == len(packets)-1 else 0) for i, packet in enumerate(packets))
    process = subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(), '-hide_banner', '-loglevel', 'error',
        '-f', 'ogg', '-i', 'pipe:0', '-ar', '16000', '-ac', '1', '-f', 's16le', 'pipe:1'], input=raw,
        capture_output=True, timeout=10, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    if process.returncode or len(process.stdout) != samples*2:
        raise ValueError('incomplete decoded audio')
    output = io.BytesIO()
    with wave.open(output, 'wb') as wav:
        wav.setnchannels(1); wav.setsampwidth(2); wav.setframerate(16000)
        wav.writeframes(process.stdout)
    return output.getvalue()


class VoiceBridge:
    def __init__(self, model, http=None):
        self.model = model
        self.http = http or self._http
        self.nonce = b''
        self.sid = 0
        self.session = ''
        self.status = EXPIRED
        self.text = ''
        self.head = b''
        self.packets = {}
        self.next_seq = 0
        self.last_ack = 0.0
        self.samples = 0
        self.finish_count = None
        self.finish_at = 0.0
        self.started = self.touched = 0.0
        self.out = []
        self.task = None
        self.audio_supported = False
        # Set per link from the hello reply: firmware that offers spoken new tasks.
        self.task_supported = False
        self.mode = 'reply'  # 'reply' to the selected session, or 'task' for a new session
        self.cwd = ''
        self.label = ''
        self.submitting = False
        self.generation = 0
        # Counts and timing only; never text or audio. Read by the stress report.
        self.stats = {}
        self.stt_started = None
        self.last_stt_ms = None

    @staticmethod
    def _http(path, data=None):
        port = int(os.environ.get('HARNESS_WHALE_VOICE_PORT', '0'))
        token = os.environ.get('HARNESS_WHALE_VOICE_TOKEN', '')
        if not 1 <= port <= 65535 or not token:
            raise ConnectionError('voice service unavailable')
        binary = isinstance(data, bytes)
        encoded = data if binary else json.dumps(data).encode() if data is not None else None
        req = urllib.request.Request(f'http://127.0.0.1:{port}{path}', data=encoded,
            headers={'Authorization': 'Bearer '+token, 'Content-Type': 'audio/wav' if binary else 'application/json'})
        with urllib.request.urlopen(req, timeout=18) as response:
            return json.loads(response.read(16384))

    def identity(self):
        return struct.pack('<Q', self.model.epoch) + self.nonce

    def notice(self, status, text=None):
        name = STATUS_NAMES.get(status, str(status))
        self.stats[name] = self.stats.get(name, 0) + 1
        if self.stt_started is not None and status in (DRAFT, EMPTY, ASR_ERROR, ARCHIVED):
            self.last_stt_ms = round((time.monotonic() - self.stt_started) * 1000)
            self.stt_started = None
        self.status = status
        if text is not None: self.text = text
        raw = self.text.encode() if status == DRAFT else b''
        self.out.append((0x41, self.identity()+bytes([status])+struct.pack('<H',len(raw))+raw))

    def cancel_task(self):
        self.generation += 1
        if self.task and not self.task.done(): self.task.cancel()
        self.task = None

    def launch(self, action):
        self.cancel_task()
        self.task = asyncio.create_task(action(self.generation))

    def matches(self, body):
        return len(body) >= 24 and body[:24] == self.identity()

    def allowed(self):
        if self.mode == 'task':
            return True
        row = self.model.sessions.get(self.sid)
        return row is not None and row.get('sessionId') == self.session and not self.model.is_hidden(self.session)

    async def prepare(self, gen):
        try:
            result = await asyncio.to_thread(self.http, '/ready')
            if gen != self.generation: return
            if not self.allowed(): self.notice(ARCHIVED)
            elif not result.get('ready'): self.notice(UNSUPPORTED)
            else: self.notice(READY)
        except asyncio.CancelledError: raise
        except Exception:
            if gen == self.generation: self.notice(UNSUPPORTED)

    async def prepare_task(self, gen):
        try:
            result = await asyncio.to_thread(self.http, '/ready')
            if gen != self.generation: return
            if not result.get('ready'): self.notice(UNSUPPORTED); return
            context = self.session if self.session and not self.model.is_hidden(self.session) else ''
            found = await asyncio.to_thread(self.http, '/target', {'sessionId': context})
            if gen != self.generation: return
            cwd = found.get('cwd')
            if found.get('status') != 'ok' or not isinstance(cwd, str) or not cwd:
                self.notice(NO_TARGET); return
            label = found.get('label') if isinstance(found.get('label'), str) else ''
            self.cwd = cwd
            self.label = _truncate_utf8(sanitize_display_text(label.strip()), MAX_LABEL).decode()
            self.out.append((TASK_TARGET, self.identity()+self.label.encode()))
            self.notice(READY)
        except asyncio.CancelledError: raise
        except Exception:
            if gen == self.generation: self.notice(UNSUPPORTED)

    def receive(self, kind, body):
        if kind in (0x40, TASK_START) and len(body) == 28 and int.from_bytes(body[:8], 'little') == self.model.epoch:
            sid = int.from_bytes(body[8:12], 'little'); nonce = body[12:28]
            if nonce == self.nonce:
                if self.mode == 'task' and self.label and self.status not in (0, UNSUPPORTED, NO_TARGET):
                    self.out.append((TASK_TARGET, self.identity()+self.label.encode()))
                self.notice(self.status); return True
            self.cancel_task()
            self.nonce, self.sid = nonce, sid
            self.mode = 'task' if kind == TASK_START else 'reply'
            self.cwd = self.label = ''
            self.session = self.model.sessions.get(sid, {}).get('sessionId', '')
            self.status = 0; self.out.clear()
            self.text = ''; self.head = b''; self.packets.clear(); self.next_seq = 0
            self.finish_count = None; self.samples = 0; self.submitting = False
            self.started = self.touched = time.monotonic()
            if self.mode == 'task':
                if not (self.audio_supported and self.task_supported): self.notice(UNSUPPORTED)
                else: self.launch(self.prepare_task)
            elif not self.audio_supported: self.notice(UNSUPPORTED)
            elif not self.allowed(): self.notice(ARCHIVED)
            elif self.model.sessions[sid].get('state') in ('question','waiting') or any(q.status == 4 and int.from_bytes(q.identity[8:12], 'little') == sid for q in self.model.questions.values()): self.notice(QUESTION)
            else: self.launch(self.prepare)
            return True
        if kind not in range(0x42, 0x47): return False
        if not self.matches(body): return True
        self.touched = time.monotonic()
        if kind == 0x42 and self.status == READY and len(body) == 43:
            head = body[24:]
            if head[:10] != b'OpusHead\x01\x01' or head[18] != 0 or int.from_bytes(head[10:12], 'little') > 960:
                self.notice(BROKEN)
            else: self.head = head
        elif kind == 0x43 and self.status == READY and len(body) == 32:
            count, samples = struct.unpack('<II', body[24:])
            if not 1 <= count <= 3001 or not 0 < samples <= 960000 or samples > count*320:
                self.notice(BROKEN)
            else:
                self.finish_count, self.samples, self.finish_at = count, samples, time.monotonic()
                self.ack()
        elif kind == 0x44:
            # A submitted message cannot be undone by discarding the recording.
            if not self.submitting and self.status not in (SENT, QUEUED, UNKNOWN):
                self.cancel_task(); self.packets.clear(); self.text = ''; self.notice(EXPIRED)
        elif kind == 0x45:
            if self.status in (DRAFT, FAILED) and not self.submitting:
                self.submitting = True; self.launch(lambda gen: self.deliver(gen, False))
            elif self.status in (SENT, QUEUED): self.notice(self.status)
            elif self.status == UNKNOWN and not (self.task and not self.task.done()): self.launch(lambda gen: self.deliver(gen, True))
        elif kind == 0x46:
            if self.submitting or self.status == UNKNOWN:
                if not (self.task and not self.task.done()): self.launch(lambda gen: self.deliver(gen, True))
            else: self.notice(self.status)
        return True

    def feed(self, raw):
        if self.status != READY or not self.head or len(raw) != 62: return
        stream, seq, samples, flags, version = HEADER.unpack_from(raw)
        if stream != int.from_bytes(self.nonce[:4], 'little'): return
        if version != 1 or seq > 3000 or seq > self.next_seq+100 or samples not in (0,320) or flags not in (0,1) or (samples == 0) != bool(flags):
            self.notice(BROKEN); return
        packet = (bytes(raw[12:]), samples, flags)
        if seq in self.packets and self.packets[seq] != packet:
            self.notice(BROKEN); return
        self.packets[seq] = packet
        while self.next_seq in self.packets: self.next_seq += 1
        if seq+1 != self.next_seq or time.monotonic()-self.last_ack >= .2: self.ack()

    def ack(self):
        self.last_ack = time.monotonic()
        self.out = [m for m in self.out if m[0] != 0x47]
        self.out.append((0x47, self.identity()+struct.pack('<I',self.next_seq)))

    async def transcribe(self, gen):
        try:
            audio = await asyncio.to_thread(decode_opus, self.head, [self.packets[i][0] for i in range(self.finish_count)], self.samples)
            if gen != self.generation: return
            self.packets.clear()
            result = await asyncio.to_thread(self.http, '/transcribe', audio)
            if gen != self.generation: return
            text = result.get('text', '')
            if not self.allowed(): self.notice(ARCHIVED)
            elif not isinstance(text,str) or len(text.encode()) > MAX_TEXT: self.notice(ASR_ERROR)
            elif not text.strip(): self.notice(EMPTY)
            else: self.notice(DRAFT, text.strip())
        except asyncio.CancelledError: raise
        except Exception:
            if gen == self.generation: self.notice(ASR_ERROR)
        finally:
            if gen == self.generation: self.packets.clear()

    async def deliver(self, gen, query):
        try:
            if self.mode == 'task':
                result = await asyncio.to_thread(self.http, '/create-query' if query else '/create',
                    {'id':self.nonce.hex(), 'cwd':self.cwd, 'text':self.text})
            else:
                result = await asyncio.to_thread(self.http, '/query' if query else '/submit',
                    {'id':self.nonce.hex(), 'sessionId':self.session, 'text':self.text})
            if gen != self.generation: return
            code = {'accepted':SENT,'queued':QUEUED,'not_sent':FAILED,'archived':ARCHIVED,'question':QUESTION,
                    'invalid':FAILED,'unavailable':FAILED,'no_target':NO_TARGET}.get(result.get('status'), UNKNOWN)
            self.notice(code)
            self.submitting = code == UNKNOWN
            if code in (SENT,QUEUED): self.text = ''
        except asyncio.CancelledError: raise
        except Exception:
            if gen == self.generation: self.notice(UNKNOWN); self.submitting = True

    def poll(self):
        now = time.monotonic()
        if self.nonce and not self.allowed() and self.status not in (ARCHIVED,SENT,QUEUED,UNKNOWN):
            self.cancel_task(); self.packets.clear(); self.notice(ARCHIVED)
        if self.status == READY:
            if self.finish_count is not None:
                if self.next_seq == self.finish_count and not (self.task and not self.task.done()):
                    if (sum(p[1] for p in self.packets.values()) != self.samples or
                        any(p[2] != (i == self.finish_count-1) for i,p in self.packets.items())):
                        self.packets.clear(); self.notice(BROKEN)
                        messages,self.out = self.out,[]
                        return messages
                    self.status = 0  # decoding; late audio cannot mutate the completed capture
                    self.stt_started = now
                    self.launch(self.transcribe)
                elif now-self.finish_at > 2: self.packets.clear(); self.notice(BROKEN)
            elif now-self.started > 70: self.packets.clear(); self.notice(BROKEN)
        if self.nonce and now-self.touched > 600 and self.status not in (SENT,QUEUED,UNKNOWN,EXPIRED):
            self.cancel_task(); self.packets.clear(); self.text=''; self.notice(EXPIRED)
        messages,self.out = self.out,[]
        return messages

    def disconnected(self):
        self.audio_supported = False
        self.task_supported = False
        if self.status in (0,READY):
            self.cancel_task(); self.packets.clear(); self.notice(BROKEN)
