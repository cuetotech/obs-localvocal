# RTX 4070 Korean sermon caption plan

Date: 2026-10-31. Branch: `feat/rtx4070-sermon-captions`.
Base: NVIDIA fork `7cfaf699e5de2a9e51e4048777f5aa88013fee41`.

## Mission and decision

Help English-speaking viewers follow the Korean sermon faithfully, give them enough time to read,
and keep OBS/PowerPoint reliable during worship. Keep Whisper Large V3 Turbo Q5 and Google Cloud
Translation as the baseline. More GPU memory usage is not a goal: useful performance means lower
measured latency, accurate words, stable presentation and adequate GPU headroom for video.

This branch implements the first pipeline improvements. It does not establish a measured optimum
for the church RTX 4070 12 GB, change the installed Sunday system, or authorize merging/deployment.

## Targets and mission support

| Target | Implemented change | Mission benefit | Validation |
| --- | --- | --- | --- |
| Trustworthy acceleration | Correct backend-change comparison, force reload of the same external model, retain prompt storage through inference | Make CUDA selection effective; preserve Korean context and avoid unnecessary reloads | Backend helper tests plus required Windows CPU/CUDA/external-model tests |
| Ordered translation | One owned FIFO worker, immutable settings and weak text-source references, success-only configuration-aware cache | Keep thoughts in sermon order; prevent stale captions or requests touching a deleted filter | Blocked-first request, repeated phrases, failure-then-repeat, reset and stop tests |
| Readable English | Independent presenter, two-line UTF-8 wrapping, length-based dwell and separate expiry | Viewers can finish reading without rapid replacement; no automatic paraphrasing | Layout/preservation tests and two responses arriving during one dwell |
| Live overload control | Eight pending requests, eight ready segments and eight archive completions; maximum waiting age; ten-second audio ingestion cap in sermon mode | Avoid unlimited lag/memory use and protect the live AV path | Queue rejection/expiry tests; real-time overload and audio-metadata alignment rehearsal |
| Repeatable setup | Explicit Korean-sermon preset, adjustable reading controls, stage/counter logs | Operators can reproduce a baseline and identify the cause of delay | OBS properties/preset and real-workload acceptance below |
| Safe lifecycle | Cancel/join owned workers, remove frontend recording callback, invoke the C++ destructor, unwind creation failures | Reduce shutdown/reload/resource risks during service preparation | Worker cancellation tests, sanitizers, repeated OBS filter lifecycle rehearsal |

## Code map

- `src/captions/caption-layout.h`: word-aware UTF-8 pages, visible-character counting, dwell, backend comparison.
- `src/captions/cloud-caption-pipeline.h`: one translation thread and one presentation thread; bounded work, cancellation, cache and completion records.
- `src/transcription-filter-callbacks.cpp`: capture configuration/source lifetime on submission, drain archival results on the ASR thread, keep Korean inactivity clearing separate from English presentation.
- `src/transcription-filter.cpp` / `src/transcription-filter-data.h`: options, ownership, cleanup, backend detection and audio packet backlog bound.
- `src/transcription-filter-properties.cpp`: preset and operator controls.
- `src/translation/cloud-translation/google-cloud.*`: per-worker connection reuse, at most five-second request/two-second connection timeout, cancellation, HTTP status check, 1 MiB response cap and sanitized errors.
- `src/whisper-utils/{whisper-processing,vad-processing,whisper-model-utils}.cpp`: ASR timing, stable prompt storage, bounded active-VAD finalization in sermon mode and external-model restart.
- `tests/regression/captions.cpp`: standalone pipeline/layout/backend/cache tests, run alongside the existing entity/prefix suite in Windows NVIDIA CI.

## Operator trial

1. Use a private/portable OBS installation with the new NVIDIA artifact; keep the known-good installation/configuration available for rollback.
2. Select the existing Turbo V3 Q5 external model. In the new **Korean sermon / readable English captions** group, apply **RTX 4070 + Google recommendations**. Review before applying: Korean `ko`, Whisper translation off, native translation off, Google enabled to English, cloud finals only, hybrid VAD at 5000 ms, partials at 1500 ms, experimental buffer off, and CUDA device when enumerated. The model, API key and source targets are retained. Verify a real CUDA model load; no device is invented if CUDA is absent.
3. Prefer separate Korean and English OBS text sources. Put the English text source into the intended livestream/projector scene and confirm it is visible. Embedded stream CC is a different route; this branch does not turn Google English into that stream-CC path automatically.
4. Start with 42 characters/line, two lines, 16 characters/second, 3000–6000 ms dwell and 10000 ms maximum waiting age. English dwell starts when its text-source update is submitted, not from Korean inference time. Actual first/last rendered frames still need verification. Readable sermon mode overrides the experimental rolling buffer.
5. Enable separate transcript/SRT output if a full record is needed. Full successful translations are sent to archival outputs independently of screen paging; an overloaded/failed request has no successful English translation to archive. Existing file/SRT routing and WebVTT timestamps need rehearsal. Do not treat an on-screen subtitle as the complete transcript.

Cloud work is ordered for all providers, including when readable sermon mode is off. That mode displays
the whole response until the next response or expiry (six seconds by default), using the owned presenter,
instead of sending cloud text through the experimental typewriter buffer. Existing recognition settings
are not automatically changed by upgrading. Applying the preset is an explicit operator action.

## Fidelity, overload and tradeoffs

No model swap, decoder-quality default change, summarizer or scripture rewriting is included. Wrapping
preserves non-whitespace text bytes, including Korean and emoji; counts are Unicode scalars, not full
grapheme clusters. Fonts, combining marks and bidirectional text need visual QA. The single worker avoids
out-of-order completion and GPU contention from an extra local translator; a slow request can still delay
the next request until its timeout. Google uses a five-second ceiling; other providers retain their existing
transport limits and do not yet support the Google-specific cancellation/deadline behavior.

