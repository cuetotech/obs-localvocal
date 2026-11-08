#pragma once

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace localvocal {

// Runtime pause is independent from mic mute, filter enablement and Whisper.
inline bool cloud_submission_allowed(bool cloud_enabled, bool paused, bool nonempty_input)
{
	return cloud_enabled && !paused && nonempty_input;
}

// Manage only text selected as a live cloud output, and the known church
// shared-overlay parent items. Never hide microphone, camera or PPT sources.
inline bool manage_cloud_caption_item(const char *scene, const char *item,
					const std::vector<std::string> &outputs)
{
	if (!scene || !item)
		return false;
	if (std::find(outputs.begin(), outputs.end(), item) != outputs.end())
		return true;
	const bool shared_english = std::find(outputs.begin(), outputs.end(),
						"English Subtitles") != outputs.end();
	return shared_english && std::strcmp(item, "Subtitles Overlay") == 0 &&
	       (std::strcmp(scene, "Sanctuary") == 0 ||
		std::strcmp(scene, "Youtube Stream") == 0);
}

} // namespace localvocal
