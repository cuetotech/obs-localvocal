#pragma once

#include "ITranslator.h"

#include <string>

namespace localvocal {

// Cloud Translation Basic v2 accepts the two-letter ISO language codes
// selected by LocalVocal. Keep source and target independent: en->ko must
// never be hardwired to the Korean->English sermon preset.
inline std::string google_language_query(const std::string &target_lang,
                                         const std::string &source_lang)
{
    std::string query = "&target=" + sanitize_language_code(target_lang);
    if (source_lang != "auto")
        query += "&source=" + sanitize_language_code(source_lang);
    return query;
}

} // namespace localvocal
