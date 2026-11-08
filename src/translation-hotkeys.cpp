#include "translation-hotkeys.h"

#include "subtitle-overlay-policy.h"
#include "transcription-filter-data.h"
#include "translation-hotkey-policy.h"

#include <obs.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

std::atomic<bool> cloud_paused{false};
std::mutex filters_mutex;
std::unordered_set<transcription_filter_data *> filters;
obs_hotkey_id pause_hotkey = OBS_INVALID_HOTKEY_ID;
obs_hotkey_id resume_hotkey = OBS_INVALID_HOTKEY_ID;
bool hotkeys_registered = false;

bool is_text_source(obs_source_t *source)
{
	const char *id = source ? obs_source_get_id(source) : nullptr;
	return id && (std::strcmp(id, "text_gdiplus_v3") == 0 ||
		      std::strcmp(id, "text_gdiplus_v2") == 0 ||
		      std::strcmp(id, "text_ft2_source_v2") == 0);
}

void add_output(std::vector<std::string> &names, const std::string &name)
{
	if (localvocal::source_choice_unset(name.c_str()))
		return;
	if (std::find(names.begin(), names.end(), name) == names.end())
		names.push_back(name);
}

std::vector<std::string> collect_outputs_locked(bool cancel_pending)
{
	std::vector<std::string> names;
	for (auto *gf : filters) {
		std::lock_guard<std::mutex> cloud_lock(gf->cloud_mutex);
		if (!gf->translate_cloud)
			continue;
		add_output(names, gf->translate_cloud_output);
		if (cancel_pending) {
			// Keep Whisper hot and all saved provider/configuration settings intact.
			gf->cloud_pipeline.reset();
			gf->openai_speech_context.clear();
			gf->openai_context_source_language.clear();
		}
	}
	return names;
}

void clear_text_source(const std::string &name)
{
	obs_source_t *source = obs_get_source_by_name(name.c_str());
	if (!is_text_source(source)) {
		if (source)
			obs_source_release(source);
		return;
	}
	obs_data_t *settings = obs_source_get_settings(source);
	if (settings) {
		obs_data_set_string(settings, "text", "");
		obs_source_update(source, settings);
		obs_data_release(settings);
	}
	obs_source_release(source);
}

struct VisibilityChange {
	bool visible;
	const std::vector<std::string> *outputs;
};

bool update_scene_visibility(void *data, obs_source_t *source)
{
	const char *id = obs_source_get_id(source);
	if (!id || std::strcmp(id, "scene") != 0)
		return true;
	auto *ctx = static_cast<VisibilityChange *>(data);
	const char *scene_name = obs_source_get_name(source);
	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene || !scene_name)
		return true;

	for (const auto &name : *ctx->outputs) {
		obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str());
		if (item && is_text_source(obs_sceneitem_get_source(item)) &&
		    localvocal::manage_cloud_caption_item(scene_name, name.c_str(), *ctx->outputs))
			obs_sceneitem_set_visible(item, ctx->visible);
	}
	obs_sceneitem_t *overlay = obs_scene_find_source(scene, localvocal::kSubtitleOverlayScene);
	if (overlay && localvocal::manage_cloud_caption_item(
				 scene_name, localvocal::kSubtitleOverlayScene, *ctx->outputs)) {
		obs_source_t *overlay_source = obs_sceneitem_get_source(overlay);
		const char *overlay_id = overlay_source ? obs_source_get_id(overlay_source) : nullptr;
		if (overlay_id && std::strcmp(overlay_id, "scene") == 0)
			obs_sceneitem_set_visible(overlay, ctx->visible);
	}
	return true;
}

void set_visible(bool visible, const std::vector<std::string> &outputs)
{
	if (outputs.empty())
		return;
	VisibilityChange ctx{visible, &outputs};
	obs_enum_sources(update_scene_visibility, &ctx);
}

