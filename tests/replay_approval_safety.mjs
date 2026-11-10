// Synthetic complete-scope approvals and races; no external bridge or device required.
import assert from 'node:assert/strict'
import dgram from 'node:dgram'
const bridge = dgram.createSocket('udp4')
await new Promise(resolve => bridge.bind(0, '127.0.0.1', resolve))
process.env.HARNESS_WHALE_UDP_PORT = String(bridge.address().port)
process.env.HARNESS_WHALE_TELEMETRY_PORT = '0'
process.env.HARNESS_WHALE_BRIDGE_SCRIPT = 'missing-safety-fixture'
const {apply, approvalPrompt} = await import('../harness/plugin/index.ts')
const pause = ms => new Promise(resolve => setTimeout(resolve, ms))
const hooks = new Map(), registry = {archivedSessionIds: []}
let cleanup
const questions = [], results = []
bridge.on('message', (data, peer) => {
  const event = JSON.parse(data)
  if (event.kind === 'whale-result') results.push(event)
  if (event.kind !== 'harness-question') return
  questions.push(event)
  bridge.send(Buffer.from(JSON.stringify({v:1, kind:'harness-answer', requestId:event.requestId, selected:0})), peer.port, '127.0.0.1')
})
apply({workspaceRegistry:registry, on:(name, fn)=>hooks.set(name,fn), commands:{register(){}},
  effect:fn=>{cleanup=fn()}, sessionTitle:{get:()=>({title:'Synthetic safety test'})}})
let serial=0
async function approve({args={command:'npm test'}, reason='run tests', displayReason,
    tool='bash', missingCall=false, duringCleanup, computer='rejected'}={}) {
  const id='approval-'+(++serial), controller=new AbortController()
  const call={type:'tool/call',data:{id, name:tool, arguments:args}}
  const session={header:{id},ownEvents:()=>missingCall?[]:[call]}
  hooks.get('session/event')(session,{type:'turn/start',data:{}})
  const req={agent:{session},toolName:tool,callId:id,reason,displayReason,signal:controller.signal}
  let computerAborted=false
  const count=questions.length
  const promise=hooks.get('approval/request')(req,()=>new Promise(resolve=>{
    req.signal.addEventListener('abort',()=>{
      computerAborted=true
      if (duringCleanup) setTimeout(()=>{
        if(duringCleanup==='cancel')controller.abort(new Error('cancelled during cleanup'))
        if(duringCleanup==='hide')registry.archivedSessionIds.push(id)
        resolve('cancelled')
      },20)
      else resolve('cancelled')
    },{once:true})
    // Computer fallbacks return without waiting for a device question.
    setTimeout(()=>{if(!computerAborted)resolve(computer)},60)
  }))
  let value,error
  try {value=await promise} catch(e) {error=e}
  await pause(25)
  return {value,error,computerAborted,offered:questions.slice(count)}
}
try {
  const multiline={command:'echo safe # comment\nrm dangerous',cwd:'/tmp',permission:'network'}
  const prompt=approvalPrompt('bash',multiline,'all commands and network')
  assert(prompt && Buffer.byteLength(prompt)<=512)
  assert.deepEqual(JSON.parse(prompt.slice(prompt.indexOf('{'))),{tool:'bash',arguments:multiline,reason:'all commands and network'})
  assert(prompt.includes('\\nrm dangerous'))
  assert.equal(approvalPrompt('bash',{command:'x'.repeat(500)+';hidden-danger'},'run'),null)
  assert.equal(approvalPrompt('bash',{command:'echo safe',scope:'x'.repeat(510)},'run'),null)
  assert.equal(approvalPrompt('bash',{command:'echo \x1b[2J'},'run'),null)
  assert.equal(approvalPrompt('bash',{command:'echo \u202eexe'},'run'),null)
  assert.equal(approvalPrompt('bash',undefined,'run'),null)
  assert.equal(approvalPrompt('bash',{command:'echo hi'},undefined),null)
  assert.equal(approvalPrompt('edit_file',{path:'/tmp/a'},'edit'),null)
  const change={path:'/tmp/a',old_string:'old',new_string:'new\nfull content'}
  assert.deepEqual(JSON.parse(approvalPrompt('edit_file',change,'edit').split('允许？ ')[1]).arguments,change)
  const sparse=[];sparse.length=1;sparse.extra='hidden'
  assert.equal(approvalPrompt('bash',{command:'echo hi',sparse},'run'),null)
  assert.equal(approvalPrompt('bash',{command:'echo hi',get hidden(){return 'mutable'}},'run'),null)
  for(const scenario of [
    {args:{command:'x'.repeat(500)+'; hidden dangerous suffix'}},
    {args:{command:'echo hi',scope:'privileged network '.repeat(40)}},
    {missingCall:true}, {args:undefined,missingCall:true},
    {tool:'edit_file',args:{path:'/tmp/a'}},
    {tool:'edit_file',args:{path:'/tmp/a',content:'x'.repeat(510)}},
    {args:{command:'echo \x1b[2J'}},
    {reason:undefined,displayReason:'permission scope '.repeat(40)},
  ]) {
    const fallback=await approve(scenario)
    assert.equal(fallback.value,'rejected')
    assert.equal(fallback.computerAborted,false)
    assert.equal(fallback.offered.length,0)
  }
  const shell=await approve({args:multiline,reason:'allow full command and network'})
  assert.equal(shell.value,'allowed-once');assert(shell.computerAborted)
  assert.deepEqual(JSON.parse(shell.offered[0].question.split('允许？ ')[1]).arguments,multiline)
  const file=await approve({tool:'edit_file',args:change,reason:'replace full contents'})
  assert.equal(file.value,'allowed-once')
  assert.deepEqual(JSON.parse(file.offered[0].question.split('允许？ ')[1]).arguments,change)
  const scoped=await approve({reason:'permission: network',displayReason:{en:'Contact example.com'}})
  assert.equal(scoped.value,'allowed-once')
  assert.deepEqual(JSON.parse(scoped.offered[0].question.split('允许？ ')[1]).reason,
    {reason:'permission: network',displayReason:{en:'Contact example.com'}})
  for(const duringCleanup of ['cancel','hide']) {
    const cancelled=await approve({duringCleanup})
    assert(cancelled.error,`${duringCleanup} must not grant permission`)
    assert.equal(cancelled.value,undefined)
    const statuses=results.filter(r=>r.requestId===cancelled.offered[0].requestId).map(r=>r.status)
    assert(statuses.includes(2));assert(!statuses.includes(0));assert(!statuses.includes(1))
  }
  console.log('PASS: full approval scope, computer fallback, multiline shell, cancellation and visibility cleanup races')
} finally {cleanup();bridge.close()}
