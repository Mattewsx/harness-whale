"""Original chiptune event cues and an offline A/B audio review page.

No game melodies or sampled game audio. Uses pulse/triangle/noise synthesis.
This does not replace firmware or pending baseline assets before review.
"""
import array
import base64
import hashlib
import json
import math
import random
import struct
import wave
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'audio-review-v2'
RATE = 16000
EVENTS = {
    'done': ('任务完成', '150 ms · 四音快速上行，明亮收束', 'C5 → E5 → G5 → C6'),
    'question': ('等你选择', '430 ms · 两组短音，中间留出明显停顿', '双音 · 停顿 · 双音'),
    'error': ('任务出错', '370 ms · 低音双击，尾部略带颗粒感', '低音下坠 · 停顿 · 更低收尾'),
    'stop': ('手动停止', '85 ms · 高频短促截断，一声收住', '极短阶梯下切'),
    'disconnect': ('蓝牙断开', '440 ms · 柔和五级下降，逐步淡出', '五音缓降，与报错节奏不同'),
}


def segment(frequency, duration, kind='pulse', duty=.25, level=1, end=None, noise=0):
    count = round(duration * RATE)
    values = []
    phase = 0
    rng = random.Random(130)
    for i in range(count):
        f = frequency if end is None else frequency + (end-frequency)*i/max(1,count-1)
        phase = (phase+f/RATE) % 1
        if frequency == 0: signal = 0
        elif kind == 'triangle': signal = 1-4*abs(phase-.5)
        else:
            # DC-free duty pulse, softly blended with a triangle fundamental.
            pulse = (1 if phase < duty else -1) - (2*duty-1)
            signal = .78*pulse + .22*(1-4*abs(phase-.5))
        if noise and i % 3 == 0: grain = rng.uniform(-1,1)
        if noise: signal = signal*(1-noise)+grain*noise
        edge = min(1,i/48,(count-1-i)/64)
        decay = 1-.38*i/max(1,count-1)
        values.append(signal*level*edge*decay)
    return values


def normalize(values):
    rms = math.sqrt(sum(x*x for x in values)/len(values))
    gain = min(10**(-14.5/20)/rms,10**(-5/20)/max(abs(x) for x in values))
    return [round(x*gain*32767) for x in values]


def save(path, values):
    with wave.open(str(path),'wb') as f:
        f.setparams((1,2,RATE,0,'NONE','not compressed'))
        f.writeframes(struct.pack('<'+'h'*len(values),*values))


def read(path):
    with wave.open(str(path),'rb') as f:
        assert (f.getnchannels(),f.getsampwidth(),f.getframerate())==(1,2,RATE)
        return list(array.array('h',f.readframes(f.getnframes())))


def uri(path):
    return 'data:audio/wav;base64,'+base64.b64encode(path.read_bytes()).decode('ascii')


