"""A warmer, more mature young-adult feminine base with the same light mechanical layer."""
import argparse
import asyncio
from pathlib import Path
from make_clockwork_voice_review import main, ROOT

CONFIG = {
    'out': ROOT/'audio-review-v5',
    'voice': {'voice':'zh-CN-XiaoxiaoNeural','rate':'-6%','pitch':'-10Hz'},
    'direction':'User requested a more mature young-adult feminine voice (少御), avoiding a childlike/loli sound; retain subtle clockwork coloration.',
    'previous':ROOT/'audio-review-v4/clockwork-three.wav',
    'copy':{
        'AUDIO REVIEW 04':'AUDIO REVIEW 05',
        '鲸鱼搭子 · 发条人偶方向试听':'鲸鱼搭子 · 少御机械声试听',
        '这次，往“发条人偶”靠近一点。':'更沉稳一点，少御机械声。',
        '将发条的参考理解为平静、克制的机械少女方向。重新制作更平稳的少女底声，降低甜腻感，再加短促的金属共鸣。':'换用年轻成年女性底声，收敛甜感和撒娇感，保持温柔、沉稳，再叠加轻微的金属共鸣。',
        '语速稍缓':'成年女声底色',
        '停顿清楚':'温柔沉稳',
        '上一版 · A 稍明显':'上一版 · 发条少女',
        '机械少女':'少御机械声',
        '克制语速与短金属共鸣':'成熟底声与轻金属共鸣',
        '发条用于创作方向参考，三句提示文案保持不变。':'按少御音方向调整，三句提示文案保持不变。',
    },
}

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--ffmpeg',required=True,type=Path)
    args=parser.parse_args();asyncio.run(main(args.ffmpeg,CONFIG))
