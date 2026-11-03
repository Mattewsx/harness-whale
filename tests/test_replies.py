import asyncio, os, shutil, struct, sys, unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'harness/bridge'))
from harness_ble_bridge import HarnessDatagram,StatusModel
import whale_v2 as v

class ReplyIntegration(unittest.IsolatedAsyncioTestCase):
    async def test_main_only_and_read_all_completed_pages(self):
        model=StatusModel();loop=asyncio.get_running_loop()
        transport,_=await loop.create_datagram_endpoint(lambda:HarnessDatagram(model),local_addr=('127.0.0.1',0))
        env={**os.environ,'HARNESS_WHALE_UDP_PORT':str(transport.get_extra_info('sockname')[1]),'HARNESS_WHALE_TELEMETRY_PORT':'0','HARNESS_WHALE_BRIDGE_SCRIPT':'missing-fixture'}
        proc=await asyncio.create_subprocess_exec(shutil.which('node'),Path(__file__).with_name('replay_replies.mjs'),env=env,stdin=asyncio.subprocess.PIPE)
        try:
            m=model.workspace
            async def wait_for(check):
                for _ in range(150):
                    if check():return
                    await asyncio.sleep(.02)
                self.fail('timeout waiting for fixture')
            await wait_for(lambda:'root-b' in m.keys and m.sessions[m.keys['root-a']].get('replyId'))
            self.assertEqual(set(m.keys),{'root-a','root-b'})
            self.assertFalse(m.questions)
            wire=v.Link(m);wire.receive(2,v.pack('BBIHHB',2,2,511,20,20,0));wire.snapshot()
            first=wire.changes();self.assertNotIn(0x15,[t for t,_ in first])
            self.assertEqual(sum(t==0x19 for t,_ in first),2)
            async def read(key):
                sid=m.keys[key];rev=m.sessions[sid]['_reply'];page=0;chunks=[]
                while True:
                    prefix=v.pack('QIII',m.epoch,sid,rev,page)
                    self.assertEqual(wire.receive(0x1a,prefix),[])
                    await wait_for(lambda:(sid,rev,page) in m.reply_cache)
                    b=next(body for t,body in wire.changes() if t==0x1b)
                    self.assertEqual(b[:20],prefix)
                    count,size=struct.unpack_from('<IH',b,20);self.assertEqual(len(b)-26,size)
                    chunks.append(b[26:].decode());page+=1
                    if page==count:return ''.join(chunks)
            self.assertEqual(await read('root-a'),'第一段。\n'+'长回复。'*120+'最后一句。')
            self.assertEqual(await read('root-b'),'另一会话的上次回复')
            self.assertEqual(wire.receive(0x1a,v.pack('QIII',m.epoch,m.keys['root-a'],0,0))[0][0],0x32)
            compat=v.Link(m);compat.receive(2,v.pack('BBIHHB',2,2,255,20,20,0));compat.snapshot()
            self.assertFalse(any(t==0x19 for t,_ in compat.changes()))
        finally:
            if proc.returncode is None:
                proc.stdin.write(b'end\n');await proc.stdin.drain();await asyncio.wait_for(proc.wait(),5)
            transport.close()
