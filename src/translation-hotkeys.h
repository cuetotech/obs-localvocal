#pragma once

#ifdef __cplusplus
struct transcription_filter_data;
extern "C" {
#endif

void localvocal_register_translation_hotkeys(void);
void localvocal_unregister_translation_hotkeys(void);

#ifdef __cplusplus
}

bool localvocal_translation_is_paused();
void localvocal_translation_filter_register(transcription_filter_data *filter);
void localvocal_translation_filter_unregister(transcription_filter_data *filter);
#endif
