#!/usr/bin/env python3
"""Off-air Korean sermon replay against OpenAI's dedicated Realtime Translation API.

Requires websocket-client only for --execute. No OBS/plugin hooks and no implicit paid requests.
"""
from __future__ import annotations

import argparse
import base64
import json
import os
import sys
import threading
import time
import wave
from datetime import datetime, timezone
from pathlib import Path

SAMPLE_RATE = 24000
FRAME_MS = 200
FRAME_SAMPLES = SAMPLE_RATE * FRAME_MS // 1000
MODEL = "gpt-realtime-translate"
SOCKET_URL = "wss://api.openai.com/v1/realtime/translations?model=" + MODEL
REFERENCE_DOLLARS_PER_MINUTE = 0.034


def inspect_wav(path: Path) -> tuple[int, float]:
    with wave.open(str(path), "rb") as source:
        if (
            source.getnchannels() != 1
            or source.getsampwidth() != 2
            or source.getframerate() != SAMPLE_RATE
            or source.getcomptype() != "NONE"
        ):
            raise ValueError("Input must be uncompressed 24 kHz, mono, signed PCM16 WAV")
        frames = source.getnframes()
    if frames <= 0:
        raise ValueError("Input WAV is empty")
    return frames, frames / SAMPLE_RATE


def read_chunks(path: Path, limit_samples: int):
    """Yield 200 ms source frames, preserving silence and final partial frame."""
    with wave.open(str(path), "rb") as source:
        remaining = limit_samples
        while remaining > 0:
            count = min(FRAME_SAMPLES, remaining)
            pcm = source.readframes(count)
            if len(pcm) != count * 2:
                raise IOError("WAV was truncated during replay")
            remaining -= count
            yield pcm


def session_update(target_language: str, source_transcript: bool) -> dict:
    audio = {"output": {"language": target_language}}
    if source_transcript:
        audio["input"] = {"transcription": {"model": "gpt-realtime-whisper"}}
    return {"type": "session.update", "session": {"audio": audio}}


def request_cost_estimate(seconds: float) -> float:
    return seconds / 60 * REFERENCE_DOLLARS_PER_MINUTE


def handshake(ws, expected: str):
    """Fail closed if the service doesn't acknowledge the intended session."""
    for _ in range(8):
        raw = ws.recv()
        if not raw:
            raise RuntimeError("Connection closed during handshake")
        event = json.loads(raw)
        if event.get("type") == "error":
            raise RuntimeError("Realtime API error: " + str(event.get("error", {}).get("message", "unknown")))
        if event.get("type") == expected:
            return event
    raise RuntimeError("Expected " + expected + " from realtime endpoint")


def receive_events(ws, stop: threading.Event, state: dict, event_path: Path, start: float) -> None:
    import websocket

    try:
        with event_path.open("w", encoding="utf-8") as output:
            while not stop.is_set():
                try:
                    raw = ws.recv()
                except websocket.WebSocketTimeoutException:
                    continue
                if not raw:
                    state["error"] = "WebSocket closed without session.closed"
                    break
                event = json.loads(raw)
                kind = event.get("type", "unknown")
                record = {"received_s": round(time.monotonic() - start, 3), "type": kind}
                if "elapsed_ms" in event:
                    record["elapsed_ms"] = event["elapsed_ms"]
                if kind in ("session.output_transcript.delta", "session.input_transcript.delta"):
                    delta = event.get("delta", "")
                    record["delta"] = delta
                    field = "translated" if kind == "session.output_transcript.delta" else "source"
                    state[field].append(delta)
                    if field == "translated":
                        if state["first_output_s"] is None:
                            state["first_output_s"] = record["received_s"]
                        print(delta, end="", flush=True)
                elif kind == "session.output_audio.delta":
                    # Record bytes only: do not persist huge/base64-encoded speech by default.
                    record["audio_bytes"] = len(base64.b64decode(event.get("delta", "")))
                    state["output_audio_bytes"] += record["audio_bytes"]
                elif kind == "error":
                    record["error"] = event.get("error")
                    state["error"] = "Realtime API error: " + str(event.get("error", {}).get("message", "unknown"))
                output.write(json.dumps(record, ensure_ascii=False) + "\n")
                output.flush()
                if kind == "session.closed":
                    state["closed"] = True
                    break
                if kind == "error":
                    break
    except Exception as exc:
        state["error"] = str(exc)
    finally:
        stop.set()


