"""Offline, non-destructive electronic coloration of the two voice auditions.

Retains the dry voice, mixes ring modulation and a short filtered delay, and
adds a small quantized layer. No resynthesis, voice cloning or runtime DSP.
Requires fonttools/brotli only for the self-contained review page font.
"""
import array
import base64
import hashlib
import json
import math
import struct
import wave
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'audio-review-v3'
RATE = 16000
TEXT = {'done':'忙完啦。','question':'帮我选一下？','error':'遇到问题啦，来看看。'}
PROFILES = {
    'light': {'label':'轻机械', 'dry':.78,'ring':.14,'delay':.05,'digital':.03,'hz':58,'delay_ms':7,'quantization_bits':9},
    'medium': {'label':'稍明显', 'dry':.61,'ring':.25,'delay':.09,'digital':.05,'hz':68,'delay_ms':9,'quantization_bits':8},
}


def read(path):
    with wave.open(str(path),'rb') as f:
        assert (f.getnchannels(),f.getsampwidth(),f.getframerate())==(1,2,RATE)
        return list(array.array('h',f.readframes(f.getnframes())))


def save(path,samples):
    with wave.open(str(path),'wb') as f:
        f.setparams((1,2,RATE,0,'NONE','not compressed'))
        f.writeframes(struct.pack('<'+'h'*len(samples),*samples))


def rms(samples):
    return math.sqrt(sum(x*x for x in samples)/len(samples))


def process(samples,profile):
    x=[s/32768 for s in samples]
    delay=round(profile['delay_ms']*RATE/1000)
    levels=2**(profile['quantization_bits']-1)
    out=[]
    for i,s in enumerate(x):
        carrier=math.cos(2*math.pi*profile['hz']*i/RATE)
        delayed=x[i-delay] if i>=delay else 0
        quantized=round(s*levels)/levels
        y=s*profile['dry']+s*carrier*profile['ring']+delayed*profile['delay']+quantized*profile['digital']
        y*=min(1,i/48,(len(x)-1-i)/48)
        out.append(y)
    gain=min(rms(x)/rms(out),10**(-3/20)/max(abs(s) for s in out))
    result=[round(s*gain*32767) for s in out]
    assert len(result)==len(samples) and max(abs(s) for s in result)<32767
    assert result[0]==0 and result[-1]==0
    return result


def data(path):
    return 'data:audio/wav;base64,'+base64.b64encode(path.read_bytes()).decode('ascii')


