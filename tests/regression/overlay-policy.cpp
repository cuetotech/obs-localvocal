#include "../../src/subtitle-overlay-policy.h"

#include <iostream>
#include <stdexcept>

void check(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
}

int main()
{
	using namespace localvocal;
	const auto fresh = plan_subtitle_overlay({false, false, false, false, false});
	check(!fresh.conflict && fresh.create_scene && fresh.create_text &&
		      fresh.add_text_to_overlay,
	      "new setup provisions one shared overlay and one text source");

	const auto existing = plan_subtitle_overlay({true, true, true, true, true});
	check(!existing.conflict && !existing.create_scene && !existing.create_text &&
		      !existing.add_text_to_overlay,
	      "existing complete overlay is never recreated or restyled");

	const auto missing_item = plan_subtitle_overlay({true, true, true, true, false});
	check(!missing_item.conflict && !missing_item.create_scene &&
		      !missing_item.create_text && missing_item.add_text_to_overlay,
	      "existing English source is reused inside existing overlay");

	const auto missing_text = plan_subtitle_overlay({true, true, false, false, false});
	check(!missing_text.conflict && !missing_text.create_scene &&
		      missing_text.create_text && missing_text.add_text_to_overlay,
	      "existing empty overlay receives only missing text");

	const auto missing_overlay = plan_subtitle_overlay({false, false, true, true, false});
	check(!missing_overlay.conflict && missing_overlay.create_scene &&
		      !missing_overlay.create_text && missing_overlay.add_text_to_overlay,
	      "existing text is never duplicated when scene is missing");

	check(plan_subtitle_overlay({true, false, false, false, false}).conflict,
	      "non-scene collision is not overwritten");
	check(plan_subtitle_overlay({true, true, true, false, false}).conflict,
	      "non-text collision is not overwritten");

	check(should_default_cloud_to_english(true, "none", "none"),
	      "unassigned cloud defaults to English Subtitles");
	check(should_default_cloud_to_english(true, "", nullptr),
	      "empty legacy selections also default");
	check(!should_default_cloud_to_english(false, "none", "none"),
	      "cloud disabled does not change routing");
	check(!should_default_cloud_to_english(true, "Existing English", "none"),
	      "explicit cloud source is preserved");
	check(!should_default_cloud_to_english(true, "none", "Korean Captions"),
	      "explicit General fallback is preserved");
	check(source_choice_unset("(null)") && !source_choice_unset("English Subtitles"),
	      "source-choice sentinel detection");

	check(kEnglishSubtitleWidth == 1600 && kEnglishSubtitleFontSize == 72 &&
		      kEnglishSubtitleBottomMargin == 40,
	      "church presentation defaults retained");
	std::cout << "subtitle overlay policy regression passed\n";
}
