"""Produce the additional disconnect phrase using the selected review-4 voice."""
import argparse
import asyncio
import hashlib
import json
import math
import subprocess
from pathlib import Path

import edge_tts
from make_clockwork_voice_review import ROOT, RATE, read, save, trim, normalize, clockwork, rms


async def main(ffmpeg):
    out=ROOT/'voice-extra';out.mkdir(exist_ok=True)
    voice=json.loads((ROOT/'audio-review-v4/manifest.json').read_text('utf8'))['voice']
    text='蓝牙已断开。'
    mp3=out/'disconnect-source.mp3';raw=out/'disconnect-source.wav'
    if not mp3.exists() or mp3.stat().st_size<128:
        pending=out/'disconnect.pending.mp3'
        await edge_tts.Communicate(text,**voice).save(str(pending))
        assert pending.stat().st_size>=128
        pending.replace(mp3)
    subprocess.run([str(ffmpeg),'-hide_banner','-loglevel','error','-y','-i',str(mp3),'-ar',str(RATE),'-ac','1','-c:a','pcm_s16le',str(raw)],check=True)
    clean=normalize([x/32768 for x in trim(read(raw))])
    samples=clockwork(clean);target=out/'voice-disconnect.wav';save(target,samples)
    result={'text':text,'voice':voice,'processing':'same fixed clockwork processing as selected review 4','file':target.name,'sample_rate':RATE,'channels':1,'bits':16,'seconds':round(len(samples)/RATE,4),'pcm_bytes':len(samples)*2,'peak_dbfs':round(20*math.log10(max(abs(x) for x in samples)/32767),2),'rms_dbfs':round(20*math.log10(rms(samples)/32767),2),'sha256':hashlib.sha256(target.read_bytes()).hexdigest()}
    (out/'manifest.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n','utf8')
    print(json.dumps(result,ensure_ascii=False))


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--ffmpeg',type=Path,required=True)
    asyncio.run(main(parser.parse_args().ffmpeg))
