import {apply} from '../harness/plugin/index.ts'
const hooks = new Map()
let cleanup
apply({workspaceRegistry:{archivedSessionIds:[]},on:(n,f)=>hooks.set(n,f),effect:f=>{cleanup=f()},commands:{register(){}},sessionTitle:{get:()=>({title:'mnemon 开发'})}})
const event=(type,data)=>({type,data})
const message=(turn,text,other=[])=>event('assistant/message',{turn,message:{role:'assistant',content:[...other,{type:'text',text}]}})
const end=(turn,kind)=>event('turn/end',{turn,reason:{kind}})
const root={header:{id:'root-a'}}
const hidden={header:{id:'internal',origin:'subagent',delegationDepth:0}}
const saved={header:{id:'root-b'},ownEvents:()=>[message(1,'另一会话的上次回复'),end(1,'completed'),message(2,'被取消的正文'),end(2,'interrupted')]}
for(const s of [root,hidden,saved])hooks.get('session/created')(s)
const send=(s,e)=>hooks.get('session/event')(s,e)
for(const s of [root,hidden]){
  hooks.get('agent/status')({status:'running',agent:{session:s}})
  send(s,event('turn/start',{turn:1}))
  send(s,message(1,'内部工具说明',[{type:'tool-call',name:'private_tool'}]))
  send(s,message(1,'第一段。\n'+'长回复。'.repeat(120)+'最后一句。',[{type:'reasoning',text:'不可发送的思考'}]))
  send(s,end(1,'completed'))
  send(s,event('turn/start',{turn:2}))
  send(s,message(2,'未完成的草稿'));send(s,end(2,'interrupted'))
}
let bypass=false
await hooks.get('tools/execute')({name:'ask_user_question',agent:{session:hidden},arguments:{questions:[{id:'x',question:'后台问题',options:[{label:'a'},{label:'b'}]}]},signal:new AbortController().signal},async()=>{bypass=true;return {}})
if(!bypass)throw Error('child question intercepted')
process.stdin.resume()
process.stdin.once('data',()=>{cleanup();process.exit(0)})