def waveform(values):
    bins = 80
    step = max(1,len(values)//bins)
    bars = []
    for i in range(bins):
        level = max([abs(x) for x in values[i*step:(i+1)*step]] or [0])/32767
        height = max(1,round(level*48))
        bars.append(f'<rect x="{i*3}" y="{18-height/2}" width="2" height="{height}" rx="1"/>')
    return '<svg viewBox="0 0 240 36" aria-label="音效节奏波形">'+''.join(bars)+'</svg>'


def main():
    OUT.mkdir(exist_ok=True)
    patterns = {
        'done': sum((segment(f,.0375,'pulse',.25) for f in [523.25,659.25,783.99,1046.50]),[]),
        'question': segment(880,.07,'pulse',.125)+segment(1318.51,.10,'pulse',.125)+segment(0,.09)+segment(880,.07,'pulse',.125)+segment(1318.51,.10,'pulse',.125),
        'error': segment(220,.13,'pulse',.5,end=164.81,noise=.07)+segment(0,.07)+segment(146.83,.17,'pulse',.5,end=98,noise=.10),
        'stop': segment(1396.91,.025,'pulse',.125)+segment(698.46,.025,'pulse',.125)+segment(349.23,.035,'pulse',.125),
        'disconnect': sum((segment(f,d,'triangle',level=l) for f,d,l in [(783.99,.064,1),(659.25,.072,.88),(523.25,.08,.76),(392,.096,.60),(261.63,.128,.42)]),[]),
    }
    manifest = {'stage':'review only, selection pending','synthesis':'original pulse/triangle/noise cues; no game audio copied','sample_rate':RATE,'channels':1,'bits':16,'target_device_effect_volume_percent':85,'browser_volume_is_not_device_loudness':True,'events':[]}
    cards=[]
    for event,values in patterns.items():
        samples=normalize(values); filename=f'chiptune-{event}.wav'; dest=OUT/filename;save(dest,samples)
        rms=math.sqrt(sum(x*x for x in samples)/len(samples))/32767
        assert all(-32768 <= x <= 32767 for x in samples)
        assert samples[0]==0 and samples[-1]==0
        item={'event':event,'file':filename,'duration_ms':len(samples)/16,'peak_dbfs':round(20*math.log10(max(abs(x) for x in samples)/32767),2),'rms_dbfs':round(20*math.log10(rms),2),'pcm_bytes':len(samples)*2,'sha256':hashlib.sha256(dest.read_bytes()).hexdigest()}
        manifest['events'].append(item)
        buttons=f'<button data-sound="old-{event}">原版</button><button class="primary" data-sound="new-{event}">8-bit 新版</button>'
        if event in ['done','question','error']:
            for profile in ['a','b']:
                combined=samples+[0]*1920+read(ROOT/f'voice-candidates/{profile}-{event}.wav')
                save(OUT/f'{profile}-{event}.wav',combined)
            buttons+=f'<button data-sound="a-{event}">新版＋A 少女声</button><button data-sound="b-{event}">新版＋B 柔软声</button>'
        label,description,rhythm=EVENTS[event]
        cards.append(f'<section><div class="row"><h2>{label}</h2><span>{item["duration_ms"]:g} ms</span></div><p>{description}</p>{waveform(samples)}<small>{rhythm}</small><div class="buttons">{buttons}</div></section>')
    all_samples=[]
    for i,event in enumerate(patterns):
        if i:all_samples += [0]*11200
        all_samples += read(OUT/f'chiptune-{event}.wav')
    save(OUT/'five-chiptune-cues.wav',all_samples)
    media={}
    for event in patterns:
        original = ROOT/'audio-original' if (ROOT/'audio-original').exists() else ROOT/'audio'
        media['old-'+event]=uri(original/f'tone-{event}.wav')
        media['new-'+event]=uri(OUT/f'chiptune-{event}.wav')
    for profile in ['a','b']:
        for event in ['done','question','error']:media[f'{profile}-{event}']=uri(OUT/f'{profile}-{event}.wav')
        media['voice-'+profile]=uri(ROOT/f'voice-candidates/{profile}-three-phrases.wav')
    media['all']=uri(OUT/'five-chiptune-cues.wav')
    html='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>鲸鱼搭子 · 8-bit 音效与少女声线</title><style>
    @font-face{font-family:Whale;src:url(FONT_DATA)}*{box-sizing:border-box}body{font:14px/1.7 Whale,"Microsoft YaHei",sans-serif;background:#edf2f5;color:#253853;margin:0;padding:28px 18px}main{max-width:920px;margin:auto}h1{font-size:29px;margin:10px 0}header p,.note{color:#62738a}header small{letter-spacing:2px;color:#446f9c}article{display:grid;grid-template-columns:1fr 1fr;gap:16px}section{border-radius:16px;background:#faf9f4;padding:20px;border:1px solid #dce5ed}h2{font-size:18px;margin:0}.row{display:flex;justify-content:space-between;align-items:center}.row span{color:#62738a;font-size:12px}section p{margin:8px 0;font-size:12px;color:#62738a}section svg{display:block;width:100%;height:40px;fill:#557ea8}section small{display:block;color:#70859a;font-size:11px;margin-bottom:12px}.buttons{display:flex;gap:8px;flex-wrap:wrap}button{border:1px solid #cbd9e5;border-radius:8px;background:white;color:#253853;padding:8px 12px;cursor:pointer;font:12px Whale,"Microsoft YaHei",sans-serif}button:hover{background:#e2edf5}button.primary{background:#dce9f3;border-color:#adc4d8}button:focus-visible{outline:3px solid #e9b64b}.toolbar{position:sticky;top:8px;z-index:2;background:#fdfdfdf5;border:1px solid #d7e2eb;padding:15px;border-radius:13px;margin:20px 0;box-shadow:0 3px 18px #2538530c}.toolbar p{font-size:12px;margin:8px 0 0;color:#62738a}.voices{margin-top:18px}.note{font-size:12px;margin-top:22px}audio{display:none}@media(max-width:650px){article{grid-template-columns:1fr}h1{font-size:24px}.toolbar{position:static}}
    </style><main><header><small>WHALE COMPANION · AUDIO REVIEW 02</small><h1>听见声音，就知道发生了什么。</h1><p>五种 8-bit 音效，用节奏、音色和长度区分事件。完成音保留 0.15 秒；只制作原创提示音，不复刻游戏旋律。</p></header><div class="toolbar"><div class="buttons"><button class="primary" data-sound="all">连续听五种新版音效</button><button id="stop">停止播放</button></div><p id="status" role="status">顺序：完成 → 待选择 → 出错 → 手动停止 → 断连</p></div><article>CARDS</article><section class="voices"><h2>少女声线对比</h2><p>A 轻甜少女 · B 柔软陪伴。都依次说「忙完啦」「帮我选一下」「遇到问题啦，来看看」。</p><div class="buttons"><button data-sound="voice-a">A · 三句语音</button><button data-sound="voice-b">B · 三句语音</button></div></section><p class="note">音效使用脉冲波、三角波与少量噪声构成，响度统一处理、边缘淡入淡出。设备音量仍计划为 85%，电脑试听不代表实机响度。少女语音为在线服务预先生成的固定成品，已嵌入页面；页面可离线播放。当前均为待选样稿，未写入固件。</p><audio id="player" preload="none"></audio></main><script>const media=MEDIA;const player=document.getElementById('player'),status=document.getElementById('status');let generation=0;document.querySelectorAll('[data-sound]').forEach(b=>b.onclick=async()=>{const ticket=++generation;player.pause();player.src=media[b.dataset.sound];status.textContent='正在播放：'+b.textContent;try{await player.play()}catch{if(ticket===generation)status.textContent='播放失败，请再次点击试听。'}});player.onended=()=>status.textContent='播放完成，可以继续对比。';document.getElementById('stop').onclick=()=>{generation++;player.pause();player.currentTime=0;status.textContent='已停止播放。'};</script></html>'''
    html=html.replace('CARDS',''.join(cards)).replace('MEDIA',json.dumps(media))
    font_path=ROOT.parents[1]/'device-firmware/managed_components/lvgl__lvgl/scripts/generators/built_in_font/SourceHanSansSC-Normal.otf'
    font=TTFont(font_path);cmap=font.getBestCmap();chars={ord(c) for c in html if ord(c)>=32}
    missing=[cp for cp in chars if 0x3400<=cp<=0x9fff and cp not in cmap];assert not missing,missing
    cut=subset.Subsetter();cut.populate(unicodes=chars & cmap.keys());cut.subset(font);font.flavor='woff2';font.save(OUT/'review-cn.woff2')
    html=html.replace('FONT_DATA','data:font/woff2;base64,'+base64.b64encode((OUT/'review-cn.woff2').read_bytes()).decode('ascii'))
    (OUT/'index.html').write_text(html,'utf8')
    (OUT/'FONT-LICENSE.txt').write_bytes((ROOT/'assets/FONT-LICENSE.txt').read_bytes())
    manifest['missing_chinese']=missing
    manifest['effects_pcm_bytes']=sum(x['pcm_bytes'] for x in manifest['events'])
    manifest['budget_with_6_poses_and_voice_a']=6*196*196*3+manifest['effects_pcm_bytes']+sum(len(read(ROOT/f'voice-candidates/a-{e}.wav'))*2 for e in ['done','question','error'])
    assert manifest['budget_with_6_poses_and_voice_a']<1048576
    (OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n','utf8')
    print(json.dumps(manifest,ensure_ascii=False))


if __name__=='__main__':main()
