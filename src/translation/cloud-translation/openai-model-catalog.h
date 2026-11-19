#pragma once

#include "openai-request.h"
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace localvocal {

// The operator edits only IDs and display labels in openai-models.json.
// Labels are never submitted to the API. This type is independent of OBS/Qt
// so catalog validation is covered by the existing Responses contract tests.
struct OpenAIModelChoice {
	std::string id;
	std::string label;
};

inline const std::vector<OpenAIModelChoice> &initial_openai_model_choices()
{
	static const std::vector<OpenAIModelChoice> defaults = {
		{"gpt-6-luna", "GPT-6 Luna (gpt-6-luna)"},
		{"gpt-6.1-terra", "GPT-6.1 Terra (gpt-6.1-terra)"}};
	return defaults;
}

inline std::string initial_openai_model_catalog_json()
{
	nlohmann::json root = {{"version", 1}, {"models", nlohmann::json::array()}};
	for (const auto &choice : initial_openai_model_choices())
		root["models"].push_back({{"id", choice.id}, {"label", choice.label}});
	return root.dump(2) + "\n";
}

inline std::vector<OpenAIModelChoice> parse_openai_model_catalog(const nlohmann::json &root)
{
	if (!root.is_object() || !root.contains("version") ||
	    !root.at("version").is_number_integer() || root.at("version").get<int>() != 1 ||
	    !root.contains("models") || !root.at("models").is_array())
		throw std::runtime_error("Expected a JSON object with version: 1 and models: []");

	const auto &models = root.at("models");
	if (models.empty() || models.size() > 64)
		throw std::runtime_error("OpenAI model list must contain 1 to 64 entries");

	std::set<std::string> ids;
	std::vector<OpenAIModelChoice> choices;
	choices.reserve(models.size());
	for (const auto &item : models) {
		if (!item.is_object() || !item.contains("id") ||
		    !item.at("id").is_string() || !item.contains("label") ||
		    !item.at("label").is_string())
			throw std::runtime_error("Each model requires string id and label fields");

		const auto id = item.at("id").get<std::string>();
		const auto label = item.at("label").get<std::string>();
		if (id.empty() || label.empty() || label.size() > 128 ||
		    label.find_first_of("\r\n\t") != std::string::npos)
			throw std::runtime_error("Invalid or empty OpenAI model ID/label");
		if (validated_openai_model(id) != id)
			throw std::runtime_error("Model ID must be exact, without surrounding whitespace");
		if (!ids.insert(id).second)
			throw std::runtime_error("Duplicate OpenAI model ID in catalog");
		choices.push_back({id, label});
	}
	return choices;
}

} // namespace localvocal
