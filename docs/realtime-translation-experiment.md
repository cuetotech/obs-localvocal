# Off-air Realtime Translation experiment (2026-11-22)

**Branch:** `experiment/realtime-translation-20261122`
**Baseline:** `feat/offline-model-library-20261119` at `437dff698a2d4bcad8cb6bd2fb498c048e841be8`
**Status:** Isolated API replay prototype; not an OBS translation-provider implementation or a church deployment.

## Purpose

Evaluate the *dedicated* OpenAI `gpt-realtime-translate` endpoint for Korean-sermon English captions without replacing the working local Whisper Q5 -> context-aware Responses/Terra -> readable caption presenter path. This is not the older deprecated `/v1/audio/translations` Whisper audio-to-English endpoint. Realtime must demonstrate accurate pronoun/anaphora resolution, terminology and doctrinal fidelity, stable captions and meaningful **speech-to-readable-display** gains before considering OBS integration.

## Minimal replay

Requirements: Python 3.10+, `websocket-client` (for live mode only); a locally prepared **24,000 Hz, mono, signed 16-bit PCM WAV** file. Recordings should be authorized for sending to an external API.

Use a 30–60-second excerpt first. PowerShell:

```powershell
py -m pip install websocket-client
py tests/realtime-translation/replay.py --wav "C:\\path\\sermon-24k-mono.wav" --max-seconds 30
# The line above is a NO-NETWORK dry run.
$env:OPENAI_API_KEY = "your-api-key"
py tests/realtime-translation/replay.py --wav "C:\\path\\sermon-24k-mono.wav" --max-seconds 30 --execute
Remove-Item Env:OPENAI_API_KEY
```

The default run is only a file-format, length and reference-price validation. Only `--execute` opens a billable WebSocket. Defaults cap replay at **60 seconds**, estimated audio cost at **US$0.05**, and post-replay drain at **30 seconds**; adjust `--max-seconds` and `--max-estimated-cost` intentionally. The reference price **$0.034/minute** is an estimate, **not invoice evidence**. Actual usage and charges must be reconciled on the OpenAI platform.

Run tests without an API key or network:

```powershell
py -m unittest discover -s tests/realtime-translation -p "test_*.py" -v
```

Output from successful or failed live runs is saved in a new local `realtime-test-runs/<UTC timestamp>/` directory (or `--out`). `events.jsonl` includes text deltas and timestamps but excludes raw audio payloads; `translated.txt` contains concatenated translation deltas; `summary.json` includes elapsed time to **first** translation delta, translated character count and completion status. This is an audit trace, **not** the final stable, two-line OBS caption layout. Source transcription deltas are optional with `--source-transcript`. Output includes sermon content and must be treated as sensitive; do not commit it to the repository. The harness does not store the original audio in the repository.

## Experimental acceptance

Compare the identical Korean audio passage under (A) existing Google text translation, (B) current context-aware Responses/Terra, and (C) this realtime stream. Capture input and translated text, exact billed usage, source-text meaning, proper nouns, theological nuance, anaphora and hallucination, first-delta versus **stable sentence** timing, partial-text rewrites, two-line readability, queue age and expired pages. A faster first delta alone is insufficient to approve church use.

The endpoint emits translated text and translated speech. This harness **counts** speech payload bytes but does not play or write generated speech. It does not display subtitles in OBS. A subsequent experimental provider integration would require controlled opt-in configuration, a reconnect/error path, incremental-to-stable-caption formatting, provider isolation, credential handling, pause/resume semantics and an explicit fallback to the current provider.

## Protocol assumption and validation boundary

The client currently assumes translation WebSocket `wss://api.openai.com/v1/realtime/translations?model=gpt-realtime-translate`, with `session.created`, `session.update`, `session.updated`, `session.input_audio_buffer.append`, `session.output_transcript.delta`, `session.close`, and `session.closed` events. This matches the published API examples reviewed for the prototype, but **no authenticated live handshake has been executed**, and event/schema compatibility is not yet independently demonstrated. Do not call this a passing end-to-end test until an opt-in run is successful. The currently published documentation should remain the authority: https://developers.openai.com/api/docs/guides/realtime-translation

## Production safety

This branch begins from the existing Somang feature stack and modifies no OBS source, filter, compiled C++ plugin, translation engine selection or production settings. Do not merge into `master` or promote into `somang-obs-latest` based solely on branch creation or test-harness availability. Known-working portable OBS remains rollback.
