#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace localvocal {
// Decode one layer of the entities used by Google's plain-text translation
// responses. Preserve markup, unknown/malformed entities, and original UTF-8.
// This is not an HTML parser and must not be used as an HTML sanitizer.
inline std::string decode_html_entities(std::string_view text)
{
	std::string output;
	output.reserve(text.size());
	for (size_t i = 0; i < text.size();) {
		if (text[i] != '&') {
			output.push_back(text[i++]);
			continue;
		}
		size_t end = i + 1;
		while (end < text.size() && end - i <= 32 && text[end] != ';' &&
		       text[end] != '&')
			++end;
		if (end == text.size() || end - i > 32 || text[end] != ';') {
			output.push_back(text[i++]);
			continue;
		}
		const auto entity = text.substr(i + 1, end - i - 1);
		uint32_t cp = 0;
		bool valid = true;
		if (entity == "amp") cp = '&';
		else if (entity == "lt") cp = '<';
		else if (entity == "gt") cp = '>';
		else if (entity == "quot") cp = '"';
		else if (entity == "apos") cp = '\'';
		else if (entity == "nbsp") cp = 0xa0;
		else if (!entity.empty() && entity[0] == '#') {
			size_t pos = 1;
			uint32_t base = 10;
			if (pos < entity.size() && (entity[pos] == 'x' || entity[pos] == 'X')) {
				base = 16;
				++pos;
			}
			valid = pos < entity.size();
			for (; valid && pos < entity.size(); ++pos) {
				const char c = entity[pos];
				uint32_t digit = 16;
				if (c >= '0' && c <= '9') digit = static_cast<uint32_t>(c - '0');
				else if (c >= 'a' && c <= 'f') digit = static_cast<uint32_t>(c - 'a' + 10);
				else if (c >= 'A' && c <= 'F') digit = static_cast<uint32_t>(c - 'A' + 10);
				if (digit >= base || cp > (0x10ffffu - digit) / base) valid = false;
				else cp = cp * base + digit;
			}
		} else valid = false;
		// Preserve invalid Unicode scalars and NUL instead of inserting them.
		valid = valid && cp != 0 && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff);
		if (!valid) {
			output.append(text.substr(i, end - i + 1));
		} else if (cp <= 0x7f) {
			output.push_back(static_cast<char>(cp));
		} else if (cp <= 0x7ff) {
			output.push_back(static_cast<char>(0xc0 | (cp >> 6)));
			output.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
		} else if (cp <= 0xffff) {
			output.push_back(static_cast<char>(0xe0 | (cp >> 12)));
			output.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
			output.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
		} else {
			output.push_back(static_cast<char>(0xf0 | (cp >> 18)));
			output.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
			output.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
			output.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
		}
		i = end + 1;
	}
	return output;
}
} // namespace localvocal
