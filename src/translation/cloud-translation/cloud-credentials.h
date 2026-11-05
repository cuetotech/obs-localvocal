#pragma once

#include <string>

namespace localvocal {

// Do not reuse credentials between Google and OpenAI, even when provider
// changes trigger an OBS filter settings update.
inline const char *dedicated_cloud_key_setting(const std::string &provider)
{
	if (provider == "google")
		return "translate_cloud_google_api_key";
	if (provider == "openai")
		return "translate_cloud_openai_api_key";
	return nullptr;
}

inline const char *cloud_key_setting(const std::string &provider)
{
	const char *dedicated = dedicated_cloud_key_setting(provider);
	return dedicated ? dedicated : "translate_cloud_api_key";
}

inline std::string selected_cloud_api_key(const std::string &provider,
					  const std::string &google_key,
					  const std::string &openai_key,
					  const std::string &other_provider_key)
{
	return provider == "google" ? google_key
	       : provider == "openai" ? openai_key
				    : other_provider_key;
}

} // namespace localvocal
