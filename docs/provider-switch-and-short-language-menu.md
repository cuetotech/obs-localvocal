# Quick switching: Google / OpenAI, Korean / English and Virginia languages

**Experimental feature on draft PR #2.** Install only after the complete NVIDIA
CI has passed and the plugin has been tested with existing portable OBS scenes.

## Provider keys: enter once per filter

In LocalVocal > Cloud Translation, use the **Google Cloud API key (saved
separately)** field when Google is selected. Switch Provider to OpenAI and enter
the **OpenAI API key (saved separately)** once. Switching back to Google
restores the Google credential without entering or overwriting the OpenAI one;
switching to OpenAI again restores OpenAI's. The other cloud providers continue
to use the legacy shared API key field. The keys belong to that OBS filter's
saved settings, not a synchronized vault or other filter.

On first update of an existing filter, if a legacy shared API key is present,
the plugin migrates it to the provider selected at upgrade, **without removing
the original**. Leave the provider at its prior value when opening the
upgraded filter for the first time. If an old Google key is not migrated,
paste it into the Google-specific field once. The original legacy value is
kept for rollback compatibility.

**Security:** OBS_TEXT_PASSWORD masks the on-screen field but **does not
encrypt keys in portable OBS settings**. Secure the USB drive/config directory;
do not distribute its settings with plugin packages or upload logs containing
keys. Project-scoped API keys and budget alerts still apply.

## Choose input and output independently

| Spoken audio (Whisper source) | Cloud translation target | Expected result |
| --- | --- | --- |
| Korean (`ko`) | English (`en`) | Korean sermon -> English captions |
| English (`en`) | Korean (`ko`) | English sermon -> Korean captions |
| Spanish (`es`) | Korean (`ko`) | Spanish speech -> Korean captions |
| Auto detect (`auto`) | Any supported target | Detect spoken language, then translate |

Use the existing **Whisper Large V3 Turbo multilingual model** for either
Korean or English speech. A **Korean-only fine-tuned** checkpoint may not be
suitable for English recognition. For **English -> Korean**, choose a Korean-
capable Text (GDI+) font (e.g. Malgun Gothic), otherwise glyphs may render
incorrectly. Keep **Cloud Translation > Only full sentences** enabled for
consistent results. Any source-model or language settings change should be
tested privately before streaming.

For Google, translation remains sentence-oriented; the same Google API key is
reused on both directions. The Basic v2 request explicitly sends `source=en`
and `target=ko` for English speech → Korean captions, or `source=ko`
and `target=en` for Korean speech → English captions. The Google path is
not restricted to sermons or one language direction. The corresponding
regression target `google_cloud_language_directions` checks both requests
without accessing a paid API; a real OBS + Google API key smoke test remains
a separate acceptance requirement. For OpenAI, the optional rolling context now tracks
finalized segments in the **current source language**, Korean **or English**,
and resets when the detected source language changes. Select 0/2/3 prior
segments. The model translates only the current segment.

## Compact 13-language list

Core languages pinned first: **Korean, English, Spanish**.

Ten further individual languages: **Arabic, Chinese, Vietnamese, Tagalog,
Amharic, Persian, Hindi, Urdu, French, Telugu**.

The user-facing Whisper source, local translation target, cloud translation
target and WebVTT dropdowns use this shortlist (plus Auto detect / None
where appropriate). This is an **interface restriction**, not deletion of
Whisper's underlying multilingual support or CT2 model catalog. Previously
saved values outside the shortlist might not be displayed as a menu choice.

Language choices follow the **2022 ACS-derived Virginia DMAS top-language
table**, cross-checked with **2024 ACS data**. Census categories such as
`Amharic, Somali or other Afro-Asiatic` and `Chinese (Mandarin/Cantonese)`
combine different languages; we pick a model-supported individual name for
the menu, not promise a single model handles all census grouped languages.
See [Virginia DMAS Language and Disability Access Plan](https://www.dmas.virginia.gov/media/r0ka0cfp/ld-access-plan-2024-copy-for-the-general-public.pdf).

## Why the previous artifact was 1.45 GB

The **GitHub Actions artifact** contained two distributions in one download:
the portable plugin ZIP **and** Windows installer EXE. Both distributions
include NVIDIA/CUDA and other native runtime libraries. The successful
November 5 packaging logs list a **small Whisper Tiny English model file**
(`ggml-model-whisper-tiny.en.bin`) and Silero VAD, **not** the 574 MB
Large V3 Turbo Q5 model. Turbo Q5 remains a **separate download** chosen
from LocalVocal's model catalog.

The updated CI now audits the staged release, asserts that no Turbo V3
`.bin` weight files were packaged, writes a `PACKAGE-MANIFEST.txt` with
individual ZIP/installer byte counts and largest installed dependencies,
and uploads **Portable NVIDIA ZIP** and **Windows NVIDIA installer**
as **separate** GitHub artifacts. Choose the portable ZIP when deploying
to an OBS installation on a USB drive. Splitting delivery does not remove
required GPU dependencies from each distribution.

No active API key, model checkpoint, or church OBS profile should be included
in these published artifacts. Full native compilation is not a substitute
for bilingual and projector/OBS rehearsal.
