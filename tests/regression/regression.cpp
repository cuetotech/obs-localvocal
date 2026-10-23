#include "../../src/translation/cloud-translation/html-entities.h"
#include "../../src/translation/translation-safety.h"
#include <iostream>
#include <utility>

int main()
{
	const std::vector<std::pair<std::string, std::string>> cases = {
		{"", ""}, {"plain text", "plain text"}, {"I'm", "I'm"},
		{"I&#39;m", "I'm"}, {"I&#x27;m", "I'm"}, {"I&#X27;m", "I'm"},
		{"&quot;yes&quot; &amp; no", "\"yes\" & no"},
		{"&apos;&lt;&gt;", "'<>"}, {"&nbsp;", "\xc2\xa0"},
		{"&amp;#39;", "&#39;"}, {"&amp;amp;", "&amp;"},
		{"<b>text</b> &amp; <x>", "<b>text</b> & <x>"},
		{"a  b\n\tc", "a  b\n\tc"},
		{"\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4", "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4"},
		{"&#54620;", "\xed\x95\x9c"}, {"&#x1f600;", "\xf0\x9f\x98\x80"},
		{"&unknown;", "&unknown;"}, {"&#39", "&#39"}, {"&", "&"},
		{"&#;", "&#;"}, {"&#x;", "&#x;"}, {"&#xGG;", "&#xGG;"},
		{"&#-1;", "&#-1;"}, {"&#0;", "&#0;"},
		{"&#xD800;", "&#xD800;"}, {"&#1114112;", "&#1114112;"},
		{"&#999999999999999999999999999999999999999999;", "&#999999999999999999999999999999999999999999;"},
		{"&&amp;", "&&"}, {"&broken&amp;", "&broken&"}
	};
	int failures = 0;
	for (size_t i = 0; i < cases.size(); ++i) {
		if (localvocal::decode_html_entities(cases[i].first) != cases[i].second) {
			std::cerr << "Entity case failed: " << i << '\n';
			++failures;
		}
	}
	std::vector<std::string> output;
	auto check = [&](bool condition, const char *name) {
		if (!condition) { std::cerr << name << '\n'; ++failures; }
	};
	check(localvocal::strip_translation_prefix({"en", "hello"}, {"en"}, output) &&
	      output == std::vector<std::string>{"hello"}, "normal prefix");
	check(!localvocal::strip_translation_prefix({"en"}, {"en", "old"}, output) &&
	      output.empty(), "truncated prefix");
	check(!localvocal::strip_translation_prefix({}, {"en"}, output), "empty hypothesis");
	check(!localvocal::strip_translation_prefix({"ko", "hello"}, {"en"}, output), "wrong prefix");
	check(localvocal::strip_translation_prefix({"hello"}, {}, output) &&
	      output == std::vector<std::string>{"hello"}, "no prefix");
	check(localvocal::strip_translation_prefix({"en"}, {"en"}, output) && output.empty(), "prefix only");
	std::cout << cases.size() + 6 << " cases; " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
