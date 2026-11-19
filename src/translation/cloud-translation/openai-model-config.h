#pragma once

#include "openai-model-catalog.h"
#include <obs-module.h>
#include <util/bmem.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace localvocal {

// Keep the operator's model list alongside the OBS plugin configuration,
// not next to the binary. This follows OBS portable/non-portable profiles.
struct OpenAIModelCatalog {
	std::vector<OpenAIModelChoice> models;
	std::string path;
	std::string error;
};

inline OpenAIModelCatalog read_openai_model_catalog()
{
	OpenAIModelCatalog result;
	char *config_path = obs_module_config_path("openai-models.json");
	if (!config_path) {
		result.error = "OBS could not locate its LocalVocal configuration folder";
		return result;
	}
	result.path = config_path;
	bfree(config_path);

	try {
		const auto file = std::filesystem::u8path(result.path);
		if (!std::filesystem::exists(file)) {
			std::filesystem::create_directories(file.parent_path());
			std::ofstream output(file, std::ios::binary | std::ios::trunc);
			if (!output)
				throw std::runtime_error("Cannot create openai-models.json");
			output << initial_openai_model_catalog_json();
			output.close();
			if (!output)
				throw std::runtime_error("Cannot finish writing openai-models.json");
		}
		if (std::filesystem::file_size(file) > 64 * 1024)
			throw std::runtime_error("openai-models.json is too large (64 KiB max)");
		std::ifstream input(file, std::ios::binary);
		if (!input)
			throw std::runtime_error("Cannot read openai-models.json");
		const auto json = nlohmann::json::parse(input);
		result.models = parse_openai_model_catalog(json);
	} catch (const std::exception &ex) {
		result.error = ex.what();
	}
	return result;
}

inline bool openai_catalog_contains(const OpenAIModelCatalog &catalog,
				     const std::string &model)
{
	for (const auto &item : catalog.models)
		if (item.id == model)
			return true;
	return false;
}

} // namespace localvocal
