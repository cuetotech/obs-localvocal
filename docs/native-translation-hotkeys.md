# Native F7/F8 translation pause in LocalVocal (portable OBS)

**Implementation candidate: not yet validated on church hardware.** This is built into the LocalVocal fork; Advanced Scene Switcher, Stream Deck and OBS WebSocket macros are not required.

## Operator controls

- **F7 = pause Google/OpenAI cloud translation and hide subtitles.** One global runtime gate blocks new requests from all LocalVocal filter instances, invalidates cloud-caption queues, cancels in-progress requests when supported and clears displayed cloud output and OpenAI rolling context. Cloud text items and the known `Subtitles Overlay` instance in `Sanctuary` and `Youtube Stream` are hidden.
- **F8 = resume cloud translation and show subtitles.** All filters previously configured with Cloud Translation enabled can submit the *next* utterance. Their Google/OpenAI provider, API keys, language selection, model, output source, and on-disk settings are unchanged.
- The church microphone remains active, unmuted and assigned to its original YouTube and sanctuary audio paths. The Whisper model stays loaded, so this **does not save GPU processing**, only cloud requests.
- A cloud request already submitted before F7 can still be billable. Local cancellation cannot undo prior billable usage.
- The global pause state defaults to **running after OBS starts**; it persists through scene switches while OBS runs, not across restarts. Press F7 before songs/readings and F8 before the sermon. The hotkeys are explicit OFF/ON states, not a toggle.

## Portable OBS setup and validation

1. Clone and back up the working OBS portable folder, profile and scene collection. Install the NVIDIA **portable ZIP for the exact tested GitHub Actions commit**, never the installer in the live church copy. Keep the previously successful **Whisper Large V3 Q5** and Google Cloud configuration.
2. With OBS closed, replace the clone's plugin tree, avoiding duplicate DLLs. Start the clone and inspect **Help > Log Files > View Current Log** to confirm the plugin loaded.
3. Open **Settings > Hotkeys**. Locate **LocalVocal: Pause translation + hide captions (F7)** and **LocalVocal: Resume translation + show captions (F8)**. The plugin assigns F7/F8 if the matching hotkey is unbound after OBS finishes loading; verify the assignment and set it manually if necessary. Unassign the old duplicate F7/F8 scene-item Show/Hide bindings to prevent conflicts.
4. Keep **General > Subtitle Output = None / No Output** and **Cloud Translation > Output = English Subtitles** for the Google baseline. Local Translation remains disabled. The filter's microphone input and Cloud Translation setting stay enabled.
5. If comparing two filters, select two separate existing OBS text sources such as **A-B Google English** and **A-B OpenAI English**, one Cloud Translation Output per filter. F7/F8 acts on both providers together without touching other scene sources.
6. In a private recording with songs, scripture and sermon, test F7/F8 repeatedly; verify the microphone is audible in the recorded YouTube audio track throughout. Confirm no *new* API requests are queued after F7 (let previously sent calls settle), the OpenAI context resets, and F8 resumes at the next finalized utterance without Whisper reloading.
7. Check scene switching, OBS restarts and repeat filter add/remove; check the full church projector/YouTube routing, caption visibility, encoder drops, GPU usage and credits. Do not deploy to a live church service based only on CI success.

## Constraints and troubleshooting

- F7/F8 do **not** mute or disable OBS audio, turn off Whisper inference, change OBS Program/Preview, or alter provider configuration. They do not pause any separately enabled local CT2 translator, which should remain disabled in the church setup.
- Visibility management only targets active cloud outputs and the reserved shared overlay in the named church parent scenes. F8 deliberately makes configured caption items visible, including a previously manually hidden matching item; check your scene collection before use.
- Exact scene/source names are required for the shared church overlay. Text sources must be Text (GDI+) on Windows. If you change caption output names, set Cloud Translation Output correctly.
- Build/regression success is not proof of actual OBS hotkey dispatch, model load retention, in-flight API billing or Sunday reliability. These require the isolated portable trial.
