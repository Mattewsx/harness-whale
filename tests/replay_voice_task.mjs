import assert from 'node:assert/strict'
import { createVoiceService, targetLabel } from '../harness/plugin/voice.ts'
// Synthetic host: two visible sessions in two folders, one hidden one elsewhere.
const mk=(id,cwd)=>({id,header:{id,cwd},ownEvents:()=>[]})
const live={a:mk('a','D:\\work\\shop'),b:mk('b','/home/me/blog'),h:mk('h','C:\\secret')}
let hiddenIds=new Set(['h']),creates=[],attached=[],followups=[],createFails=false,followFails=false,recent='b'
let attachFails=false,workspaceMissing=false,createWithoutAgent=false
const shop={id:'w1',sessionIds:['a'],async attachSession(id){
 if(attachFails)throw Error('attachment rejected')
 this.sessionIds.push(id);attached.push(id)
}}
const blog={id:'w2',sessionIds:['b']}
const workspaces=[shop,blog]
const visible=id=>!hiddenIds.has(id)&&workspaces.some(w=>w.sessionIds.includes(id))
const agents={get:id=>live[id]?{session:live[id]}:undefined,roots:()=>Object.values(live).map(session=>({session})),
 async create(options){creates.push(options)
  const id=options.sessionId,session=mk(id,options.meta.cwd)
  live[id]=session // creation can succeed even if the host then rejects the call
  if(createFails)throw Error('boom')
  if(createWithoutAgent)return {}
  return {agent:{id,session,followup(m){
   assert(visible(id),'followup requires workspace membership')
   if(followFails)throw Error('x');followups.push(m)
  }}}}}
const ctx={agents,speechToText:{snapshot:()=>({providers:[{id:'sensevoice-local',preparation:{phase:'ready'}}]})},
 agentDefaultModel:{currentSelection:()=>({provider:'deepseek',model:'synthetic'})},
 agentPresets:{resolve:async()=>({id:'default'}),mount:async()=>{}},
 workspaceRegistry:{resolveByPath:async cwd=>workspaceMissing?undefined:cwd==='D:\\work\\shop'?shop:cwd==='/home/me/blog'?blog:undefined}}
let service=createVoiceService(ctx,visible,()=>true,()=>false,()=>recent)
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
 assert(visible(creates[0].sessionId),'accepted sessions appear through workspace.sessionIds')
 assert.equal(followups.length,1);assert.equal(followups[0].content[0].text,req.text)
 assert.equal((await call('/create',{...req,text:'changed'})).status,'invalid')
 assert.equal((await call('/create-query',req)).status,'accepted')
 assert.equal((await call('/create-query',{...req,id:'b'.repeat(32)})).status,'unknown','query never creates')
 assert.equal(creates.length,1)
 // Paths must belong to a visible session; arbitrary or hidden folders are refused.
 for(const cwd of ['C:\\secret','C:\\Windows','']) assert.equal((await call('/create',{...req,id:'c'.repeat(32),cwd})).status,'no_target')
 assert.equal((await call('/create',{...req,id:'c'.repeat(32),text:' '})).status,'invalid')
 assert.equal(creates.length,1)
 // Resolve a usable workspace and attachment API before creating anything.
 workspaceMissing=true
 assert.equal((await call('/create',{...req,id:'1'.repeat(32)})).status,'no_target')
 workspaceMissing=false
 assert.deepEqual(await call('/create',{...req,id:'2'.repeat(32),cwd:'/home/me/blog'}),
  {status:'unavailable',reason:'attach_missing'})
 assert.equal(creates.length,1,'workspace failures cannot leave ungrouped sessions')
 // Attachment failure retains the nonce and never sends the first message.
 attachFails=true
 const rejected={...req,id:'3'.repeat(32)}
 assert.equal((await call('/create',rejected)).status,'unknown')
 const unattachedId=creates.at(-1).sessionId
 assert(!visible(unattachedId));assert.equal(followups.length,1)
 attachFails=false
 for(const path of ['/create','/create-query'])
  assert.equal((await call(path,rejected)).status,'unknown')
 assert.equal(creates.length,2,'attachment retries never create a second session')
 assert.equal(followups.length,1,'attachment retries never send or report acceptance')
 // A rejection can arrive after session creation, so keep uncertainty and dedup.
 createFails=true;assert.equal((await call('/create',{...req,id:'d'.repeat(32)})).status,'unknown');createFails=false
 assert.equal((await call('/create',{...req,id:'d'.repeat(32)})).status,'unknown','uncertain creation is never repeated')
 assert.equal((await call('/create-query',{...req,id:'d'.repeat(32)})).status,'unknown')
 assert.equal(creates.length,3);assert.equal(followups.length,1)
 createWithoutAgent=true
 assert.equal((await call('/create',{...req,id:'4'.repeat(32)})).status,'unknown');createWithoutAgent=false
 assert.equal((await call('/create',{...req,id:'4'.repeat(32)})).status,'unknown')
 assert.equal(creates.length,4);assert.equal(followups.length,1)
 followFails=true;assert.equal((await call('/create',{...req,id:'e'.repeat(32)})).status,'unknown')
 assert.equal((await call('/create',{...req,id:'e'.repeat(32)})).status,'unknown','never a second session');followFails=false
 assert.equal(creates.length,5)
 const model=ctx.agentDefaultModel;ctx.agentDefaultModel=undefined
 assert.deepEqual(await call('/create',{...req,id:'f'.repeat(32)}),{status:'unavailable',reason:'model_missing'})
 ctx.agentDefaultModel=model;assert.equal(creates.length,5,'nothing created without a model')
 // Receipts remain authoritative even after later recordings fill the guard.
 const original=req.id
 for(let i=0;i<123;i++)
  assert.equal((await call('/create',{...req,id:i.toString(16).padStart(32,'0')})).status,'accepted')
 assert.equal(creates.length,128)
 assert.deepEqual(await call('/create',{...req,id:'5'.repeat(32)}),{status:'unavailable',reason:'recording_limit'})
 assert.equal((await call('/create',{...req,id:original})).status,'accepted')
 assert.equal((await call('/create',rejected)).status,'unknown')
 assert.equal(creates.length,128,'full receipt guard never forgets existing nonce identities')
 hiddenIds=new Set(Object.keys(live));recent=undefined
 assert.deepEqual(await call('/target',{sessionId:'a'}),{status:'no_target'})
 console.log('Voice task: workspace attachment, visible acceptance, uncertain creation, nonce retries and capacity PASS')
} finally { service.close() }
