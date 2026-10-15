"""Check synchronous SwitchV2 routes and preserve the application's extra routes."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ApiRoutes(unittest.TestCase):
    def setUp(self):
        self.source = (ROOT / 'ESP8266_AscomSwitch.ino').read_text()
        self.routes = re.findall(r'server\.on\("([^"]+)"\s*,\s*(HTTP_\w+)\s*,\s*(\w+)\s*\)', self.source)

    def test_standard_switch_routes_and_methods(self):
        common = {
            'action': ('PUT', 'handleAction'), 'commandblind': ('PUT', 'handleCommandBlind'),
            'commandbool': ('PUT', 'handleCommandBool'), 'commandstring': ('PUT', 'handleCommandString'),
            'description': ('GET', 'handleDescriptionGet'), 'driverinfo': ('GET', 'handleDriverInfoGet'),
            'driverversion': ('GET', 'handleDriverVersionGet'), 'interfaceversion': ('GET', 'handleInterfaceVersionGet'),
            'name': ('GET', 'handleNameGet'), 'supportedactions': ('GET', 'handleSupportedActionsGet'),
            'maxswitch': ('GET', 'handlerDriver0Maxswitch'), 'canwrite': ('GET', 'handlerDriver0CanWrite'),
            'getswitchdescription': ('GET', 'handlerDriver0SwitchDescription'),
            'getswitch': ('GET', 'handlerDriver0SwitchState'), 'setswitch': ('PUT', 'handlerDriver0SwitchState'),
            'getswitchname': ('GET', 'handlerDriver0SwitchName'), 'setswitchname': ('PUT', 'handlerDriver0SwitchName'),
            'getswitchvalue': ('GET', 'handlerDriver0SwitchValue'), 'setswitchvalue': ('PUT', 'handlerDriver0SwitchValue'),
            'minswitchvalue': ('GET', 'handlerDriver0MinSwitchValue'),
            'maxswitchvalue': ('GET', 'handlerDriver0MaxSwitchValue'), 'switchstep': ('GET', 'handlerDriver0SwitchStep')}
        expected = [('/api/v1/switch/0/' + path, 'HTTP_' + method, handler)
                    for path, (method, handler) in common.items()]
        expected += [('/api/v1/switch/0/connected', 'HTTP_' + verb, 'handleConnected') for verb in ['GET', 'PUT']]
        for route in expected:
            with self.subTest(route=route):
                self.assertEqual(self.routes.count(route), 1)
                self.assertNotIn((route[0], 'HTTP_ANY', route[2]), self.routes)

    def test_standard_setup_route_and_page_link(self):
        self.assertIn(('/setup/v1/switch/0/setup', 'HTTP_GET', 'handlerDriver0Setup'), self.routes)
        self.assertIn(('/setup', 'HTTP_GET', 'handlerDeviceSetup'), self.routes)
        page = (ROOT / 'ESP8266_relayhandler.h').read_text()
        self.assertIn('href=\\"/setup/v1/switch/0/setup\\"', page)

    def test_management_routes(self):
        for path, handler in [('apiversions', 'handleMgmtVersions'),
                              ('v1/description', 'handleMgmtDescription'),
                              ('v1/configureddevices', 'handleMgmtConfiguredDevices')]:
            self.assertRegex(self.source, re.escape('server.on("/management/' + path + '"') +
                             r'\s*,\s*HTTP_GET,\s*\[\]\(\)\{\s*recordAlpacaReadActivity\(\);\s*' +
                             handler + r'\(\);\s*\}')

    def test_extra_routes_preserved(self):
        expected = [('/api/v1/switch/0/getswitchtype', 'HTTP_GET', 'handlerDriver0SwitchType'),
                    ('/api/v1/switch/0/setswitchtype', 'HTTP_ANY', 'handlerDriver0SwitchType'),
                    ('/api/v1/switch/0/numswitches', 'HTTP_ANY', 'handlerDriver0SetupNumSwitches'),
                    ('/api/v1/switch/0/switches', 'HTTP_ANY', 'handlerDriver0SetupSwitches'),
                    ('/setup/hostname', 'HTTP_ANY', 'handlerDeviceHostname'),
                    ('/setup/location', 'HTTP_ANY', 'handlerDeviceLocation'),
                    ('/setup/udpport', 'HTTP_ANY', 'handlerDeviceUdpPort'),
                    ('/status', 'HTTP_GET', 'handlerStatus'), ('/restart', 'HTTP_ANY', 'handlerRestart')]
        for route in expected:
            self.assertIn(route, self.routes)
        #The working checkout and legacy HEAD use different nonstandard setup aliases.
        self.assertTrue(any((path, 'HTTP_GET', 'handlerDriver0Setup') in self.routes
                            for path in ['setup/api/v1/switch/0/setup', '/api/v1/switch/0/setup']))
        self.assertRegex(self.source, r'server\.on\("/",\s*handlerStatus\s*\)')


if __name__ == '__main__':
    unittest.main()
