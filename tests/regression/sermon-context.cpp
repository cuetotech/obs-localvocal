#include "../../src/translation/cloud-translation/sermon-context.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
int failures = 0;

void check(bool value, const char *name)
{
	if (!value) {
		std::cerr << "FAILED: " << name << '\n';
		++failures;
	}
}
} // namespace

int main()
{
	localvocal::RollingKoreanContext context;
	check(context.snapshot(3).empty(), "new context is empty");
	context.remember("다윗 왕은 하나님을 의지했습니다.");
	check(context.snapshot(2) == "다윗 왕은 하나님을 의지했습니다.",
	      "first finalized context retained");
	const std::string before_current = context.snapshot(3);
	context.remember("그래서 하나님께 기도했습니다.");
	check(before_current.find("그래서") == std::string::npos,
	      "request snapshot excludes current sentence");
	check(context.snapshot(3).find("다윗 왕은") == 0,
	      "previous speaker available in chronological order");
	check(context.snapshot(3).find("그래서") != std::string::npos,
	      "new sentence only included in later requests");
	context.remember("그래서 하나님께 기도했습니다.");
	check(context.size() == 2, "duplicated final segment is coalesced");
	context.remember("느헤미야");
	check(context.snapshot(0).empty(), "disabled context excludes old sentences");
	check(context.snapshot(1) == "느헤미야", "one prior sentence");
	check(context.snapshot(2).find("그래서") == 0, "two prior sentences ordered");

	const auto saved = context.snapshot(3, 1000);
	check(saved.find("느헤미야") != std::string::npos,
	      "utf8 sentences preserved without byte truncation");
	check(context.snapshot(3, 5).empty(), "UTF-8 segments not truncated mid-codepoint");

	context.remember("fourth");
	context.remember("fifth");
	context.remember("sixth");
	check(context.size() == 4, "retention limited to four finalized segments");
	check(context.snapshot(3) == "fourth\nfifth\nsixth",
	      "bounded most-recent 3 sentences in chronological order");
	context.clear();
	check(context.snapshot(3).empty(), "reset removes prior sermon context");
	context.remember("");
	context.remember(std::string(4097, 'a'));
	check(context.size() == 0, "empty and oversized sentences ignored");

	if (failures)
		return EXIT_FAILURE;
	std::cout << "Korean sermon context tests passed\n";
	return EXIT_SUCCESS;
}
