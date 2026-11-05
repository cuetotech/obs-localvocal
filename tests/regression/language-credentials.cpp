#include "../../src/translation/language-shortlist.h"
#include "../../src/translation/cloud-translation/cloud-credentials.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const char *message)
{
	if (!ok) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}
}

int main()
{
	const auto &languages = localvocal::short_language_choices();
	check(languages.size() == 13, "exactly 13 operator language choices");
	check(std::string(languages[0].whisper_code) == "ko", "Korean pinned first");
	check(std::string(languages[1].whisper_code) == "en", "English pinned second");
	check(std::string(languages[2].whisper_code) == "es", "Spanish pinned third");
	std::set<std::string> sources, clouds, locals;
	for (const auto &lang : languages) {
		sources.insert(lang.whisper_code);
		clouds.insert(lang.cloud_code);
		locals.insert(lang.local_code);
	}
	check(sources.size() == 13 && clouds.size() == 13 && locals.size() == 13,
	      "no duplicate menu codes");
	check(localvocal::is_short_source_language("en") &&
	      localvocal::is_short_source_language("ko"), "both sermon source directions available");
	check(localvocal::is_short_cloud_target("ko") &&
	      localvocal::is_short_cloud_target("en"), "both cloud target directions available");
	check(localvocal::is_short_cloud_target("__ko__") &&
	      localvocal::is_short_cloud_target("__en__"), "local legacy targets preserved");
	check(localvocal::is_short_source_language("auto"), "source auto detection retained");
	check(!localvocal::is_short_source_language("xx"), "unknown language not shown");
	check(!localvocal::is_short_cloud_target("ru"), "non-shortlisted language hidden");
	check(std::string(localvocal::cloud_key_setting("google")) ==
		      "translate_cloud_google_api_key", "Google has distinct field");
	check(std::string(localvocal::cloud_key_setting("openai")) ==
		      "translate_cloud_openai_api_key", "OpenAI has distinct field");
	check(std::string(localvocal::cloud_key_setting("deepl")) ==
		      "translate_cloud_api_key", "other providers keep their legacy field");
	check(localvocal::selected_cloud_api_key("google", "GOOGLE", "OPENAI", "LEGACY") ==
		      "GOOGLE", "Google selection never uses OpenAI key");
	check(localvocal::selected_cloud_api_key("openai", "GOOGLE", "OPENAI", "LEGACY") ==
		      "OPENAI", "OpenAI selection never uses Google key");
	check(localvocal::selected_cloud_api_key("azure", "GOOGLE", "OPENAI", "LEGACY") ==
		      "LEGACY", "other providers preserve existing credentials");
	check(std::string(localvocal::dedicated_cloud_key_setting("google")) ==
		      "translate_cloud_google_api_key", "Google migration target");
	check(localvocal::dedicated_cloud_key_setting("custom") == nullptr,
	      "no migration to unrelated provider");
	if (failures)
		return EXIT_FAILURE;
	std::cout << "Compact languages and separate provider credentials passed\n";
	return EXIT_SUCCESS;
}
