#include "transcription-filter-utils.h"
#include "subtitle-overlay-policy.h"

#include <obs-module.h>
#include <obs.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <graphics/vec2.h>

#include <atomic>
#include <cstring>
#include <initializer_list> // MSVC requires this for braced range-for lists.

namespace {

std::atomic<bool> overlay_callback_pending{false};

bool is_text_output(obs_source_t *source)
{
	if (!source)
		return false;
	const char *id = obs_source_get_id(source);
	return id && (std::strcmp(id, "text_gdiplus_v3") == 0 ||
		      std::strcmp(id, "text_gdiplus_v2") == 0 ||
		      std::strcmp(id, "text_ft2_source_v2") == 0);
}

bool is_scene(obs_source_t *source)
{
	const char *id = source ? obs_source_get_id(source) : nullptr;
	return id && std::strcmp(id, "scene") == 0;
}

obs_source_t *create_church_text_source()
{
	// New-source defaults from cts/status.md. Never rewrite an existing source.
	obs_data_t *settings = obs_data_create();
	obs_data_set_bool(settings, "word_wrap", true);
	obs_data_set_bool(settings, "extents", true);
	obs_data_set_int(settings, "extents_cx", localvocal::kEnglishSubtitleWidth);
	obs_data_set_int(settings, "extents_cy", localvocal::kEnglishSubtitleHeight);
	obs_data_set_int(settings, "color", 0xFFFFFF);
	obs_data_set_bool(settings, "outline", true);
	obs_data_set_int(settings, "outline_color", 0x000000);
	obs_data_set_int(settings, "outline_size", 7);
#ifdef _WIN32
	obs_data_set_int(settings, "bk_color", 0x000000);
	obs_data_set_int(settings, "bk_opacity",
			 localvocal::kEnglishSubtitleBackgroundOpacity);
#endif
	obs_data_t *font = obs_data_create();
	obs_data_set_string(font, "face", "Arial");
	obs_data_set_string(font, "style", "Regular");
	obs_data_set_int(font, "size", localvocal::kEnglishSubtitleFontSize);
	obs_data_set_int(font, "flags", 0);
	obs_data_set_obj(settings, "font", font);
	obs_data_release(font);
#ifdef _WIN32
	const char *text_id = "text_gdiplus_v3";
#else
	const char *text_id = "text_ft2_source_v2";
#endif
	obs_source_t *source =
		obs_source_create(text_id, localvocal::kEnglishSubtitlesSource, settings, nullptr);
	obs_data_release(settings);
	if (!source)
		obs_log(LOG_ERROR, "Unable to create English Subtitles text source");
	return source;
}

void attach_new_overlay_to_known_scenes(obs_scene_t *overlay)
{
	// Only newly created overlays are attached. Existing scene collections,
	// direct English source items, and per-scene transforms are left untouched.
	obs_source_t *overlay_source = obs_scene_get_source(overlay);
	bool attached = false;
	for (const char *name : {"Sanctuary", "Youtube Stream"}) {
		obs_source_t *parent = obs_get_source_by_name(name);
		if (!is_scene(parent)) {
			if (parent)
				obs_source_release(parent);
			continue;
		}
		obs_scene_t *parent_scene = obs_scene_from_source(parent);
		if (obs_scene_find_source(parent_scene, localvocal::kEnglishSubtitlesSource)) {
			obs_log(LOG_INFO, "Leaving direct subtitles in '%s' unchanged; attach overlay manually",
				name);
		} else if (obs_scene_find_source(parent_scene,
						localvocal::kSubtitleOverlayScene) ||
			   obs_scene_add(parent_scene, overlay_source)) {
			attached = true;
		}
		obs_source_release(parent);
	}
	if (attached)
		return;

	// Preserve the legacy behavior for a new scene collection with no named
	// church scenes. Do not overlay a scene already showing direct subtitles.
	obs_source_t *current = obs_frontend_get_current_scene();
	if (is_scene(current) &&
	    std::strcmp(obs_source_get_name(current), localvocal::kSubtitleOverlayScene) != 0) {
		obs_scene_t *scene = obs_scene_from_source(current);
		if (!obs_scene_find_source(scene, localvocal::kEnglishSubtitlesSource) &&
		    !obs_scene_find_source(scene, localvocal::kSubtitleOverlayScene))
			obs_scene_add(scene, overlay_source);
	}
	if (current)
		obs_source_release(current);
}

bool ensure_subtitle_overlay()
{
	obs_source_t *overlay_source = obs_get_source_by_name(localvocal::kSubtitleOverlayScene);
	obs_source_t *text_source = obs_get_source_by_name(localvocal::kEnglishSubtitlesSource);
	obs_scene_t *overlay = is_scene(overlay_source)
				      ? obs_scene_from_source(overlay_source)
				      : nullptr;
	const auto plan = localvocal::plan_subtitle_overlay({
		overlay_source != nullptr, overlay != nullptr, text_source != nullptr,
		is_text_output(text_source),
		overlay && obs_scene_find_source(overlay, localvocal::kEnglishSubtitlesSource),
	});
	if (plan.conflict) {
		obs_log(LOG_WARNING,
			"Subtitle overlay name collision: expected a scene named '%s' and a text source named '%s'; no sources changed",
			localvocal::kSubtitleOverlayScene, localvocal::kEnglishSubtitlesSource);
		if (text_source)
			obs_source_release(text_source);
		if (overlay_source)
			obs_source_release(overlay_source);
		return true; // Do not retry a permanent collision each scene event.
	}
	bool created_overlay = false;
	if (plan.create_scene) {
		overlay = obs_scene_create(localvocal::kSubtitleOverlayScene);
		if (!overlay) {
			obs_log(LOG_ERROR, "Unable to create Subtitles Overlay scene");
			if (text_source)
				obs_source_release(text_source);
			return false;
		}
		created_overlay = true;
	}
	if (plan.create_text) {
		text_source = create_church_text_source();
		if (!text_source) {
			if (created_overlay)
				obs_scene_release(overlay);
			else if (overlay_source)
				obs_source_release(overlay_source);
			return false;
		}
	}
	if (plan.add_text_to_overlay) {
		obs_sceneitem_t *item = obs_scene_add(overlay, text_source);
		if (!item) {
			obs_log(LOG_ERROR, "Unable to add English Subtitles to overlay");
			obs_source_release(text_source);
			if (created_overlay)
				obs_scene_release(overlay);
			else if (overlay_source)
				obs_source_release(overlay_source);
			return false;
		}
		obs_source_t *overlay_as_source = obs_scene_get_source(overlay);
		const int scene_height = static_cast<int>(obs_source_get_height(overlay_as_source));
		struct vec2 pos = {160.0f, static_cast<float>(
			(scene_height > localvocal::kEnglishSubtitleHeight +
			 localvocal::kEnglishSubtitleBottomMargin)
				? scene_height - localvocal::kEnglishSubtitleHeight -
					  localvocal::kEnglishSubtitleBottomMargin
				: 0)};
		obs_sceneitem_set_pos(item, &pos);
		obs_log(LOG_INFO, "Attached English Subtitles to shared Subtitles Overlay");
	}
	if (created_overlay) {
		attach_new_overlay_to_known_scenes(overlay);
		obs_log(LOG_INFO, "Created shared Subtitles Overlay; verify parent scenes and F7/F8 hotkeys off-air");
	} else {
		obs_log(LOG_INFO, "Reusing existing Subtitles Overlay without changing its style or visibility");
	}
	if (text_source)
		obs_source_release(text_source);
	if (created_overlay)
		obs_scene_release(overlay);
	else if (overlay_source)
		obs_source_release(overlay_source);
	return true;
}

void ensure_overlay_on_scene_event(obs_frontend_event event, void *)
{
	if (event != OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED &&
	    event != OBS_FRONTEND_EVENT_FINISHED_LOADING)
		return;
	obs_source_t *current = obs_frontend_get_current_scene();
	if (!current)
		return; // Scene loading not complete: keep the one-shot callback.
	obs_source_release(current);
	if (ensure_subtitle_overlay()) {
		obs_frontend_remove_event_callback(ensure_overlay_on_scene_event, nullptr);
		overlay_callback_pending.store(false);
	}
}

} // namespace

void create_obs_text_source_if_needed()
{
	obs_source_t *current = obs_frontend_get_current_scene();
	if (current) {
		obs_source_release(current);
		if (ensure_subtitle_overlay())
			return;
	}
	bool expected = false;
	if (overlay_callback_pending.compare_exchange_strong(expected, true))
		obs_frontend_add_event_callback(ensure_overlay_on_scene_event, nullptr);
}

bool add_sources_to_list(void *list_property, obs_source_t *source)
{
	if (!is_text_output(source))
		return true;
	obs_property_t *sources = static_cast<obs_property_t *>(list_property);
	const char *name = obs_source_get_name(source);
	obs_property_list_add_string(sources, name, name);
	return true;
}
