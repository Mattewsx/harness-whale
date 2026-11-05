// Run with Playwright installed and optionally EDGE_PATH / PROTOTYPE_REPORT_DIR.
const {chromium}=require('playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs'), path=require('node:path');
const {pathToFileURL}=require('node:url');
(async()=>{
 const out=process.env.PROTOTYPE_REPORT_DIR||path.join(__dirname,'test-results');fs.mkdirSync(out,{recursive:true});
 const browser=await chromium.launch({headless:true,...(process.env.EDGE_PATH?{executablePath:process.env.EDGE_PATH}:{})});
 const checks=[], errors=[], requests=[];
 const page=await browser.newPage({viewport:{width:1120,height:1000}});
 // Deterministic simulation time; genuine DOM key holds and Web Audio still run.
 await page.addInitScript(()=>{window.setInterval=()=>0;});
 page.on('pageerror',e=>errors.push(e.message));page.on('request',r=>{if(/^https?:/.test(r.url()))requests.push(r.url());});
 const api=(method,...args)=>page.evaluate(({method,args})=>window.whalePrototype[method](...args),{method,args});
 const snap=()=>api('snapshot'), view=()=>page.locator('#screen').getAttribute('data-page');
 const key=async(k,long=false)=>{await page.locator(long?`[data-long="${k}"]`:`[data-key="${k}"]`).click();};
 const scene=x=>page.locator(`[data-scene="${x}"]`).click();
 const event=x=>page.locator(`[data-event="${x}"]`).click();
 const advance=x=>api('step',x);
 const check=(name)=>checks.push(name);
 const screenshot=async(name)=>page.locator('#screen').screenshot({path:path.join(out,name+'.png')});
 try{
  await page.goto(pathToFileURL(path.join(__dirname,'index.html')).href);await page.evaluate(()=>document.fonts.ready);
  assert.equal(await view(),'home');assert.equal((await snap()).mode,1);check('Fresh install defaults to voice');await screenshot('01-home');
  await key('up');assert.equal(await view(),'companion');await screenshot('02-companion');
  for(const [i,pose] of ['hello','happy','feed','coffee'].entries()){
   if(i)await key('down');await key('ok');assert.equal((await snap()).action,pose);
   const end=(await snap()).actionUntil;await key('ok');assert.equal((await snap()).actionUntil,end);
   await page.locator('#screen img').evaluateAll(items=>Promise.all(items.map(im=>im.decode())));
   await screenshot(`0${3+i}-${pose}`);
   if(pose==='coffee'){assert.equal(await page.locator('.coffee-cup').count(),1);await advance(2.3);assert.equal(await page.locator('.coffee-cup').count(),0);assert.match(await page.locator('.companion-copy').innerText(),/精神/);await screenshot('06-coffee-satisfied');await advance(.8);}else await advance(3.1);
   assert.equal((await snap()).action,null);assert.equal(await page.locator('.interaction-effect').count(),0);
  }
  await key('down');await key('ok');assert.equal(await view(),'home');check('Manual companion, four actions, duplicate confirmation ignored, coffee finishing phase and return action');
  await key('up');await key('up');assert.equal((await snap()).choice,4);await key('down');assert.equal((await snap()).choice,0);check('Five-item selection including Return wraps correctly');
  for(const [action,index] of [['coffee',3]]){
   await scene('offline');for(let n=0;n<index;n++)await key('down');await key('ok');assert.equal((await snap()).action,action);assert.equal((await snap()).played.length,0);await key('ok',true);assert.equal(await view(),'home');assert.equal(await page.locator('.interaction-effect').count(),0);
   for(const interrupt of ['start','done','question','error']){
    await scene('manual');await key('up');for(let n=0;n<index;n++)await key('down');await key('ok');await event(interrupt);assert.equal(await view(),interrupt==='start'?'home':'list');assert.equal((await snap()).action,null);assert.equal(await page.locator('.interaction-effect').count(),0);
   }
  }check('Coffee works offline, stays silent, cancels immediately and cleans up on all work interruptions');
  assert.deepEqual(await page.evaluate(()=>ACTIONS),['打招呼','摸摸','喂小鱼','喝咖啡','返回任务']);assert.deepEqual(await page.evaluate(()=>Object.keys(MEDIA.effects).sort()),['coffee-cup','coffee-steam']);check('Hug and cola removed from menu and embedded effects; only coffee props remain');
  await scene('manual');
  await key('up');await key('ok');await key('ok',true);assert.equal(await view(),'home');assert.equal((await snap()).action,null);check('Long confirm interrupts animation immediately');
  await scene('auto');await advance(119);assert.equal(await view(),'home');await advance(1);assert.equal(await view(),'companion');assert(await page.locator('#screen').evaluate(e=>e.classList.contains('dim')));
  await key('up');assert.equal(await view(),'companion');assert.equal((await snap()).choice,0);check('120-second auto entry preserves dimming; first key only wakes');
  await event('start');assert.equal(await view(),'home');assert.equal((await snap()).current,'a');assert.equal((await snap()).queue.length,0);check('New work interrupts companion silently');
  await key('up');await event('done');assert.equal(await view(),'list');assert((await snap()).tasks[0].unread);assert.equal(await page.locator('.task-row b').first().innerText(),'整理明天的开发计划');
  await screenshot('06-attention-list');await key('ok');assert.equal(await view(),'reply');assert.equal((await snap()).tasks[0].unread,false);await key('down');assert(await page.locator('#reply-text').evaluate(e=>e.scrollTop>0));await key('ok');check('Reply event interrupts to list; only opening latest continuous reply acknowledges it');
  await scene('background');await advance(121);assert.equal(await view(),'home');assert.equal((await snap()).tasks.length,10);await key('up');await key('up');assert.equal(await view(),'companion');check('Working session beyond first page prevents automatic entry, manual entry remains');
  await scene('manual');await page.check('#legacy');await advance(121);assert.equal(await view(),'home');await key('up');await key('up');assert.equal(await view(),'companion');check('Legacy bridge disables connected automatic entry, preserves manual companion');
  await scene('offline');assert.equal(await view(),'companion');assert.match(await page.locator('.connection').innerText(),/未连接/);await key('ok');await advance(3.1);await screenshot('07-offline');await page.click('#reconnect');assert.equal(await view(),'companion');assert.equal((await snap()).queue.length,0);
  await page.click('#history');assert.equal(await view(),'list');assert.equal((await snap()).played.length,0);check('Offline interaction, idle reconnect stays, unread reconnect goes to list without historical audio');
  await scene('manual');await page.click('#disconnect');await advance(4.9);assert.equal((await snap()).queue.length,0);await page.click('#reconnect');await advance(2);assert.equal((await snap()).played.length,0);
  await page.click('#disconnect');await advance(5);assert.equal((await snap()).queue.length,1);await advance(1.1);assert.equal((await snap()).played[0].type,'disconnect');await advance(20);assert.equal((await snap()).played.length,1);check('Disconnect debounce 5 seconds, quick reconnect cancels, once per outage');
  await scene('manual');await event('done');await page.selectOption('#target','b');await event('error');await page.selectOption('#target','c');await event('question');await advance(1.1);assert.deepEqual((await snap()).played.map(x=>x.type),['question']);assert.equal((await snap()).tasks.filter(x=>x.unread).length,3);
  await page.click('#repeat-event');await advance(2);assert.equal((await snap()).played.length,1);check('One-second batch priority, one cue, other unread retained, duplicate event suppressed');
  await key('down',true);await key('ok');assert.equal(await view(),'question');assert((await snap()).tasks.find(t=>t.id==='c').question);await key('ok');await key('ok');assert.equal((await snap()).tasks.find(t=>t.id==='c').question,false);check('Question preview and second confirmation; merely viewing does not answer');
  await key('down',true);await key('ok');assert.equal(await view(),'error');assert.equal((await snap()).tasks.find(t=>t.id==='b').unread,false);check('Opening error details clears only that unread error');
  await scene('record');await event('done');await advance(1.1);assert.equal(await view(),'voice');assert.equal((await snap()).played.length,0);await screenshot('08-recording-deferred');await key('ok');assert.equal((await snap()).voicePhase,'preview');assert.equal((await snap()).played[0].type,'done');check('Recording defers valid reminder; recording stop revalidates and plays');
  await scene('record');await event('question');await page.click('#resolve');await advance(1.1);await key('ok');assert.equal((await snap()).played.length,0);check('Resolved reminder is discarded after recording');
  await scene('record');await event('done');await page.click('#archive');assert.equal((await snap()).tasks.length,2);assert.equal((await snap()).recording,false);await advance(2);assert.equal((await snap()).played.length,0);check('Archive removes current session, cancels recording and queued reminder');
  await scene('manual');await event('done');await page.click('#delete');await advance(2);assert.equal((await snap()).played.length,0);check('Deletion removes pending cue and task');
  await scene('manual');await key('up');await page.click('#add-task');assert.equal(await view(),'home');assert.equal((await snap()).tasks.length,4);assert.equal((await snap()).current,'new1');
  for(const quiet of ['thinking','tool','waiting']){await event(quiet);await advance(1.1);}
  assert.equal((await snap()).played.length,0);assert.equal((await snap()).tasks.find(t=>t.id==='new1').status,'start');check('New session interrupts companion; thinking, tools and network waits stay silent');
  await advance(62);assert.match(await page.locator('.home-state').innerText(),/01:05/);check('Task timer continues across quiet work phases and minute boundaries');
  for(const alert of ['question','error']){await scene('manual');await key('up');await key('ok');await event(alert);assert.equal(await view(),'list');assert.equal((await snap()).action,null);assert((await snap()).tasks[0].unread);}check('Question and error interrupt an active interaction without acknowledging markers');
  for(const protectedPage of ['reply','question','voice','manage','connection','list']){
   await scene('manual');
   if(protectedPage==='reply'){await event('done');await key('down');}
   if(protectedPage==='question'){await event('question');await key('ok');}
   if(protectedPage==='voice')await key('up',true);
   if(['manage','connection'].includes(protectedPage)){await key('ok',true);if(protectedPage==='connection'){await key('down');await key('ok');}}
   if(protectedPage==='list')await key('ok');
   assert.equal(await view(),protectedPage);await advance(121);assert.equal(await view(),protectedPage);
  }check('No auto-navigation from reply, question, recording, management, connection or list');
  await scene('sound');await screenshot('09-sound-settings');await page.click('#enable-audio');
  assert.equal(await page.locator('.menu-row').count(),4);assert.doesNotMatch(await page.locator('#screen').innerText(),/静音|音效＋短语/);check('Settings contain exactly two sound modes plus preview and return');
  await key('up');await key('ok');assert.equal((await snap()).mode,0);await page.reload();assert.equal((await snap()).mode,0);check('Effects mode survives reload, including numeric zero');
  await scene('sound');await key('down');await key('ok');assert.equal((await snap()).mode,1);await page.reload();assert.equal((await snap()).mode,1);check('Voice mode survives reload');
  await page.evaluate(()=>{localStorage.setItem('whale-v130-sound','0');localStorage.removeItem('whale-v130-sound-mode-v2');});await page.reload();assert.equal((await snap()).mode,1);
  await page.evaluate(()=>localStorage.setItem('whale-v130-sound-mode-v2','mute'));await page.reload();assert.equal((await snap()).mode,1);check('Old three-mode storage and invalid values cannot restore mute or override default voice');
  await scene('sound');await page.click('#enable-audio');await key('down');await key('ok');
  await page.waitForFunction(()=>document.getElementById('audio-status').textContent.startsWith('试听完成'),{},{timeout:6000});
  assert.deepEqual(await page.evaluate(()=>[...decoded.keys()]),['voice-done']);await key('down');await key('ok');assert.equal(await view(),'manage');check('Preview uses voice only; return item navigates to management');
  for(const type of ['done','question','error','disconnect']){
   await scene('manual');await page.click('#enable-audio');await page.evaluate(()=>decoded.clear());
   if(type==='disconnect'){await page.click('#disconnect');await advance(5);}else await event(type);
   await advance(1.1);await page.waitForFunction(()=>document.getElementById('audio-status').textContent.startsWith('试听完成'),{},{timeout:6000});
   const expected=(['done','question','error','disconnect'].includes(type)?'voice-':'tone-')+type;
   assert.deepEqual(await page.evaluate(()=>[...decoded.keys()]),[expected]);
  }check('Voice mode directly speaks completion, choice, error and disconnect');
  for(const mode of ['voice','effects']){
   await page.evaluate(mode=>localStorage.setItem('whale-v130-sound-mode-v2',mode),mode);await scene('manual');await page.click('#enable-audio');await page.evaluate(()=>decoded.clear());
   await event('start');await key('up');await event('stop');await page.click('#repeat-event');await advance(1.1);
   assert.equal((await snap()).tasks[0].status,'stop');assert.equal((await snap()).tasks[0].unread,false);assert.equal(await view(),'companion');assert.equal((await snap()).queue.length,0);assert.equal((await snap()).played.length,0);assert.equal(await page.evaluate(()=>decoded.size),0);
   await scene('record');await event('question');await event('stop');await advance(1.1);await key('ok');assert.equal((await snap()).played.length,0);assert.equal((await snap()).queue.length,0);
  }
  await page.evaluate(()=>localStorage.setItem('whale-v130-sound-mode-v2','voice'));check('User stop is silent in both modes, including repeats and recording deferral, while its status updates');
  assert.equal(await page.locator('[data-sample="stop"]').count(),0);assert.equal(await page.evaluate(()=>Object.hasOwn(MEDIA.audio,'tone-stop')),false);check('Stop preview and obsolete sound are excluded from the active prototype');
  await scene('manual');await page.click('#enable-audio');await page.evaluate(()=>decoded.clear());await page.click('#disconnect');await advance(5);await page.click('#reconnect');await advance(1.1);assert.equal((await snap()).played.length,0);assert.equal(await page.evaluate(()=>decoded.size),0);check('Reconnect cancels a disconnect phrase already queued after the five-second threshold');
  await scene('sound');await key('up');await key('ok');await scene('manual');await page.click('#enable-audio');await page.evaluate(()=>decoded.clear());await event('done');await advance(1.1);
  await page.waitForFunction(()=>document.getElementById('audio-status').textContent.startsWith('试听完成'),{},{timeout:6000});assert.deepEqual(await page.evaluate(()=>[...decoded.keys()]),['tone-done']);
  await scene('sound');await key('down');await key('ok');check('Effects mode event uses no speech, switching back to voice works');
  // Actual audio decoding and playback completion, not merely the existence of WAVs.
  for(const type of ['done','question','error','disconnect']){
   await page.locator(`[data-sample="${type}"][data-mode="0"]`).click();
   await page.waitForFunction(()=>document.getElementById('audio-status').textContent.startsWith('试听完成'),{},{timeout:5000});
  }
  for(const type of ['done','question','error','disconnect']){
   await page.locator(`[data-voice="${type}"]`).click();
   await page.waitForFunction(()=>document.getElementById('audio-status').textContent.startsWith('试听完成'),{},{timeout:6000});
  }check('Four WAV effects and all four fixed Chinese speech samples decode and finish playback');
  await scene('manual');await page.locator('[data-voice="error"]').click();await key('up',true);assert((await snap()).recording);assert.equal(await page.evaluate(()=>audioNode===null),true);check('Recording starts by cancelling active playback');
  await key('ok',true);assert.equal(await view(),'home');await page.locator('[data-key="up"]').click({delay:650});assert.equal(await view(),'voice');check('Real pointer hold enters recording without short-up companion');
  await key('ok',true);await page.locator('#screen').focus();await page.keyboard.press('ArrowUp');assert.equal(await view(),'companion');await page.keyboard.press('Shift+Enter');assert.equal(await view(),'home');check('Keyboard short and long press paths');
  await page.locator('#battery').fill('19');await page.locator('#battery').dispatchEvent('input');assert.match(await page.locator('.battery').innerText(),/19%/);check('Battery display updates');
  await scene('manual');await key('up');await page.screenshot({path:path.join(out,'desktop.png'),fullPage:true});
  await page.setViewportSize({width:360,height:900});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);await page.screenshot({path:path.join(out,'mobile.png'),fullPage:true});
  const size=await page.locator('#screen').boundingBox();assert.equal(size.width,240);assert.equal(size.height,320);
  assert.equal(await page.locator('img').evaluateAll(xs=>xs.every(x=>x.complete&&x.naturalWidth>0)),true);check('Desktop and mobile layouts, exact 240×320 screen, all embedded images loaded');
  assert.deepEqual(errors,[]);assert.deepEqual(requests,[]);check('Zero page exceptions and zero external network requests');
  fs.writeFileSync(path.join(out,'validation.json'),JSON.stringify({stage:'prototype only',checks,errors,externalRequests:requests,deviceTests:'not run; no firmware changes'},null,2));
  console.log(JSON.stringify({passed:checks.length,checks,out},null,2));
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
