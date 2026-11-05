# Automatic church subtitle overlay (Windows OBS)

The LocalVocal fork checks for the shared scene `Subtitles Overlay` and the text source `English Subtitles` when the audio filter is instantiated. This change is scoped to the operator's OBS scene collection; it does not manage the PowerPoint plugin or YouTube streaming state.

## Reuse and creation rules

1. If both the scene and its existing Text (GDI+) source are present, **reuse both as-is**. Preserve the source font, color, background, geometry, visibility and scene-item transforms.
2. If only the text source exists, create the shared overlay scene, then add the **existing** source to it. If only the overlay scene exists, create the missing `English Subtitles` source.
3. If neither exists, create the shared scene and the Text (GDI+) source. Newly created source defaults: 1600 x 230 wrapped text area; Arial Regular 72 pt; white text, black outline size 7; optional-style black background at 40% OBS opacity. Newly added overlay text item starts at X=160, Y=(scene height - 230 - 40), corresponding to approximately X=160, Y=810 on a 1080-pixel canvas.
4. Only when creating an overlay, try adding it to `Sanctuary` and `Youtube Stream` if these are existing scenes without a direct `English Subtitles` item. If an existing parent has a direct subtitle item, **do not silently remove or reposition it**; the operator must convert that layout off-air. If neither parent name exists, try the current scene. Overlay creation does not switch the live Program scene.
5. If `translate_cloud` is enabled and its output selection is unassigned (`none`), select `English Subtitles` as the Cloud Translation output. Existing explicitly named Cloud outputs and the independent General transcription output are preserved. The General output defaults to no extra source, not a new `LocalVocal Subtitles` text source.
6. If a conflicting OBS object already uses either reserved name with the wrong type, log a warning and make no automatic changes. Never delete another source or overwrite a configured name.

## Operating instructions

On the church workstation, use a staging/portable OBS profile for first run. Confirm only one `English Subtitles` text source exists and is inside `Subtitles Overlay`. Nest the shared scene into both `Sanctuary` and `Youtube Stream` (or verify automatic attachment), inspect the placement in each output, and verify that the text source updates from Korean-to-English Google Cloud Translation. F7/F8 remain **operator-configured OBS show/hide hotkeys** for the nested English scene item; automatic creation does not install or replace hotkeys. Program should remain `Youtube Stream` while `Sanctuary` is separately projected. Do not replace a working church OBS scene collection without an off-air backup and dry run.

The English subtitle readability defaults are presentation settings on a newly created Text (GDI+) source. LocalVocal's 42-character wrapping, 16 characters/second pacing, 3000–6000 ms dwell and 10000 ms max age are **separate filter settings** from the RTX 4070 + Google preset and are not changed by overlay setup.

Regression test `subtitle_overlay_policy` validates the idempotency/selection policy. Windows NVIDIA full CI checks compilation and packaging; neither test simulates live OBS scene rendering, hotkeys, cloud translation nor the church projection/YouTube routing.
