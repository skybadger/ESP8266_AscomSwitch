import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class InterfaceVersionChecks(unittest.TestCase):
    def test_numeric_switch_v2_declaration(self):
        source = (ROOT / "Webrelay_common.h").read_text()
        declaration = re.search(r"static const uint32_t InterfaceVersion = 2;", source)
        self.assertIsNotNone(declaration)
        output = ROOT / ".pio/interface_version_checks.cpp"
        output.write_text("#include <stdint.h>\n" + declaration.group() +
                          '\nstatic_assert(InterfaceVersion == 2, "SwitchV2 version");\n'
                          'static_assert(sizeof(InterfaceVersion) == 4, "32-bit integer");\n')

    def test_common_handler_assigns_numeric_value(self):
        source = (ROOT.parent / "libraries/ASCOM_rest/ASCOMAPICommon_rest.h").read_text()
        start = source.index("void handleInterfaceVersionGet(void)\n{")
        body = source[start:source.index("\n}\n", start)]
        self.assertIn('root["Value"] = (int32) InterfaceVersion;', body)


if __name__ == "__main__":
    unittest.main()
