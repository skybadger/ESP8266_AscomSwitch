"""Exercise the firmware's printf formats with the Windows C runtime."""
import ctypes
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER = ROOT.parent / "libraries/ASCOM_rest/Skybadger_common_funcs.h"
CRT = ctypes.CDLL("msvcrt")
# snprintf is inline in the Windows headers; use the exported bounded formatter.
PRINTF = CRT._snprintf
PRINTF.restype = ctypes.c_int
PRINTF.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_char_p]


def render(format_string, *arguments):
    buffer = ctypes.create_string_buffer(256)
    length = PRINTF(buffer, len(buffer), format_string.encode(), *arguments)
    if not 0 <= length < len(buffer):
        raise AssertionError("Formatting failed or output was truncated")
    return buffer.value.decode()


class FormattingTests(unittest.TestCase):
    def test_timestamp_retains_64_bit_seconds_and_millisecond_padding(self):
        source = HEADER.read_text(encoding="utf-8")
        function = source.split("String& getTimeAsString2(String& output)", 1)[1]
        match = re.search(
            r'snprintf\(\s*buf,\s*sizeof\(buf\),\s*"([^"]+)",\s*'
            r'\(long long\) now,\s*\(int\) \( tv.tv_usec / 1000 \)\s*\)',
            function,
        )
        self.assertIsNotNone(match, "Expected bounded, explicitly typed timestamp call")
        for seconds, microseconds, expected in [
            (0, 0, "0.000"),
            (2147483648, 1000, "2147483648.001"),
            (4294967296, 123456, "4294967296.123"),
            (4294967296, 999999, "4294967296.999"),
        ]:
            with self.subTest(seconds=seconds, microseconds=microseconds):
                self.assertEqual(
                    render(match[1], ctypes.c_longlong(seconds), ctypes.c_int(microseconds // 1000)),
                    expected,
                )

    def test_adc_gain_and_voltage_use_floating_point_arguments(self):
        source = (ROOT / "ESP8266_AscomSwitch.ino").read_text(encoding="utf-8")
        match = re.search(r'debugI\("(ADC reading for channel [^"]+)"', source)
        self.assertIsNotNone(match)
        format_string = match[1].replace(r"\n", "\n")
        self.assertEqual(
            render(format_string, ctypes.c_int(2), ctypes.c_int(1000),
                   ctypes.c_double(0.003), ctypes.c_double(3.0)),
            "ADC reading for channel 2 is 1000 using gain factor 0.003000 and output 3.000000\n",
        )


if __name__ == "__main__":
    unittest.main()
