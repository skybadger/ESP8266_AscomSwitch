"""Compile the assembly pin map, including deliberately invalid configurations."""
from pathlib import Path
import os
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
COMPILER = Path(os.environ['USERPROFILE']) / '.platformio/packages/toolchain-xtensa/bin/xtensa-lx106-elf-g++.exe'


class BoardPinMapChecks(unittest.TestCase):
    def compile_profile(self, rows=None, board='ESP8266_01'):
        work = ROOT / '.pio/board-map-checks'
        work.mkdir(exist_ok=True)
        source = (ROOT / 'BoardPinMap.h').read_text()
        if rows is not None:
            start = source.index('constexpr std::array', source.index('#if defined ESP8266_01'))
            end = source.index('#elif defined ESP8266_12', start)
            source = source[:start] + 'constexpr std::array<SourceMapping::NativeResource, %d> nativeSources = {{%s}};\n' % (len(rows), ','.join(rows)) + source[end:]
        (work / 'BoardPinMap.h').write_text(source)
        unit = work / 'test.cpp'
        unit.write_text('#include "BoardPinMap.h"\nstatic_assert(validBoardPinMap(), "assembly map");\n')
        return subprocess.run([str(COMPILER), '-std=gnu++17', '-Wall', '-Wextra', '-Werror', '-D' + board,
                               '-I' + str(ROOT), '-c', str(unit), '-o', str(work / 'test.o')],
                              capture_output=True, text=True)

    def test_default_esp01_map(self):
        result = self.compile_profile()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_esp12_native_adc_map(self):
        result = self.compile_profile(board='ESP8266_12')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_zero_exposed_native_connectors(self):
        result = self.compile_profile([])
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_reject_unsupported_or_conflicting_connectors(self):
        for label, rows in [
            ('A0 PWM', ['{256, SourceMapping::Pwm, false, "A0"}']),
            ('GPIO ADC', ['{3, SourceMapping::Adc, false, "GPIO3"}']),
            ('native DAC', ['{3, SourceMapping::Dac, false, "GPIO3"}']),
            ('I2C collision', ['{2, SourceMapping::DigitalOut, false, "SDA"}']),
            ('UART collision', ['{1, SourceMapping::DigitalOut, false, "TX"}']),
            ('flash collision', ['{6, SourceMapping::DigitalOut, false, "FLASH"}']),
            ('duplicate physical pin', ['{3, SourceMapping::DigitalOut, false, "A"}', '{3, SourceMapping::Pwm, false, "B"}'])]:
            with self.subTest(label=label):
                result = self.compile_profile(rows)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Invalid exposed connector', result.stderr)


if __name__ == '__main__':
    unittest.main()
