import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LIB = ROOT.parent / "libraries/ASCOM_rest"


class ManagementPaths(unittest.TestCase):
    def test_api_version_is_one(self):
        source = (LIB / "AlpacaManagement.h").read_text()
        start = source.index("void handleMgmtVersions(void)\n{")
        body = source[start:source.index("\n}", start)]
        self.assertIn("values.add(1);", body)
        self.assertNotIn("(InterfaceVersion)", body)

    def test_discovery_validates_before_response(self):
        source = (LIB / "AlpacaManagement.h").read_text()
        start = source.index("void handleDiscovery( int udpBytesCount )\n{")
        body = source[start:source.index("//Management API", start)]
        self.assertLess(body.index("if (!readAlpacaDiscoveryRequest"), body.index("Udp.beginPacket"))
        self.assertNotIn("strncpy", body)
        self.assertNotIn("inBytes", body)

    def test_reader_bounds_and_discards(self):
        source = (LIB / "AlpacaDiscovery.h").read_text()
        self.assertIn("udp.read(bytes, sizeof(bytes))", source)
        self.assertIn("while (udp.available() > 0)", source)
        self.assertIn("received == packetLength", source)


if __name__ == "__main__":
    unittest.main()
