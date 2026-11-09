import asyncio
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'harness/bridge'))
from whale_v2 import Workspace, Link, pack
from whale_voice import VoiceBridge, HEADER, READY, DRAFT, SENT, UNKNOWN, BROKEN, ARCHIVED, decode_opus

class AttentionTests(unittest.TestCase):
    def test_priority_refresh_is_global_stable_and_read_aware(self):
        m=Workspace()
        for i in range(10):
            m.update(dict(sessionId=f'idle-{i}',runId='r',state='idle'))
        m.update(dict(sessionId='working',runId='r',state='thinking'))
        m.update(dict(sessionId='reply',runId='r',state='done',replyId='answer'))
        m.update(dict(sessionId='error',runId='r',state='error'))
        m.update(dict(sessionId='waiting',runId='r',state='waiting'))
        link=Link(m);link.attention=True;link.ready=True
        link.snapshot()
        def names():return [m.sessions[s]['sessionId'] for s in link.rows]
        self.assertEqual(names()[:4],['error','reply','working','waiting'])
        order=list(link.order)
        m.read_mark(m.keys['error'],m.sessions[m.keys['error']]['_rid'],2)
        m.read_mark(m.keys['reply'],m.sessions[m.keys['reply']]['_reply'],1)
        link.changes()
        self.assertEqual(link.order,order)  # no moving targets while using the list
        m.update(dict(sessionId='new-question',runId='r',state='question'))
        link.changes()
        self.assertEqual(link.order[:len(order)],order)
        link.snapshot()
        self.assertEqual(names()[:3],['new-question','working','waiting'])
        removed=m.keys['waiting'];m.evict('waiting');link.changes()
        self.assertNotIn(removed,link.order)
        link.snapshot(8)
        self.assertEqual(len(link.rows),6)
        legacy=Link(m);legacy.snapshot()
        self.assertEqual(list(legacy.rows),list(m.sessions)[:8])

    def test_read_is_revision_specific_and_persists(self):
        m=Workspace(); event=dict(sessionId='synthetic',runId='r',state='error',replyId='reply-1')
        m.update(event); sid=m.keys['synthetic']; row=m.sessions[sid]
        self.assertEqual(m.row(sid)[10]&24,0)
        self.assertEqual(m.row(sid,True)[10]&24,24)
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'read.json';m.load_marks(path)
            m.read_mark(sid,row['_reply']+100,1)
            self.assertEqual(m.row(sid,True)[10]&8,8)
            m.read_mark(sid,row['_reply'],1);m.read_mark(sid,row['_rid'],2)
            self.assertEqual(m.row(sid,True)[10]&24,0)
            self.assertEqual(row['state'],'error')
            other=Workspace();other.load_marks(path);other.update(event)
            self.assertEqual(other.row(other.keys['synthetic'],True)[10]&24,0)
        m.update({**event,'replyId':'reply-2'})
        self.assertEqual(m.row(sid,True)[10]&24,8)
        m.evict('synthetic');self.assertFalse(m.sessions)

class VoiceTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.m=Workspace();self.m.update(dict(sessionId='synthetic',runId='r',state='idle'))
        self.sid=self.m.keys['synthetic'];self.calls=[]
        def http(path,data=None):
            self.calls.append((path,data))
            return {'ready':True} if path=='/ready' else {'text':'测试语音'} if path=='/transcribe' else {'status':'accepted'}
        self.v=VoiceBridge(self.m,http);self.v.audio_supported=True
        self.nonce=b'0123456789abcdef'
        self.v.receive(0x40,pack('QI',self.m.epoch,self.sid)+self.nonce)
        await self.v.task;self.assertEqual(self.v.status,READY)
        self.head=b'OpusHead\x01\x01'+struct.pack('<HIhB',312,16000,0,0)
        self.v.receive(0x42,self.v.identity()+self.head)
    async def asyncTearDown(self):
        self.v.cancel_task();await asyncio.sleep(0)
    def packet(self,seq,tail=False):
        return HEADER.pack(int.from_bytes(self.nonce[:4],'little'),seq,0 if tail else 320,int(tail),1)+bytes(50)
    async def test_reorder_duplicate_gap_and_ack(self):
        self.v.feed(self.packet(1));self.assertEqual(self.v.next_seq,0)
        self.v.feed(self.packet(0));self.assertEqual(self.v.next_seq,2)
        self.v.feed(self.packet(0));self.assertEqual(len(self.v.packets),2)
        self.v.receive(0x43,self.v.identity()+pack('II',4,960))
        self.v.finish_at-=3;self.v.poll();self.assertEqual(self.v.status,BROKEN)
        self.assertFalse(any(p=='/transcribe' for p,_ in self.calls))
    async def test_conflicting_duplicate_rejected(self):
        self.v.feed(self.packet(0));bad=bytearray(self.packet(0));bad[-1]=1;self.v.feed(bad)
        self.assertEqual(self.v.status,BROKEN)
    async def test_draft_requires_explicit_confirm_and_query_never_resends(self):
        self.v.notice(DRAFT,'测试语音');self.v.poll()
        self.assertFalse(any(p=='/submit' for p,_ in self.calls))
        self.v.receive(0x45,self.v.identity());await self.v.task
        self.assertEqual(self.v.status,SENT)
        self.v.receive(0x45,self.v.identity())
        self.assertEqual(sum(p=='/submit' for p,_ in self.calls),1)
        self.v.status=UNKNOWN;self.v.submitting=True
        self.v.receive(0x46,self.v.identity());await self.v.task
        self.assertEqual(self.calls[-1][0],'/query')
    async def test_disconnect_discards_incomplete_but_preserves_draft(self):
        self.v.feed(self.packet(0));self.v.disconnected()
        self.assertEqual(self.v.status,BROKEN);self.assertFalse(self.v.packets)
        self.v.notice(DRAFT,'保留');self.v.disconnected();self.assertEqual(self.v.text,'保留')
    async def test_complete_upload_transcribes_only_after_stop(self):
        from unittest.mock import patch
        self.v.feed(self.packet(0));self.v.feed(self.packet(1,True))
        self.v.poll();self.assertFalse(any(p=='/transcribe' for p,_ in self.calls))
        self.v.receive(0x43,self.v.identity()+pack('II',2,320))
        with patch('whale_voice.decode_opus',return_value=b'synthetic wave') as decode:
            self.v.poll();await self.v.task
            decode.assert_called_once()
        self.assertEqual(self.v.status,DRAFT);self.assertFalse(self.v.packets)
        self.assertEqual(self.v.text,'测试语音')
        self.assertFalse(any(p=='/submit' for p,_ in self.calls))
    async def test_waiting_question_prevents_recording(self):
        from whale_voice import QUESTION
        self.m.sessions[self.sid]['state']='waiting'
        self.v.receive(0x40,pack('QI',self.m.epoch,self.sid)+b'fedcba9876543210')
        self.assertEqual(self.v.status,QUESTION)
    async def test_archive_blocks_capture(self):
        self.m.evict('synthetic');self.v.poll();self.assertEqual(self.v.status,ARCHIVED)
    async def test_tail_samples_must_agree(self):
        self.v.feed(self.packet(0));self.v.feed(self.packet(1,True))
        self.v.receive(0x43,self.v.identity()+pack('II',2,640));self.v.poll()
        self.assertEqual(self.v.status,BROKEN)

