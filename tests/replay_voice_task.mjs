import assert from 'node:assert/strict'
import { createVoiceService, targetLabel } from '../harness/plugin/voice.ts'
// Synthetic host: two visible sessions in two folders, one hidden one elsewhere.
const mk=(id,cwd)=>({id,header:{id,cwd},ownEvents:()=>[]})
const live={a:mk('a','D:\\work\\shop'),b:mk('b','/home/me/blog'),h:mk('h','C:\\secret')}
let hiddenIds=new Set(['h']),creates=[],attached=[],followups=[],createFails=false,followFails=false,recent='b'
const agents={get:id=>live[id]?{session:live[id]}:undefined,roots:()=>Object.values(live).map(session=>({session})),
 async create(options){creates.push(options);if(createFails)throw Error('boom')
  const id=options.sessionId,session=mk(id,options.meta.cwd)
  return {agent:{id,session,followup(m){if(followFails)throw Error('x');followups.push(m)}}}}}
const ctx={agents,speechToText:{snapshot:()=>({providers:[{id:'sensevoice-local',preparation:{phase:'ready'}}]})},
 agentDefaultModel:{currentSelection:()=>({provider:'deepseek',model:'synthetic'})},
 agentPresets:{resolve:async()=>({id:'default'}),mount:async()=>{}},
 workspaceRegistry:{resolveByPath:async cwd=>cwd==='D:\\work\\shop'?{id:'w1',attachSession:async id=>attached.push(id)}:undefined}}
let service=createVoiceService(ctx,id=>!hiddenIds.has(id),()=>true,()=>false,()=>recent)
const env=async()=>{for(let i=0;i<50;i++){const e=service.environment();if(e.HARNESS_WHALE_VOICE_PORT)return e;await new Promise(r=>setTimeout(r,10))}throw Error('server unavailable')}
let e=await env()
const call=async(path,data)=>{const r=await fetch(`http://127.0.0.1:${e.HARNESS_WHALE_VOICE_PORT}${path}`,{method:'POST',headers:{Authorization:`Bearer ${e.HARNESS_WHALE_VOICE_TOKEN}`},body:JSON.stringify(data)});return r.json()}
try {
 assert.equal(targetLabel('D:\\work\\shop\\'),'shop');assert.equal(targetLabel('/home/me/blog'),'blog')
 assert(Buffer.byteLength(targetLabel('/x/'+'项目'.repeat(40)))<=48)
 // The session the device showed wins, then the most recently active one.
 assert.deepEqual(await call('/target',{sessionId:'a'}),{status:'ok',cwd:'D:\\work\\shop',label:'shop'})
 assert.equal((await call('/target',{sessionId:''})).cwd,'/home/me/blog')
 assert.equal((await call('/target',{sessionId:'h'})).cwd,'/home/me/blog','hidden sessions never lend a folder')
 const req={id:'a'.repeat(32),cwd:'D:\\work\\shop',text:'把登录页的 bug 修一下'}
 const results=await Promise.all(Array.from({length:6},()=>call('/create',req)))
 assert(results.every(r=>r.status==='accepted'));assert.equal(creates.length,1,'one recording opens one session')
 assert.equal(creates[0].meta.cwd,'D:\\work\\shop');assert.equal(creates[0].meta.agentPreset,'default')
 assert.equal(creates[0].parentAgent,undefined);assert.equal(creates[0].meta.origin,undefined)
 assert.deepEqual(creates[0].agentOptions,{provider:'deepseek',model:'synthetic'})
 assert.deepEqual(attached,[creates[0].sessionId])
 assert.equal(followups.length,1);assert.equal(followups[0].content[0].text,req.text)
 assert.equal((await call('/create',{...req,text:'changed'})).status,'invalid')
 assert.equal((await call('/create-query',req)).status,'accepted')
 assert.equal((await call('/create-query',{...req,id:'b'.repeat(32)})).status,'unknown','query never creates')
 assert.equal(creates.length,1)
 // Paths must belong to a visible session; arbitrary or hidden folders are refused.
 for(const cwd of ['C:\\secret','C:\\Windows','']) assert.equal((await call('/create',{...req,id:'c'.repeat(32),cwd})).status,'no_target')
 assert.equal((await call('/create',{...req,id:'c'.repeat(32),text:' '})).status,'invalid')
 assert.equal(creates.length,1)
 createFails=true;assert.equal((await call('/create',{...req,id:'d'.repeat(32)})).status,'not_sent');createFails=false
 assert.equal((await call('/create',{...req,id:'d'.repeat(32)})).status,'accepted','a failed create may be retried')
 followFails=true;assert.equal((await call('/create',{...req,id:'e'.repeat(32)})).status,'unknown')
 assert.equal((await call('/create',{...req,id:'e'.repeat(32)})).status,'unknown','never a second session');followFails=false
 assert.equal(creates.length,4)
 const model=ctx.agentDefaultModel;ctx.agentDefaultModel=undefined
 assert.deepEqual(await call('/create',{...req,id:'f'.repeat(32)}),{status:'unavailable',reason:'model_missing'})
 ctx.agentDefaultModel=model;assert.equal(creates.length,4,'nothing created without a model')
 hiddenIds=new Set(Object.keys(live));recent=undefined
 assert.deepEqual(await call('/target',{sessionId:'a'}),{status:'no_target'})
 console.log('Voice task: folder choice, one session per recording, path guard, model/preset, query-only PASS')
} finally { service.close() }
