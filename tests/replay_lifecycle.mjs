import readline from 'node:readline'
import {apply} from '../harness/plugin/index.ts'

const hooks = new Map(), live = new Map(), stored = new Set(), handles = new Map()
const registry = {archivedSessionIds:['already-hidden']}
let cleanup, storageError=false
function create(id,title=id,header={}) {
  const s={header:{id,...header},title}
  handles.set(id,s); live.set(id,s); stored.add(id)
  hooks.get('session/created')?.(s)
}
create('startup','同名');create('already-hidden');create('child','助手',{origin:'subagent'})
apply({
  on:(n,f)=>hooks.set(n,f),effect:f=>{cleanup=f()},commands:{register(){}},
  sessions:{get:k=>live.get(k),list:()=>Array.from(live.values())},
  sessionPersistence:{stat:async k=>{if(storageError)throw Error('fixture I/O');return stored.has(k)?{header:{id:k}}:undefined}},
  workspaceRegistry:registry,sessionTitle:{get:s=>({title:s.title})}
})
for await(const line of readline.createInterface({input:process.stdin})){
  const [op,id,arg]=JSON.parse(line)
  const s=handles.get(id)
  if(op==='create')create(id,arg)
  if(op==='rename'){s.title=arg;hooks.get('session/event')(s,{type:'session/title',data:{title:arg}})}
  if(op==='archive')registry.archivedSessionIds.push(id)
  if(op==='restore')registry.archivedSessionIds=registry.archivedSessionIds.filter(x=>x!==id)
  if(op==='late')hooks.get('agent/status')({status:'running',agent:{session:s}})
  if(op==='unload'){live.delete(id);hooks.get('session/disposed')(s)}
  if(op==='delete')stored.delete(id)
  if(op==='fault')storageError=arg
  if(op==='exit')break
  console.log('ack')
}
cleanup()
