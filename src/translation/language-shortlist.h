#pragma once

#include <array>
#include <string>

namespace localvocal {

// Operator-facing list: Korean/English/Spanish are pinned, then ten
// commonly used Virginia languages (2024 ACS B16001).
// Census combines some distinct languages in broad groups; the shortlist
// therefore uses individual model-supported language choices.
struct LanguageChoice {
	const char *name;
	const char *whisper_code;
	const char *cloud_code;
	const char *local_code;
};

inline const std::array<LanguageChoice, 13> &short_language_choices()
{
	static const std::array<LanguageChoice, 13> choices = {{
		{"Korean", "ko", "ko", "__ko__"},
		{"English", "en", "en", "__en__"},
		{"Spanish", "es", "es", "__es__"},
		{"Arabic", "ar", "ar", "__ar__"},
		{"Chinese", "zh", "zh", "__zh__"},
		{"Vietnamese", "vi", "vi", "__vi__"},
		{"Tagalog", "tl", "tl", "__tl__"},
		{"Amharic", "am", "am", "__am__"},
		{"Persian", "fa", "fa", "__fa__"},
		{"Hindi", "hi", "hi", "__hi__"},
		{"Urdu", "ur", "ur", "__ur__"},
		{"French", "fr", "fr", "__fr__"},
		{"Telugu", "te", "te", "__te__"},
	}};
	return choices;
}

inline bool is_short_source_language(const std::string &code)
{
	if (code == "auto")
		return true;
	for (const auto &choice : short_language_choices())
		if (code == choice.whisper_code)
			return true;
	return false;
}

inline bool is_short_cloud_target(const std::string &code)
{
	for (const auto &choice : short_language_choices())
		if (code == choice.cloud_code || code == choice.local_code)
			return true;
	return false;
}

} // namespace localvocal
