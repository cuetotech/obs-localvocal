# Managing OpenAI models in LocalVocal (without rebuilding)

The **OpenAI model** control in **Filters > LocalVocal Transcription > Cloud Translation** is a fixed-choice dropdown. Its rows come from a user-editable JSON file; the **label** is for the operator, but the **id** is the exact value sent to `/v1/responses`.

## Configuration file

LocalVocal creates `openai-models.json` on first opening the filter with the new build, under the OBS LocalVocal **plugin configuration directory** (not in the installed binary folder):

- Standard Windows OBS: `%APPDATA%\obs-studio\plugin_config\obs-localvocal\openai-models.json`
- Portable OBS: `<portable OBS root>\config\obs-studio\plugin_config\obs-localvocal\openai-models.json`

The new filter UI also displays the actual path. **Do not use the start-in directory of a Windows shortcut as proof of OBS portable mode.** Check `Help > Log Files > View Current Log` for `Portable mode: true/false`.

The auto-created JSON initially contains:

```json
{
  "version": 1,
  "models": [
    {"id": "gpt-6-luna", "label": "GPT-6 Luna (gpt-6-luna)"},
    {"id": "gpt-6.1-terra", "label": "GPT-6.1 Terra (gpt-6.1-terra)"}
  ]
}
```

## Add, remove, or rename a model

1. Open the path shown in the filter. Edit **only** `openai-models.json` in a text editor.
2. Add or remove an object inside `models`, keeping valid JSON. Each row requires unique `id` (exact API model ID) and a display-only `label`.
3. Save the file. Close and reopen **Filters > LocalVocal Transcription** to reload the dropdown; no rebuild or OBS restart is needed.
4. Choose the new item from the fixed-choice dropdown, then run a short **off-air** translation test.

You can remove models from the file at any time. If an active filter still has a removed model selected, its next translation fails with a **not listed in openai-models.json** error instead of silently using an old model. Select an allowed model in OBS before the next service.

## Safety and troubleshooting

- The model list is **local operator configuration**, not an account-authorized model directory. A listed model may still be unavailable to a specific OpenAI project or incompatible with the Responses API. Use an ID known to your project and test first.
- IDs permit letters, numbers, hyphens, underscores, periods, colons and forward slashes, up to 200 characters. Spaces, duplicate IDs, empty catalogs, and malformed JSON fail closed; a visible error appears in the filter and OBS log.
- If the API rejects the request, LocalVocal reports the HTTP status plus a machine-readable API error code where available. No full API body or credentials are logged.
- The local session spending limit estimates prices only for the two previously supported models (`gpt-6-luna`, `gpt-6.1-terra`). To use a new custom model ID, set **OpenAI estimated session limit = 0** and use platform-side budget monitoring. Confirm pricing independently.
- Select **OpenAI reasoning effort = Auto** for a model that rejects `None`; this omits the reasoning parameter, and may change request latency.
- Preserve the Google API key and working provider settings while testing. The JSON file contains no credentials. The sermon card can remain blank.

## Verification

Source: `src/translation/cloud-translation/openai-model-catalog.h`, `openai-model-config.h`, `src/transcription-filter-properties.cpp`, `src/translation/cloud-translation/openai.cpp`. JSON parsing, canonical-ID mapping and rejection cases are covered in `tests/responses/openai-responses.cpp`. Windows NVIDIA CI and church deployment acceptance are separate gates.
