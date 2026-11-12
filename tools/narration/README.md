# English narration sidecar — experimental bench prototype

This is an **off-air development prototype**, not a church deployment or RF transmitter.
The Python process speaks text already translated by LocalVocal; it does not process Korean audio itself.

## Setup (isolated Windows 11 development environment)

1. Check out the experiment branch and build the LocalVocal plugin in the **isolated OBS development tree only**. Do not install into production. The C++ event bridge is disabled unless both `SOMANG_NARRATION_OUTPUT_SOURCE` and `SOMANG_NARRATION_TOKEN` exist in the OBS launch environment.
2. Use Python 3.11+ on Windows. For the audio-output test install `python -m pip install numpy sounddevice`. Dry-run uses the standard library alone.
3. Choose a random 24+ character secret and set `$env:SOMANG_NARRATION_TOKEN='...'` in both the sidecar and **dev OBS launch** PowerShell shells; set `$env:SOMANG_NARRATION_OUTPUT_SOURCE='English Subtitles'` only for the dev OBS process. Use the exact target source name; the allowlist prevents duplicate Google and OpenAI filter narration.
4. Dry-run: `python tools/narration/somang_narrator.py --source "English Subtitles"`. No audio generated or transmitted. Check `GET /health` with the shared `X-Somang-Token` header. Run `python -m unittest discover -s tools/narration -p test_narrator.py`.
5. In a separate Windows shell list PortAudio output devices: `python tools/narration/somang_narrator.py --source "English Subtitles" --list-devices`. Stop dry-run before starting another server on the same port. To play into a **headphone/loopback device only** run `python tools/narration/somang_narrator.py --source "English Subtitles" --speak --device 12 --volume 0.5`, replacing 12 with the verified output-only device ID. A Windows English SAPI voice must be installed; optionally `--voice` selects a known installed voice.
6. In dev OBS, keep the pastor's original audio stream/sanctuary audio unchanged. Present a real authorized Korean sermon recording to the EXISTING working speech input path; confirm the two applications exchange complete English sentences. Do not use OBS microphone mute as a cost control.
7. Use OBS's **actual configured cloud-translation Pause/Resume hotkeys**, not hardcoded physical F7/F8 assumptions. The feature branch's source *defaults* to F7 Pause/F8 Resume only when unbound, but the operator has reported a different mapping. Pause clears sidecar backlog; Resume starts a fresh queue. A separate authenticated `/control` action supports stop/flush.
8. DO NOT connect the church transmitter, energize RF, or change broadcast OBS audio routing for this test. After audio output is verified, check the transmitter model/input type and lawful RF operating basis independently before subsequent hardware work.

## Operational contracts and limitations

* Loopback HTTP: `POST /event` accepts source/run/seq/produced_unix_ms/text; `POST /control` accepts action/source. `GET /health` reports counters. All calls require `X-Somang-Token`. Bound to 127.0.0.1.
* Events contain **final, complete English translations** from `drain_cloud_captions`, not caption pages. No names, API keys, or sermon content printed in success logs. The source allowlist selects one translation destination; do not configure two filters with that same destination until single-producer selection is added.
* Bounded, in-order sidecar queue (5), stale TTL 12 seconds, duplicate sequences rejected per run, immediate pause/stop/flush, device selection, amplitude clamp and configurable gain.
* In this first prototype, sending an HTTP event is performed on a small independent native worker with 600ms timeout; busy sidecar may reject an event. **Rejected, expired or discarded events must be visible in health/log metrics** and reviewed for doctrinal omission; they are not re-translated or summarized. Do not promote to production until this semantic-loss policy is reviewed.
* Current SAPI uses **complete-segment synthesis** and will inherently lag relative to streaming speech. It is free/offline and keeps GPU use negligible, but it is a functional baseline, not proof of sufficient English voice quality. The voice may pronounce Bible names poorly.
* Audio device changes or unplugging produce a failed-segment metric; no automatic rerouting is performed. Output never enters OBS or the original Korean transcription path.
* No actual Korean recording, Windows speech/driver runtime, church transmitter/audio jack, 20-30 minute performance, end-to-end latency, cloud pricing, language quality, or broadcast safety has been validated yet.
* POC stop/flush interrupts stream playback; **in-progress SAPI synthesis is not cancellable** until PowerShell terminates or its 15-second timeout. Avoid confusing this with a production-hard interruption guarantee.
* The source `src/translation-hotkeys.cpp` maps semantic pause/resume; it does not change operator hotkey bindings. No changes to the CUDA experimental branch.

## Next acceptance gates
Build Windows NVIDIA/OBS development plugin and run unit tests; verify localhost event delivery and pause; perform real Korean sermon-to-English-headphone trial; collect p50/p95/p99 event-to-first-sound and total speech-to-output lag, queue metrics, interruptions, fidelity and spending; have a bilingual reviewer flag doctrinal omissions; characterize original transmitter audio input **off air**; obtain RF compliance review before considering live transmission.