def main():
    OUT.mkdir(exist_ok=True)
    media={}
    manifest={'status':'audition only, not integrated into prototype or firmware','sample_rate':RATE,'channels':1,'bits':16,'processing':'local offline DSP; dry voice plus ring modulation, short delay and light quantization','profiles':PROFILES,'samples':[]}
    cards=[]
    for voice,label in [('a','A · 轻甜少女'),('b','B · 柔软陪伴')]:
        media[f'{voice}-original']=data(ROOT/f'voice-candidates/{voice}-three-phrases.wav')
        buttons=f'<button data-sound="{voice}-original">原少女声</button>'
        for strength,profile in PROFILES.items():
            full=[]
            for event,text in TEXT.items():
                source=ROOT/f'voice-candidates/{voice}-{event}.wav'
                original=read(source); samples=process(original,profile)
                name=f'{voice}-{strength}-{event}.wav';dest=OUT/name;save(dest,samples)
                media[f'{voice}-{strength}-{event}']=data(dest)
                if full:full += [0]*7200
                full += samples
                manifest['samples'].append({'voice':voice,'strength':strength,'event':event,'text':text,'file':name,'seconds':len(samples)/RATE,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'sha256':hashlib.sha256(dest.read_bytes()).hexdigest(),'peak_dbfs':round(20*math.log10(max(abs(x) for x in samples)/32767),2),'rms_change_db':round(20*math.log10(rms(samples)/rms(original)),2),'same_length_as_source':len(samples)==len(original)})
                # Preview the complete event cue without changing the base chiptune files.
                cue=read(ROOT/f'audio-review-v2/chiptune-{event}.wav')+[0]*1920+samples
                paired=OUT/f'cue-{voice}-{strength}-{event}.wav';save(paired,cue)
                media[f'cue-{voice}-{strength}-{event}']=data(paired)
            combined=OUT/f'{voice}-{strength}-three-phrases.wav';save(combined,full)
            media[f'{voice}-{strength}']=data(combined)
            buttons+=f'<button class="{"primary" if strength=="light" else ""}" data-sound="{voice}-{strength}">{profile["label"]} · 三句</button>'
        rows=[]
        for event,text in TEXT.items():
            rows.append(f'<div class="cue"><span>{text}</span><div class="buttons"><button data-sound="cue-{voice}-light-{event}">8-bit＋轻机械</button><button data-sound="cue-{voice}-medium-{event}">8-bit＋稍明显</button></div></div>')
        cards.append(f'<section><h2>{label}</h2><p>先比较同一底声，再听与状态音效的组合。</p><div class="buttons">{buttons}</div>'+''.join(rows)+'</section>')
    html='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>鲸鱼搭子 · 少女机械声试听</title><style>@font-face{font-family:Whale;src:url(FONT_DATA)}*{box-sizing:border-box}body{margin:0;padding:28px 18px;background:#edf2f5;color:#253853;font:14px/1.75 Whale,"Microsoft YaHei",sans-serif}main{max-width:960px;margin:auto}header small{letter-spacing:2px;color:#446f9c}h1{font-size:29px;margin:10px 0}p{color:#62738a}article{display:grid;grid-template-columns:1fr 1fr;gap:18px}section{background:#faf9f4;border:1px solid #d6e1ea;border-radius:18px;padding:22px}h2{font-size:21px;margin:0}section p{font-size:12px}.buttons{display:flex;flex-wrap:wrap;gap:8px}button{font:12px Whale,"Microsoft YaHei",sans-serif;padding:9px 12px;border:1px solid #c2d3e1;background:white;color:#253853;border-radius:8px;cursor:pointer}button:hover{background:#e8f1f7}button.primary{background:#dce9f3;border-color:#a8c2d7}button:focus-visible{outline:3px solid #e9b64b}.toolbar{background:#ffffffde;border:1px solid #d6e1ea;border-radius:12px;padding:14px;margin:20px 0}.toolbar p{margin:7px 0 0;font-size:12px}.cue{border-top:1px solid #dce4ea;margin-top:20px;padding-top:16px}.cue span{display:block;margin-bottom:8px}.note{font-size:12px;margin-top:20px}audio{display:none}@media(max-width:700px){article{grid-template-columns:1fr}h1{font-size:25px}}</style><main><header><small>WHALE COMPANION · AUDIO REVIEW 03</small><h1>少女声，带一点机器人气质。</h1><p>保留原声音的发音与语速，加入轻微电子颤音、短延迟和数字颗粒。两种底声都有两档机械感，可直接对比。</p></header><div class="toolbar"><button id="stop">停止播放</button><p id="status" role="status">三句顺序：忙完啦 → 帮我选一下 → 遇到问题啦，来看看</p></div><article>CARDS</article><p class="note">“轻机械”以保留少女感为主；“稍明显”提高电子成分占比。纯语音时长与原稿一致，并做响度匹配及峰值保护。全部在电脑上预处理成固定 PCM，设备不需要实时变声，也不增加联网依赖。本页是待选样稿，真实扬声器效果未验证。</p><audio id="player"></audio></main><script>const media=MEDIA;const player=document.getElementById('player'),status=document.getElementById('status');let generation=0;document.querySelectorAll('[data-sound]').forEach(b=>b.onclick=async()=>{let ticket=++generation;player.pause();player.src=media[b.dataset.sound];status.textContent='正在播放：'+b.closest('section').querySelector('h2').textContent+' / '+b.textContent;try{await player.play()}catch{if(ticket===generation)status.textContent='播放失败，请重新点击。'}});player.onended=()=>status.textContent='播放完成，可以继续比较。';document.getElementById('stop').onclick=()=>{generation++;player.pause();player.currentTime=0;status.textContent='已停止。'};</script></html>'''
    html=html.replace('CARDS',''.join(cards)).replace('MEDIA',json.dumps(media))
    font=TTFont(ROOT.parents[1]/'device-firmware/managed_components/lvgl__lvgl/scripts/generators/built_in_font/SourceHanSansSC-Normal.otf')
    cmap=font.getBestCmap();chars={ord(c) for c in html if ord(c)>=32}
    missing=[c for c in chars if 0x3400<=c<=0x9fff and c not in cmap];assert not missing
    cut=subset.Subsetter();cut.populate(unicodes=chars & cmap.keys());cut.subset(font);font.flavor='woff2';font.save(OUT/'review-cn.woff2')
    html=html.replace('FONT_DATA','data:font/woff2;base64,'+base64.b64encode((OUT/'review-cn.woff2').read_bytes()).decode('ascii'))
    (OUT/'index.html').write_text(html,'utf8');(OUT/'FONT-LICENSE.txt').write_bytes((ROOT/'assets/FONT-LICENSE.txt').read_bytes())
    manifest['missing_chinese']=missing
    (OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n','utf8')
    print(json.dumps({'processed_phrases':len(manifest['samples']),'duration_preserved':all(x['same_length_as_source'] for x in manifest['samples']),'max_absolute_rms_change_db':max(abs(x['rms_change_db']) for x in manifest['samples']),'missing_chinese':missing}))


if __name__=='__main__':main()
