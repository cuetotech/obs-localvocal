"""Offline contract tests for the opt-in Realtime Translation replay harness."""
import argparse
import json
import tempfile
import unittest
import wave
from pathlib import Path

import replay


class RealtimeReplayTests(unittest.TestCase):
    def make_wav(self, directory: Path, frames: int, rate: int = 24000, channels: int = 1):
        path = directory / "input.wav"
        with wave.open(str(path), "wb") as output:
            output.setnchannels(channels)
            output.setsampwidth(2)
            output.setframerate(rate)
            output.writeframes(b"\0\0" * frames * channels)
        return path

    def test_audio_frame_size_and_preserved_silence(self):
        with tempfile.TemporaryDirectory() as root:
            wav = self.make_wav(Path(root), 9601)
            count, duration = replay.inspect_wav(wav)
            self.assertEqual(count, 9601)
            self.assertAlmostEqual(duration, 9601 / 24000)
            chunks = list(replay.read_chunks(wav, count))
            self.assertEqual([len(c) for c in chunks], [9600, 9600, 2])
            self.assertTrue(all(set(c) == {0} for c in chunks))

    def test_audio_caps_do_not_change_audio_format(self):
        with tempfile.TemporaryDirectory() as root:
            wav = self.make_wav(Path(root), 24000)
            chunks = list(replay.read_chunks(wav, 4800))
            self.assertEqual(len(chunks), 1)
            self.assertEqual(len(chunks[0]), 9600)

    def test_reject_wrong_sample_rate(self):
        with tempfile.TemporaryDirectory() as root:
            wav = self.make_wav(Path(root), 4800, rate=16000)
            with self.assertRaisesRegex(ValueError, "24 kHz"):
                replay.inspect_wav(wav)

    def test_session_update_translation_only_by_default(self):
        event = replay.session_update("en", False)
        self.assertEqual(event["session"]["audio"], {"output": {"language": "en"}})
        self.assertEqual(event["type"], "session.update")

    def test_optional_source_transcription(self):
        event = replay.session_update("en", True)
        self.assertEqual(
            event["session"]["audio"]["input"]["transcription"]["model"],
            "gpt-realtime-whisper",
        )

    def test_price_math(self):
        self.assertAlmostEqual(replay.request_cost_estimate(60), 0.034)
        self.assertAlmostEqual(replay.request_cost_estimate(90), 0.051)

    def test_handshake_detects_wrong_events_and_errors(self):
        class Socket:
            def __init__(self, events):
                self.events = iter(events)
            def recv(self):
                return json.dumps(next(self.events))
        ok = Socket([{"type": "session.created", "session": {"type": "translation"}}])
        self.assertEqual(replay.handshake(ok, "session.created")["type"], "session.created")
        bad = Socket([{"type": "error", "error": {"message": "bad key"}}])
        with self.assertRaisesRegex(RuntimeError, "bad key"):
            replay.handshake(bad, "session.created")

    def test_default_run_is_no_network(self):
        with tempfile.TemporaryDirectory() as root:
            wav = self.make_wav(Path(root), 4800)
            args = argparse.Namespace(
                wav=str(wav), max_seconds=60, max_estimated_cost=0.05,
                execute=False, target_language="en",
            )
            self.assertEqual(replay.run(args), 0)

    def test_budget_cap_blocks_live_run_before_credential_access(self):
        with tempfile.TemporaryDirectory() as root:
            wav = self.make_wav(Path(root), 24000)
            args = argparse.Namespace(
                wav=str(wav), max_seconds=60, max_estimated_cost=0.001,
                execute=True, target_language="en",
            )
            with self.assertRaisesRegex(ValueError, "exceeds"):
                replay.run(args)


if __name__ == "__main__":
    unittest.main()
