#!/usr/bin/env python3
"""Development-only Somang English narrator. No radio transmission code.

Dry-run is the default. --speak requires Windows PowerShell 5.1, installed
System.Speech voice, sounddevice and numpy, and an explicit device ID.
Listen only on 127.0.0.1:8765; authenticate every request with a shared token.
"""
import argparse
import hmac
import json
import os
import pathlib
import queue
import subprocess
import sys
import tempfile
import threading
import time
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MAX_BODY = 8192
MAX_QUEUE = 5
MAX_AGE_MS = 12000
MAX_TEXT = 3000


class Narrator:
    def __init__(self, source, token, speak=False, device=None, volume=0.6, voice=""):
        self.source, self.token = source, token
        self.speak, self.device, self.volume, self.voice = speak, device, volume, voice
        self.cv = threading.Condition()
        self.pending = []
        self.generation = 0
        self.paused = False
        self.active_stream = None
        self.last_seq = {}
        self.metrics = dict(accepted=0, played=0, rejected=0, expired=0,
                            overflow=0, failed=0, flushed=0)
        self.finished = False
        self.worker = threading.Thread(target=self._worker, daemon=True)
        self.worker.start()

    def submit(self, event):
        source = event.get("source")
        text = event.get("text")
        run = event.get("run")
        seq = event.get("seq")
        produced = event.get("produced_unix_ms")
        if source != self.source or not isinstance(text, str) or not text.strip() or len(text) > MAX_TEXT:
            return False, "invalid source/text"
        if not isinstance(run, str) or not (1 <= len(run) <= 128) or not isinstance(seq, int) or seq < 1:
            return False, "invalid sequence"
        if not isinstance(produced, int) or abs(int(time.time() * 1000) - produced) > MAX_AGE_MS:
            with self.cv:
                self.metrics["expired"] += 1
            return False, "stale event"
        with self.cv:
            key = (source, run)
            if self.paused or seq <= self.last_seq.get(key, 0):
                self.metrics["rejected"] += 1
                return False, "paused/duplicate"
            self.last_seq[key] = seq
            if len(self.pending) >= MAX_QUEUE:
                self.metrics["overflow"] += 1
                return False, "queue full; no content silently discarded"
            self.pending.append((self.generation, produced, text))
            self.metrics["accepted"] += 1
            self.cv.notify()
        return True, "accepted"

    def control(self, action, source):
        if source != self.source or action not in ("pause", "resume", "flush", "stop"):
            return False
        with self.cv:
            if action in ("pause", "stop", "flush"):
                self.generation += 1
                self.pending.clear()
                self.metrics["flushed"] += 1
                if self.active_stream is not None:
                    try:
                        self.active_stream.abort()
                    except Exception:
                        pass
                if action in ("pause", "stop"):
                    self.paused = True
            elif action == "resume":
                self.paused = False
            self.cv.notify_all()
        return True

    def status(self):
        with self.cv:
            return dict(self.metrics, queued=len(self.pending), paused=self.paused,
                        dry_run=not self.speak, device=self.device)

    def _valid(self, generation, produced):
        with self.cv:
            return (not self.paused and self.generation == generation and
                    int(time.time() * 1000) - produced <= MAX_AGE_MS)

    def _worker(self):
        while True:
            with self.cv:
                self.cv.wait_for(lambda: self.finished or bool(self.pending))
                if self.finished:
                    return
                generation, produced, text = self.pending.pop(0)
            if not self._valid(generation, produced):
                with self.cv:
                    self.metrics["expired"] += 1
                continue
            try:
                if self.speak:
                    self._speak(text, generation, produced)
                else:
                    # Dry-run does not generate or play audio, nor expose sermon text in logs.
                    pass
                with self.cv:
                    self.metrics["played"] += 1
            except Exception as exc:
                with self.cv:
                    self.metrics["failed"] += 1
                print(f"Narrator error: {type(exc).__name__}", file=sys.stderr)

    def _speak(self, text, generation, produced):
        import numpy as np
        import sounddevice as sd
        with tempfile.TemporaryDirectory(prefix="somang-narration-") as d:
            root = pathlib.Path(d)
            input_path = root / "input.txt"
            wav_path = root / "audio.wav"
            input_path.write_text(text, encoding="utf-8")
            script = pathlib.Path(__file__).with_name("sapi-render.ps1")
            args = ["powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy",
                    "Bypass", "-File", str(script), "-InputFile", str(input_path),
                    "-WavFile", str(wav_path)]
            if self.voice:
                args += ["-Voice", self.voice]
            proc = subprocess.run(args, timeout=15, capture_output=True, text=True)
            if proc.returncode != 0:
                raise RuntimeError("SAPI synthesis failed")
            if not self._valid(generation, produced):
                return
            with wave.open(str(wav_path), "rb") as wav:
                if wav.getcomptype() != "NONE" or wav.getsampwidth() != 2:
                    raise RuntimeError("Expected uncompressed 16-bit PCM")
                channels = wav.getnchannels()
                sample_rate = wav.getframerate()
                with sd.OutputStream(device=self.device, samplerate=sample_rate,
                                     channels=channels, dtype="float32", blocksize=1024) as stream:
                    with self.cv:
                        self.active_stream = stream
                    try:
                        while self._valid(generation, produced):
                            frames = wav.readframes(1024)
                            if not frames:
                                break
                            values = np.frombuffer(frames, dtype="<i2").astype(np.float32)
                            values = np.clip((values / 32768.0) * self.volume, -0.9, 0.9)
                            stream.write(values.reshape(-1, channels))
                    finally:
                        with self.cv:
                            self.active_stream = None


