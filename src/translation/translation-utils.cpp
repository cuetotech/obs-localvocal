#include <obs-module.h>

#include "translation-includes.h"
#include "translation.h"
#include "translation-utils.h"
#include "plugin-support.h"
#include "model-utils/model-downloader.h"
#include "model-utils/installed-models.h"
#include <obs-frontend-api.h>
#include <QTimer>

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
	gf->translate = false;
	const auto found = models_info().find(gf->translation_model_index);
	if (found == models_info().end()) {
		obs_log(LOG_ERROR, "Unknown local translation model '%s'",
			gf->translation_model_index.c_str());
		return;
	}
	// Do not download or pop a modal progress dialog when OBS starts.
	// A user must install missing CTranslate2 files via Manage Models.
	const auto &model_info = found->second;
	if (!localvocal::model_installed_locally(model_info)) {
		obs_log(LOG_ERROR, "Local translation model '%s' is not installed. "
			"Open Manage Models to download it.",
			model_info.friendly_name.c_str());
		return;
	}
	const auto folder = find_model_folder(model_info);
	if (!folder)
		return;
	const auto model_path = folder->string();
	// The settings update holds settings_mutex; defer model initialization
	// until that update returns, as the old downloader's success callback did.
	QTimer::singleShot(
		0, static_cast<QObject *>(obs_frontend_get_main_window()),
		[lifetime, session, options, model_path] {
			lifetime->run(session, [&](auto &owner) {
				build_and_enable_translation(&owner, model_path, &options);
			});
		});

}
