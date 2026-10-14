"""Check standard switch handlers validate inputs before entry access/writes."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
NAMES = ["Maxswitch", "CanWrite", "SwitchState", "SwitchDescription", "SwitchName",
         "SwitchValue", "MinSwitchValue", "MaxSwitchValue", "SwitchStep"]


class RequestPaths(unittest.TestCase):
    def setUp(self):
        self.source = (ROOT / "ESP8266_relayhandler.h").read_text(encoding="utf-8")

    def body(self, suffix):
        return re.search(r"void handlerDriver0" + suffix + r"\(void\)\n\{(.*?)\n\}",
                         self.source, re.S)[1]

    def test_all_standard_handlers_validate_transactions(self):
        for name in NAMES:
            with self.subTest(handler=name):
                body = self.body(name)
                self.assertIn("beginSwitchRequest(root, message,", body)
                self.assertNotIn(".toInt()", body)
                self.assertNotIn(".toFloat()", body)

    def test_id_is_validated_before_entry_access(self):
        for name in NAMES[1:]:
            with self.subTest(handler=name):
                body = self.body(name)
                self.assertLess(body.index("readSwitchId("), body.index("switchEntry["))

    def test_state_is_parsed_before_any_output(self):
        body = self.body("SwitchState")
        self.assertLess(body.index("readSwitchBool("), body.index("setRelayOutput("))
        self.assertLess(body.index("readSwitchBool("), body.index("setPwmOutput("))

    def test_double_is_parsed_before_any_output(self):
        body = self.body("SwitchValue")
        self.assertIn("double value", body)
        self.assertLess(body.index("readSwitchValue("), body.index("setRelayOutput("))
        self.assertLess(body.index("readSwitchValue("), body.index("setPwmOutput("))

    def test_readonly_guard_precedes_both_value_parsers(self):
        for name, parser in [("SwitchState", "readSwitchBool("), ("SwitchValue", "readSwitchValue(")]:
            with self.subTest(handler=name):
                body = self.body(name)
                self.assertLess(body.index("requireWritableSwitch("), body.index(parser))


if __name__ == "__main__":
    unittest.main()
