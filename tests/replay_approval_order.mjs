// Cordis waterfall ordering with a computer answerer registered before the plugin.
import assert from 'node:assert/strict'
import dgram from 'node:dgram'
const bridge = dgram.createSocket('udp4')
await new Promise(resolve => bridge.bind(0, '127.0.0.1', resolve))
process.env.HARNESS_WHALE_UDP_PORT = String(bridge.address().port)
process.env.HARNESS_WHALE_TELEMETRY_PORT = '0'
process.env.HARNESS_WHALE_BRIDGE_SCRIPT = 'missing-order-fixture'
const {apply} = await import('../harness/plugin/index.ts')
const listeners = new Map(), timers = new Set()
const on = (name, callback, options = {}) => {
  const callbacks = listeners.get(name) ?? []
  callbacks[options.prepend ? 'unshift' : 'push'](callback)
  listeners.set(name, callbacks)
}
const dispatch = (name, ...args) => {
  const callbacks = [...(listeners.get(name) ?? [])]
  const next = () => callbacks.length ? callbacks.shift()(...args, next) : Promise.resolve('unavailable')
  return next()
}
const delay = (fn, ms) => {
  const timer = setTimeout(() => {timers.delete(timer); fn()}, ms)
  timers.add(timer)
  return timer
}
let active, cleanup, sequence = 0
// Like DSH's RPC answerer, the computer owns the request until it resolves.
on('approval/request', req => {
  active.computerStarted = true
  return new Promise(resolve => {
    const abort = () => {active.computerCancelled = true; resolve('cancelled')}
    if (req.signal.aborted) return abort()
    req.signal.addEventListener('abort', abort, {once:true})
    if (active.computerFirst) delay(() => resolve('rejected'), 5)
  })
})
bridge.on('message', (data, peer) => {
  const event = JSON.parse(data)
  if (event.kind !== 'harness-question') return
  active.questions++
  const selected = active.selected
  delay(() => bridge.send(Buffer.from(JSON.stringify({v:1, kind:'harness-answer',
    requestId:event.requestId, selected})), peer.port, '127.0.0.1'), active.computerFirst ? 60 : 0)
})
apply({workspaceRegistry:{archivedSessionIds:[]}, on, commands:{register(){}},
  effect:fn => {cleanup=fn()}, sessionTitle:{get:() => ({title:'Synthetic ordering test'})}})
async function exercise({selected=1, computerFirst=false, oversized=false, displayReason} = {}) {
  active = {selected, computerFirst, questions:0, computerStarted:false, computerCancelled:false}
  const controller = new AbortController(), id = `order-${++sequence}`
  const session = {header:{id}, ownEvents:() => [{type:'tool/call', data:{callId:id,
    name:'exec_command', arguments:JSON.stringify({command:oversized ? 'x'.repeat(600) : 'echo WHALE_APPROVAL_TEST'})}}]}
  for (const fn of listeners.get('session/event') ?? []) fn(session, {type:'turn/start', data:{}})
  const req = {agent:{session}, toolName:'exec_command', callId:id, reason:'Test approval',
    displayReason, signal:controller.signal}
  let timeout
  try {
    const outcome = await Promise.race([dispatch('approval/request', req), new Promise((_, reject) => {
      timeout = delay(() => reject(new Error('computer answerer blocked the device')), 1500)
    })])
    // Let delayed synthetic answers finish before the next request.
    await new Promise(resolve => delay(resolve, 80))
    return {...active, outcome}
  } finally {
    clearTimeout(timeout); timers.delete(timeout)
    controller.abort('fixture finished')
  }
}
try {
  for (const selected of [1, 0]) {
    const result = await exercise({selected})
    assert.equal(result.questions, 1)
    assert.equal(result.computerStarted, true)
    assert.equal(result.computerCancelled, true)
    assert.equal(result.outcome, selected ? 'rejected' : 'allowed-once')
  }
  const computer = await exercise({selected:0, computerFirst:true})
  assert.equal(computer.outcome, 'rejected')
  assert.equal(computer.questions, 1)
  const languages = Object.fromEntries(['en','zh','ja','es','fr','de','ru','pt'].map(locale =>
    [locale, locale === 'zh' ? '允许设备测试' : 'Allow the device approval test '.repeat(5)]))
  const translated = await exercise({displayReason:languages})
  assert.equal(translated.questions, 1)
  assert.equal(translated.outcome, 'rejected')
  assert.equal(translated.computerCancelled, true)
  const fallback = await exercise({computerFirst:true, oversized:true})
  assert.equal(fallback.questions, 0)
  assert.equal(fallback.computerStarted, true)
  assert.equal(fallback.outcome, 'rejected')
  console.log('PASS: pre-existing computer answerer, device allow/deny, computer-first race and full-scope fallback')
} finally {
  for (const timer of timers) clearTimeout(timer)
  cleanup?.()
  bridge.close()
}