def run(args: argparse.Namespace) -> int:
    input_path = Path(args.wav)
    frames, total_seconds = inspect_wav(input_path)
    limit_samples = min(frames, int(args.max_seconds * SAMPLE_RATE))
    if limit_samples <= 0:
        raise ValueError("--max-seconds must be greater than zero")
    seconds = limit_samples / SAMPLE_RATE
    estimate = request_cost_estimate(seconds)
    if estimate > args.max_estimated_cost:
        raise ValueError(
            f"Estimated reference price ${estimate:.4f} exceeds --max-estimated-cost "
            f"${args.max_estimated_cost:.4f}; lower --max-seconds or raise the explicit cap"
        )
    print(
        f"Source duration: {total_seconds:.2f}s; selected: {seconds:.2f}s; "
        f"estimated audio cost: ${estimate:.4f}; mode: {'LIVE API' if args.execute else 'DRY RUN'}",
        file=sys.stderr,
    )
    if not args.execute:
        print("No network request made. Supply --execute to authorize the paid off-air replay.", file=sys.stderr)
        return 0

    api_key = os.environ.get("OPENAI_API_KEY")
    if not api_key:
        raise RuntimeError("Set OPENAI_API_KEY in the environment before --execute")
    try:
        import websocket
    except ImportError as exc:
        raise RuntimeError("Install dependency: py -m pip install websocket-client") from exc

    output_dir = Path(args.out) if args.out else Path("realtime-test-runs") / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output_dir.mkdir(parents=True, exist_ok=False)
    state = {
        "translated": [], "source": [], "first_output_s": None,
        "output_audio_bytes": 0, "closed": False, "error": None,
    }
    stop = threading.Event()
    ws = websocket.WebSocket(enable_multithread=True)
    sent_samples = 0
    start = time.monotonic()
    receiver = None
    try:
        ws.connect(
            SOCKET_URL,
            header=[f"Authorization: Bearer {api_key}"],
            timeout=30,
        )
        created = handshake(ws, "session.created")
        session = created.get("session", {})
        if session.get("model") != MODEL or session.get("type") != "translation":
            raise RuntimeError("Unexpected realtime session type/model")
        ws.send(json.dumps(session_update(args.target_language, args.source_transcript)))
        updated = handshake(ws, "session.updated")
        if updated.get("session", {}).get("audio", {}).get("output", {}).get("language") != args.target_language:
            raise RuntimeError("Server did not confirm requested output language")

        ws.settimeout(1.0)
        start = time.monotonic()
        receiver = threading.Thread(
            target=receive_events,
            args=(ws, stop, state, output_dir / "events.jsonl", start),
            daemon=True,
        )
        receiver.start()
        for pcm in read_chunks(input_path, limit_samples):
            if stop.is_set():
                raise RuntimeError(state["error"] or "Receiver ended before audio replay completed")
            target = start + sent_samples / SAMPLE_RATE
            delay = target - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            ws.send(json.dumps({
                "type": "session.input_audio_buffer.append",
                "audio": base64.b64encode(pcm).decode("ascii"),
            }))
            sent_samples += len(pcm) // 2

        remaining = start + sent_samples / SAMPLE_RATE - time.monotonic()
        if remaining > 0:
            time.sleep(remaining)
        ws.send(json.dumps({"type": "session.close"}))
        receiver.join(timeout=args.drain_seconds)
        if receiver.is_alive():
            raise RuntimeError("Timed out waiting for session.closed; translated tail may be incomplete")
        if state["error"] or not state["closed"]:
            raise RuntimeError(state["error"] or "Service did not return session.closed")
        print("\nSession fully drained.", file=sys.stderr)
        return 0
    finally:
        stop.set()
        ws.close()
        if receiver and receiver.is_alive():
            receiver.join(timeout=2)
        (output_dir / "translated.txt").write_text("".join(state["translated"]), encoding="utf-8")
        if args.source_transcript:
            (output_dir / "source.txt").write_text("".join(state["source"]), encoding="utf-8")
        summary = {
            "model": MODEL, "target_language": args.target_language,
            "input_audio_seconds": round(sent_samples / SAMPLE_RATE, 3),
            "reference_estimated_usd": round(request_cost_estimate(sent_samples / SAMPLE_RATE), 6),
            "reference_price_usd_per_minute": REFERENCE_DOLLARS_PER_MINUTE,
            "first_transcript_delta_s_since_replay_start": state["first_output_s"],
            "translated_characters": len("".join(state["translated"])),
            "output_audio_bytes_received": state["output_audio_bytes"],
            "session_closed_ack": state["closed"],
            "error": state["error"],
            "note": "First delta is NOT a stable readable-caption latency measurement; billing estimate is NOT invoice data.",
        }
        (output_dir / "summary.json").write_text(
            json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
        print(f"Saved audit files to {output_dir}", file=sys.stderr)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wav", required=True, help="24 kHz mono signed PCM16 WAV")
    parser.add_argument("--target-language", default="en", help="Translation target language code (default: en)")
    parser.add_argument("--max-seconds", type=float, default=60, help="Audio replay cap (default 60 seconds)")
    parser.add_argument("--max-estimated-cost", type=float, default=0.05, help="Reference audio cost cap in USD (default 0.05)")
    parser.add_argument("--drain-seconds", type=float, default=30, help="Wait for session.closed after replay (default 30)")
    parser.add_argument("--source-transcript", action="store_true", help="Request optional source transcript deltas")
    parser.add_argument("--out", help="New output directory (must not already exist)")
    parser.add_argument("--execute", action="store_true", help="Make a real, billable API request; without this only validate")
    args = parser.parse_args()
    if args.max_seconds <= 0 or args.max_estimated_cost <= 0 or args.drain_seconds <= 0:
        parser.error("Duration, budget and drain timeout must be positive")
    try:
        return run(args)
    except (ValueError, RuntimeError, OSError, wave.Error) as exc:
        print("ERROR:", exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