class VoiceTaskTests(unittest.IsolatedAsyncioTestCase):
    """Hold-to-talk on the home page opens a new session instead of replying."""
    async def asyncSetUp(self):
        self.m=Workspace();self.m.update(dict(sessionId='synthetic',runId='r',state='waiting'))
        self.sid=self.m.keys['synthetic'];self.calls=[];self.target={'status':'ok','cwd':'D:\\work\\demo','label':'demo'}
        self.create={'status':'accepted'}
        def http(path,data=None):
            self.calls.append((path,data))
            if path=='/ready': return {'ready':True}
            if path=='/target': return self.target
            if path in ('/create','/create-query'): return self.create
            return {'status':'accepted'}
        self.v=VoiceBridge(self.m,http);self.v.audio_supported=True;self.v.task_supported=True
    async def asyncTearDown(self):
        self.v.cancel_task();await asyncio.sleep(0)
    async def start(self,nonce=b'0123456789abcdef',sid=None):
        self.v.receive(0x48,pack('QI',self.m.epoch,self.sid if sid is None else sid)+nonce)
        if self.v.task: await self.v.task
        return self.v.poll()
    async def test_target_precedes_ready_even_when_a_question_waits(self):
        out=await self.start()
        self.assertEqual([k for k,_ in out],[0x49,0x41])
        self.assertEqual(out[0][1],self.v.identity()+'demo'.encode())
        self.assertEqual(self.v.status,READY)
        self.assertEqual(self.calls[1],('/target',{'sessionId':'synthetic'}))
    async def test_confirm_creates_once_with_resolved_folder(self):
        from whale_voice import QUEUED
        await self.start();self.v.notice(DRAFT,'把登录页的 bug 修一下')
        self.v.receive(0x45,self.v.identity());await self.v.task
        self.assertEqual(self.v.status,SENT)
        self.assertEqual(self.calls[-1],('/create',{'id':self.v.nonce.hex(),'cwd':'D:\\work\\demo','text':'把登录页的 bug 修一下'}))
        self.v.receive(0x45,self.v.identity())
        self.assertEqual(sum(p=='/create' for p,_ in self.calls),1)
        self.assertFalse(any(p=='/submit' for p,_ in self.calls))
        self.v.status=UNKNOWN;self.v.submitting=True
        self.v.receive(0x46,self.v.identity());await self.v.task
        self.assertEqual(self.calls[-1][0],'/create-query')
    async def test_no_folder_is_reported(self):
        from whale_voice import NO_TARGET
        self.target={'status':'no_target'}
        out=await self.start()
        self.assertEqual(self.v.status,NO_TARGET);self.assertNotIn(0x49,[k for k,_ in out])
    async def test_unknown_context_and_old_firmware(self):
        from whale_voice import UNSUPPORTED
        await self.start(sid=999)
        self.assertEqual(self.calls[1],('/target',{'sessionId':''}))
        self.v.task_supported=False
        await self.start(nonce=b'fedcba9876543210')
        self.assertEqual(self.v.status,UNSUPPORTED)
    async def test_label_is_display_safe_and_bounded(self):
        self.target={'status':'ok','cwd':'/x','label':'项目'*40}
        out=await self.start()
        label=out[0][1][24:]
        self.assertLessEqual(len(label),48);label.decode()
    async def test_reply_capture_still_targets_session(self):
        await self.start()
        self.m.sessions[self.sid]['state']='idle'
        self.v.receive(0x40,pack('QI',self.m.epoch,self.sid)+b'fedcba9876543210');await self.v.task
        self.v.notice(DRAFT,'追加');self.v.receive(0x45,self.v.identity());await self.v.task
        self.assertEqual(self.calls[-1][0],'/submit')

class LinkVoiceTaskTests(unittest.TestCase):
    def test_bit_16_needs_voice_and_firmware_echo(self):
        from whale_v2 import VOICE_TASK, hello
        self.assertTrue(struct.unpack('<BBIHH',hello()[1])[2] & VOICE_TASK)
        for caps,expected in ((63|512|VOICE_TASK,True),(63|512,False),(63|VOICE_TASK,False)):
            link=Link(Workspace());link.receive(2,struct.pack('<BBIHHB',2,2,caps,20,20,0))
            self.assertEqual(link.voice_task,expected)

