from pathlib import Path
import sys,subprocess
w=Path(__file__).resolve().parent
sys.path.insert(0,str(w.parents[1]/'harness/bridge'))
import whale_v2 as v

m=v.Workspace()
for key in ['alpha','beta']:
    m.update(dict(sessionId=key,runId='turn1',title='同名任务',state='tool',tool='terminal',todoDone=0,todoTotal=4))
m.question(dict(sessionId='beta',runId='turn1',question='选择格式',options=['文本','网页','图片','日志','摘要'],requestId='q-beta'),('127.0.0.1',1))
q=next(iter(m.questions.values()));link=v.Link(m);link.receive(2,v.pack('BBIHHB',2,2,255,20,20,0))
lines=[];serial=[0,0]
def send(kind,body):
    lane=kind in v.CONTROL;serial[lane]+=1
    for frag in v.fragments(kind,serial[lane],body):lines.append('rx '+frag.hex())
send(1,v.pack('BBIHH',2,2,255,20,20))
frames=link.snapshot()
# Corrupt only the snapshot total while leaving its CRC unchanged.
bad=bytearray(frames[0][1]);bad[24]=3
for kind,body in [(17,bytes(bad)),*frames[1:]]:send(kind,body)
lines.append('assert 0 0 0')
for kind,body in frames:send(kind,body)
lines+=['assert 0 2 0','key 2','key 1','key 2','key 2','assert 8 2 1']
for kind,body in link.receive(0x24,q.identity+b'\xff')+link.receive(0x24,q.identity+b'\0'):send(kind,body)
lines+=['assert 10 2 1','key 1','key 1','key 1','assert 8 2 1','key 2','assert 8 2 1']
for kind,body in link.receive(0x24,q.identity+b'\1'):send(kind,body)
lines+=['key 1','key 2','assert 13 2 1','key 2','down','assert 14 2 1']
send(1,v.pack('BBIHH',2,2,255,20,20))
for kind,body in link.snapshot():send(kind,body)
m.resolve(q.request,0,4)
for kind,body in link.receive(0x25,q.identity):send(kind,body)
lines+=['assert 16 2 1']
for kind,body in link.changes():send(kind,body)
# Select a completed answer in the first session and read both pages.
lines+=['key 2','key 2','key 0','assert 1 2 0']
sid=m.keys['alpha'];rev=9;preview='真实回复'.encode()
send(0x19,v.pack('QIIB',m.epoch,sid,rev,len(preview))+preview)
lines+=['key 2','assert 0 2 0','key 1','assert 20 2 0']
def reply_page(session,page,text):
    data=text.encode()
    send(0x1b,v.pack('QIIIIH',m.epoch,session,rev,page,2,len(data))+data)
reply_page(m.keys['beta'],0,'别的会话')
lines.append('assert 20 2 0')
reply_page(sid,0,'一。\n二。\n三。\n四。\n五。\n六。\n七。')
lines+=['assert 21 2 0','scroll 0','key 1','scroll 1','key 0','scroll 0','key 1','key 1','assert 21 2 0']
reply_page(sid,1,'末页')
lines+=['assert 21 2 0']

# Backward scrolling stays in one reply, and reopening uses the local cache.
lines += ['key 0','assert 21 2 0','scroll 0','key 2','assert 0 2 0','key 2','assert 1 2 0',
          'key 1','key 2','assert 0 2 1','key 257','key 0','key 2','assert 0 2 0','key 1','assert 21 2 0']
# The latest final reply replaces the old one; old replies have no navigation entry.
rev=10;send(0x19,v.pack('QIIB',m.epoch,sid,rev,len(preview))+preview)
lines+=['assert 20 2 0']
reply_page(sid,0,'用户选择更新')
lines+=['assert 21 2 0']
# Archiving the focused task while its final reply is open closes the reader.
m.set_visibility('alpha',True,10000)
for kind,body in link.changes():send(kind,body)
lines.append('assert 1 1 0')
# A stale page from the archived task must not reopen it.
reply_page(sid,1,'过期数据');lines.append('assert 1 1 0')
m.set_visibility('beta',True,10001)
for kind,body in link.changes():send(kind,body)
lines.append('assert 1 0 0')
# Page to the last window, then remove its only row while selected.
for i in range(17):m.update(dict(sessionId='s'+str(i),runId='r',title='同名',sentAt=11000))
for kind,body in link.changes():send(kind,body)
lines+=['key 1']*8
for kind,body in link.snapshot(8):send(kind,body)
lines+=['key 1']*8
for kind,body in link.snapshot(16):send(kind,body)
lines.append('assert 1 1 0')
m.set_visibility('s16',True,12000)
for kind,body in link.changes():send(kind,body)
lines.append('assert 1 8 0')
# A pending device question must disappear if its owning task disappears.
m.question(dict(sessionId='s8',runId='r',question='继续吗',options=['是','否'],requestId='archived-question'),('127.0.0.1',1))
for kind,body in link.changes():send(kind,body)
lines+=['key 2','assert 0 8 0','key 2','assert 8 8 0']
m.set_visibility('s8',True,13000)
for kind,body in link.notices()+link.changes():send(kind,body)
lines.append('assert 1 7 0')

r=subprocess.run([w/'fw_replay.exe'],input='\n'.join(lines)+'\n',capture_output=True,text=True,timeout=8)
if r.returncode:raise RuntimeError(r.stderr+r.stdout)
submits=[v for v in r.stdout.splitlines() if v.startswith('tx 22 ')]
assert submits==['tx 22 '+(q.identity+b'\4\0').hex()],submits
assert 'tx 25 '+q.identity.hex() in r.stdout
print('PASS: Python bridge -> actual C firmware dispatcher; corrupt metadata rejected, same-title tasks, page identity, blocked partial submit, one answer only, disconnect/reconnect result query')

assert 'tx 1a '+v.pack('QIII',m.epoch,sid,9,1).hex() in r.stdout
print('PASS: latest reply -> continuous scroll -> cached reopen; wrong-session and old replies rejected')