Finite queues can reject new work; expired segments/pages can be omitted from the live display. OBS logs
explicitly count failure, expiry, rejection and archive-overflow gaps. Successful full translations remain
available to archival output if its completion queue has capacity. Sustained speech faster than reading
capacity cannot be solved by faster CUDA alone. Raise the permitted waiting age only after measuring lag;
retain a full transcript and operator oversight. This branch does not silently condense doctrine to fit.

The audio cap trims whole old packets together with metadata only under overload in sermon mode,
resets retained ASR buffers, and reports the gap. This protects freshness at the cost of lost old audio.
The active-VAD speech cap is checked at processing boundaries, not a hard real-time deadline. Hybrid
five-second boundaries may split Korean clauses; bilingual review is required before service use.

## Acceptance and promotion gates

### Automated checks

The dependency-free suite checks layout/word preservation, minimum/length/maximum dwell, backend
selection comparisons, provider/credential-aware cache identity, serial execution, independent archival
completion, failure recovery, legitimate repeats, reset cancellation, finite queues, expiry and owned stop.
The existing 35 entity/prefix cases remain required. Compile with warnings as errors and run address/
undefined-behavior sanitizers where supported. These checks do not execute OBS, CUDA or the Google API.

### Windows/OBS and RTX 4070 checks (required, not established by unit tests)

- Verify the full Windows NVIDIA compile/package from the exact branch commit. Record artifact digest,
  installed plugin version, model checksum, GPU driver/runtime and all test settings.
- Confirm CPU → CUDA → CPU actually changes the loaded backend for the *same* external Q5 model;
  changing unrelated properties must not reload the model. Test missing CUDA/invalid-device errors,
  flash-attention toggles and a second GPU where available. The existing backend-device index mapping
  is retained; multiple-backend enumeration still needs runtime validation.
- Replay one authorized Korean recording in real time at least three times per variant: baseline,
  hybrid 7000/5000/4000 ms, and partials on/off. Include uninterrupted fast speech, quiet endings,
  prayer, music, silence, Bible references, names, numbers and negation. Review clause-boundary loss,
  duplicates and English fidelity with a bilingual reviewer. Change one setting at a time.
- Record p50/p95/max audio-to-Korean-final, ASR inference, Google request/queue time, English first-visible
  time and actual dwell, plus gaps, characters/request, CPU/GPU/VRAM and OBS rendering/encoding lag.
  Stage logs expose ASR and cloud timings, but they are not a full capture-to-render trace. Use recorded
  video/OBS instrumentation to close that gap. Establish a numerical latency budget from the baseline.
- Feed two translations 100 ms apart and verify English holds its configured dwell despite Korean
  partials and the old source clear timer. Check long multi-page translations, legitimate repeated
  sentences, punctuation/entities, emoji, blank text and deleted/recreated text sources.
- Inject slow/offline/429/5xx/invalid-key/malformed/oversized responses. Require finite work, visible log
  counters, no stale repaint after reset/provider/output change, and recovery without duplicate final
  ordering. No automatic retry loop or paid-provider fallback is included.
- With a blocked Google request, repeatedly disable/remove/recreate the filter, switch output/model,
  reset media and start/stop recording after removal. Close OBS. Run at least 20 cycles; require no
  hang, stale callback, worker/thread growth or monotonic host/model memory retention. Confirm the
  cancellation behavior of the packaged Windows curl resolver during a connection stall.
- Force an ASR backlog >10 seconds; verify the channel samples and metadata remain aligned, old
  speech is explicitly reported as dropped, timestamps recover and current speech resumes.
- Run OBS encoding + PowerPoint + captioning for at least 60 minutes on the shared RTX 4070 12 GB.
  Require stable resources, responsive slides, correct caption order/dwell and no new rendering/
  encoding overload compared with the same baseline. Promote only after the service rehearsal passes.

## Verification recorded on 2026-10-31

- Local CMake/CTest: both suites passed, including 51 caption/backend/cache/worker checks and 35 existing entity/prefix cases.
- The caption suite also passed AddressSanitizer + UndefinedBehaviorSanitizer and ThreadSanitizer locally.
- [Windows NVIDIA run 41204170563](https://github.com/cuetotech/obs-localvocal/actions/runs/41204170563) completed **SUCCESS**, building exact source `e25348b6d4d25b4232c0622e2406b713f9fde8f9`. Windows regression tests, full x64 NVIDIA compilation, CUDA definition/library checks, installer/ZIP packaging, provenance/checksum generation and upload all passed.
- [Test artifact 13044490896](https://github.com/cuetotech/obs-localvocal/actions/runs/41204170563/artifacts/13044490896): 1,451,703,435 bytes; GitHub-reported outer artifact ZIP SHA-256 `371ae08ee631e7aeef47e4beaf8cb29f6eddc1bc012abaf7beb15028ece8583e`; retention expires November 30, 2026. This is the outer ZIP digest, not an installer checksum. The artifact was not downloaded or executed here.
- This verification note is documentation after the compiled source commit. No RTX 4070 inference, Google API request, Windows OBS interaction, bilingual quality evaluation or church rehearsal was performed by these automated checks.

## Follow-on work after measurement

Consider two cloud workers only if serialized Google time is the measured bottleneck; that requires a
bounded reorder buffer and per-sequence deadlines. Evaluate flash attention or decoder alternatives
individually with Korean fidelity checks. Treat native CT2, external ASR backends, full grapheme-aware
layout and a broader audit of legacy buffer/model-download lifecycle races as separate work. This branch
does not claim every pre-existing threading or model-download lifetime issue is fixed.
