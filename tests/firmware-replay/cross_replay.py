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
lines.append('assert 26 0 0')
for kind,body in frames:send(kind,body)
lines+=['assert 26 2 0','key 2','key 1','key 2','assert 8 2 1']
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
lines+=['key 2','key 0','key 0','assert 1 2 0']
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
lines += ['key 0','assert 21 2 0','scroll 0','key 2','assert 0 2 0','key 0','assert 1 2 0',
          'key 1','key 2','assert 0 2 1','key 0','key 0','key 2','assert 0 2 0','key 1','assert 21 2 0']
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
lines+=['key 1'] # Empty-list focus stays on settings; Down selects the first task.
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
lines+=['key 2','assert 8 8 0']
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

# v1.3 extension: real Python frames drive global state, silent baseline,
# background completion and an off-page work interruption in production C.
m=v.Workspace();m.set_roster({f'g{i}' for i in range(12)})
for i in range(12):m.update(dict(sessionId=f'g{i}',runId='run',state='idle',title=f'Task {i}'))
link=v.Link(m);link.receive(2,v.pack('BBIHHB',2,2,v.CAPS,20,20,0));lines=[];serial=[0,0]
send(1,v.pack('BBIHH',2,2,v.CAPS,20,20))
for kind,body in link.snapshot()+link.changes():send(kind,body)
lines+=['view 26','cues 0','advance 119000']
link.global_at=0
for kind,body in link.changes():send(kind,body)
lines+=['advance 1001','view 28']
sid=m.keys['g11'];m.update(dict(sessionId='g11',runId='run',state='thinking',title='Outside window'))
for kind,body in link.changes():send(kind,body)
lines+=['view 18']
for kind,body in link.receive(0x52,v.pack('QI',m.epoch,sid)):send(kind,body)
lines+=['view 0','key 256','key 0','key 2','view 24']
m.update(dict(sessionId='g11',runId='run',state='done',replyId='final',title='Outside window'))
changes=link.changes()
for kind,body in changes:send(kind,body)
lines+=['view 24','advance 1001','cues 1']
for kind,body in changes:
    if kind==0x51:send(kind,body)
lines+=['advance 1001','cues 1']
r=subprocess.run([w/'fw_replay.exe'],input='\n'.join(lines)+'\n',capture_output=True,text=True,timeout=8)
if r.returncode:raise RuntimeError(r.stderr+r.stdout)
assert 'tx 52 '+v.pack('QI',m.epoch,sid).hex() in r.stdout
print('PASS: v1.3 global frames, off-page work seek, attention interruption and duplicate cue suppression')

# v1.4 desk display: the Python wall clock reaches production C, an idle home
# becomes the desk display after 120 s, and work starting returns home.
m=v.Workspace();m.set_roster({'d0','d1'})
for i in range(2):m.update(dict(sessionId=f'd{i}',runId='run',state='idle',title=f'Desk {i}'))
link=v.Link(m);link.receive(2,v.pack('BBIHHB',2,2,v.CAPS,20,20,0));lines=[];serial=[0,0]
send(1,v.pack('BBIHH',2,2,v.CAPS,20,20))
for kind,body in link.snapshot()+link.changes():send(kind,body)
clock=link.clock(now=1793975400,tz_minutes=480)  # 2026-11-06 22:30 in UTC+8
assert [k for k,_ in clock]==[0x53]
for kind,body in clock:send(kind,body)
lines+=['clock 22 30','view 26','advance 119000','view 26']
link.global_at=0  # the bridge's 10 s heartbeat summary keeps the idle state fresh
for kind,body in link.changes():send(kind,body)
lines+=['advance 1001','view 28']
m.update(dict(sessionId='d1',runId='run2',state='thinking',title='Desk 1'))
for kind,body in link.changes():send(kind,body)
lines+=['view 0','assert 0 2 1']
r=subprocess.run([w/'fw_replay.exe'],input='\n'.join(lines)+'\n',capture_output=True,text=True,timeout=8)
if r.returncode:raise RuntimeError(r.stderr+r.stdout)
print('PASS: v1.4 wall clock sync, idle desk display entry and work-start return home')

# Protocol 2.3: after the production C handshake grants bit 12, the same Python model
# delivers a full eight-row window in version-3 frames; 2.2 frames remain accepted.
m=v.Workspace()
for i in range(8):m.update(dict(sessionId=f'L{i}',runId='run',state='done',title=f'长标题任务{i}'+'很长'*12,replyId='r',replyPreview='预览'))
offered=v.offered_frame(247);assert offered==244
link=v.Link(m);link.offered=offered
granted=v.pack('BBIHHB',2,3,v.CAPS|v.LARGE_FRAMES,offered,offered,0)
link.receive(2,granted);assert link.ready and link.frame==offered
lines=[f'limit {offered}'];serial=[0,0]
send(*v.hello(offered));lines.append(f'frame {offered}')
window=link.snapshot()+link.changes()
def frames(limit):return sum(len(list(v.fragments(k,1,b,limit))) for k,b in window)
large,legacy=frames(offered),frames(20)
for kind,body in window:
    lane=kind in v.CONTROL;serial[lane]+=1
    for frag in v.fragments(kind,serial[lane],body,link.frame):lines.append('rx '+frag.hex())
