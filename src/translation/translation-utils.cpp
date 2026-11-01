#include <obs-module.h>

#include "translation-includes.h"
#include "translation.h"
#include "translation-utils.h"
#include "plugin-support.h"
#include "model-utils/model-downloader.h"

void start_translation(struct transcription_filter_data *gf,
		       const ctranslate2::TranslationOptions &options)
{
	obs_log(LOG_INFO, "Starting translation...");
	gf->model_callbacks->cancel(1);

	if (gf->translation_model_index == "!!!external!!!") {
		obs_log(LOG_INFO, "External model selected.");
		if (gf->translation_model_path_external.empty()) {
			obs_log(LOG_ERROR, "External model path is empty.");
			gf->translate = false;
			return;
		}
		std::string model_file_found = gf->translation_model_path_external;
		build_and_enable_translation(gf, model_file_found, &options);
		return;
	}

	const auto lifetime = gf->model_callbacks;
	const auto session = lifetime->begin(1);
	const ModelInfo &translation_model_info = models_info().at(gf->translation_model_index);
	gf->translate = false;
	download_model_with_ui_dialog(
		translation_model_info,
		[lifetime, session, options](int status, const std::string &path) {
			lifetime->run(session, [&](auto &owner) {
				if (status == 0)
					build_and_enable_translation(&owner, path, &options);
				else {
					obs_log(LOG_ERROR, "Translation model download failed");
					owner.translate = false;
				}
			});
		},
		session);
}
