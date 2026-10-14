"""Run the sketch's ADC printf format against the Windows C runtime."""
import ctypes
import pathlib
import re
import unittest


class AdcFormatTest(unittest.TestCase):
    def test_adc_gain_and_voltage_are_floating_point(self):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = (root / "ESP8266_AscomSwitch.ino").read_text(encoding="utf-8")
        match = re.search(r'debugI\("(ADC reading for channel [^"]+)"', source)
        self.assertIsNotNone(match)
        formatter = ctypes.CDLL("msvcrt")._snprintf
        formatter.restype = ctypes.c_int
        formatter.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_char_p]
        output = ctypes.create_string_buffer(256)
        length = formatter(
            output, len(output), match[1].replace(r"\n", "\n").encode(),
            ctypes.c_int(2), ctypes.c_int(1000), ctypes.c_double(0.003), ctypes.c_double(3.0),
        )
        self.assertGreaterEqual(length, 0)
        self.assertLess(length, len(output))
        self.assertEqual(output.value.decode(),
                         "ADC reading for channel 2 is 1000 using gain factor 0.003000 and output 3.000000\n")


if __name__ == "__main__":
    unittest.main()
