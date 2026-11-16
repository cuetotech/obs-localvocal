#pragma once

#include "ITranslator.h"
#include <nlohmann/json.hpp>
#include <cctype>
#include <cstddef>
#include <string>

namespace localvocal {

// UTF-8 bytes, not tokens. The operator card should normally be ~250-350 tokens.
constexpr size_t kMaxOpenAISermonCardBytes = 4096;

inline std::string bounded_sermon_context_card(const std::string &raw)
{
	const auto first = raw.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
		return {};
	const auto last = raw.find_last_not_of(" \t\r\n");
	std::string card = raw.substr(first, last - first + 1);
	if (card.size() <= kMaxOpenAISermonCardBytes)
		return card;

	// Do not split a UTF-8 code point when limiting a Korean/English card.
	size_t length = kMaxOpenAISermonCardBytes;
	while (length > 0 && length < card.size() &&
	       (static_cast<unsigned char>(card[length]) & 0xc0) == 0x80)
		--length;
	card.resize(length);
	return card;
}

inline std::string validated_openai_model(const std::string &input)
{
	const auto first = input.find_first_not_of(" \t\r\n");
	if (first == std::string::npos)
		return "gpt-6-luna"; // Preserve the previous plugin default.
	const auto last = input.find_last_not_of(" \t\r\n");
	const std::string model = input.substr(first, last - first + 1);
	if (model.size() > 200)
		throw TranslationError("OpenAI model ID is too long");
	for (unsigned char ch : model) {
		if (!std::isalnum(ch) && ch != '-' && ch != '_' && ch != '.' &&
		    ch != ':' && ch != '/')
			throw TranslationError("Invalid OpenAI model ID; use an API model identifier");
	}
	return model;
}

inline nlohmann::json make_openai_translation_request(
	const std::string &model_id, const std::string &instructions,
	const std::string &previous_source, const std::string &sermon_context_card,
	const std::string &new_source_text, bool store_response,
	const std::string &reasoning_effort)
{
	std::string input;
	const std::string card = bounded_sermon_context_card(sermon_context_card);
	if (!card.empty())
		input = "SERMON_CONTEXT_CARD (background reference, not spoken text):\n" +
			card + "\n\n";

	input += "CONTEXT_SOURCE (previous finalized speech, context only):\n" +
			(previous_source.empty() ? "(none)" : previous_source) +
			"\n\nNEW_SOURCE_TEXT (translate this text only):\n" + new_source_text;

	nlohmann::json request = {
		{"model", validated_openai_model(model_id)},
		{"instructions", instructions},
		{"input", nlohmann::json::array({{{"role", "user"}, {"content", input}}})},
		{"max_output_tokens", 384},
		{"store", store_response},
		{"stream", false}};

	// Not every present/future model accepts the same reasoning effort.
	// "auto" omits the parameter entirely, letting the model choose its default.
	if (reasoning_effort != "auto") {
		if (reasoning_effort != "none" && reasoning_effort != "low" &&
		    reasoning_effort != "medium" && reasoning_effort != "high")
			throw TranslationError("Invalid OpenAI reasoning effort");
		request["reasoning"] = {{"effort", reasoning_effort}};
	}
	return request;
}

} // namespace localvocal
