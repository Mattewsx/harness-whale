"""Original clockwork-girl direction inspired by the user's Orianna reference.

Reference is creative direction, not a measured acoustic match. No game audio
or actor recording is used. Three original cue texts are synthesized, then
locally colored with a mild resonant/metallic layer for fixed offline playback.
"""
import asyncio
import base64
import hashlib
import json
import math
import subprocess
from pathlib import Path

import edge_tts
from fontTools import subset
from fontTools.ttLib import TTFont

from make_mechanical_voice_review import read, save, rms, data, RATE

ROOT=Path(__file__).resolve().parent
OUT=ROOT/'audio-review-v4'
TEXT={'done':'忙完啦。','question':'帮我选一下？','error':'遇到问题啦，来看看。'}
VOICE={'voice':'zh-CN-XiaoyiNeural','rate':'-12%','pitch':'+2Hz'}


def peak_filter(samples,frequency,q,gain_db):
    a=10**(gain_db/40);w=2*math.pi*frequency/RATE;alpha=math.sin(w)/(2*q)
    b0,b1,b2=1+alpha*a,-2*math.cos(w),1-alpha*a
    a0,a1,a2=1+alpha/a,-2*math.cos(w),1-alpha/a
    b0,b1,b2,a1,a2=[c/a0 for c in (b0,b1,b2,a1,a2)]
    x1=x2=y1=y2=0;out=[]
    for x in samples:
        y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2
        out.append(y);x2,x1,y2,y1=x1,x,y1,y
    return out


def trim(samples):
    active=[i for i,x in enumerate(samples) if abs(x)>150]
    assert active
    return samples[max(0,active[0]-960):min(len(samples),active[-1]+1920)]


def normalize(samples,target=.1):
    gain=min(target/rms(samples),10**(-3/20)/max(abs(x) for x in samples))
    return [round(x*gain*32767) for x in samples]


def clockwork(samples):
    x=[v/32768 for v in samples]
    # Preserve Chinese lexical tones: no pitch-flattening or syllable resampling.
    colored=peak_filter(peak_filter(x,1280,2.2,4),2460,3.1,2)
    d1,d2=120,224
    out=[]
    for i,s in enumerate(colored):
        # Short signed reflections create a restrained metallic cavity, not a long echo.
        reflected=(colored[i-d1] if i>=d1 else 0)*.065-(colored[i-d2] if i>=d2 else 0)*.035
        mechanical=.10*s*math.cos(2*math.pi*109*i/RATE)
        y=.65*x[i]+.25*s+mechanical+reflected
        y*=min(1,i/64,(len(x)-1-i)/80)
        out.append(y)
    result=normalize(out)
    assert result[0]==result[-1]==0 and max(abs(v) for v in result)<32767
    return result


def combine(clips,gap_ms=450):
    values=[]
    for clip in clips:
        if values:values += [0]*round(gap_ms*RATE/1000)
        values += clip
    return values


