"""Pack approved v1.3 art and fixed PCM into memory-mapped Flash assets."""
from pathlib import Path
import struct
import wave
from PIL import Image
ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'assets'
def main():
    blob=bytearray()
    images=[]
    for name in ('idle','blink','hello','happy','feed','sleepy','coffee-cup','coffee-steam'):
        im=Image.open(SOURCE/'images/companion-v130'/f'{name}.png').convert('RGBA')
        rgba=im.tobytes(); offset=len(blob)
        blob.extend(b''.join(struct.pack('<H',((r>>3)<<11)|((g>>2)<<5)|(b>>3)) for r,g,b in zip(rgba[::4],rgba[1::4],rgba[2::4])))
        blob.extend(rgba[3::4]);images.append((offset,*im.size))
    (ROOT/'main/companion.rgb').write_bytes(blob)
    pcm=bytearray();sounds=[]
    for mode in ('tone','voice'):
        for event in ('question','error','done','disconnect'):
            with wave.open(str(SOURCE/'music/whale-cues-v130'/f'{mode}-{event}.wav'),'rb') as f:
                assert (f.getframerate(),f.getsampwidth(),f.getnchannels())==(16000,2,1)
                data=f.readframes(f.getnframes())
            sounds.append((len(pcm),len(data)));pcm.extend(data)
    (ROOT/'main/cues.pcm').write_bytes(pcm)
    h='#pragma once\n#include <stdint.h>\n'
    h+='typedef struct { uint32_t offset, bytes; } whale_pcm_asset_t;\n'
    h+='static const whale_pcm_asset_t whale_pcm_assets[8]={'+','.join('{%d,%d}'%x for x in sounds)+'};\n'
    h+='typedef struct { uint32_t offset; uint16_t width, height; } whale_image_asset_t;\n'
    h+='static const whale_image_asset_t whale_image_assets[8]={'+','.join('{%d,%d,%d}'%x for x in images)+'};\n'
    h+=f'#define WHALE_COMPANION_BYTES {len(blob)}\n#define WHALE_PCM_BYTES {len(pcm)}\n'
    (ROOT/'main/whale_assets.h').write_bytes(h.encode('utf-8'))
    assert len(blob)+len(pcm)<1024*1024
    print(f'New art + PCM: {len(blob)+len(pcm)} bytes')
if __name__=='__main__':main()
