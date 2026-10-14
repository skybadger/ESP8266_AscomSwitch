"""Check all standard switch response exits use the tested response sender."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
HANDLERS = ["Maxswitch", "CanWrite", "SwitchState", "SwitchDescription", "SwitchName",
            "SwitchValue", "MinSwitchValue", "MaxSwitchValue", "SwitchStep"]


class ResponsePaths(unittest.TestCase):
    def test_every_standard_switch_response_uses_sender(self):
        source = (ROOT / "ESP8266_relayhandler.h").read_text(encoding="utf-8")
        for suffix in HANDLERS:
            with self.subTest(handler=suffix):
                match = re.search(r"void handlerDriver0" + suffix + r"\(void\)\n\{(.*?)\n\}",
                                  source, re.S)
                self.assertIsNotNone(match)
                self.assertIn("sendSwitchResponse(root, message)", match[1])
                self.assertNotIn("server.send(", match[1])

    def test_missing_id_exit_serializes_via_sender(self):
        source = (ROOT / "ESP8266_relayhandler.h").read_text(encoding="utf-8")
        match = re.search(r'bool readSwitchId\(.*?\n\{(.*?)\n\}', source, re.S)
        self.assertIsNotNone(match)
        self.assertIn("return switchRequestError(root, message,", match[1])
        error_sender = re.search(r'bool switchRequestError\(.*?\n\{(.*?)\n\}', source, re.S)
        self.assertIsNotNone(error_sender)
        self.assertIn('root["ErrorNumber"] = invalidValue;', error_sender[1])
        self.assertIn("sendSwitchResponse(root, message);", error_sender[1])

    def test_connected_is_registered_only_for_get_and_put(self):
        source = (ROOT / "ESP8266_AscomSwitch.ino").read_text(encoding="utf-8")
        routes = re.findall(r'server.on\("/api/v1/switch/0/connected",\s*(.*?)\);', source)
        self.assertEqual(sorted(routes), ["HTTP_GET, handleConnected ", "HTTP_PUT, handleConnected "])

    def test_unknown_path_response_is_plain_text(self):
        source = (ROOT / "ESP8266_relayhandler.h").read_text(encoding="utf-8")
        match = re.search(r'void handlerNotFound\(\)\n\{(.*?)\n\}', source, re.S)
        self.assertIsNotNone(match)
        self.assertIn('server.send(400, "text/plain"', match[1])

    def test_common_validation_uses_guarded_response_sender(self):
        source = (ROOT.parent / "libraries/ASCOM_rest/ASCOMAPICommon_rest.h").read_text(encoding="utf-8")
        match = re.search(r'bool validateCommonRequest\(.*?\n\{(.*?)\n\}', source, re.S)
        self.assertIsNotNone(match)
        self.assertIn("sendCommonResponse(message, 200);", match[1])
        self.assertNotIn("server.send(400", match[1])


if __name__ == "__main__":
    unittest.main()