async def main(ffmpeg, config=None):
    config=config or {}
    out=config.get('out',OUT);voice=config.get('voice',VOICE)
    out.mkdir(exist_ok=True);media={};rows=[]
    report={'status':'creative audition; approval pending','reference':'User: League of Legends Orianna (clockwork-girl direction)','reference_url':'https://www.leagueoflegends.com/zh-tw/champions/orianna/','reference_scope':'character concept and creative interpretation, not an acoustic comparison or voice clone','direction':config.get('direction','calm clockwork-girl'),'voice':voice,'source':'Edge online TTS of original three cue texts; no game/actor recordings','processing':{'resonances_hz':[1280,2460],'ring_hz':109,'ring_mix':.10,'reflection_ms':[7.5,14],'no_lexical_pitch_flattening':True},'samples':[]}
    clean=[];processed=[];cues=[]
    for event,text in TEXT.items():
        mp3=out/f'source-{event}.mp3';raw=out/f'source-{event}.wav'
        if not mp3.exists() or mp3.stat().st_size<128:
            pending=out/f'source-{event}.pending.mp3'
            await edge_tts.Communicate(text,**voice).save(str(pending))
            assert pending.stat().st_size>=128, 'Incomplete synthesis output'
            pending.replace(mp3)
        subprocess.run([str(ffmpeg),'-hide_banner','-loglevel','error','-y','-i',str(mp3),'-ar',str(RATE),'-ac','1','-c:a','pcm_s16le',str(raw)],check=True)
        base=trim(read(raw));base=normalize([x/32768 for x in base]);save(out/f'clean-{event}.wav',base)
        samples=clockwork(base);dest=out/f'clockwork-{event}.wav';save(dest,samples)
        cue=read(ROOT/f'audio-review-v2/chiptune-{event}.wav')+[0]*1920+samples;save(out/f'cue-{event}.wav',cue)
        clean.append(base);processed.append(samples);cues.append(cue)
        for name in [f'clean-{event}',f'clockwork-{event}',f'cue-{event}']:media[name]=data(out/f'{name}.wav')
        report['samples'].append({'event':event,'text':text,'file':dest.name,'seconds':round(len(samples)/RATE,4),'pcm_bytes':len(samples)*2,'peak_dbfs':round(20*math.log10(max(abs(s) for s in samples)/32767),2),'rms_dbfs':round(20*math.log10(rms(samples)/32767),2),'sha256':hashlib.sha256(dest.read_bytes()).hexdigest()})
        rows.append(f'<section><h2>{text}</h2><p>{len(samples)/RATE:.2f} 秒 · 克制语速与短金属共鸣</p><div class="buttons"><button data-sound="clean-{event}">新底声</button><button class="primary" data-sound="clockwork-{event}">机械少女</button><button data-sound="cue-{event}">配 8-bit 提示音</button></div></section>')
    for name,clips in [('clean-three',clean),('clockwork-three',processed),('cues-three',cues)]:
        save(out/f'{name}.wav',combine(clips));media[name]=data(out/f'{name}.wav')
    media['previous']=data(config.get('previous',ROOT/'audio-review-v3/a-medium-three-phrases.wav'))
    report['resource_estimate_with_6_poses_and_5_chiptunes']=6*196*196*3+sum(x['pcm_bytes'] for x in report['samples'])+sum(len(read(ROOT/f'audio-review-v2/chiptune-{e}.wav'))*2 for e in ['done','question','error','stop','disconnect'])
    html='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>鲸鱼搭子 · 发条人偶方向试听</title><style>@font-face{font-family:Whale;src:url(FONT_DATA)}*{box-sizing:border-box}body{margin:0;padding:32px 18px;background:#edf2f5;color:#253853;font:14px/1.8 Whale,"Microsoft YaHei",sans-serif}main{max-width:850px;margin:auto}header small{color:#446f9c;letter-spacing:2px}h1{font-size:29px;margin:10px 0}p{color:#62738a}.directions{display:flex;flex-wrap:wrap;gap:10px;margin:18px 0}.directions span{font-size:12px;background:#dfeaf3;border-radius:20px;padding:4px 12px}.toolbar,section{background:#faf9f4;border:1px solid #d4e0e9;border-radius:16px;padding:22px;margin:14px 0}h2{font-size:18px;margin:0}.buttons{display:flex;flex-wrap:wrap;gap:8px}button{font:12px Whale,"Microsoft YaHei",sans-serif;padding:9px 13px;border-radius:8px;border:1px solid #c2d3e1;background:white;color:#253853;cursor:pointer}button:hover{background:#e5eff7}button.primary{background:#dce9f3;border-color:#aac3d8}button:focus-visible{outline:3px solid #e9b64b}.toolbar p,section p,.note{font-size:12px}.toolbar p{margin:10px 0 0}audio{display:none}@media(max-width:500px){h1{font-size:25px}section,.toolbar{padding:17px}}</style><main><header><small>WHALE COMPANION · AUDIO REVIEW 04</small><h1>这次，往“发条人偶”靠近一点。</h1><p>将发条的参考理解为平静、克制的机械少女方向。重新制作更平稳的少女底声，降低甜腻感，再加短促的金属共鸣。</p><div class="directions"><span>语速稍缓</span><span>停顿清楚</span><span>轻金属共鸣</span><span>保留中文声调</span></div></header><div class="toolbar"><div class="buttons"><button class="primary" data-sound="clockwork-three">新版 · 连听三句</button><button data-sound="cues-three">新版＋8-bit 音效</button><button data-sound="previous">上一版 · A 稍明显</button><button data-sound="clean-three">新版未处理底声</button><button id="stop">停止</button></div><p id="status" role="status">顺序：忙完啦 → 帮我选一下 → 遇到问题啦，来看看</p></div>ROWS<p class="note">发条用于创作方向参考，三句提示文案保持不变。音频已完成预处理，可离线播放。当前仍为试听样稿，尚未写入固件，实际设备听感待确认。</p><audio id="player"></audio></main><script>const media=MEDIA;const player=document.getElementById('player'),status=document.getElementById('status');let generation=0;document.querySelectorAll('[data-sound]').forEach(b=>b.onclick=async()=>{const ticket=++generation;player.pause();player.src=media[b.dataset.sound];status.textContent='正在播放：'+b.textContent;try{await player.play()}catch{if(ticket===generation)status.textContent='播放失败，请重试。'}});player.onended=()=>status.textContent='播放完成，可继续对比。';document.getElementById('stop').onclick=()=>{generation++;player.pause();player.currentTime=0;status.textContent='已停止。'};</script></html>'''
    html=html.replace('ROWS',''.join(rows)).replace('MEDIA',json.dumps(media))
    for before,after in config.get('copy',{}).items():html=html.replace(before,after)
    source=ROOT.parents[1]/'device-firmware/managed_components/lvgl__lvgl/scripts/generators/built_in_font/SourceHanSansSC-Normal.otf'
    font=TTFont(source);cmap=font.getBestCmap();chars={ord(c) for c in html if ord(c)>=32}
    assert not [cp for cp in chars if 0x3400<=cp<=0x9fff and cp not in cmap]
    cut=subset.Subsetter();cut.populate(unicodes=chars & cmap.keys());cut.subset(font);font.flavor='woff2';font.save(out/'review-cn.woff2')
    html=html.replace('FONT_DATA','data:font/woff2;base64,'+base64.b64encode((out/'review-cn.woff2').read_bytes()).decode('ascii'))
    (out/'index.html').write_text(html,'utf8');(out/'FONT-LICENSE.txt').write_bytes((ROOT/'assets/FONT-LICENSE.txt').read_bytes())
    assert report['resource_estimate_with_6_poses_and_5_chiptunes']<1048576
    (out/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n','utf8')
    print(json.dumps({'samples':report['samples'],'resource_estimate':report['resource_estimate_with_6_poses_and_5_chiptunes']},ensure_ascii=False))


if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser();parser.add_argument('--ffmpeg',required=True,type=Path)
    args=parser.parse_args();asyncio.run(main(args.ffmpeg))
