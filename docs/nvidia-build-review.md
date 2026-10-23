# Windows NVIDIA build and translation code review

Date: 2026-10-23
Implementation commit: `b5ec85f1dc3ac3f8310c890a4cfb66ea571c9295`
Base: `7b7bdbe8fd2686eac063e8c19e8289052719b9f6` (`build/google-html-fix`)
Build workflow: `.github/workflows/build-nvidia.yml`
Build run: https://github.com/cuetotech/obs-localvocal/actions/runs/39899519087

This is an experimental fork build, not an upstream release. Consult the run for current build status. A successful Windows compile/package does not validate GPU execution, OBS behavior, model compatibility, or sustained caption quality.

## Build and package

From the repository root in a supported Windows build environment:

```powershell
$env:ACCELERATION = "nvidia"
./.github/scripts/Build-Windows.ps1 -Target x64 -Configuration RelWithDebInfo
./.github/scripts/Package-Windows.ps1 -Target x64 -Configuration RelWithDebInfo -Acceleration nvidia -BuildInstaller
```

The workflow runs these commands on Windows 2022, tests the standalone regression helpers first, checks `ACCELERATION=nvidia`, verifies the generated project's `POLYGLOT_WITH_CUDA` definition, checks the plugin/CT2/Whisper CUDA DLLs in the install tree, and packages an installer plus ZIP. `BUILDINFO.txt` records the source commit and `SHA256SUMS.txt` hashes both packages. Actions are pinned to reviewed immutable revisions. Tokens are read-only and are not persisted by checkout. Concurrency cancellation and a 60-minute timeout limit abandoned builds. No API keys or translation accounts are required to compile.

The old Generic build run 39896844468 failed with MSVC C2039 because `QString::fromHtmlEscaped()` does not exist in its Qt API. It produced no usable installer. This branch replaces that implementation instead of claiming to preserve a working binary.

## Changes implemented

1. **Build-blocking entity decoder repair.** `google-cloud.cpp` uses a dependency-light decoder in `html-entities.h` and requests `format=text`. It decodes one layer of common named and valid decimal/hex numeric entities, retains unknown/malformed entities, preserves UTF-8, whitespace and literal markup, and rejects invalid Unicode scalar values. It is not an HTML parser/sanitizer. Live Google response behavior still requires an OBS test.
2. **Tokenizer lifetime correction.** Replace `target_processor.release()` with `reset()` when switching to a model without a separate target tokenizer. `release()` abandoned the allocation.
3. **Lower model-reload allocation peak.** Reset the previous CT2 translator before constructing its replacement, rather than holding both model instances while the new one loads. Clear prior input/output context on reload. Failed reloads disable translation; this is not a transactional rollback to the previous model, nor a guarantee about allocator/driver VRAM release.
4. **Validate inference results.** Reject an empty result batch or a truncated/mismatched hard target prefix before indexing or advancing an iterator. Clear output on errors rather than leaking the prior text.
5. **Commit context after success.** Update paired translation context only after inference and decoding succeed. This prevents ordinary inference failures from adding source context without corresponding translated context. It is not an out-of-memory transaction guarantee.
6. **Useful timing without unconditional token dumps.** CT2 logs elapsed translation-stage milliseconds and input/output UTF-8 byte counts instead of unconditional input/target/output token content. This timing includes tokenization and decoding but excludes ASR, sentence buffering, queueing before the call, and display. Other plugin logging paths may still expose text; this is not a complete privacy audit.
7. **Regression checks.** `tests/regression` contains 29 entity cases and six prefix-handling cases. The standalone C++17 suite passed locally with warnings-as-errors and separately with address/undefined-behavior sanitizers. Windows CI repeats it. These tests do not execute CT2, CUDA, a real model, the Google API, or OBS.

## Prioritized opportunities not implemented in this build

### P1: Cloud request ownership, bounded work, and shutdown safety

`src/transcription-filter-callbacks.cpp::send_sentence_to_cloud_translation_async` creates a detached thread per request and captures a raw `gf` pointer. Mutable settings/cache fields are accessed without synchronization in that function. `transcription_filter_destroy` ultimately frees `gf` and does not join those detached cloud workers. This creates a credible lifetime/race risk during shutdown, filter removal, or settings changes; it is a source finding, not a reproduced crash.

Replace detached work with an owned, bounded translation worker. Copy immutable request configuration, assign session/segment/revision IDs, cancel or discard superseded partials, serialize or sequence publication, and stop/join before freeing the filter. Test delayed responses, out-of-order completion, filter removal during a request, timeout, provider change, and burst inputs. Keep finalized archival ordering independent of low-latency display policy.

### P1: Separate local translation from recognition and source-caption publication

`set_text_callback` calls `send_sentence_to_translation` synchronously before its final Korean/source-caption publication. Local inference can therefore postpone source display and callback completion even when recognition has finished. CUDA reduces some compute time; it does not remove this coupling or sentence-finalization delay.

Publish the source caption immediately where outputs are distinct; move translation to an owned bounded worker with appropriate model-lifetime locking. Preserve current output override rules, final/partial semantics and model reload safety. Measure audio-to-source, source-to-English, inference, queue and render times separately before setting latency targets.

### P1: Repair cache correctness together with threading

The cloud function reads `last_text_cloud_translation` for a duplicate, but writes successful output to `last_text_translation` (the local cache). It also records the source before success. The local function similarly updates its last-source key before success. A failure followed by a repeated phrase can therefore reuse an unrelated/stale result. Cloud races make a one-line fix insufficient.

Use separate provider/config/version-aware cache records, update source/result together only after success, invalidate on model/language/prompt changes, and deduplicate in-flight requests. Add failure-then-repeat and provider-switch regression tests. No monthly billing limiter or paid fallback was added here.

### P2: Explicit device/compute controls and model validation

Keep the external CT2 selector, but validate weights, tokenizer files, tokenization convention and target-language compatibility before enabling translation. Add a runtime CPU/CUDA choice and supported compute-type display; keep automatic fallback opt-in and visible. GPU availability checks and error messages must not claim success merely because the build defines CUDA. GGUF models are not directly accepted by the current CT2 loader.

### P2: Evaluate generation limits for caption quality

The constructor initializes 64-token input/output limits and a no-repeat-ngram size of one. Actual UI settings can override these. Review current settings and benchmark repeated words, long Korean sentences and context growth before changing defaults: banning repeated single tokens or truncating context can damage fidelity. This build deliberately does not change those quality parameters.

### P2: Complete transport and log hardening

Review Google GET query parameters containing source text/key; evaluate POST bodies and supported key headers, bounded response size, HTTP status validation, connection reuse, cancellation and explicit retry budgets. Remove or gate text in other log paths. Test without committing secrets or placing credentials into public diagnostics.

## Workstation acceptance and rollback

Back up the existing plugin files and OBS configuration. Close OBS before replacing the plugin. Prefer a separate portable OBS test installation; do not install duplicate LocalVocal DLLs in active plugin search paths. Keep the proven transcription model and settings unchanged for the first comparison. Do not overwrite the working Sunday installation before rehearsal.

Load a compatible native CT2 model with cloud translation disabled for the isolated test. In the OBS log require `CT2 Using CUDA` followed by `CT2 Model loaded`, then actual successful English output and timing messages. GPU activity from Whisper alone is not proof of CT2 GPU translation. Check driver/runtime failures, VRAM, render/encode lag, caption order and accuracy. Compare the same authorized Korean recording against the previous working setup. Rehearse sustained operation, model reload, filter disable/remove and recovery. The GitHub build runner does not establish any of these runtime results.
