#include "model-downloader-types.h"
#include "plugin-support.h"

#include <obs-module.h>

#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

// The installed-model picker must not perform networking during OBS startup.
// Browse the bundled, offline catalog in Manage Models instead. Updates to the
// catalog are a deliberate operator action, never an automatic fetch.

/**
 * @brief Parses a JSON object to extract model information.
 *
 * This function takes a JSON object representing a model and extracts various
 * fields to populate a ModelInfo structure. It performs validation on the
 * presence and types of required fields and logs warnings for any missing or
 * invalid fields.
 *
 * @param model The JSON object containing the model information.
 * @return An optional ModelInfo object. If the required fields are missing or
 *         invalid, it returns std::nullopt.
 *
 * The JSON object is expected to have the following structure:
 * {
 *     "friendly_name": "string",          // Required
 *     "local_folder_name": "string",      // Optional
 *     "type": "string",                   // Optional, expected values: "MODEL_TYPE_TRANSCRIPTION", "MODEL_TYPE_TRANSLATION", or "MODEL_TYPE_TRANSCRIPTION_COREML"
 *     "files": [                          // Optional, array of file objects
 *         {
 *             "url": "string",            // Required in each file object
 *             "sha256": "string"          // Required in each file object
 *         },
 *         ...
 *     ],
 *     "extra": {                          // Optional
 *         "language": "string",           // Optional
 *         "description": "string",        // Optional
 *         "source": "string"              // Optional
 *     }
 * }
 */
std::optional<ModelInfo> parse_model_json(const nlohmann::json &model)
{
	ModelInfo model_info;

	try {
		if (!model.contains("friendly_name") || !model["friendly_name"].is_string()) {
			obs_log(LOG_WARNING,
				"Missing or invalid 'friendly_name' for a model. Skipping this model.");
			return std::nullopt;
		}
		model_info.friendly_name = model["friendly_name"].get<std::string>();

		if (model.contains("local_folder_name") && model["local_folder_name"].is_string()) {
			model_info.local_folder_name =
				model["local_folder_name"].get<std::string>();
		} else {
			obs_log(LOG_WARNING, "Missing or invalid 'local_folder_name' for model: %s",
				model_info.friendly_name.c_str());
		}

		if (model.contains("type") && model["type"].is_string()) {
			const std::string &type_str = model["type"].get<std::string>();
			if (type_str == "MODEL_TYPE_TRANSCRIPTION")
				model_info.type = ModelType::MODEL_TYPE_TRANSCRIPTION;
			else if (type_str == "MODEL_TYPE_TRANSLATION")
				model_info.type = ModelType::MODEL_TYPE_TRANSLATION;
			else if (type_str == "MODEL_TYPE_TRANSCRIPTION_COREML")
				model_info.type = ModelType::MODEL_TYPE_TRANSCRIPTION_COREML;
			else
				obs_log(LOG_WARNING, "Invalid 'type' for model: %s",
					model_info.friendly_name.c_str());
		} else {
			obs_log(LOG_WARNING, "Missing or invalid 'type' for model: %s",
				model_info.friendly_name.c_str());
		}

		if (model.contains("files") && model["files"].is_array()) {
			for (const auto &file : model["files"]) {
				ModelFileDownloadInfo file_info;

				if (file.contains("url") && file["url"].is_string())
					file_info.url = file["url"].get<std::string>();
				else
					obs_log(LOG_WARNING,
						"Missing or invalid 'url' for a file in model: %s",
						model_info.friendly_name.c_str());

				if (file.contains("sha256") && file["sha256"].is_string())
					file_info.sha256 = file["sha256"].get<std::string>();

				model_info.files.push_back(file_info);
			}
		} else {
			obs_log(LOG_WARNING, "Missing or invalid 'files' array for model: %s",
				model_info.friendly_name.c_str());
		}

		// Parse the new "extra" field
		if (model.contains("extra") && model["extra"].is_object()) {
			const auto &extra = model["extra"];
			if (extra.contains("language") && extra["language"].is_string())
				model_info.extra.language = extra["language"].get<std::string>();
			if (extra.contains("description") && extra["description"].is_string())
				model_info.extra.description =
					extra["description"].get<std::string>();
			if (extra.contains("source") && extra["source"].is_string())
				model_info.extra.source = extra["source"].get<std::string>();
		}

		return model_info;
	} catch (const std::exception &e) {
		obs_log(LOG_ERROR, "Error parsing model: %s", e.what());
		return std::nullopt;
	}
}

/**
 * Load the shipped model catalog offline. This function is invoked by OBS
 * filter property construction, so a network request here stalls startup.
 */
std::map<std::string, ModelInfo> load_models_info()
{
	std::map<std::string, ModelInfo> result;
	char *catalog_path = obs_module_file("models/models_directory.json");
	if (!catalog_path) {
		obs_log(LOG_ERROR, "Bundled LocalVocal model directory unavailable");
		return result;
	}
	const std::string path = catalog_path;
	bfree(catalog_path);
	try {
		std::ifstream input(path, std::ios::binary);
		if (!input)
			throw std::runtime_error("Cannot open bundled model catalog");
		const auto catalog = nlohmann::json::parse(input);
		if (!catalog.is_object() || !catalog.contains("models") ||
		    !catalog["models"].is_array())
			throw std::runtime_error("Bundled model catalog has no models array");
		for (const auto &entry : catalog["models"]) {
			auto model = parse_model_json(entry);
			if (model && !model->friendly_name.empty() &&
			    !model->local_folder_name.empty() && !model->files.empty())
				result[model->friendly_name] = *model;
		}
	} catch (const std::exception &ex) {
		obs_log(LOG_ERROR, "Cannot load bundled LocalVocal model catalog: %s",
			ex.what());
	}
	obs_log(LOG_INFO, "Loaded %zu offline model catalog entries (no startup download)",
		result.size());
	return result;
}

const std::map<std::string, ModelInfo> &models_info()
{
	static const std::unique_ptr<const std::map<std::string, ModelInfo>> cached_models_info =
		std::make_unique<const std::map<std::string, ModelInfo>>(load_models_info());

	return *cached_models_info;
}

const std::vector<ModelInfo> get_sorted_models_info(std::optional<ModelType> type_filter)
{
	const auto &models_map = models_info();
	std::vector<ModelInfo> standard_models;
	std::vector<ModelInfo> huggingface_models;

	// Separate models into two categories
	for (const auto &[key, model] : models_map) {
		if (!type_filter.has_value() || model.type == type_filter.value()) {
			if (!model.extra.source.empty()) {
				huggingface_models.push_back(model);
			} else {
				standard_models.push_back(model);
			}
		}
	}

	// Sort both vectors based on friendly_name
	auto sort_by_name = [](const ModelInfo &a, const ModelInfo &b) {
		return a.friendly_name < b.friendly_name;
	};
	std::sort(standard_models.begin(), standard_models.end(), sort_by_name);
	std::sort(huggingface_models.begin(), huggingface_models.end(), sort_by_name);

	// Combine the sorted vectors with a separator
	std::vector<ModelInfo> result = std::move(standard_models);
	if (!huggingface_models.empty()) {
		ModelInfo info;
		info.friendly_name = "--------- HuggingFace Models ---------";
		info.type = ModelType::MODEL_TYPE_TRANSCRIPTION;
		result.push_back(info);
		result.insert(result.end(), huggingface_models.begin(), huggingface_models.end());
	}

	return result;
}
