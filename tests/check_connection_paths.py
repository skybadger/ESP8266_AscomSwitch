import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ConnectionPaths(unittest.TestCase):
    def test_opt_in_flags_are_set_before_shared_headers(self):
        source = (ROOT / "ESP8266_AscomSwitch.ino").read_text(encoding="utf-8")
        for flag in ["ASCOM_STRICT_OPTIONAL_IDS", "ASCOM_CUSTOM_CONNECTED_HANDLER", "ASCOM_VALIDATE_REQUEST_OWNERSHIP"]:
            self.assertLess(source.index("#define " + flag), source.index('#include "Skybadger_common_funcs.h"'))

    def test_shared_parser_has_scoped_optional_id_hook(self):
        source = (ROOT.parent / "libraries/ASCOM_rest/Skybadger_common_funcs.h").read_text(encoding="utf-8")
        self.assertIn("#if defined(ASCOM_STRICT_OPTIONAL_IDS)", source)
        self.assertIn("parseAlpacaOptionalUInt32<ESP8266WebServer, String>", source)
        self.assertIn("#else", source)

    def test_default_shared_connected_handler_is_guarded(self):
        source = (ROOT.parent / "libraries/ASCOM_rest/ASCOMAPICommon_rest.h").read_text(encoding="utf-8")
        self.assertRegex(source, r"#ifndef ASCOM_CUSTOM_CONNECTED_HANDLER\s+void handleConnected\(void\)")

    def test_operational_handlers_check_connection_before_entry_access(self):
        source = (ROOT / "ESP8266_relayhandler.h").read_text(encoding="utf-8")
        for name in ["SwitchState", "SwitchValue"]:
            body = re.search(r'void handlerDriver0' + name + r'\(void\)\n\{(.*?)\n\}', source, re.S)[1]
            self.assertLess(body.index("requireConnectedSwitch("), body.index("switchEntry["))

    def test_connection_errors_are_serialized(self):
        source = (ROOT / "ESP8266_relayhandler.h").read_text(encoding="utf-8")
        body = re.search(r'bool requireConnectedSwitch\(.*?\n\{(.*?)\n\}', source, re.S)[1]
        self.assertIn('root["ErrorNumber"] = notConnected;', body)
        self.assertIn('sendSwitchResponse(root, message);', body)

    def test_connected_get_and_status_use_boolean_state(self):
        source = (ROOT / "ESP8266_relayhandler.h").read_text(encoding="utf-8")
        self.assertIn('root["Value"] = alpacaConnection.connected();', source)
        self.assertIn('root["connected"] = alpacaConnection.connected();', source)
        self.assertIn('root["connectedId"] = alpacaConnection.ownerClientID;', source)
        self.assertNotIn('connected != NOT_CONNECTED', source)

    def test_shared_common_validation_enforces_ownership(self):
        source = (ROOT.parent / "libraries/ASCOM_rest/ASCOMAPICommon_rest.h").read_text(encoding="utf-8")
        body = re.search(r'bool validateCommonRequest\(.*?\n\{(.*?)\n\}', source, re.S)[1]
        self.assertIn('ASCOM_VALIDATE_REQUEST_OWNERSHIP(clientID,', body)
        self.assertIn('server.method() == HTTP_PUT || server.method() == HTTP_POST', body)

    def test_timeout_is_polled_without_requests(self):
        source = (ROOT / "ESP8266_AscomSwitch.ino").read_text(encoding="utf-8")
        self.assertRegex(source, r'void loop\(\)\s*\{\s*checkAlpacaConnectionTimeout\(\);')

    def test_legacy_setting_handlers_are_guarded_at_entry(self):
        source = (ROOT / "ESP8266_relayhandler.h").read_text(encoding="utf-8")
        for name in ['handlerDriver0SwitchType', 'handlerRestart', 'handlerDeviceHostname',
                     'handlerDeviceLocation', 'handlerDeviceUdpPort', 'handlerDriver0SetupNumSwitches',
                     'handlerDriver0SetupSwitches']:
            with self.subTest(handler=name):
                body = re.search(r'void ' + name + r'\([^)]*\)\s*\{(.*?)\n\s*\}', source, re.S)[1]
                self.assertRegex(body, r'^\s*if \(!validateSettingHandlerRequest\(')

    def test_connected_put_has_explicit_acquire_release(self):
        source = (ROOT / "ESP8266_relayhandler.h").read_text(encoding="utf-8")
        body = re.search(r'bool beginSwitchRequest\(.*?\n\{(.*?)\n\}', source, re.S)[1]
        self.assertIn('strcmp(method, "Connected") == 0', body)
        self.assertIn('const bool setting = !connectionCommand', body)
        self.assertIn('validateAlpacaRequestOwnership(clientID, setting)', body)
        self.assertIn('ClientID must be supplied and greater than zero', source)

    def test_setup_forms_submit_positive_client_id(self):
        source = (ROOT / "ESP8266_relayhandler.h").read_text(encoding="utf-8")
        self.assertIn('name=\\"ClientID\\" min=\\"1\\"', source)
        self.assertIn('required value=\\"', source)
        self.assertNotIn('Method=\\"PUT\\"', source)


if __name__ == "__main__":
    unittest.main()
