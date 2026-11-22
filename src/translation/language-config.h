#pragma once

#include "language-shortlist.h"
#include "plugin-support.h"
#include "language_codes.h"
#include "whisper-utils/whisper-language.h"

#include <obs-module.h>
#include <util/bmem.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace localvocal {

struct ManagedLanguage {
	std::string name;
	std::string code;
	std::string local_code;
};

inline std::vector<ManagedLanguage> all_supported_languages()
{
	std::vector<ManagedLanguage> result;
	for (const auto &[code, name] : whisper_available_lang) {
		if (code == "auto")
			continue;
		const auto translated = language_codes_from_whisper.find(code);
		if (translated == language_codes_from_whisper.end())
			continue; // Only show languages supported by both model mappings.
		result.push_back({name, code, translated->second});
	}
	std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
		return a.name < b.name;
	});
	return result;
}

inline std::vector<std::string> default_visible_languages()
{
	std::vector<std::string> result;
	for (const auto &language : short_language_choices())
		result.emplace_back(language.whisper_code);
	return result;
}

inline std::filesystem::path language_shortlist_file()
{
	char *path = obs_module_config_path("visible-languages.json");
	if (!path)
		return {};
	const auto result = std::filesystem::u8path(path);
	bfree(path);
	return result;
}

inline std::vector<std::string> load_visible_languages()
{
	const auto defaults = default_visible_languages();
	try {
		const auto path = language_shortlist_file();
		if (path.empty() || !std::filesystem::exists(path))
			return defaults;
		if (std::filesystem::file_size(path) > 16 * 1024)
			throw std::runtime_error("Visible language list exceeds 16 KiB");
		std::ifstream input(path, std::ios::binary);
		if (!input)
			throw std::runtime_error("Cannot read visible-languages.json");
		const auto root = nlohmann::json::parse(input);
		if (!root.is_object() || !root.contains("version") ||
		    !root.at("version").is_number_integer() ||
		    root.at("version").get<int>() != 1 ||
		    !root.contains("visible") || !root.at("visible").is_array())
			throw std::runtime_error("Invalid visible languages JSON schema");
		std::set<std::string> valid;
		for (const auto &item : all_supported_languages())
			valid.insert(item.code);
		std::set<std::string> seen;
		std::vector<std::string> result;
		for (const auto &item : root["visible"]) {
			if (!item.is_string())
				throw std::runtime_error("Language codes must be strings");
			auto code = item.get<std::string>();
			if (!valid.count(code))
				throw std::runtime_error("Unsupported language in visible list");
			if (seen.insert(code).second)
				result.push_back(code);
		}
		if (result.empty())
			throw std::runtime_error("Visible language list cannot be empty");
		// Preserve core Somang directions, even if a file omits them.
		for (const auto &core : {"ko", "en", "es"})
			if (seen.insert(core).second)
				result.insert(result.begin(), core);
		return result;
	} catch (const std::exception &ex) {
		obs_log(LOG_WARNING, "LocalVocal visible-languages.json: %s; using shortlist",
			ex.what());
		return defaults;
	}
}

inline std::vector<ManagedLanguage> visible_languages()
{
	std::vector<ManagedLanguage> result;
	const auto available = all_supported_languages();
	for (const auto &code : load_visible_languages()) {
		const auto found = std::find_if(
			available.begin(), available.end(),
			[&code](const auto &candidate) { return candidate.code == code; });
		if (found != available.end())
			result.push_back(*found);
	}
	return result;
}

} // namespace localvocal