lines+=['assert 26 8 0']
# Disconnect drops the grant; a later 2.2 hello on the same device stays legacy.
lines+=['down','frame 20','limit 20']
send(*v.hello(offered));lines.append('frame 20')
r=subprocess.run([w/'fw_replay.exe'],input='\n'.join(lines)+'\n',capture_output=True,text=True,timeout=8)
if r.returncode:raise RuntimeError(r.stderr+r.stdout)
hellos=[x for x in r.stdout.splitlines() if x.startswith('tx 02 ')]
assert hellos==['tx 02 '+granted.hex(),'tx 02 '+v.pack('BBIHHB',2,3,v.CAPS,20,20,0).hex()],hellos
assert large*4<legacy,(large,legacy)
print(f'PASS: protocol 2.3 grant from C, version-3 eight-row window ({large} frames vs {legacy} legacy), reset on disconnect')

# Integrated features: time sync and global work outside the current page must
# still reach the desk display after switching to large frames.
m=v.Workspace();m.set_roster({f'I{i}' for i in range(10)})
for i in range(10):m.update(dict(sessionId=f'I{i}',runId='r',state='idle',title=f'Idle {i}'))
link=v.Link(m);link.offered=244
link.receive(2,v.pack('BBIHHB',2,3,v.CAPS|v.LARGE_FRAMES,244,244,0))
lines=['limit 244'];serial=[0,0]
send(*v.hello(244));lines.append('frame 244')
def send_large(messages):
    for kind,body in messages:
        lane=kind in v.CONTROL;serial[lane]+=1
        lines.extend('rx '+f.hex() for f in v.fragments(kind,serial[lane],body,244))
send_large(link.snapshot()+link.changes())
off_page=next(sid for sid in m.sessions if sid not in link.rows)
off_key=next(key for key,sid in m.keys.items() if sid==off_page)
send_large(link.clock(now=1793975400,tz_minutes=480))
lines+=['clock 22 30','advance 119000']
link.global_at=0
send_large(link.changes())
lines+=['advance 1001','view 28']
m.update(dict(sessionId=off_key,runId='new',state='thinking',title='Off-page task'))
send_large(link.changes())
lines+=['view 18']
send_large(link.receive(0x52,v.pack('QI',m.epoch,off_page)))
lines+=['view 0',f'selected {off_page}']
r=subprocess.run([w/'fw_replay.exe'],input='\n'.join(lines)+'\n',capture_output=True,text=True,timeout=8)
if r.returncode:raise RuntimeError(r.stderr+r.stdout)
assert 'tx 52 '+v.pack('QI',m.epoch,off_page).hex() in r.stdout
print('PASS: integrated 2.3 clock sync, idle desk display and off-page work interruption')

# A global question outside the cached page counts once alongside its reply.
# Visiting the picker or task is not a read receipt for the question text.
m = v.Workspace()
m.set_roster({str(i) for i in range(10)})
for i in range(10):
    m.update(dict(sessionId=str(i), runId='run', title='Session', state='idle'))
m.update(dict(sessionId='9', runId='run', title='Question', state='question', replyId='reply'))
m.question(dict(sessionId='9', runId='run', requestId='pick', question='Which output format do you want to use for this document?', options=['A','B','C']), None)
link = v.Link(m)
link.receive(2, v.pack('BBIHHB',2,2,v.CAPS,20,20,0))
lines = []
serial = [0,0]
send(*v.hello())
for kind, body in link.snapshot(0) + link.changes(): send(kind, body)
lines += ['view 26','badge 1','key 2','view 18']
for kind, body in link.snapshot(0): send(kind, body)
lines += ['view 1','draw','badge 1','key 2','view 8','draw','badge 1']
question = m.by_request['pick']
for kind, body in link.receive(0x24, question + bytes([255])) + link.receive(0x24, question + bytes([0])):
    send(kind, body)
lines += ['view 10','options 3','draw','draw','badge 1','key 257','view 11','key 2','view 10','key 0','key 2','view 0']
result = subprocess.run([w/'fw_replay.exe'],input='\n'.join(lines)+'\n',capture_output=True,text=True,timeout=8)
if result.returncode: raise RuntimeError(result.stderr+result.stdout)
epoch, sid, run, qid = v.IDENTITY.unpack(question)
receipt = v.pack('QIIB', epoch, sid, qid, 3)
assert [x for x in result.stdout.splitlines() if x.startswith('tx 4a ')] == ['tx 4a '+receipt.hex()]
link.receive(0x4a, receipt)
assert m.row(sid, True, True)[10] & 56 == 8 and m.questions[question].status == 4
assert 'tx 22 ' not in result.stdout
print('PASS: room badge counted once, read on display, deferred without answering, reply still unread')

# Read session 1, navigate to 9 and back with the real keys, then archive it off-page.
m = v.Workspace()
for i in range(9):
    m.update(dict(sessionId=str(i), runId='r', state='done', replyId='answer'))
