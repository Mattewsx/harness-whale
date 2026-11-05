"""Create optional fixed voice auditions; never used by the device at runtime.

Requires edge-tts; passes only the three public cue texts to Edge online TTS.
Existing approved/pending prototype audio is preserved until a voice is selected.
"""
import argparse
import asyncio
import base64
import hashlib
import json
import subprocess
import wave
from pathlib import Path

import edge_tts
from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parent
LINES = {"done": "忙完啦。", "question": "帮我选一下？", "error": "遇到问题啦，来看看。"}
PROFILES = {
    "a": {"label": "A · 轻甜少女", "voice": "zh-CN-XiaoyiNeural", "rate": "-3%", "pitch": "+18Hz", "description": "晓伊音色，轻微提高音高，语速稍放缓。"},
    "b": {"label": "B · 柔软陪伴", "voice": "zh-CN-XiaoxiaoNeural", "rate": "-5%", "pitch": "+26Hz", "description": "晓晓音色，音高略抬，保持轻柔舒缓的节奏。"},
}


async def main(ffmpeg, font_source):
    target = ROOT / "voice-candidates"
    target.mkdir(exist_ok=True)
    result = {"status": "audition only, user selection pending", "provider": "Edge online TTS via edge-tts; offline fixed playback on device", "profiles": PROFILES, "samples": []}
    cards = []
    for key, profile in PROFILES.items():
        clips = []
        for event, text in LINES.items():
            name = f"{key}-{event}"
            mp3, wav = target / (name + ".mp3"), target / (name + ".wav")
            if not mp3.exists():
                await edge_tts.Communicate(text, profile["voice"], rate=profile["rate"], pitch=profile["pitch"]).save(str(mp3))
            subprocess.run([str(ffmpeg), "-hide_banner", "-loglevel", "error", "-y", "-i", str(mp3), "-af", "loudnorm=I=-20:TP=-3:LRA=7", "-ar", "16000", "-ac", "1", "-c:a", "pcm_s16le", str(wav)], check=True)
            with wave.open(str(wav), "rb") as f:
                assert f.getnchannels() == 1 and f.getsampwidth() == 2 and f.getframerate() == 16000
                frames = f.readframes(f.getnframes())
                seconds = f.getnframes() / 16000
                assert .3 < seconds < 5 and any(frames)
            result['samples'].append({"profile": key, "event": event, "text": text, "file": wav.name, "seconds": round(seconds, 3), "sha256": hashlib.sha256(wav.read_bytes()).hexdigest()})
            data = base64.b64encode(wav.read_bytes()).decode('ascii')
            clips.append(f'<label>{text}<audio controls preload="metadata" src="data:audio/wav;base64,{data}"></audio></label>')
        # A single WAV containing the three complete phrases with 450 ms gaps.
        combined = target / f"{key}-three-phrases.wav"
        with wave.open(str(combined), 'wb') as f:
            f.setparams((1, 2, 16000, 0, 'NONE', 'not compressed'))
            for i, event in enumerate(LINES):
                if i: f.writeframes(bytes(14400))
                with wave.open(str(target / f'{key}-{event}.wav'), 'rb') as source:
                    f.writeframes(source.readframes(source.getnframes()))
        cards.append(f'<section><h2>{profile["label"]}</h2><p>{profile["description"]}</p>{"".join(clips)}</section>')
    source = TTFont(font_source)
    text = Path(__file__).read_text('utf8')
    codes = {ord(c) for c in text if not c.isspace()} | set(range(32, 127))
    missing = sorted(c for c in codes if 0x3400 <= c <= 0x9fff and c not in source.getBestCmap())
    assert not missing, missing
    cut = subset.Subsetter(); cut.populate(unicodes=codes); cut.subset(source)
    source.flavor = 'woff2'; source.save(target / 'voice-cn.woff2')
    font = base64.b64encode((target / 'voice-cn.woff2').read_bytes()).decode('ascii')
    html = '<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>鲸鱼搭子 · 少女声线对比</title><style>@font-face{font-family:Whale;src:url(data:font/woff2;base64,'+font+')}*{box-sizing:border-box}body{background:#edf2f5;color:#253853;font:15px/1.8 Whale,"Microsoft YaHei",sans-serif;margin:0;padding:32px 18px}main{max-width:850px;margin:auto}h1{font-size:28px}p{color:#62738a}article{display:grid;grid-template-columns:1fr 1fr;gap:20px}section{background:#faf9f4;border-radius:20px;padding:24px}h2{font-size:21px;margin-top:0}label{display:block;margin-top:22px}audio{display:block;width:100%;margin-top:8px}small{display:block;color:#62738a;margin-top:24px}@media(max-width:650px){article{grid-template-columns:1fr}}</style><main><p>DEEPSEEK HARNESS · VOICE AUDITION</p><h1>让声音也像你的鲸鱼搭子。</h1><p>两种少女声线，文案相同。请按播放逐句比较；选择后再替换交互稿中的系统女声。</p><article>'+''.join(cards)+'</article><small>本次使用在线语音服务预先制作固定音频。页面已内嵌成品，可离线试听；设备运行时无需联网合成。均为合成声线样稿，未做真人声音克隆。主观听感与设备扬声器响度待确认。</small></main><script>document.addEventListener("play",e=>{document.querySelectorAll("audio").forEach(a=>{if(a!==e.target)a.pause()})},true)</script></html>'
    (target/'index.html').write_text(html,'utf8')
    (target/'manifest.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n','utf8')
    print(json.dumps(result,ensure_ascii=False))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--ffmpeg', required=True, type=Path)
    parser.add_argument('--font', required=True, type=Path)
    args = parser.parse_args()
    asyncio.run(main(args.ffmpeg, args.font))
