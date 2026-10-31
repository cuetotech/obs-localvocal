#include "google-cloud.h"
#include "curl-helper.h"
#include "html-entities.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>
#include <limits>

using json = nlohmann::json;

namespace {
size_t bounded_response(void *data, size_t size, size_t count, void *user)
{
	constexpr size_t limit = 1024 * 1024;
	auto &response = *static_cast<std::string *>(user);
	if (size && count > std::numeric_limits<size_t>::max() / size)
		return 0;
	const size_t bytes = size * count;
	if (bytes > limit - response.size())
		return 0;
	try {
		response.append(static_cast<char *>(data), bytes);
	} catch (...) {
		return 0;
	}
	return bytes;
}
int cancelled_request(void *user, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	const auto token = static_cast<std::atomic<bool> *>(user);
	return token && token->load() ? 1 : 0;
}
} // namespace

GoogleTranslator::GoogleTranslator(const std::string &api_key,
				   std::shared_ptr<std::atomic<bool>> cancelled, long timeout_ms)
	: api_key_(api_key),
	  curl_helper_(std::make_unique<CurlHelper>()),
	  cancelled_(std::move(cancelled)),
	  timeout_ms_(std::clamp(timeout_ms, 1L, 5000L))
{
}

GoogleTranslator::~GoogleTranslator() = default;

std::string GoogleTranslator::translate(const std::string &text, const std::string &target_lang,
					const std::string &source_lang)
{
	// Reuse connections/DNS within the owned worker, without sharing handles across threads.
	thread_local std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(),
									      curl_easy_cleanup);

	if (!curl) {
		throw TranslationError("Failed to initialize CURL session");
	}
	curl_easy_reset(curl.get());
	if (cancelled_ && cancelled_->load())
		throw TranslationError("Google request cancelled");

	std::string response;

	try {
		// Captions are plain text, not an HTML document.
		std::stringstream url;
		url << "https://translation.googleapis.com/language/translate/v2"
		    << "?key=" << api_key_
		    << "&format=text&q=" << CurlHelper::urlEncode(curl.get(), text)
		    << "&target=" << sanitize_language_code(target_lang);

		if (source_lang != "auto") {
			url << "&source=" << sanitize_language_code(source_lang);
		}

		curl_easy_setopt(curl.get(), CURLOPT_URL, url.str().c_str());
		curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, bounded_response);
		curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
		curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS,
				 std::min(2000L, timeout_ms_));
		curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, timeout_ms_);
		curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, cancelled_request);
		curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, cancelled_.get());

		CURLcode res = curl_easy_perform(curl.get());

		if (res != CURLE_OK) {
			throw TranslationError(std::string("CURL request failed: ") +
					       curl_easy_strerror(res));
		}
		long status = 0;
		curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
		if (status < 200 || status >= 300)
			throw TranslationError("Google HTTP status " + std::to_string(status));

		return parseResponse(response);

	} catch (const json::exception &) {
		throw TranslationError("Invalid Google translation response");
	}
}

std::string GoogleTranslator::parseResponse(const std::string &response_str)
{
	json response = json::parse(response_str);

	if (response.contains("error")) {
		throw TranslationError("Google translation API returned an error");
	}

	// Decode one entity layer without interpreting caption text as HTML.
	// Unknown entities, literal markup, whitespace, and UTF-8 are preserved.
	const std::string translated =
		response["data"]["translations"][0]["translatedText"].get<std::string>();
	return localvocal::decode_html_entities(translated);
}
