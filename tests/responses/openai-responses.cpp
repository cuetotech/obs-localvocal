#include <cctype>
#include "../../src/translation/cloud-translation/openai-responses.h"

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
	if (failures)
		return EXIT_FAILURE;
	std::cout << "Responses API parser contract tests passed\n";
	return EXIT_SUCCESS;
}
