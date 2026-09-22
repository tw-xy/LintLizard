"""Run: python3 -m unittest discover -s pi -v (no physical hardware accessed)."""
import os
import sys
import threading
import types
import unittest
from unittest.mock import patch

if os.name == "nt":
    # Windows lacks termios; parser tests never open a serial device.
    sys.modules.setdefault("termios", types.ModuleType("termios"))

import server


class RadarTests(unittest.TestCase):
    def setUp(self):
        self.clock = patch.object(server.time, "monotonic", return_value=10.0)
        self.now = self.clock.start()
        self.addCleanup(self.clock.stop)
        self.radar = server.RadarBuffer()

    def test_fragmented_and_coalesced_lines(self):
        self.radar.feed(b"R,12")
        self.assertEqual(self.radar.snapshot()["points"], [])
        self.radar.feed(b"345,1234\r\nR,0,120\nR,35999,8000\n")
        self.assertEqual(self.radar.snapshot()["points"],
                         [[0.0, 120], [123.45, 1234], [359.99, 8000]])

    def test_reject_invalid_without_refreshing_online(self):
        self.radar.feed(b"boot\nR,-1,500\nR,36000,500\nR,0,0\n"
                        b"R,0,119\nR,0,8001\nR,NaN,500\nR,0,500,1\n"
                        b"R, 10,500\nR,\xff,500\n")
        state = self.radar.snapshot()
        self.assertFalse(state["online"])
        self.assertEqual(state["invalid"], 10)
        self.assertEqual(state["received"], 0)

    def test_overlong_line_resynchronizes_at_newline(self):
        self.radar.feed(b"x" * 100000)
        self.assertLessEqual(len(self.radar.pending), self.radar.MAX_LINE)
        self.radar.feed(b"R,0,500\nR,9000,1000\n")
        self.assertEqual(self.radar.snapshot()["points"], [[90.0, 1000]])
        self.assertEqual(self.radar.snapshot()["invalid"], 1)

    def test_latest_point_per_degree_and_bounded_storage(self):
        for angle in range(36000):
            self.radar.feed(("R,%d,500\n" % angle).encode())
        self.assertEqual(len(self.radar.snapshot()["points"]), 360)
        self.radar.feed(b"R,99,1500\n")
        self.assertEqual(self.radar.snapshot()["points"][0], [0.99, 1500])

    def test_individual_expiry_and_offline(self):
        self.radar.feed(b"R,0,500\n")
        self.now.return_value = 10.3
        self.radar.feed(b"R,9000,1000\n")
        self.now.return_value = 10.5
        self.assertEqual(self.radar.snapshot()["points"], [[90.0, 1000]])
        self.now.return_value = 11.0
        state = self.radar.snapshot()
        self.assertEqual(state["points"], [])
        self.assertFalse(state["online"])
        self.assertEqual(state["received"], 2)

    def test_reconnect_does_not_join_old_partial_line(self):
        self.radar.feed(b"R,0,500\nR,12")
        self.radar.reset_stream()
        self.radar.feed(b"34,500\nR,18000,1500\n")
        self.assertEqual(self.radar.snapshot()["points"], [[180.0, 1500]])


@unittest.skipIf(os.name == "nt", "Requires Linux PTY/termios")
class SerialHttpTests(unittest.TestCase):
    def test_bidirectional_serial_and_http(self):
        import json
        import pty
        import time
        from urllib.request import urlopen

        master, slave = pty.openpty()
        link = server.SerialLink(os.ttyname(slave), 115200)
        self.assertIsNotNone(link.fd)
        radar = server.RadarBuffer()
        httpd = server.ThreadingHTTPServer(("127.0.0.1", 0), server.Handler)
        thread = threading.Thread(target=httpd.serve_forever, daemon=True)
        with patch.object(server, "SERIAL", link), patch.object(server, "RADAR", radar), \
                patch.object(server, "camera_available", return_value=False):
            thread.start()
            try:
                os.write(master, b"R,9000,")
                time.sleep(0.02)
                radar.feed(link.read())
                os.write(master, b"1234\nR,0,500\n")
                time.sleep(0.02)
                radar.feed(link.read())
                base = "http://127.0.0.1:%d" % httpd.server_port

                def get(path):
                    with urlopen(base + path, timeout=2) as response:
                        return json.load(response)

                self.assertEqual(get("/scan")["points"], [[0.0, 500], [90.0, 1234]])
                self.assertEqual(get("/status")["radar_points"], 2)
                self.assertTrue(get("/cmd?x=0&y=0")["ok"])
                import select
                self.assertTrue(select.select([master], [], [], 1)[0])
                self.assertEqual(os.read(master, 1024), b'{"x":0,"y":0}\n')
                time.sleep(0.55)
                self.assertFalse(get("/scan")["online"])
                self.assertEqual(get("/status")["radar_points"], 0)
            finally:
                httpd.shutdown()
                httpd.server_close()
                thread.join(timeout=2)
                os.close(link.fd)
                os.close(master)
                os.close(slave)


if __name__ == "__main__":
    unittest.main()