link = v.Link(m)
link.receive(2, v.pack('BBIHHB', 2, 2, v.CAPS, 20, 20, 0))
lines, serial = [], [0, 0]
send(*v.hello())
def window(cursor, sort=0):
    for msg in link.receive(0x10, v.pack('QIBB', m.epoch, cursor, 8, sort)) + link.changes():
        send(*msg)
window(0, 2)
sid = m.keys['0']
rev = m.sessions[sid]['_reply']
lines += ['key 2']
window(0, 2)
lines += ['key 2','view 20']
send(0x1b, v.pack('QIIIIH',m.epoch,sid,rev,0,1,5) + b'first')
lines += ['view 21','draw','key 2','view 0','key 0']
link.receive(0x4a, v.pack('QIIB', m.epoch, sid, rev, 1))
window(0)
def next_window():
    lines.extend(['key 1'] * 8)
    window(8)
    lines.append('selected '+str(m.keys['8']))
next_window()
lines += [f'cached {sid} {rev} 1','key 0']
window(0)
lines += ['key 0'] * 7 + [f'selected {sid}','key 2','key 1','view 21','key 2','key 0']
window(0)
next_window()
offpage_lines = lines.copy()
m.set_visibility('0', True, 123)
for msg in link.changes(): send(*msg)
lines.append(f'cached {sid} {rev} 0')
result = subprocess.run([w/'fw_replay.exe'],input='\n'.join(lines)+'\n',capture_output=True,text=True,timeout=8)
if result.returncode: raise RuntimeError(result.stderr+result.stdout)
assert [s for s in result.stdout.splitlines() if s.startswith('tx 1a ')] == [
    'tx 1a '+v.pack('QIII',m.epoch,sid,rev,0).hex()]
print('PASS: navigate 1 -> 9 -> 1, one body request, off-page archive purges the cached reply')

# A replayed notification for an old revision cannot erase a newer revision.
# If the bounded journal no longer includes every change, fail closed instead.
lines = offpage_lines
for epoch, seq, stale_rev in [(m.epoch ^ 1, 1, rev), (m.epoch, 1, rev + 1), (m.epoch, 1, rev)]:
    send(0x1c, v.pack('QQII', epoch, seq, sid, stale_rev))
    lines.append(f'cached {sid} {rev} 1')
send(0x1c, v.pack('QQII', m.epoch, 35, sid, rev + 1))
lines.append(f'cached {sid} {rev} 0')
subprocess.run([w/'fw_replay.exe'],input='\n'.join(lines)+'\n',capture_output=True,text=True,timeout=8,check=True)
print('PASS: cache invalidations validate epoch, exact revision, duplicates and sequence gaps')

# Bit 15: the bridge's approval and multi-select questions drive production C.
for mode,labels,keys,expect in [
    ('multi',['摘要','日志','图片','附录'],['key 2','key 1','key 1','key 2','key 1'],None),
    ('approval',['允许','拒绝'],['key 0','key 2','view 12','key 2','view 13'],'tx 22 '),
]:
    m=v.Workspace()
    m.update(dict(sessionId='r0',runId='run',state='waiting',hint=2,title='审批'))
    m.question(dict(sessionId='r0',runId='run',question='允许运行命令？ npm test',options=labels,requestId='rq',mode=mode),('127.0.0.1',1))
    q=next(iter(m.questions.values()))
    link=v.Link(m);link.receive(2,v.pack('BBIHHB',2,2,v.CAPS,20,20,0));lines=[];serial=[0,0]
    send(1,v.pack('BBIHH',2,2,v.CAPS,20,20))
    for kind,body in link.snapshot():send(kind,body)
    lines+=['key 2','view 18']
    for kind,body in link.snapshot():send(kind,body)
    lines+=['view 1','key 2','view 8']
    for kind,body in link.receive(0x24,q.identity+b'\xff')+link.receive(0x24,q.identity+b'\0'):send(kind,body)
    lines+=['view 10']+keys
    if mode=='multi':
        lines+=['view 8']
        for kind,body in link.receive(0x24,q.identity+b'\1'):send(kind,body)
        lines+=['view 10','key 2','key 1','key 2','view 13']
    r=subprocess.run([w/'fw_replay.exe'],input='\n'.join(lines)+'\n',capture_output=True,text=True,timeout=8)
    if r.returncode:raise RuntimeError(r.stderr+r.stdout)
    sent=[x for x in r.stdout.splitlines() if x.startswith(('tx 22 ','tx 26 '))]
    want='tx 26 '+(q.identity+v.pack('Q',0b1101)).hex() if mode=='multi' else 'tx 22 '+(q.identity+b'\0\0').hex()
    assert sent==[want],(mode,sent)
    for kind,body in [(0x26 if mode=='multi' else 0x22,bytes.fromhex(want[6:]))]:
        assert link.receive(kind,body)[0][1][20]==4
print('PASS: bit 15 approval (deny by default, allow after full text) and paged multi-select mask from C to Python')
