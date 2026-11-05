"""Check the resolved configuration AND linked LVGL pool, not just defaults."""
import re
import subprocess
import sys
from pathlib import Path


def verify(build):
    build = Path(build)
    config = (build / 'sdkconfig').read_text(encoding='utf-8')
    if not re.search(r'^CONFIG_LV_MEM_SIZE=32768$', config, re.M):
        raise ValueError('resolved LVGL byte-sized pool is not 32768')
    legacy = re.search(r'^CONFIG_LV_MEM_SIZE_KILOBYTES=(\d+)$', config, re.M)
    if legacy and int(legacy[1]):
        raise ValueError('deprecated LVGL KB setting is still enabled')
    result = subprocess.run(
        ['riscv32-esp-elf-nm', '-S', '--defined-only', str(build / 'FoloToy-AI-Passport.elf')],
        capture_output=True, text=True, check=True,
    )
    pools = re.findall(r'^\s*[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s+[bB]\s+work_mem_int(?:\.\d+)?\s*$',
                       result.stdout, re.M)
    if len(pools) != 1 or int(pools[0], 16) != 32768:
        raise ValueError('linked LVGL pool does not match the 32768-byte budget')
    print('Whale LVGL pool: PASS (32768 bytes in resolved config and ELF)')


if __name__ == '__main__':
    verify(sys.argv[1])
