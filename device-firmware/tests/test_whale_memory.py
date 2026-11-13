import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    'whale_memory', Path(__file__).resolve().parents[1] / 'tools/verify_whale_memory.py')
memory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(memory)


class MemoryBudgetTests(unittest.TestCase):
    def test_checks_linked_pool_even_when_config_looks_correct(self):
        with tempfile.TemporaryDirectory() as tmp:
            config = Path(tmp) / 'sdkconfig'
            config.write_text('CONFIG_LV_MEM_SIZE=32768\n')
            for symbols in ('3fca1110 00010000 b work_mem_int.0\n', '',
                            '3fca1110 00008000 b work_mem_int.0\n3fcb1110 00008000 b work_mem_int.1\n'):
                with patch.object(memory.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, symbols)):
                    with self.assertRaisesRegex(ValueError, 'linked LVGL'):
                        memory.verify(tmp)
            with patch.object(memory.subprocess, 'run', return_value=subprocess.CompletedProcess(
                    [], 0, '3fca1110 00008000 b work_mem_int.0\n3c4c42bc D _binary_accessories_rgb_start\n')):
                memory.verify(tmp)
            with patch.object(memory.subprocess, 'run', return_value=subprocess.CompletedProcess(
                    [], 0, '3fca1110 00008000 b work_mem_int.0\n3c765035 D _binary_accessories_rgb_start\n')):
                with self.assertRaisesRegex(ValueError, 'unaligned 16-bit asset'):
                    memory.verify(tmp)

    def test_rejects_old_kilobyte_configuration_and_64k_default(self):
        with tempfile.TemporaryDirectory() as tmp:
            for value in ('CONFIG_LV_MEM_SIZE_KILOBYTES=32\n',
                          'CONFIG_LV_MEM_SIZE=65536\nCONFIG_LV_MEM_SIZE_KILOBYTES=32\n',
                          'CONFIG_LV_MEM_SIZE=32768\nCONFIG_LV_MEM_SIZE_KILOBYTES=32\n'):
                (Path(tmp) / 'sdkconfig').write_text(value)
                with patch.object(memory.subprocess, 'run') as command:
                    with self.assertRaises(ValueError):
                        memory.verify(tmp)
                    command.assert_not_called()


if __name__ == '__main__':
    unittest.main()
