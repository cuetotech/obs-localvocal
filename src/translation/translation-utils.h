#ifndef TRANSLATION_UTILS_H
#define TRANSLATION_UTILS_H

#include "transcription-filter-data.h"

void start_translation(struct transcription_filter_data *gf,
		       const ctranslate2::TranslationOptions &options);

#endif // TRANSLATION_UTILS_H