void apply_cloud_pause(bool paused)
{
	std::vector<std::string> outputs;
	{
		// Destruction first unregisters under this mutex; pointers stay alive
		// while F7 invalidates queued/in-flight cloud presentations.
		std::lock_guard<std::mutex> lock(filters_mutex);
		cloud_paused.store(paused, std::memory_order_release);
		outputs = collect_outputs_locked(paused);
	}
	if (paused)
		for (const auto &output : outputs)
			clear_text_source(output);
	set_visible(!paused, outputs);
	obs_log(LOG_INFO, "Translation %s: %zu cloud destinations; microphone unchanged",
		paused ? "PAUSED (F7)" : "RESUMED (F8)", outputs.size());
}

void on_pause(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		apply_cloud_pause(true);
}

void on_resume(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		apply_cloud_pause(false);
}

void assign_default_if_unbound(obs_hotkey_id id, obs_key_t key)
{
	if (id == OBS_INVALID_HOTKEY_ID)
		return;
	obs_data_array_t *saved = obs_hotkey_save(id);
	if (!saved)
		return;
	const bool unbound = obs_data_array_count(saved) == 0;
	obs_data_array_release(saved);
	if (unbound) {
		obs_key_combination_t combo = {0, key};
		obs_hotkey_load_bindings(id, &combo, 1);
	}
}

void on_frontend_event(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		// Do not overwrite an existing saved hotkey combination.
		assign_default_if_unbound(pause_hotkey, OBS_KEY_F7);
		assign_default_if_unbound(resume_hotkey, OBS_KEY_F8);
	}
	if ((event == OBS_FRONTEND_EVENT_FINISHED_LOADING ||
	     event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED) &&
	    cloud_paused.load(std::memory_order_acquire)) {
		std::vector<std::string> outputs;
		{
			std::lock_guard<std::mutex> lock(filters_mutex);
			outputs = collect_outputs_locked(false);
		}
		set_visible(false, outputs);
	}
}

} // namespace

bool localvocal_translation_is_paused()
{
	return cloud_paused.load(std::memory_order_acquire);
}

void localvocal_translation_filter_register(transcription_filter_data *gf)
{
	if (!gf)
		return;
	{
		std::lock_guard<std::mutex> lock(filters_mutex);
		filters.insert(gf);
		if (cloud_paused.load(std::memory_order_acquire)) {
			std::lock_guard<std::mutex> cloud_lock(gf->cloud_mutex);
			gf->cloud_pipeline.reset();
		}
	}
	if (localvocal_translation_is_paused()) {
		std::vector<std::string> outputs;
		{
			std::lock_guard<std::mutex> lock(filters_mutex);
			outputs = collect_outputs_locked(false);
		}
		set_visible(false, outputs);
	}
}

void localvocal_translation_filter_unregister(transcription_filter_data *gf)
{
	std::lock_guard<std::mutex> lock(filters_mutex);
	filters.erase(gf);
}

extern "C" void localvocal_register_translation_hotkeys(void)
{
	if (hotkeys_registered)
		return;
	pause_hotkey = obs_hotkey_register_frontend(
		"localvocal.pause_cloud_translation",
		"LocalVocal: Pause translation + hide captions (F7)", on_pause, nullptr);
	resume_hotkey = obs_hotkey_register_frontend(
		"localvocal.resume_cloud_translation",
		"LocalVocal: Resume translation + show captions (F8)", on_resume, nullptr);
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	hotkeys_registered = true;
	obs_log(LOG_INFO, "Registered F7/F8 LocalVocal translation controls");
}

extern "C" void localvocal_unregister_translation_hotkeys(void)
{
	if (!hotkeys_registered)
		return;
	obs_frontend_remove_event_callback(on_frontend_event, nullptr);
	if (pause_hotkey != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(pause_hotkey);
	if (resume_hotkey != OBS_INVALID_HOTKEY_ID)
		obs_hotkey_unregister(resume_hotkey);
	pause_hotkey = OBS_INVALID_HOTKEY_ID;
	resume_hotkey = OBS_INVALID_HOTKEY_ID;
	hotkeys_registered = false;
}
