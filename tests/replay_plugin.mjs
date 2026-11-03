// Synthetic DSH callbacks with the same exec.agent.session shape as the installed host.
import {apply} from '../harness/plugin/index.ts'
const hooks=new Map();let cleanup
apply({workspaceRegistry:{archivedSessionIds:[]},on:(name,fn)=>hooks.set(name,fn),commands:{register(){}},effect:(fn)=>{cleanup=fn()},sessionTitle:{get:()=>({title:'重连测试'})}})
const sessions=[{header:{id:'room-a'}},{header:{id:'room-b'}}]
for(const session of sessions){
  hooks.get('session/event')(session,{type:'turn/start',data:{}})
  hooks.get('session/event')(session,{type:'todo/write',data:{todos:[{status:'done'},{status:'open'}]}})
}
const calls=sessions.map((session,i)=>{
  const exec={name:'ask_user_question',agent:{session},signal:new AbortController().signal,arguments:{questions:[{id:'format',question:'选择导出格式',options:['文本','网页','图片','日志','摘要'].map(label=>({label}))}]}}
  return hooks.get('tools/execute')(exec,()=>new Promise((resolve,reject)=>{
    exec.signal.addEventListener('abort',()=>reject(new Error('hardware won')),{once:true})
    if(i===1)setTimeout(()=>resolve({value:{desktop:true}}),600)
  }))
})
try{
  const res=await Promise.race([Promise.all(calls),new Promise((_,reject)=>{const timer=setTimeout(()=>reject(new Error('replay timeout')),6500);timer.unref()})])
  if(res[0].value.answers[0].selected[0]!=='摘要'||!res[1].value.desktop)throw Error('wrong answer owner')
  await new Promise(r=>setTimeout(r,100))
}finally{cleanup()}
