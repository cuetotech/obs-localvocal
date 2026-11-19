#include <cctype>
#include "../../src/translation/cloud-translation/openai-responses.h"
#include "../../src/translation/cloud-translation/openai-request.h"
#include "../../src/translation/cloud-translation/openai-model-catalog.h"

#include <cstdlib>
#include <iostream>
#include <string>

using nlohmann::json;

namespace {
int failures = 0;
void check(bool ok, const char *label)
{
	if (!ok) {
		std::cerr << "FAIL: " << label << "\n";
		++failures;
	}
}

bool rejected(const json &object)
{
	try {
		(void)localvocal::response_translation(object);
		return false;
	} catch (const TranslationError &) {
		return true;
	}
}
}

int main()
{
	json good = {
		{"status", "completed"},
		{"output",
		 json::array({
			 {{"type", "reasoning"}, {"summary", json::array()}},
			 {{"type", "message"},
			  {"status", "completed"},
			  {"role", "assistant"},
			  {"content", json::array({{{"type", "output_text"},
						    {"text", "So he prayed to God."}}})}}
		 })},
		{"usage", {{"input_tokens", 120}, {"output_tokens", 17},
			  {"input_tokens_details", {{"cached_tokens", 20}}}}}};

	check(localvocal::response_translation(good) == "So he prayed to God.",
	      "extract message after reasoning item");
	const auto usage = localvocal::response_usage(good);
	check(usage.input == 120 && usage.output == 17 && usage.cached == 20,
	      "read token usage and cached details");
	check(localvocal::estimate_nano_usd("gpt-6-luna", usage) ==
		      100 * 100 + 20 * 10 + 17 * 500,
	      "Luna USD token rate");
	check(localvocal::estimate_nano_usd("gpt-6.1-terra", usage) ==
		      100 * 1000 + 20 * 50 + 17 * 5000,
	      "Terra USD token rate");

	json incomplete = good;
	incomplete["status"] = "incomplete";
	check(rejected(incomplete), "reject incomplete response");
	json refused = good;
	refused["output"][1]["content"] =
		json::array({{{"type", "refusal"}, {"refusal", "Not available"}}});
	check(rejected(refused), "reject refusal");
	json empty = good;
	empty["output"][1]["content"][0]["text"] = "   ";
	check(rejected(empty), "reject empty translation");
	json multi = good;
	multi["output"][1]["content"].push_back({{"type", "output_text"}, {"text", "again"}});
	check(rejected(multi), "reject multiple text candidates");
	json wrong = good;
	wrong["output"] = json::array();
	check(rejected(wrong), "reject missing translation");
	json huge = good;
	huge["output"][1]["content"][0]["text"] = std::string(4097, 'x');
	check(rejected(huge), "reject giant translation");
	check(localvocal::response_usage(json::object()).input == 0,
	      "missing usage does not crash");

	// Newly configured model IDs must not be silently substituted with Luna.
	// Operators control the dropdown using JSON. Labels must never be API IDs.
	const auto seeded_models = localvocal::parse_openai_model_catalog(
		json::parse(localvocal::initial_openai_model_catalog_json()));
	check(seeded_models.size() == 2 && seeded_models[0].id == "gpt-6-luna" &&
		      seeded_models[1].id == "gpt-6.1-terra",
	      "generated JSON loads the shipped catalog");
	const auto models = localvocal::parse_openai_model_catalog(json::parse(
		R"({"version":1,"models":[{"id":"gpt-6-luna","label":"Affordable model"},{"id":"gpt-example-2027","label":"English translation test"}]})"));
	check(models.size() == 2 && models[0].label == "Affordable model" &&
		      models[0].id == "gpt-6-luna" &&
		      models[1].id == "gpt-example-2027",
	      "JSON labels stay distinct from canonical API IDs and accept additional models");
	bool bad_catalog = false;
	try {
		(void)localvocal::parse_openai_model_catalog(json::parse(
			R"({"version":1,"models":[{"id":"gpt-6-luna","label":"Valid"},{"id":"gpt-6-luna","label":"Duplicate"}]})"));
	} catch (const std::exception &) {
		bad_catalog = true;
	}
	check(bad_catalog, "duplicate model IDs are rejected");
	bad_catalog = false;
	try {
		(void)localvocal::parse_openai_model_catalog(json::parse(
			R"({"version":1,"models":[{"id":"friendly model label","label":"Invalid"}]})"));
	} catch (const std::exception &) {
		bad_catalog = true;
	}
	check(bad_catalog, "labels and invalid IDs cannot be submitted as model IDs");
	bad_catalog = false;
	try {
		(void)localvocal::parse_openai_model_catalog(json::parse(
			R"({"version":1,"models":[]})"));
	} catch (const std::exception &) {
		bad_catalog = true;
	}
	check(bad_catalog, "empty model list is rejected");

	check(localvocal::validated_openai_model("  gpt-test-2027 ") == "gpt-test-2027",
	      "editable model ID is preserved");
	check(localvocal::validated_openai_model(" \n") == "gpt-6-luna",
	      "empty model keeps previous default");
	bool invalid_id_rejected = false;
	try {
		(void)localvocal::validated_openai_model("model id with spaces");
	} catch (const TranslationError &) {
		invalid_id_rejected = true;
	}
	check(invalid_id_rejected, "malformed model IDs fail closed");
	check(localvocal::supports_openai_cost_estimate("gpt-6-luna") &&
	      localvocal::supports_openai_cost_estimate("gpt-6.1-terra"),
	      "known models retain estimated-pricing contract");
	check(!localvocal::supports_openai_cost_estimate("future-unknown-model") &&
	      localvocal::estimate_nano_usd("future-unknown-model", usage) == 0,
	      "future models are not mislabeled with Luna prices");

	const auto request = localvocal::make_openai_translation_request(
		"gpt-new-model", "Translate only the new segment.", "Previously spoken.",
		"본문: 사도행전. 바울 — Paul.", "안녕하세요.", false, "auto");
	check(request["model"] == "gpt-new-model" &&
	      request["store"] == false && request["stream"] == false &&
	      !request.contains("reasoning"),
	      "custom model + private default + no unsupported reasoning field");
	const std::string request_input = request["input"][0]["content"].get<std::string>();
	check(request_input.find("SERMON_CONTEXT_CARD") != std::string::npos &&
	      request_input.find("바울 — Paul.") != std::string::npos &&
	      request_input.find("CONTEXT_SOURCE") < request_input.find("NEW_SOURCE_TEXT") &&
	      request_input.find("NEW_SOURCE_TEXT") < request_input.find("안녕하세요."),
	      "card and previous context precede new Korean speech");
	const auto stored_request = localvocal::make_openai_translation_request(
		"gpt-6-luna", "Translate", "", "", "예수", true, "none");
	check(stored_request["store"] == true &&
	      stored_request["reasoning"]["effort"] == "none" &&
	      stored_request["input"][0]["content"].get<std::string>().find(
		      "SERMON_CONTEXT_CARD") == std::string::npos,
	      "store opt-in and no-card compatibility");
	const auto trimmed_request = localvocal::make_openai_translation_request(
		"gpt-6.1-terra", "Translate", "", "  CARD  ", "test", false, "low");
	check(trimmed_request["input"][0]["content"].get<std::string>().find(
		      "CARD") != std::string::npos &&
	      trimmed_request["reasoning"]["effort"] == "low",
	      "bounded card and explicit effort");
	const std::string too_long = std::string(4095, 'x') + "예";
	const std::string bounded = localvocal::bounded_sermon_context_card(too_long);
	check(bounded.size() == 4095 && bounded == std::string(4095, 'x'),
	      "UTF-8 card cap never slices a Korean code point");
	bool bad_effort = false;
	try {
		(void)localvocal::make_openai_translation_request(
			"gpt-test", "Translate", "", "", "speech", false, "extreme");
	} catch (const TranslationError &) {
		bad_effort = true;
	}
	check(bad_effort, "invalid reasoning values fail closed");
	if (failures)
		return EXIT_FAILURE;
	std::cout << "Responses API parser contract tests passed\n";
	return EXIT_SUCCESS;
}