def serve(narrator, port):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args):
            pass

        def respond(self, status, payload):
            data = json.dumps(payload).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def auth(self):
            return hmac.compare_digest(self.headers.get("X-Somang-Token", ""), narrator.token)

        def do_GET(self):
            if not self.auth():
                return self.respond(403, {"error": "unauthorized"})
            if self.path == "/health":
                return self.respond(200, narrator.status())
            return self.respond(404, {"error": "not found"})

        def do_POST(self):
            if not self.auth():
                return self.respond(403, {"error": "unauthorized"})
            try:
                size = int(self.headers.get("Content-Length", "0"))
                if size <= 0 or size > MAX_BODY:
                    return self.respond(413, {"error": "invalid body length"})
                body = json.loads(self.rfile.read(size))
                if not isinstance(body, dict):
                    return self.respond(400, {"error": "invalid JSON"})
            except (ValueError, TypeError, json.JSONDecodeError):
                return self.respond(400, {"error": "invalid JSON"})
            if self.path == "/event":
                ok, detail = narrator.submit(body)
                return self.respond(202 if ok else 409, {"result": detail})
            if self.path == "/control":
                ok = narrator.control(body.get("action"), body.get("source"))
                return self.respond(200 if ok else 400, {"ok": ok})
            return self.respond(404, {"error": "not found"})

    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, help="Cloud subtitle destination source name")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--speak", action="store_true", help="Enable audio output (off by default)")
    parser.add_argument("--device", type=int, help="Explicit PortAudio output device index")
    parser.add_argument("--voice", default="", help="Installed Windows English voice name")
    parser.add_argument("--volume", type=float, default=0.6)
    parser.add_argument("--list-devices", action="store_true")
    args = parser.parse_args()
    if args.list_devices:
        import sounddevice
        print(sounddevice.query_devices())
        return
    token = os.environ.get("SOMANG_NARRATION_TOKEN", "")
    if len(token) < 16 or not all(c.isalnum() or c in "_.-" for c in token):
        parser.error("Set SOMANG_NARRATION_TOKEN (16-128 ASCII alnum/underscore/dot/hyphen)")
    if args.speak and (sys.platform != "win32" or args.device is None):
        parser.error("--speak requires Windows and explicit --device")
    if args.speak and not (0.0 <= args.volume <= 1.0):
        parser.error("--volume must be between 0 and 1")
    print("Somang narrator listening on localhost; " + ("AUDIO ENABLED" if args.speak else "DRY RUN"))
    serve(Narrator(args.source, token, args.speak, args.device, args.volume, args.voice), args.port)


if __name__ == "__main__":
    main()