class VoiceInstallerTests(unittest.TestCase):
    def test_complete_payload_in_clean_install(self):
        import importlib.util
        script=Path(__file__).resolve().parents[1]/'installer/install_companion.py'
        spec=importlib.util.spec_from_file_location('voice_installer_fixture',script)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as tmp:
            plugin,bridge,_=module.install_payload(Path(tmp))
            self.assertTrue(plugin.with_name('voice.ts').is_file())
            self.assertTrue(bridge.with_name('whale_voice.py').is_file())
            self.assertTrue(bridge.with_name('whale_v2.py').is_file())

class PluginVoiceTests(unittest.TestCase):
    def test_service_replay(self):
        subprocess.run(['node',str(Path(__file__).with_name('replay_voice.mjs'))],check=True,timeout=20)
    def test_new_task_service_replay(self):
        subprocess.run(['node',str(Path(__file__).with_name('replay_voice_task.mjs'))],check=True,timeout=20)

class OpusTests(unittest.TestCase):
    def test_header_delay_codec_pin(self):
        firmware=Path(__file__).resolve().parents[1]/'device-firmware'
        self.assertIn('espressif/esp_audio_codec: "2.6.2"',
                      (firmware/'main/idf_component.yml').read_text())

    def test_speech_tail_is_preserved_with_application_header(self):
        import imageio_ffmpeg
        import math
        import wave
        import io
        # A synthetic tone through the final sample catches truncated encoder delay.
        for duration in (1, 10, 60):
            with self.subTest(seconds=duration):
                count=duration*16000
                pcm=struct.pack('<'+'h'*count,*(int(12000*math.sin(2*math.pi*440*i/16000)) for i in range(count)))
                data=subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-hide_banner','-loglevel','error',
                    '-f','s16le','-ar','16000','-ac','1','-i','pipe:0','-c:a','libopus','-b:a','20k',
                    '-vbr','off','-compression_level','0','-frame_duration','20','-application','voip',
                    '-f','ogg','pipe:1'],input=pcm,capture_output=True,check=True,timeout=15).stdout
                packets=[];partial=b'';i=0
                while i<len(data):
                    n=data[i+26];laces=data[i+27:i+27+n];start=i+27+n
                    for size in laces:
                        partial+=data[start:start+size];start+=size
                        if size<255:packets.append(partial);partial=b''
                    i=start
                head=b'OpusHead\x01\x01'+struct.pack('<HIhB',312,16000,0,0)
                self.assertEqual(packets[0],head)
                self.assertEqual(len(packets[2:]),duration*50+1)
                self.assertTrue(all(len(p)==50 for p in packets[2:]))
                with wave.open(io.BytesIO(decode_opus(head,packets[2:],count)),'rb') as wav:
                    self.assertEqual(wav.getnframes(),count)
                    decoded=struct.unpack('<'+'h'*count,wav.readframes(count))
                self.assertGreater(sum(x*x for x in decoded[-80:])/80,1000000)

    def test_real_codec_roundtrip(self):
        import imageio_ffmpeg
        # Synthetic silence only, never microphone or real session data.
        encoded=subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-hide_banner','-loglevel','error','-f','s16le',
            '-ar','16000','-ac','1','-i','pipe:0','-c:a','libopus','-b:a','20k','-vbr','off',
            '-frame_duration','20','-application','voip','-f','ogg','pipe:1'],input=bytes(32000),capture_output=True,check=True).stdout
        packets=[];partial=b'';i=0
        while i<len(encoded):
            self.assertEqual(encoded[i:i+4],b'OggS');n=encoded[i+26];laces=encoded[i+27:i+27+n];start=i+27+n
            for size in laces:
                partial+=encoded[start:start+size];start+=size
                if size<255:packets.append(partial);partial=b''
            i=start
        wave=decode_opus(packets[0],packets[2:],16000)
        self.assertEqual(len(wave),32044);self.assertEqual(wave[:4],b'RIFF')

if __name__=='__main__': unittest.main()
