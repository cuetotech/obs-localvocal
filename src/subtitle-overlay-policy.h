#ifndef SUBTITLE_OVERLAY_POLICY_H
#define SUBTITLE_OVERLAY_POLICY_H

#include <cstring>

namespace localvocal {

constexpr const char *kSubtitleOverlayScene = "Subtitles Overlay";
constexpr const char *kEnglishSubtitlesSource = "English Subtitles";
constexpr int kEnglishSubtitleWidth = 1600;
constexpr int kEnglishSubtitleHeight = 230;
constexpr int kEnglishSubtitleFontSize = 72;
constexpr int kEnglishSubtitleBottomMargin = 40;
constexpr int kEnglishSubtitleBackgroundOpacity = 40;

struct SubtitleOverlayState {
	bool overlay_exists;
	bool overlay_is_scene;
	bool text_exists;
	bool text_is_output;
	bool text_in_overlay;
};

struct SubtitleOverlayPlan {
	bool conflict;
	bool create_scene;
	bool create_text;
	bool add_text_to_overlay;
};

inline SubtitleOverlayPlan plan_subtitle_overlay(const SubtitleOverlayState &state)
{
	if ((state.overlay_exists && !state.overlay_is_scene) ||
	    (state.text_exists && !state.text_is_output))
		return {true, false, false, false};
	return {false, !state.overlay_exists, !state.text_exists, !state.text_in_overlay};
}

inline bool source_choice_unset(const char *name)
{
	return !name || !*name || std::strcmp(name, "none") == 0 ||
	       std::strcmp(name, "(null)") == 0;
}

inline bool should_default_cloud_to_english(bool cloud_enabled, const char *cloud_output,
					    const char *general_output)
{
	// An unset cloud route must never fall back to General's Korean source.
	// Respect any explicit cloud destination. General routing is independent.
	(void)general_output;
	return cloud_enabled && source_choice_unset(cloud_output);
}

} // namespace localvocal

#endif // SUBTITLE_OVERLAY_POLICY_H
