# Caption text and model download safety

Date: 2026-11-01. Branch: `feat/rtx4070-sermon-captions`.

These fixes follow the general C++ review. They support faithful sermon text and reliable
preparation/reloading of the caption system. They do not demonstrate faster RTX 4070 inference
or change the chosen Turbo V3 Q5 external model, Google provider, or decoder settings.

## Changes

| Confirmed problem | Change | Source |
| --- | --- | --- |
| English cleanup changed `God` to `od` and `Amen!` to `men!` | Trim surrounding ASCII whitespace only; preserve the first letter, punctuation, scripture references, Korean and emoji | `src/transcription-utils.cpp`, `src/text/caption-text.h` |
| Windows UTF-8 repair wrote through `c_str()` and read beyond truncated input | Read bounded sequences into owned output, preserve valid UTF-8, retain the historical repair only for a complete valid candidate, replace unrepairable bytes with U+FFFD | `src/text/caption-text.h` |
| Transfer/hash failures could emit success and leave a partial final model | One terminal success/error result; adjacent unique staging directory, closed file validation, then rename; failed/cancelled staging cleaned by RAII | `src/model-utils/model-downloader-ui.cpp`, `download-safety.h` |
| Existing file presence bypassed checksums | Validate every catalog file on the worker, including cached files; repair corrupt cache; serialize transfers to the same destination within the process | `src/model-utils/model-integrity.h`, `download-safety.h` |
| Raw filter captures could outlive model download | Shared cancellation/session state and an owner lease; close the gate before filter destruction, cancel superseded/disabled loads, reject late completions | `src/transcription-filter-data.h`, `src/whisper-utils/whisper-model-utils.cpp`, `src/translation/translation-utils.cpp` |
| Worker/dialog/resource cleanup was incomplete | Owned curl/config/file resources, worker deletion on thread finish, direct thread quit on every outcome, cancel and join on dialog close/reject/destruction | `src/model-utils/model-downloader-ui.*` |

Checksum work runs off the OBS UI thread for catalog models. Completion/model initialization
still runs synchronously under the owner lease and can delay shutdown while it finishes.
External user-selected models have no catalog checksum and retain their existing load path.
Legacy catalog entries without a checksum accept a nonempty regular file and log that limitation;
this is not cryptographic verification. A verified staged file is promoted by rename; replacing
a known-invalid destination requires removing that invalid file first on Windows. There is no
claim of power-loss durability, cross-process locking, or validation of extracted CoreML contents.

Transfers retain TLS verification and add HTTP failure/status checks, a ten-second connect limit,
a thirty-second low-speed limit and a one-hour overall limit. Cancellation is polled during
hashing, per-file lock waits and curl progress, including unknown-length transfers. Packaged curl
DNS behavior and real OBS close latency still require the Windows rehearsal below.

## Verification

- Standalone C++17 regression suites: 35 translation cases, 51 caption/backend/cache/worker
  checks, and 2,178 text/download/lifetime checks passed on macOS with strict warnings.
- The text/download/lifetime suite passed AddressSanitizer plus UndefinedBehaviorSanitizer,
  and a separate ThreadSanitizer run. These validate the isolated code exercised, not all OBS
  threading paths.
- The actual `ModelDownloadWorker`, Qt thread/signals, curl and SHA implementation passed
  34 checks against a local HTTP server with the repository's pinned Qt 6.6.3 SDK and system
  curl 8.7.1. Cases cover bad hashes, HTTP 503, truncated bodies, stalled-transfer cancellation,
  success, verified cache reuse, corrupt-cache repair and pre-cancellation. OBS path/log functions
  are test substitutes; this is not an OBS/GPU integration run.
- Windows NVIDIA CI now builds/runs both suites before packaging. A fresh successful run is
  required for this source revision; the earlier successful caption-package run does not
  validate these new changes.

Run the independent suites with:

```sh
cmake -S tests/regression -B build-regression
cmake --build build-regression
ctest --test-dir build-regression --output-on-failure
cmake -S tests/download-qt -B build-download -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build-download
ctest --test-dir build-download --output-on-failure
```

## Windows / OBS acceptance before deployment

1. Build the exact branch revision with the NVIDIA workflow; both suites and packaging must pass.
2. In portable OBS, load the existing Turbo V3 Q5 external model, confirm CUDA device selection,
   and exercise the Korean/Google-English sermon preset described in `rtx4070-sermon-plan.md`.
3. Confirm leading letters, scripture references, punctuation and Korean survive the relevant
   caption paths. Test malformed/truncated UTF-8 with repair enabled and valid text unchanged.
4. For a catalog model, test verified cache reuse, a deliberately corrupt disposable cache,
   interrupted download, server error, closing/rejecting the dialog, and a successful retry.
   A failure must never load a partial model or report success.
5. Switch models, disable/remove the filter and close OBS during a disposable download. Old
   completions must not load a model or access a deleted filter; no orphan thread or Qt warning.
   Repeat with two filters requesting the same model.
6. Rehearse a representative fast Korean sermon while recording latency, readability, OBS frame
   loss and GPU headroom. No runtime performance improvement is claimed until measured there.

Broader work from the review remains separate: legacy rolling-buffer races/empty token handling,
large shared filter state, long ASR settings locks, presenter callbacks under its mutex, clock
consistency and repeated regex construction. This patch does not claim to resolve those paths.
