// Synthetic DSH approval and multi-select callbacks answered through the bridge.
import {apply} from '../harness/plugin/index.ts'
const hooks=new Map();let cleanup
apply({workspaceRegistry:{archivedSessionIds:[]},on:(name,fn)=>hooks.set(name,fn),commands:{register(){}},effect:(fn)=>{cleanup=fn()},sessionTitle:{get:()=>({title:'审批测试'})}})
const call={type:'tool/call',data:{id:'call-1',name:'bash',arguments:{command:'npm test'}}}
const approver={header:{id:'room-approval'},ownEvents:()=>[call]}
const picker={header:{id:'room-multi'}}
for(const session of [approver,picker])hooks.get('session/event')(session,{type:'turn/start',data:{}})
hooks.get('session/event')(approver,call)
hooks.get('session/event')(approver,{type:'approval/asked',data:{id:'ap-1',toolName:'bash'}})
let computerCancelled=false
const req={agent:{session:approver},toolName:'bash',callId:'call-1',reason:'run tests',signal:new AbortController().signal}
const approval=hooks.get('approval/request')(req,()=>new Promise(resolve=>{
  req.signal.addEventListener('abort',()=>{computerCancelled=true;resolve('cancelled')},{once:true})
}))
const exec={name:'ask_user_question',agent:{session:picker},signal:new AbortController().signal,arguments:{questions:[{id:'parts',question:'要导出哪些部分',multi_select:true,options:['摘要','日志','图片','附录'].map(label=>({label}))}]}}
const multi=hooks.get('tools/execute')(exec,()=>new Promise((_,reject)=>{
  exec.signal.addEventListener('abort',()=>reject(new Error('device won')),{once:true})
}))
try{
  const [outcome,picked]=await Promise.race([Promise.all([approval,multi]),new Promise((_,reject)=>{const timer=setTimeout(()=>reject(new Error('replay timeout')),6500);timer.unref()})])
  if(outcome!=='allowed-once'||!computerCancelled)throw Error('approval not decided on device: '+outcome)
  if(JSON.stringify(picked.value.answers[0].selected)!==JSON.stringify(['摘要','图片']))throw Error('wrong multi answer '+JSON.stringify(picked.value))
  await new Promise(r=>setTimeout(r,100))
}finally{cleanup()}
