#include "../../src/translation-hotkey-policy.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

void check(bool value, const char *message)
{
	if (!value)
		throw std::runtime_error(message);
}

int main()
{
	using localvocal::cloud_submission_allowed;
	using localvocal::manage_cloud_caption_item;
	check(cloud_submission_allowed(true, false, true), "enabled provider may submit");
	check(!cloud_submission_allowed(true, true, true), "F7 prevents Google/OpenAI requests");
	check(!cloud_submission_allowed(false, false, true), "disabled provider stays disabled after F8");
	check(!cloud_submission_allowed(true, false, false), "empty utterance never submits");
	check(cloud_submission_allowed(true, false, true), "F8 resumes submitted utterances");
	const std::vector<std::string> english{"English Subtitles"};
	check(manage_cloud_caption_item("Subtitles Overlay", "English Subtitles", english),
	      "shared English text controlled");
	check(manage_cloud_caption_item("Sanctuary", "Subtitles Overlay", english),
	      "Sanctuary shared overlay controlled");
	check(manage_cloud_caption_item("Youtube Stream", "Subtitles Overlay", english),
	      "YouTube shared overlay controlled");
	check(!manage_cloud_caption_item("PowerPoint", "Subtitles Overlay", english),
	      "unrelated scene overlay untouched");
	check(!manage_cloud_caption_item("Youtube Stream", "Church Mic", english),
	      "microphone never affected");
	const std::vector<std::string> ab{"A-B Google English", "A-B OpenAI English"};
	check(manage_cloud_caption_item("Translation A-B", "A-B Google English", ab) &&
	      manage_cloud_caption_item("Translation A-B", "A-B OpenAI English", ab),
	      "A/B cloud destinations controlled together");
	check(!manage_cloud_caption_item("Sanctuary", "Subtitles Overlay", ab),
	      "A/B outputs do not alter production overlay");
	check(!manage_cloud_caption_item("Translation A-B", "English Subtitles", ab),
	      "unselected text source untouched");
	check(!manage_cloud_caption_item(nullptr, "A-B Google English", ab),
	      "missing scene is not a match");
	std::cout << "translation hotkey policy regression passed\n";
}
