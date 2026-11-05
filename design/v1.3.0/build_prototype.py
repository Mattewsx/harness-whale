"""Build the standalone review HTML, assets and measurable resource manifest.

Requires Pillow, fonttools and brotli. Only sheet slicing/resizing is performed;
the character sheet is the completed built-in image generation output.
"""
import argparse
import array
import base64
import hashlib
import json
import math
import shutil
import struct
import wave
from pathlib import Path

from PIL import Image
from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[1]
POSES = ["idle", "blink", "hello", "happy", "feed", "sleepy"]
EFFECTS = ["coffee-cup", "coffee-steam"]
RATE = 16000


def wav_write(path, values):
    with wave.open(str(path), "wb") as f:
        f.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        f.writeframes(struct.pack("<" + "h" * len(values), *values))


def tone(notes):
    result = []
    for start, end, duration, level in notes:
        count = round(RATE * duration)
        for i in range(count):
            t = i / RATE
            envelope = min(1, i / 80, (count - 1 - i) / 80)
            phase = 2 * math.pi * (start * t + (end - start) * t * t / (2 * duration))
            result.append(round(32767 * level * envelope * math.sin(phase)))
    return result


def prepare_assets():
    sheet = Image.open(ROOT / "source/companion-sheet.png").convert("RGBA")
    assert sheet.size == (1536, 1024)
    for i, name in enumerate(POSES):
        x, y = i % 3 * 512, i // 3 * 512
        image = sheet.crop((x, y, x + 512, y + 512)).resize((196, 196), Image.Resampling.LANCZOS)
        image.save(ROOT / f"assets/{name}.png", optimize=True)
    # Decode the existing approved firmware sprites, so the work home retains its actual character.
    raw = (REPO / "device-firmware/main/poses.rgb").read_bytes()
    count = 196 * 196
    for name, index in {"idle": 0, "thinking": 2, "question": 11, "done": 12, "error": 13, "stopped": 14}.items():
        frame = raw[index * count * 3:(index + 1) * count * 3]
        colors = struct.unpack("<" + "H" * count, frame[:count * 2])
        pixels = [((c >> 11) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31, frame[count * 2 + i]) for i, c in enumerate(colors)]
        image = Image.new("RGBA", (196, 196)); image.putdata(pixels)
        image.save(ROOT / f"assets/work-{name}.png", optimize=True)
    patterns = {
        "done": [(740, 1140, .15, .42)],
        "question": [(880, 880, .085, .32), (0, 0, .075, 0), (1050, 1050, .105, .32)],
        "error": [(480, 480, .105, .34), (0, 0, .06, 0), (350, 350, .14, .34)],
        "stop": [(640, 460, .09, .30)],
        "disconnect": [(700, 390, .18, .25)],
    }
    for name, notes in patterns.items():
        wav_write(ROOT / f"audio/tone-{name}.wav", tone(notes))
    for name in ("done", "question", "error"):
        with wave.open(str(ROOT / f"audio/voice-{name}-raw.wav"), "rb") as f:
            assert f.getparams()[:3] == (1, 2, RATE)
            samples = array.array("h", f.readframes(f.getnframes()))
        # Trim system TTS padding, retain 40 ms at both ends; normalize RMS with peak protection.
        occupied = [i for i, x in enumerate(samples) if abs(x) > 180]
        samples = samples[max(0, occupied[0] - 640):min(len(samples), occupied[-1] + 640)]
        rms = math.sqrt(sum(x * x for x in samples) / len(samples))
        peak = max(abs(x) for x in samples)
        gain = min(10 ** (-20 / 20) * 32767 / rms, 10 ** (-3 / 20) * 32767 / peak)
        samples = [round(x * gain) for x in samples]
        wav_write(ROOT / f"audio/voice-{name}.wav", samples)


def select_audio():
    # Keep the first review reproducible when the main prototype adopts a new voice.
    original = ROOT / "audio-original"
    original.mkdir(exist_ok=True)
    for path in (ROOT / "audio").glob("*.wav"):
        if path.stem.startswith('tone-') or path.stem in ('voice-done', 'voice-question', 'voice-error'):
            shutil.copyfile(path, original / path.name)
    selection = json.loads((ROOT / "audio-selection.json").read_text("utf8"))
    for destination, source in selection['files'].items():
        shutil.copyfile(ROOT / source, ROOT / 'audio' / destination)


