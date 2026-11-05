# Opt-in OpenAI Responses translation for Korean sermons

**Status:** experimental, draft PR #2. This document describes implementation and
acceptance requirements; it is not evidence of church runtime qualification.

## Purpose

Leave the RTX 4070 Whisper Large V3 Turbo Q5 transcription and existing
readable-caption presenter in place. Improve Korean-to-English **translation of
omitted subjects, pronouns, quotations and biblical names** by sending the most
recent finalized Korean lines as bounded context to the OpenAI Responses API.
Translate only the current Korean sentence. Previous sentences are context
rather than additional output. No embeddings, local LLM, second ASR service,
conversation ID or Responses `previous_response_id` are used.

## Current scope

The existing OBS provider `OpenAI-Translate` now uses stateless
`POST https://api.openai.com/v1/responses`, with `store: false`,
`reasoning: {"effort":"none"}`, no tools, no stream, 384 maximum output
tokens and a maximum five-second request timeout (reduced to remaining
caption age when less). The translation worker is sequential; cancellation
stops a stale request rather than letting it block indefinitely. Only
`completed` Responses containing one nonempty assistant `output_text`
are shown. HTTP failures, incomplete responses, refusals, malformed JSON and
oversized output result in no English caption from that segment.

The operator can choose `gpt-6-luna` (economy) or `gpt-6.1-terra`
(quality trial). Actual model availability depends on the configured API
organization. The default continues to be **Google Cloud**; the Google
translator implementation, existing Korean model and OBS scene routing are
unchanged. Selecting OpenAI requires an explicit provider change and API key.

## OBS experiment (not for a live service until validated)

1. Back up the portable OBS installation and working LocalVocal plugin.
   Keep the Google provider's existing API key and settings available.
2. Install the CI-produced NVIDIA ZIP only into an isolated portable OBS
   test copy. Do not mix old/new LocalVocal DLLs in plugin search paths.
3. Open the LocalVocal audio filter. In **Cloud Translation**, explicitly
   choose **OpenAI-Translate** and English target `en`.
4. Paste a **project-scoped** OpenAI API key. The key field is masked in
   the OBS UI but saved by OBS in the portable configuration; keep that
   configuration secure. Never upload it, screenshots containing keys,
   or unredacted OBS logs.
5. Set **OpenAI model** to GPT-6 Luna first, then test Terra against the
   identical recorded Korean speech. Choose **Previous finalized Korean
   segments** = 2 or 3, and leave **Presbyterian Korean sermon terminology
   guidance** on. For a provider-only baseline, use 0 context.
6. Configure **Translation Output** = existing shared
   `English Subtitles` Text (GDI+) source and **Only full sentences** on.
   Keep local Whisper translation disabled, Korean source separate,
   and the existing `Subtitles Overlay` scene / F7/F8 hotkeys.
7. To limit local costs, set **OpenAI estimated session limit (USD)** to a
   small nonzero amount during testing (e.g. $2). This uses **observed
   response token usage**, applies to requests from this one LocalVocal
   filter until OBS restarts, and stops new requests once observed estimated
   spend reaches the threshold. It is **not** a guaranteed OpenAI account
   billing cap, not synchronized across API projects, and may be exceeded
   by an in-flight call. Also configure limits in the API Platform.
8. Record one authorized Korean sermon excerpt with biblical names, clear
   quotations, subject omission and rapid continuous speech. Compare
   Google, Luna, and Terra on this identical recording, not anecdotes
   from different Sundays. Check actual p50/p95 first-visible English,
   word/subject fidelity, missing segments, OBS rendering/encoding lag and
   credit use.

## Data, security, and cost

Each OpenAI request contains the current finalized Korean segment and at most
three prior finalized segments. The context buffer is in process memory,
not a saved transcript. On filter settings update/reset, prior context and
pending display work are cleared. The cache key includes the effective
context and model so reused Korean sentences do not inherit an unrelated
speaker. A 1 MiB response cap and 4 KiB translated-text cap limit malformed
responses. The transcript itself can still be present in existing optional
LocalVocal verbose logging; disable raw word logging for production.

`store:false` disables response storage for later API retrieval, but does not
guarantee zero retention by the service. Confirm provider policies and church
data permissions before transmitting sermon text. API keys remain operator
secrets. HTTP error response bodies are intentionally not logged by this
adapter. Per-response OBS log lines show aggregate request count, input and
output tokens and estimated USD, without the key or sermon text. Estimates
use **standard** text-token prices checked Nov 4, 2026: Luna $0.10/M input,
$0.50/M output, $0.01/M cached input; Terra $1/M input, $5/M output,
$0.05/M cached input. Other processing modes and future pricing may differ.
The budget guard should not be treated as a charge prevention guarantee.

## Quality and operational risks

- Prior Korean context can help restore omitted subject references, but it
  does not establish the original meaning with certainty; do not invent
  speakers or reword doctrine. Human bilingual review is mandatory.
- A slow Responses request can block newer FIFO requests until the bounded
  timeout; caption age/queue caps can omit visible translations. The Google
  baseline can also be faster; benchmark both.
- Requests are stateless; anaphora context uses only recent finalized Korean
  text, not a sermon-long model memory.
- The profile is recognition/terminology context, not permission to add
  theologically appropriate content that wasn't said.
- The OpenAI model selector and context are experimental. Old OBS presets
  leave cloud provider `google` and previous output configuration intact.

## Acceptance criteria

- Native Windows NVIDIA CI and standalone regression tests pass.
- Mocked Responses parsing validates reasoning-plus-message outputs, refusal,
  incomplete, missing/oversized content, token usage and cost calculations.
- Real API key, billing, latency and rate-limit behavior are tested privately;
  production credentials never appear in code or GitHub Actions.
- Context windows, repeated phrases, reset/cancel, filter removal, failed
  requests and server unavailability cause no stale captions or crashes.
- At least a 60-90 minute private/full-service simulation with Korean source
  audio, PPTBridge, YouTube Program and independent Sanctuary projector passes
  without materially greater OBS rendering/encoding lag.
- Rollback to the known-good Google/NVIDIA package is rehearsed before
  considering any production adoption.

Sources: [Responses migration](https://developers.openai.com/api/docs/guides/migrate-to-responses),
[API reference](https://developers.openai.com/api/reference/resources/responses/methods/create),
[pricing](https://developers.openai.com/api/docs/pricing).
