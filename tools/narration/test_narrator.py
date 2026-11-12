import importlib.util
import pathlib
import time
import unittest

spec = importlib.util.spec_from_file_location("somang_narrator", pathlib.Path(__file__).with_name("somang_narrator.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class NarratorTests(unittest.TestCase):
    def setUp(self):
        self.n = module.Narrator("English Subtitles", "abcdefghijklmnop")
        self.now = int(time.time() * 1000)

    def event(self, seq=1, **kwargs):
        return dict(source="English Subtitles", text="Test text", run="test-run",
                    seq=seq, produced_unix_ms=self.now, **kwargs)

    def test_accept_replay_and_source(self):
        self.assertTrue(self.n.submit(self.event())[0])
        self.assertFalse(self.n.submit(self.event())[0])
        self.assertFalse(self.n.submit(dict(self.event(2), source="Wrong"))[0])

    def test_pause_flush_resume(self):
        self.assertTrue(self.n.control("pause", "English Subtitles"))
        self.assertFalse(self.n.submit(self.event())[0])
        self.assertTrue(self.n.control("resume", "English Subtitles"))
        self.assertTrue(self.n.submit(self.event(2))[0])

    def test_stale(self):
        e = self.event()
        e["produced_unix_ms"] -= 20000
        self.assertFalse(self.n.submit(e)[0])

    def test_wrong_control_source(self):
        self.assertFalse(self.n.control("stop", "Other"))


if __name__ == "__main__":
    unittest.main()
