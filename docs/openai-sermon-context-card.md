# OpenAI sermon context card, model selection and optional response retention

**Experimental feature.** This work is based on the already developed contextual
OpenAI translation branch plus CUDA loader diagnostics. It is not an approved
church production release. Validate the exact Windows NVIDIA CI artifact and
run a full off-air rehearsal before replacing the working installation.

## Operator setup

On the existing Korean mic source in OBS, select the **OpenAI LocalVocal
Transcription filter**. In **Cloud Translation** select **OpenAI** (not Google).
Preserve your existing API key, Korean source language, English target,
English subtitle output, and **Previous 3 segments**. The new controls are:

| Field | Behavior |
| --- | --- |
| **OpenAI model** | Editable selector with existing preset choices. Choose one or type an exact OpenAI Responses API model ID. No JSON file or plugin rebuild needed to enter a new ID. API availability and feature support still depend on the model/account. |
| **OpenAI reasoning effort** | Defaults to **None** to preserve the current latency profile. Select **Auto** for models that reject `none`; Auto omits the `reasoning` field and uses the model's default, which may be slower. Low/Medium/High are optional experiments; not every model supports them. |
| **Store OpenAI Responses for audit** | Off by default. When enabled, each successful request sends `store:true`, making the response retrievable according to OpenAI account retention and data policies (typically 30 days). Does not archive microphone audio. |
| **Today's sermon context card** | Multiline Korean-first card with preferred English names/terms. Paste the pastor-approved text, typically 250–350 tokens. Leave blank to send no per-service card. The transmitted card is capped at 4096 UTF-8 bytes, without splitting Korean code points. |

**Previous Presbyterian terminology checkbox removed.** The current sermon
description is now driven by the pasted context card instead of fixed
Presbyterian-church assumptions. The base system instruction still enforces
translation fidelity, avoiding invented words, quotations, doctrines, or
translation of the background card itself. Existing saved
`translate_cloud_openai_sermon_profile` settings are ignored; the new card
starts blank until the operator pastes content.

The request remains **one /v1/responses call per finalized transcript**.
The static card is passed alongside up to three previous finalized
source-language segments and the new Korean text. There is no second
model request, document retrieval, or background summarization on air.
Card tokens add some cost and may alter latency; measure before declaring
the latency impact negligible.

## Repeat each service

1. Prepare and review the sermon card with the pastor while creating PPT.
2. Off-air, replace any **previous** card with today's card. It is saved in
   the OBS filter settings and **does not automatically expire**, so
   clearing or replacing it before every Sunday is essential.
3. Choose the intended model. For any new model, switch reasoning to Auto
   if it does not support None.
4. Leave **Store Responses** unchecked unless pastor/church retention consent
   explicitly covers API request storage; enable only for approved audit.
5. Verify a representative Korean sermon clip and a music-only passage;
   check that original YouTube audio, F7/F8, captions, VAD and CUDA stability
   remain intact. Verify both the OpenAI output and Google comparison filter
   continue to use their existing independent settings.
6. Review actual OpenAI usage and p50/p95 live-caption timing; revert to
   the working OBS plugin if any API/error/subtitle regression occurs.

## Model and cost safety

- Arbitrary typed model IDs **are not silently rewritten** to Luna. Blank
  IDs keep the existing Luna fallback. Malformed IDs fail before network.
- Existing estimated local USD spend limit has known embedded rates only
  for `gpt-6-luna` and `gpt-6.1-terra` (their rates can become stale).
  If a different model is selected while the **local USD limit is greater
  than zero**, requests fail explicitly rather than assigning a fictitious
  Luna price. Use the platform-side project budget/alerts and set the local
  limit to **0** to trial a new model. Confirm actual pricing independently.
  Usage tokens still accumulate but cost estimates omit unknown models.
- Selecting a new model does not guarantee compatibility with the Responses
  endpoint, the chosen reasoning level, or the 384-output-token budget.
  Trial off-air, validate exact API errors and subtitle quality.
- `store:true` does not cause extra OpenAI requests by itself and has no
  separately advertised standard Responses-storage fee; token charges
  still apply. Check the current OpenAI pricing and retention policies.
  Stored output is a temporary debugging backstop, not an accurate full
  KO/EN service audit. Local ASR and completed translated-text logging,
  plus authorized audio recording, is preferable for full audits.

## Privacy and troubleshooting

OBS saved filter settings can contain API keys and today's sermon card in
plain text; protect the profile/portable directory. Never commit a live
card, keys, transcripts, or screenshots with secrets into this repository.
The card is **untrusted contextual data**; it may guide terminology but must
not dictate speech that was not recognized. Input is capped rather than
transmitted wholesale if the user pastes a full sermon.

Settings changes clear the cloud-caption pipeline and its recent context;
make edits off-air rather than while a sentence is being translated. If
reloading the filter appears to alter expected caption ordering or output
sources, stop and revert to the known-good package. For storage retrieval,
open the correct project in https://platform.openai.com/logs or retrieve
specific response IDs with the Responses API; no retrospective recovery
of requests made with `store:false`.

## Source/test coverage

- `src/translation/cloud-translation/openai-request.h`: creates bounded,
  strongly separated input with store/effort/model options.
- `src/translation/cloud-translation/openai.cpp`: uses the builder and
  enforces known-model local USD cap behavior.
- `src/transcription-filter-properties.cpp`: OpenAI-only UI.
- `src/transcription-filter.cpp`: reads saved per-filter settings.
- `src/translation/cloud-translation/translation-cloud.h`: cache key
  includes all relevant settings, so old-card translations cannot be reused.
- `tests/responses/openai-responses.cpp`: typed payload contract tests.

GitHub Actions Windows x64 NVIDIA compilation and OBS runtime verification
are separate release gates; passing source/fixture tests does not prove that
the church workstation, RTX 4070, or live stream is ready.
