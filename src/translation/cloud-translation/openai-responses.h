#pragma once

#include "ITranslator.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <string>

namespace localvocal {

struct OpenAITokens {
	uint64_t input = 0;
	uint64_t output = 0;
	uint64_t cached = 0;
};

inline uint64_t token_count(const nlohmann::json &object, const char *key)
{
	if (!object.is_object() || !object.contains(key) ||
	    (!object[key].is_number_unsigned() && !object[key].is_number_integer()))
		return 0;
	const auto &value = object[key];
	if (value.is_number_integer() && value.get<int64_t>() < 0)
		return 0;
	return value.get<uint64_t>();
}

inline OpenAITokens response_usage(const nlohmann::json &response)
{
	OpenAITokens tokens;
	if (!response.is_object() || !response.contains("usage") ||
	    !response["usage"].is_object())
		return tokens;
	const auto &usage = response["usage"];
	tokens.input = token_count(usage, "input_tokens");
	tokens.output = token_count(usage, "output_tokens");
	if (usage.contains("input_tokens_details") &&
	    usage["input_tokens_details"].is_object())
		tokens.cached = std::min(tokens.input,
					 token_count(usage["input_tokens_details"], "cached_tokens"));
	return tokens;
}

// Nanodollars (USD * 1e9) allow precise integer accounting at Luna rates.
// This is an estimate for standard text processing, not an account-wide spend cap.
inline uint64_t estimate_nano_usd(const std::string &model, OpenAITokens usage)
{
	const uint64_t in_rate = model == "gpt-6.1-terra" ? 1000 : 100;
	const uint64_t cached_rate = model == "gpt-6.1-terra" ? 50 : 10;
	const uint64_t out_rate = model == "gpt-6.1-terra" ? 5000 : 500;
	const uint64_t uncached = usage.input - std::min(usage.input, usage.cached);
	return uncached * in_rate + usage.cached * cached_rate + usage.output * out_rate;
}

inline std::string response_translation(const nlohmann::json &response)
{
	if (!response.is_object() || !response.contains("status") ||
	    !response["status"].is_string() ||
	    response["status"].get<std::string>() != "completed")
		throw TranslationError("OpenAI response did not complete");
	if (!response.contains("output") || !response["output"].is_array())
		throw TranslationError("OpenAI response has no output array");

	std::string translation;
	for (const auto &item : response["output"]) {
		if (!item.is_object() || !item.contains("type") ||
		    !item["type"].is_string() ||
		    item["type"].get<std::string>() != "message")
			continue; // Ignore reasoning or other non-message items.
		if (item.contains("role") && item["role"].is_string() &&
		    item["role"].get<std::string>() != "assistant")
			continue;
		if (!item.contains("content") || !item["content"].is_array())
			continue;
		for (const auto &part : item["content"]) {
			if (!part.is_object() || !part.contains("type") || !part["type"].is_string())
				continue;
			const auto type = part["type"].get<std::string>();
			if (type == "refusal")
				throw TranslationError("OpenAI declined the translation");
			if (type == "output_text") {
				if (!part.contains("text") || !part["text"].is_string())
					throw TranslationError("Invalid OpenAI text output");
				if (!translation.empty())
					throw TranslationError("Unexpected multiple OpenAI text outputs");
				translation = part["text"].get<std::string>();
			}
		}
	}
	if (translation.empty() || translation.size() > 4096 ||
	    translation.find_first_not_of(" \t\r\n") == std::string::npos)
		throw TranslationError("OpenAI returned empty or oversized translation");
	return translation;
}

} // namespace localvocal
