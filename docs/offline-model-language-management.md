# Offline-first LocalVocal models and language management

**Development feature; not production-approved.** This change targets the
church operator's report that every OBS startup shows a short
"Checking / downloading model..." dialog and network activity. It does not
change the church's existing installed model weights, its API credentials, its
translation provider, or subtitle timing.

## Behavior

- **OBS startup: offline.** LocalVocal reads its bundled
  `data/models/models_directory.json`; it does not fetch a new catalog from
  GitHub just to build the model picker.
- **Already installed Whisper transcription weights:** load silently from the
  OBS plugin model cache. No downloader dialog, no network check, no automatic
  CoreML download. Missing weights produce an OBS log error rather than a
  surprise download, preserving service-startup predictability.
- **Whisper model dropdown:** lists only locally installed models recognized
  in the bundled catalog (plus the existing configured external model, if
  applicable). Local Translation lists built-in Whisper-based translation,
  installed CTranslate2 models, and the legacy external-folder route.
- **Manage Models:** a searchable dialog for *available* catalog entries,
  with Installed/Available status, separate tabs for Speech recognition and
  Local translation, explicit Download selected, and Refresh installed.
  Download progress uses the existing checked model-download worker. If a
  model is installed during the dialog, close and reopen Filters to see it
  in the model selection.
- **Manage Languages:** under General, search the supported Whisper/cloud
  language mappings and check which codes should appear in compact source,
  cloud-target, local-translation and WebVTT dropdowns. The three core
  languages Korean, English and Spanish remain pinned. Saving creates
  `visible-languages.json` atomically in the OBS LocalVocal config directory.
  Multilingual Whisper languages are not separate model files, so there is
  no language download step.

## Operator workflow

1. Off-air, open **OBS > Sources > Korean microphone > Filters > LocalVocal**.
2. Under **Transcription** select an installed Whisper model; no download.
3. To install another model, select **Manage Models**, search the catalog,
   choose a model, click **Download selected**, wait for completion, then
   reopen Filters and choose the installed model.
4. Under **General**, select **Manage Languages**, search and check
   languages you want on the visible lists, and click **Save language
   selection**. Reopen Filters to refresh all source/target dropdowns.
5. For the church, leave Korean input and English output selected; a new
   downloaded model/language does not automatically change the live filter.

## Compatibility, validation, and limitations

- Cached model presence is a fast **nonempty-file** check at filter creation;
  full SHA-256 verification still occurs in the explicit download worker.
  The choice avoids hashing multi-gigabyte files at every church startup.
- The manager searches the **bundled catalog**; it is not arbitrary
  Hugging Face model discovery. To add catalog entries not shipped in the
  build, a separately reviewed catalog refresh/source change is needed.
  The old model-catalog download-on-startup path was removed.
- **Cloud OpenAI model IDs** do not use local model weights: they are managed
  separately in `openai-models.json` via the fixed-choice dropdown.
  Do not hide cloud models based on local disk checks.
- The language manager changes dropdown visibility, not a provider's actual
  translation support or a multilingual Whisper model's capabilities.
- Saved but missing local models never silently fall back to another weight,
  download again, or replace the operator's choice.
- Built-in model catalog entries with no usable local files do not appear
  as runnable models. Existing external-model paths are retained for
  compatibility.
- The bundled model catalog in this branch includes Whisper Large v3
  Turbo q5 (574Mb) in `ggml-large-v3-turbo-q5_0`, matching the church's
  recorded model cache.
- Windows NVIDIA CI can be affected by upstream release-asset 404s. The
  branch includes a same-version fallback to Microsoft's NuGet package
  with SHA512 validation and CMake SHA256 pinning. CI success and on-site
  startup/translation testing are **release gates**, not assumed by source
  commits.

## Config files

Standard Windows OBS LocalVocal plugin configuration:
`%APPDATA%\obs-studio\plugin_config\obs-localvocal\`

- `models\`: downloaded local model weights
- `openai-models.json`: cloud API model names/IDs, from the previous feature
- `visible-languages.json`: pinned and optionally visible language codes,
  created when saved through Manage Languages

Portable OBS uses its own `config\obs-studio\plugin_config\obs-localvocal\`
under the portable installation. Confirm the `Portable mode:` log value;
a Windows shortcut's Start in folder alone does not establish portable mode.
