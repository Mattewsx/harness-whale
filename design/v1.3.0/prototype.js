'use strict';
// A browser-only, synthetic model. This is not the firmware implementation.
const $ = id => document.getElementById(id);
const ESC = x => String(x).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const MODE_NAMES = ['音效', '语音'];
const MODE_KEYS = ['effects', 'voice'];
const MODE_STORAGE = 'whale-v130-sound-mode-v2';
const INTERACTIONS = [
 {id:'hello',label:'打招呼',pose:'hello',line:'嗨，我在这里呀。'},
 {id:'happy',label:'摸摸',pose:'happy',line:'嘿嘿，再摸摸嘛。'},
 {id:'feed',label:'喂小鱼',pose:'feed',line:'啊呜，谢谢你的小鱼。'},
 {id:'coffee',label:'喝咖啡',pose:'feed',line:'喝咖啡'}
];
const ACTIONS = [...INTERACTIONS.map(x=>x.label), '返回任务'];
const POSES = ['idle', 'blink', 'hello', 'happy', 'feed', 'sleepy'];
const POSE_NAMES = ['陪伴待机', '轻轻眨眼', '打个招呼', '被摸摸啦', '吃一口小鱼', '有一点困'];
const PHRASES = ['我在呢，慢慢来。', '陪你歇一小会儿。', '一起等下一件事吧。'];
const EVENTS = {
 question:{label:'等你回答',phrase:'帮我选一下？',priority:0},
 error:{label:'任务出错',phrase:'遇到问题啦，来看看。',priority:1},
 done:{label:'任务完成',phrase:'忙完啦。',priority:2},
 disconnect:{label:'蓝牙断开',phrase:'蓝牙已断开。',priority:3}
};
let state, audioContext, audioNode, audioGeneration=0, soundReady=false;
const decoded = new Map();
// New two-mode schema: old three-mode drafts must not restore mute or override the new default.
function loadMode(){try{const i=MODE_KEYS.indexOf(localStorage.getItem(MODE_STORAGE));return i>=0?i:1;}catch{return 1;}}
function saveMode(x){state.mode=x;try{localStorage.setItem(MODE_STORAGE,MODE_KEYS[x]);}catch{log('浏览器不允许保存；本次会话内仍有效。');}log('声音设置：'+MODE_NAMES[x]);}
function task(id,title){return {id,title,status:'idle',startedAt:0,elapsed:0,revision:0,unread:false,question:false,reply:'这是这次任务的最新回复。\n\n我已整理好需要的内容。你可以继续查看，或回到任务主页与鲸鱼娘互动。\n\n这里只展示最近一次回复，向下连续滚动，不提供历史回复翻页。',error:'模拟错误：这次任务未能完成，请回电脑检查后重试。'};}
function reset(){
 stopAudio();
 state={now:0,lastInput:0,page:'home',current:'a',connected:true,globalCap:true,battery:76,mode:loadMode(),choice:0,action:null,actionUntil:0,menu:0,soundChoice:0,soundFocus:0,listIndex:0,questionIndex:0,questionConfirm:false,recording:false,voicePhase:'recording',voiceChoice:0,recordStarted:0,queue:[],due:null,seen:new Set(),outage:0,disconnectDue:null,disconnectedNotified:false,log:[],played:[],lastEvent:null,tasks:[task('a','整理明天的开发计划'),task('b','检查页面与按键'),task('c','准备发布说明')],replyOffset:0};
 $('legacy').checked=false;$('battery').value=76;$('battery-value').textContent='76%';
 updateTargets();$('target').value=state.current;log('模拟器已重置，音量模式沿用本浏览器设置。');render();
}
function log(message){state.log.unshift(`${state.now.toFixed(1)}s · ${message}`);state.log.length=Math.min(45,state.log.length);}
function current(){return state.tasks.find(t=>t.id===state.current)||state.tasks[0];}
function priority(t){return t.question?0:t.status==='error'&&t.unread?1:t.status==='done'&&t.unread?2:t.status==='start'?3:4;}
function ordered(){return [...state.tasks].sort((a,b)=>priority(a)-priority(b)||a.id.localeCompare(b.id));}
function globalSummary(){return {running:state.tasks.filter(t=>t.status==='start').length,questions:state.tasks.filter(t=>t.question).length,unread:state.tasks.filter(t=>t.unread).length};}
function globalBusy(){const g=globalSummary();return Boolean(g.running||g.questions||g.unread);}
function dimmed(){return state.now-state.lastInput>=120;}
function navigate(page){state.action=null;state.actionUntil=0;state.page=page;if(page==='list')state.listIndex=0;}
function enterCompanion(reason){navigate('companion');state.choice=0;log('进入陪伴：'+reason);}
function workDestination(){
 const list=ordered();
 if(list.some(t=>t.question||t.unread)){navigate('list');log('待处理事项优先，返回列表；未读保留。');}
 else if(list.some(t=>t.status==='start')){state.current=list.find(t=>t.status==='start').id;navigate('home');log('有任务运行，返回对应任务主页。');}
}
function valid(e){
 if(e.type==='disconnect')return !state.connected&&e.outage===state.outage;
 if(!state.connected)return false;
 const t=state.tasks.find(t=>t.id===e.id);
 if(!t||t.revision!==e.revision)return false;
 return e.type==='question'?t.question:e.type==='done'||e.type==='error'?t.status===e.type&&t.unread:t.status===e.type;
}
function enqueue(e){if(!EVENTS[e.type])return;if(state.seen.has(e.key)){log('已忽略重复事件：'+e.type);return;}state.seen.add(e.key);state.queue.push(e);if(state.due===null)state.due=state.now+1;log('提醒入队：'+EVENTS[e.type].label+(state.recording?'（录音中暂缓）':''));}
function emit(type,id=$('target').value){
 const t=state.tasks.find(x=>x.id===id);if(!t){log('目标会话已不存在。');render();return;}
 if(!state.connected){log('电脑未连接，事件尚未送达；可先重连同步。');render();return;}
 const detail={thinking:'思考中',tool:'工具执行中',waiting:'等待网络'}[type];
 if(detail)type='start';
 if(type==='start'&&t.status!=='start')t.startedAt=state.now;
 if(t.status==='start')t.elapsed=state.now-t.startedAt;
 t.revision++;t.status=type;t.detail=detail||'';t.question=type==='question';t.unread=['done','error','question'].includes(type);
 if(type==='done')t.reply='这是本次任务最新的一条回复。\n\n已经完成你要求的整理与检查。未读标记会在打开这条回复后清除。\n\n接下来可以继续工作，也可以短按上键去陪鲸鱼娘歇一会儿。\n\n这段文字用于演示长回复的连续滚动，不包含真实会话内容。';
 const e={type,id,revision:t.revision,key:`${id}:${t.revision}:${type}`};state.lastEvent=e;
  if(EVENTS[type])enqueue(e);else log((type==='stop'?'用户已停止任务：':'任务开始：')+t.title+'（无提示音）');
 if(state.page==='companion'){
  if(type==='start'){state.current=id;navigate('home');log('工作开始，中断互动，回到相关主页。');}
  else if(['done','error','question'].includes(type)){navigate('list');log('提醒打断陪伴，返回待处理列表；未读保留。');}
 }
 render();
}
function flush(){
 if(state.recording||state.due===null||state.now<state.due)return;
 const events=state.queue.filter(valid).sort((a,b)=>EVENTS[a.type].priority-EVENTS[b.type].priority);
 const count=state.queue.length;state.queue=[];state.due=null;
 if(events.length){const top=events[0];log(`本批 ${count} 条，只提醒「${EVENTS[top.type].label}」；其他事项留在列表。`);playCue(top.type,false,state.mode);}
 else if(count)log('已过期或已处理的提醒已丢弃。');
}
function step(seconds){
 state.now+=seconds;
 if(state.action&&state.now>=state.actionUntil){state.action=null;log('互动结束，回到陪伴待机。');}
 if(!state.connected&&!state.disconnectedNotified&&state.disconnectDue!==null&&state.now>=state.disconnectDue){
  state.disconnectedNotified=true;enqueue({type:'disconnect',outage:state.outage,key:'disconnect:'+state.outage});
 }
 flush();
 if(state.page==='home'&&state.now-state.lastInput>=120&&(!state.connected||(state.globalCap&&!globalBusy())))enterCompanion('两分钟无操作；'+(!state.connected?'离线陪伴':'全局主会话无待办'));
 render();
}
function disconnect(){if(!state.connected)return;state.connected=false;state.outage++;state.disconnectDue=state.now+5;state.disconnectedNotified=false;
 if(state.recording){endRecording(true);state.voicePhase='failed';log('连接中断，录音模拟已停止。');}
 log('蓝牙断开；持续 5 秒后才形成提醒。');render();}