def prepare_font(font):
    text = "".join((ROOT / f).read_text("utf8") for f in ("prototype.template.html", "prototype.js", "prototype.css"))
    chars = set(text) | {chr(c) for c in range(32, 127)}
    source = TTFont(font)
    cmap = source.getBestCmap()
    missing_zh = sorted(c for c in chars if '\u3400' <= c <= '\u9fff' and ord(c) not in cmap)
    assert not missing_zh, f"Missing Chinese glyphs: {missing_zh}"
    options = subset.Options()
    options.flavor = "woff2"
    sub = subset.Subsetter(options=options)
    sub.populate(unicodes=[ord(c) for c in chars if ord(c) in cmap])
    sub.subset(source)
    source.flavor = "woff2"
    source.save(ROOT / "assets/whale-cn.woff2")
    return {"chinese_characters_checked": len([c for c in chars if '\u3400' <= c <= '\u9fff']), "missing_chinese": missing_zh, "source": "Source Han Sans SC Normal", "firmware_font_check": "pending implementation; browser coverage is not LVGL coverage"}


def uri(path, mime):
    return f"data:{mime};base64," + base64.b64encode(path.read_bytes()).decode("ascii")


def build(font_report):
    selection = json.loads((ROOT / "audio-selection.json").read_text("utf8"))
    media = {"images": {p.stem: uri(p, "image/png") for p in (ROOT / "assets").glob("*.png")},
             "effects": {name: uri(ROOT / 'assets' / (name + '.svg'), "image/svg+xml") for name in EFFECTS},
             "audio": {Path(name).stem: uri(ROOT / 'audio' / name, "audio/wav") for name in selection['files']}}
    css = (ROOT / "prototype.css").read_text("utf8").replace("/*FONT*/", uri(ROOT / "assets/whale-cn.woff2", "font/woff2"))
    html = (ROOT / "prototype.template.html").read_text("utf8").replace("/*STYLE*/", css).replace("/*MEDIA*/", json.dumps(media, ensure_ascii=False)).replace("/*SCRIPT*/", (ROOT / "prototype.js").read_text("utf8"))
    (ROOT / "index.html").write_text(html, "utf8")
    audio = []
    for path in sorted((ROOT / "audio").glob("*.wav")):
        if path.name not in selection['files']:
            continue
        with wave.open(str(path), "rb") as f:
            samples = array.array("h", f.readframes(f.getnframes()))
            audio.append({"file": path.relative_to(ROOT).as_posix(), "duration_seconds": round(len(samples) / RATE, 4), "pcm_bytes": len(samples) * 2, "sample_rate": RATE, "channels": 1, "bits": 16, "peak_dbfs": round(20 * math.log10(max(abs(x) for x in samples) / 32767), 2), "rms_dbfs": round(20 * math.log10(math.sqrt(sum(x*x for x in samples) / len(samples)) / 32767), 2)})
    sprite_bytes = 6 * 196 * 196 * 3
    manifest = {"stage": "1 - review prototype, not firmware", "base": "v1.2.0 / whale-R11.4-2.2", "new_pose_order": POSES, "new_pose_size": [196, 196], "new_poses_rgb565a8_estimate": sprite_bytes, "audio": audio, "audio_pcm_bytes": sum(x['pcm_bytes'] for x in audio), "new_pose_plus_pcm_estimate": sprite_bytes + sum(x['pcm_bytes'] for x in audio), "budget_bytes": 1048576, "budget_excludes": "new LVGL font glyphs and code; require actual firmware build", "font": font_report, "speech_source": "Microsoft Huihui Desktop, rate +1; local fixed TTS review samples; not character voice cloning", "files": []}
    selection = json.loads((ROOT / "audio-selection.json").read_text("utf8"))
    manifest['speech_source'] = selection['speech_source']
    manifest['selection'] = selection
    manifest['interaction_effects'] = [{'file': 'assets/' + name + '.svg', 'svg_source_bytes': (ROOT / 'assets' / (name + '.svg')).stat().st_size} for name in EFFECTS]
    for folder in ("assets", "audio", "source", "audio-original"):
        for p in sorted((ROOT / folder).iterdir()):
            if p.is_file():manifest['files'].append({"file": p.relative_to(ROOT).as_posix(), "bytes": p.stat().st_size, "sha256": hashlib.sha256(p.read_bytes()).hexdigest()})
    (ROOT / "asset-manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+"\n", "utf8")
    print(json.dumps({"html_bytes": len(html.encode('utf8')), "new_pose_plus_pcm_estimate": manifest['new_pose_plus_pcm_estimate'], "font": font_report, "audio": audio}, ensure_ascii=False))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--font", type=Path, help="OFL SourceHanSansSC-Normal.otf, required to regenerate the embedded subset")
    args = parser.parse_args()
    prepare_assets()
    select_audio()
    if args.font:
        report = prepare_font(args.font)
    else:
        report = json.loads((ROOT / "asset-manifest.json").read_text('utf8'))['font']
    build(report)
