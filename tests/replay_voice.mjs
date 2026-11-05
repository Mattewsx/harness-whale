import assert from 'node:assert/strict'
import { createVoiceService, validWave, normalizeVoiceTerms } from '../harness/plugin/voice.ts'
const events=[];let deliveries=0, hidden=false, waiting=false, phase='ready'
const agent={status:'running',session:{id:'synthetic',ownEvents:()=>events},followup(message){deliveries++;events.push({type:'agent/inbox/spliced',data:{inserted:[message]}})}}
let recognitionText='合成测试'
const ctx={agents:{get:()=>agent},speechToText:{snapshot:()=>({providers:[{id:'sensevoice-local',preparation:{phase}}]}),resolve:x=>x,transcribe:async()=>({text:recognitionText})}}
let service=createVoiceService(ctx,()=>!hidden,()=>true,()=>waiting)
const env=async()=>{for(let i=0;i<50;i++){const e=service.environment();if(e.HARNESS_WHALE_VOICE_PORT)return e;await new Promise(r=>setTimeout(r,10))}throw Error('server unavailable')}
let e=await env()
async function call(path,data,auth=true){const response=await fetch(`http://127.0.0.1:${e.HARNESS_WHALE_VOICE_PORT}${path}`,{method:data===undefined?'GET':'POST',headers:auth?{Authorization:`Bearer ${e.HARNESS_WHALE_VOICE_TOKEN}`}:{},body:data===undefined?undefined:JSON.stringify(data)});return [response.status,await response.json()]}
const req={id:'a'.repeat(32),sessionId:'synthetic',text:'合成测试'}
try {
 assert.equal((await call('/ready',undefined,false))[0],401)
 assert.equal((await call('/ready'))[1].ready,true)
 // DSH keeps verified resources in standby and wakes its worker on transcribe.
 phase='standby';assert.equal((await call('/ready'))[1].ready,true)
 const wave=Buffer.alloc(684)
 wave.write('RIFF');wave.writeUInt32LE(wave.length-8,4);wave.write('WAVEfmt ',8)
 wave.writeUInt32LE(16,16);wave.writeUInt16LE(1,20);wave.writeUInt16LE(1,22)
 wave.writeUInt32LE(16000,24);wave.writeUInt32LE(32000,28)
 wave.writeUInt16LE(2,32);wave.writeUInt16LE(16,34);wave.write('data',36);wave.writeUInt32LE(640,40)
 async function transcribe(){return fetch(`http://127.0.0.1:${e.HARNESS_WHALE_VOICE_PORT}/transcribe`,{method:'POST',headers:{Authorization:`Bearer ${e.HARNESS_WHALE_VOICE_TOKEN}`},body:wave})}
 assert.equal((await (await transcribe()).json()).text,'合成测试')
 for (const [raw, expected] of [
  ['请打开 deep sick，然后查看最新回复。','请打开 DeepSeek，然后查看最新回复。'],
  ['使用一下DEEP SEEK生成代码','使用一下DeepSeek生成代码'],
  ['重启 deepseek Harness','重启 DeepSeek Harness'],
 ]) {
  assert.equal(normalizeVoiceTerms(raw),expected)
  recognitionText=raw
  assert.equal((await (await transcribe()).json()).text,expected)
  assert.equal(deliveries,0,'preview must not send')
 }
 for (const literal of [
  '请打开 deep 然后检查回复','deep sick','I feel a deep sickness.',
  '请打开 deep sleep 模式','请打开 deep seeker','请打开 deep sick.txt',
  '请打开 deep_sick','请打开 deep sick2','请翻译“打开 deep sick”',
  '请解释 `打开 deep sick`','https://example.test/打开deepseek',
  '请把任务列表里的新回复标记出来。',
 ]) assert.equal(normalizeVoiceTerms(literal),literal)
 recognitionText='合成测试'
 for(phase of ['checking','preparing','failed','cancelled','unknown']) {
  assert.equal((await call('/ready'))[1].ready,false)
  assert.equal((await transcribe()).status,503)
 }
 phase='ready'
 const results=await Promise.all(Array.from({length:8},()=>call('/submit',req)))
 assert(results.every(x=>x[1].status==='queued'));assert.equal(deliveries,1)
 assert.equal((await call('/submit',{...req,text:'changed'}))[1].status,'invalid')
 assert.equal((await call('/submit',{...req,sessionId:'wrong'}))[1].status,'invalid')
 service.close();service=createVoiceService(ctx,()=>!hidden,()=>true,()=>waiting);e=await env()
 assert.equal((await call('/query',req))[1].status,'accepted')
 assert.equal((await call('/submit',req))[1].status,'accepted');assert.equal(deliveries,1)
 hidden=true;assert.equal((await call('/submit',{...req,id:'b'.repeat(32)}))[1].status,'archived');hidden=false
 waiting=true;assert.equal((await call('/submit',{...req,id:'c'.repeat(32)}))[1].status,'question');waiting=false
 assert.equal((await call('/query',{...req,id:'d'.repeat(32)}))[1].status,'not_sent');assert.equal(deliveries,1)
 let resumed=0
 ctx.agents.get=()=>undefined
 ctx.sessionController={resolveAgent:async id=>{assert.equal(id,'synthetic');resumed++;return {agent}}}
 assert.equal((await call('/submit',{...req,id:'e'.repeat(32)}))[1].status,'queued')
 assert.equal(resumed,1);assert.equal(deliveries,2)
 assert.equal(validWave(Buffer.alloc(44)),false)
 console.log('Voice service: auth, concurrent dedup, durable receipt, target identity, archive, question and query-only PASS')
}finally{service.close()}