function reconnect(history=false){
 state.connected=true;state.disconnectDue=null;state.disconnectedNotified=false;state.queue=[];state.due=null;stopAudio();
 if(history){state.tasks[0].status='done';state.tasks[0].unread=true;state.tasks[0].revision++;}
 log('同步全局快照，只更新界面，不补播历史事件。');
 if(state.page==='companion'&&state.globalCap)workDestination();render();
}
function resolveTarget(){const t=state.tasks.find(x=>x.id===$('target').value);if(!t)return;t.unread=false;t.question=false;if(t.status==='question')t.status='idle';log('电脑端已处理目标提醒。');render();}
function removeTarget(verb){const id=$('target').value;state.tasks=state.tasks.filter(t=>t.id!==id);state.queue=state.queue.filter(e=>e.id!==id);if(state.current===id){state.current=state.tasks[0]?.id||'';if(state.recording)endRecording(true);if(state.page!=='companion')navigate('home');}updateTargets();log(verb+'会话，同步移除条目及提醒。');render();}
function updateTargets(){const old=$('target').value;$('target').innerHTML=state.tasks.map(t=>`<option value="${t.id}">${ESC(t.title)}</option>`).join('');if(state.tasks.some(t=>t.id===old))$('target').value=old;}
function startRecording(){if(!state.connected||!current()){log('无法开始模拟录音：需要已连接的会话。');return;}stopAudio();state.recording=true;state.recordStarted=state.now;state.voicePhase='recording';state.voiceChoice=0;navigate('voice');log('开始模拟录音：立即停止播放，提醒暂缓。');}
function endRecording(cancel=false){state.recording=false;state.voicePhase=cancel?'cancelled':'preview';state.voiceChoice=0;log(cancel?'已取消录音。':'模拟转写完成，发送前需确认。');flush();}
function openTask(t){if(!t)return;state.current=t.id;
 if(t.question){state.questionIndex=0;state.questionConfirm=false;navigate('question');}
 else if(t.status==='error'&&t.unread){t.unread=false;navigate('error');log('已查看错误详情，清除未读错误。');}
 else if(t.status==='done'&&t.unread){t.unread=false;state.replyOffset=0;navigate('reply');log('已查看最新回复，清除新回复标记。');}
 else navigate('home');
}
function key(k,long=false){
 const wake=dimmed();state.lastInput=state.now;
 if(wake){log('按键唤醒亮度；本次不执行操作。');render();return;}
 if(long){
  if(k==='ok'){if(state.recording)endRecording(true);if(state.page==='home'){navigate('manage');state.menu=0;}else navigate('home');}
  else if(k==='down'){if(state.recording)endRecording(true);navigate('list');}
  else if(k==='up'&&state.page==='home')startRecording();
  render();return;
 }
 const t=current(), direction=k==='up'?-1:1;
 switch(state.page){
 case 'home':
  if(k==='up')enterCompanion('手动进入');
  if(k==='down'&&t&&t.status==='done'){t.unread=false;state.replyOffset=0;navigate('reply');}
  if(k==='ok'){if(t?.question){state.questionIndex=0;state.questionConfirm=false;navigate('question');}else navigate('list');}
  break;
 case 'companion':
  if(k!=='ok'){if(!state.action)state.choice=(state.choice+direction+ACTIONS.length)%ACTIONS.length;}
  else if(state.action){log('动作播放中，忽略重复确认。');}
  else if(state.choice===INTERACTIONS.length)navigate('home');
  else{state.action=INTERACTIONS[state.choice].id;state.actionUntil=state.now+3;log(ACTIONS[state.choice]+'：约 3 秒后自动返回待机。');}
  break;
 case 'list':{
  const list=ordered();state.listIndex=Math.min(state.listIndex,Math.max(0,list.length-1));
  if(k==='ok')openTask(list[state.listIndex]);else if(list.length)state.listIndex=(state.listIndex+direction+list.length)%list.length;break;}
 case 'reply':
  if(k==='ok')navigate('home');else state.replyOffset=Math.max(0,state.replyOffset+direction*64);break;
 case 'error':if(k==='ok')navigate('home');break;
 case 'question':
  if(!t?.question){navigate('home');break;}
  if(k==='ok'){if(state.questionConfirm){t.question=false;t.unread=false;t.status='start';navigate('home');log('模拟单选已确认，任务继续。');}else state.questionConfirm=true;}
  else if(state.questionConfirm)state.questionConfirm=false;else state.questionIndex=(state.questionIndex+direction+2)%2;break;
 case 'manage':
  if(k==='ok'){if(state.menu===0){navigate('sound');state.soundChoice=state.mode;state.soundFocus=state.mode;}else if(state.menu===1)navigate('connection');else navigate('home');}
  else state.menu=(state.menu+direction+3)%3;break;
 case 'connection':if(k==='ok')navigate('manage');break;
 case 'sound':
  if(k==='ok'){if(state.soundFocus<2)saveMode(state.soundFocus);else if(state.soundFocus===2)playCue('done',true,state.mode);else navigate('manage');}
  else state.soundFocus=(state.soundFocus+direction+4)%4;break;
 case 'voice':
  if(state.recording&&k==='ok')endRecording();
  else if(state.voicePhase==='preview'){
   if(k==='ok'){if(state.voiceChoice===0){state.voicePhase='sent';log('仅模拟发送，未向任何真实会话发消息。');}else startRecording();}
   else state.voiceChoice=1-state.voiceChoice;
  }else if(['sent','failed'].includes(state.voicePhase)&&k==='ok')navigate('home');break;
 }
 render();
}
function bar(){return `<div class="statusbar"><span class="connection ${state.connected?'':'off'}">${state.connected?'电脑已连接':'电脑未连接'}</span><span class="status-actions">${state.page==='home'?'<span class="list-shortcut"><span class="keycap">确认</span> 列表</span>':''}<span class="battery">▰ ${state.battery}%</span></span></div>`;}
function footer(s){return `<div class="screen-footer">${s}</div>`;}
function actor(name,classes=''){return `<img class="actor ${classes}" src="${MEDIA.images[name]}" alt="鲸鱼娘${ESC(name)}" draggable="false">`;}
function effect(name,classes){return `<img class="interaction-effect ${classes}" src="${MEDIA.effects[name]}" alt="${name==='coffee-cup'?'美式咖啡杯':'咖啡热气'}" draggable="false">`;}
function title(s,sub=''){return `<div class="page-title">${s}</div><div class="page-sub">${sub}</div>`;}
function label(t){return t.question?'等你回答':t.unread&&t.status==='error'?'未读错误':t.unread&&t.status==='done'?'有新回复':t.detail||({idle:'待机',start:'正在工作',done:'已完成',error:'出错',stop:'已停止'}[t.status]||'待机');}
function elapsed(t){const n=Math.floor(t?(t.status==='start'?state.now-t.startedAt:t.elapsed):0);return String(Math.floor(n/60)).padStart(2,'0')+':'+String(n%60).padStart(2,'0');}
let previousHTML='';
let titleScroll={key:'',started:0};
function scrollHomeTitle(){
 const box=document.querySelector('.home-title');
 if(!box){titleScroll.key='';return;}
 const identity=JSON.stringify([state.current,box.textContent]);
 if(titleScroll.key!==identity||state.now<titleScroll.started){titleScroll={key:identity,started:state.now};}
 const distance=Math.max(0,box.scrollWidth-box.clientWidth);
 if(!distance){box.scrollLeft=0;return;}
 const travel=distance/22, phase=(state.now-titleScroll.started)%(3+travel);
 box.scrollLeft=phase<1.5?0:Math.min(distance,(phase-1.5)*22);
}

function render(){
 const t=current();let h=bar(), help='';
 if(state.page==='companion'){
  const phase=state.now%24, interaction=INTERACTIONS.find(x=>x.id===state.action), satisfied=state.action==='coffee'&&state.actionUntil-state.now<=.8;
  const pose=satisfied?'happy':interaction?.pose||(phase>20&&phase<22?'sleepy':state.now%5>4.75?'blink':'idle');
  if(state.action==='coffee')h+='<div class="actor-stage coffee-stage">'+actor(pose,satisfied?'coffee-satisfied':'')+(!satisfied?effect('coffee-cup','coffee-cup'):'')+'</div>'+(!satisfied?effect('coffee-steam','coffee-steam'):'');
  else h+=actor(pose,state.action||'breathe');
  h+=`<div class="companion-copy">${satisfied?'嗯，精神一点啦。':interaction?interaction.line:PHRASES[Math.floor(state.now/24)%PHRASES.length]}</div><div class="choice-bar"><span>‹</span><span>${state.action?'陪伴中…':ACTIONS[state.choice]}</span><span>›</span></div>`;
  if(state.action)h+='<div class="action-progress"></div>';
  if(state.action==='feed'&&state.actionUntil-state.now>2.35)h+='<span class="fish"><svg width="28" height="18" viewBox="0 0 28 18" aria-label="小鱼"><path d="M2 3L10 9L2 15Z" fill="#e9b64b"/><ellipse cx="18" cy="9" rx="9" ry="6" fill="#e9b64b"/><circle cx="22" cy="7" r="1" fill="#826024"/></svg></span>';
  h+=footer(state.action?'长按确认可中断并返回':'↑↓选择　确认互动');help='长按确认返回任务；长按 ↓ 打开会话列表。';
 }else if(state.page==='home'){
  const pose=t?({start:'work-thinking',done:'work-done',question:'work-question',error:'work-error',stop:'work-stopped'}[t.status]||'work-idle'):'work-idle';
  h+=`<div class="home-title" aria-label="${ESC(t?.title||'还没有会话')}"><span>${ESC(t?.title||'还没有会话')}</span></div>`+actor(pose,'home-actor')+`<div class="home-state"><span class="state-${t?.status||'idle'}">${t?label(t):'可以先去陪伴'}</span><span>${elapsed(t)}</span></div>`+'<div class="screen-footer home-footer">↑ 陪伴　<span class="hold-hint">长按 ↑ 语音</span>　↓ 回复</div>';help='长按 ↑ 模拟录音；长按确认打开管理。';
 }else if(state.page==='list'){
  const list=ordered();state.listIndex=Math.min(state.listIndex,Math.max(0,list.length-1));const start=Math.floor(state.listIndex/4)*4;
  h+=title('会话列表',`${list.length} 个主会话 · 待处理优先`)+`<div class="list-body">${list.length?list.slice(start,start+4).map((x,i)=>`<div class="task-row ${start+i===state.listIndex?'selected':''}"><b>${ESC(x.title)}</b><span class="state-${x.status}">${label(x)}</span></div>`).join(''):'<p class="body-copy">暂无会话</p>'}</div>`+footer('↑↓选择　确认查看');help='列表只显示名称与彩色状态。打开对应内容才清除未读。长按确认回主页。';
 }else if(state.page==='manage'){
  h+=title('设备管理','长按确认返回任务')+'<div class="menu">'+['声音提醒','连接管理','返回任务'].map((x,i)=>`<div class="menu-row ${i===state.menu?'selected':''}">${x}${i===0?`<small>当前：${MODE_NAMES[state.mode]}</small>`:''}</div>`).join('')+'</div>'+footer('↑↓选择　确认进入');help='声音设置在管理页内；默认语音，可切换音效。';
 }else if(state.page==='sound'){
  h+=title('声音提醒','修改后自动保存')+'<div class="menu" style="top:84px">'+[...MODE_NAMES,'试听当前模式','返回管理'].map((x,i)=>`<div class="menu-row ${i===state.soundFocus?'selected':''}" style="padding:6px 10px;margin-bottom:3px">${x}${i===state.mode?'<span class="tick">✓</span>':''}</div>`).join('')+'</div>'+footer('↑↓选择　确认设置');help='选择模式后确认保存；选择「试听当前模式」听完成提醒。';
 }else if(state.page==='connection'){
  h+=title('连接管理','演示期间不会修改真实配对')+'<div class="body-copy">'+(state.connected?'电脑已连接':'电脑未连接')+'<br><br>可使用原型外的断开／重连按钮演示。<br><br><span class="info-pill">确认返回管理</span></div>'+footer('确认返回　长按确认回主页');help='连接管理期间不会自动进入陪伴。';
 }else if(state.page==='reply'||state.page==='error'){
  h+=title(state.page==='reply'?'最近回复':'错误详情',ESC(t?.title||''))+`<div class="body-copy reply-text" id="reply-text">${ESC(state.page==='reply'?t?.reply||'暂无回复':t?.error||'会话已移除')}</div>`+footer('↑↓滚动　确认收起');help='仅展示最近一次回复，连续滚动；本原型内容在内存中，不发网络请求。';
 }else if(state.page==='question'){
  h+=title(state.questionConfirm?'确认这项选择？':'需要你选一下',ESC(t?.title||''))+'<div class="body-copy">输出哪种文件？</div><div class="menu" style="top:130px">'+(state.questionConfirm?`<div class="menu-row selected">${['Markdown','PDF'][state.questionIndex]}<small>再次确认后提交（模拟）</small></div>`:['Markdown','PDF'].map((x,i)=>`<div class="menu-row ${state.questionIndex===i?'selected':''}">${x}</div>`).join(''))+'</div>'+footer(state.questionConfirm?'↑↓重新选择　确认提交':'↑↓选择　确认预览');help='待选择状态只有完成选择后解除；离开页面不会替你作答。';
 }else if(state.page==='voice'){
  if(state.recording){h+=title('正在听你说','模拟录音 · 不调用真实麦克风')+'<div class="record-orb active">◉</div><div class="voice-copy">说完后按确认</div><div class="voice-sub">'+Math.floor(state.now-state.recordStarted)+' 秒'+(state.queue.length?'<br>工作提醒已暂缓':'<br>边说边传（流程模拟）')+'</div>'+footer('确认停止　长按确认取消');}
  else if(state.voicePhase==='preview'){h+=title('预览识别文字','模拟识别结果')+'<div class="body-copy">请帮我检查 DeepSeek<br>任务的最新结果。</div><div class="voice-actions"><span class="'+(state.voiceChoice===0?'selected':'')+'">发送</span><span class="'+(state.voiceChoice===1?'selected':'')+'">重录</span></div>'+footer('↑↓选择　确认执行');}
  else h+=title(state.voicePhase==='failed'?'录音已中断':'已发送（模拟）')+'<div class="body-copy">'+(state.voicePhase==='failed'?'电脑连接已断开，请重连后重试。':'原型没有发送真实消息。<br>确认返回任务主页。')+'</div>'+footer('确认返回');help='录音立即中止播放。停止后只播仍有效的提醒；长按确认取消。';
 }
 const screen=$('screen');if(h!==previousHTML){screen.innerHTML=h;previousHTML=h;}
 screen.dataset.page=state.page;screen.classList.toggle('dim',dimmed());
 scrollHomeTitle();
 if($('reply-text'))$('reply-text').scrollTop=state.replyOffset;
 $('context-help').textContent=(dimmed()?'当前已调暗：第一次按键只唤醒。 ':'')+help;
 const g=globalSummary();$('diagnostics').textContent=`页面：${state.page}　时间：${state.now.toFixed(1)}s\n全局：${state.tasks.length} 个主会话 / 运行 ${g.running} / 待回答 ${g.questions} / 未读 ${g.unread}\n全局摘要：${state.globalCap?'支持':'不支持，已连接时禁止自动陪伴'}\n待播 ${state.queue.length} / 录音 ${state.recording?'是':'否'} / 模式 ${MODE_NAMES[state.mode]}\n亮度：${dimmed()?'调暗（自动进入不重置）':'正常'} / 已决策播放 ${state.played.length} 次`;
 $('log').innerHTML=state.log.map(x=>`<li>${ESC(x)}</li>`).join('');
}
function stopAudio(){audioGeneration++;if(audioNode){try{audioNode.stop();}catch{}audioNode=null;}if(soundReady)$('audio-status').textContent='声音已停止；可继续试听。';}
async function enableAudio(){
 if(!audioContext)audioContext=new (window.AudioContext||window.webkitAudioContext)();
 await audioContext.resume();soundReady=audioContext.state==='running';
 $('audio-status').textContent=soundReady?'试听已启用；请从五类样本或设备设置播放。':'浏览器未启用声音，请再点击一次。';
 return soundReady;
}
async function buffer(name){if(decoded.has(name))return decoded.get(name);const raw=atob(MEDIA.audio[name].split(',')[1]);const data=Uint8Array.from(raw,c=>c.charCodeAt(0));const value=await audioContext.decodeAudioData(data.buffer);decoded.set(name,value);return value;}
async function playCue(type,preview=false,mode=state.mode){
 if(!EVENTS[type])return;
 if(state.recording){log('录音优先，试听暂不可用。');render();return;}
 if(!preview)state.played.push({type,time:state.now});
 stopAudio();const generation=audioGeneration;
 if(!soundReady){log('声音尚未启用：'+EVENTS[type].label+'（仅模拟提醒决策）');$('audio-status').textContent='请先点击「启用试听声音」；本次未实际播放。';render();return;}
 const names=[(mode===1&&EVENTS[type].phrase?'voice-':'tone-')+type];
 try{
  const buffers=await Promise.all(names.map(buffer));
  if(generation!==audioGeneration||state.recording)return;
  $('audio-status').textContent='正在试听：'+EVENTS[type].label+' / '+(names[0].startsWith('voice-')?'语音':'音效');
  for(let i=0;i<buffers.length;i++){
   if(generation!==audioGeneration||state.recording)break;
   const node=audioContext.createBufferSource();node.buffer=buffers[i];node.connect(audioContext.destination);audioNode=node;
   await new Promise(resolve=>{node.onended=resolve;node.start(audioContext.currentTime+(i?.12:0));});
  }
  if(generation===audioGeneration){audioNode=null;$('audio-status').textContent='试听完成：'+EVENTS[type].label;}
 }catch(e){$('audio-status').textContent='无法播放声音，请重新启用后再试。';log('浏览器音频失败：'+e.name);}
 render();
}
function scene(name){
 reset();
 const guides={manual:'短按 ↑ 进入陪伴，↑↓ 切换，确认互动；最后选择返回任务。',auto:'点击「快进 2 分钟」观察自动进入与调暗，然后注入新任务或新提醒。',offline:'离线仍可互动。点击重连同步；无待处理时继续陪伴，有工作时返回任务。',sound:'↑↓ 选择模式并确认，再选试听当前模式；原型外也可逐条试听。',record:'已进入模拟录音。先注入完成／待选择／错误，再快进 1.1 秒；确认停止录音后听有效提醒。',background:'共 10 个会话，第 10 个正在运行。快进 2 分钟不会自动陪伴，短按 ↑ 仍可手动进入（调暗后先唤醒）。'};
 if(name==='offline'){state.connected=false;state.disconnectDue=null;state.disconnectedNotified=true;enterCompanion('离线场景');}
 if(name==='sound'){navigate('sound');state.soundFocus=state.mode;}
 if(name==='record')startRecording();
 if(name==='background'){for(let i=4;i<=10;i++)state.tasks.push(task('t'+i,'后台会话 '+i));state.tasks[9].status='start';updateTargets();}
 $('scene-help').textContent=guides[name];render();
}
document.querySelectorAll('[data-key]').forEach(button=>{
 let started=null,held=false,timer;
 button.addEventListener('pointerdown',e=>{if(e.button!==0)return;button.setPointerCapture(e.pointerId);started=performance.now();held=false;timer=setTimeout(()=>{held=true;key(button.dataset.key,true);},500);});
 button.addEventListener('pointerup',()=>{clearTimeout(timer);if(started!==null&&!held)key(button.dataset.key);started=null;});
 button.addEventListener('pointercancel',()=>{clearTimeout(timer);started=null;});
 button.addEventListener('click',e=>{if(e.detail===0)key(button.dataset.key);});
});
document.querySelectorAll('[data-long]').forEach(b=>b.onclick=()=>key(b.dataset.long,true));
const heldKeys=new Map();
document.addEventListener('keydown',e=>{
 const k={ArrowUp:'up',ArrowDown:'down',Enter:'ok'}[e.key];if(!k||/INPUT|SELECT|BUTTON|SUMMARY/.test(e.target.tagName))return;e.preventDefault();if(e.repeat)return;
 if(e.shiftKey){key(k,true);heldKeys.set(e.key,{long:true});return;}
 const hold={long:false,timer:setTimeout(()=>{hold.long=true;key(k,true);},500)};heldKeys.set(e.key,hold);
});
document.addEventListener('keyup',e=>{const h=heldKeys.get(e.key);if(!h)return;e.preventDefault();clearTimeout(h.timer);heldKeys.delete(e.key);if(!h.long)key({ArrowUp:'up',ArrowDown:'down',Enter:'ok'}[e.key]);});
window.addEventListener('blur',()=>{for(const h of heldKeys.values())clearTimeout(h.timer);heldKeys.clear();});
document.querySelectorAll('[data-scene]').forEach(b=>b.onclick=()=>scene(b.dataset.scene));
document.querySelectorAll('[data-event]').forEach(b=>b.onclick=()=>emit(b.dataset.event));
document.querySelectorAll('[data-advance]').forEach(b=>b.onclick=()=>step(Number(b.dataset.advance)));
$('repeat-event').onclick=()=>{if(state.lastEvent&&state.lastEvent.type!=='start')enqueue(state.lastEvent);render();};
$('disconnect').onclick=disconnect;$('reconnect').onclick=()=>reconnect();$('history').onclick=()=>reconnect(true);
$('resolve').onclick=resolveTarget;$('archive').onclick=()=>removeTarget('归档');$('delete').onclick=()=>removeTarget('删除');
$('add-task').onclick=()=>{if(!state.connected){log('先重连电脑再新增会话。');render();return;}let n=1;while(state.tasks.some(t=>t.id==='new'+n))n++;const t=task('new'+n,'新会话 '+n);state.tasks.push(t);updateTargets();$('target').value=t.id;emit('start',t.id);};
$('long-title').onclick=()=>{const t=current();if(!t)return;t.title='整理 DeepSeek Harness 明天的开发计划与语音回复交互验收清单';updateTargets();render();};
$('reset').onclick=()=>scene('manual');$('legacy').onchange=e=>{state.globalCap=!e.target.checked;render();};
$('battery').oninput=e=>{state.battery=Number(e.target.value);$('battery-value').textContent=state.battery+'%';render();};
$('enable-audio').onclick=()=>enableAudio().catch(()=>{$('audio-status').textContent='浏览器无法启用音频。';});
$('stop-audio').onclick=()=>{stopAudio();$('audio-status').textContent='已停止试听。';};
$('sound-samples').innerHTML=Object.entries(EVENTS).map(([id,e])=>`<div class="sample"><span>${e.label}<small>${e.phrase||'两种模式均播放音效'}</small></span><button data-sample="${id}" data-mode="0">音效</button>${e.phrase?`<button data-voice="${id}">语音</button>`:''}</div>`).join('');
document.querySelectorAll('[data-sample]').forEach(b=>b.onclick=async()=>{await enableAudio();playCue(b.dataset.sample,true,Number(b.dataset.mode));});
document.querySelectorAll('[data-voice]').forEach(b=>b.onclick=async()=>{await enableAudio();playCue(b.dataset.voice,true,1);});
$('pose-gallery').innerHTML=POSES.map((p,i)=>`<div class="pose-card"><img src="${MEDIA.images[p]}" alt="${POSE_NAMES[i]}">${POSE_NAMES[i]}<small>${p}</small></div>`).join('');
reset();
let wall=performance.now();setInterval(()=>{const now=performance.now();step((now-wall)/1000);wall=now;},125);
// Read-only-ish test driver for synthetic state; never connected to real hardware.
window.whalePrototype={snapshot:()=>structuredClone({...state,seen:[...state.seen]}),key,scene,emit,step,reset,disconnect,reconnect,resolveTarget,removeTarget};
