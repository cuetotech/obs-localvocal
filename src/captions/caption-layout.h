#pragma once

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace localvocal {
struct CaptionOptions {
	bool readable = false;
	size_t line_chars = 42;
	unsigned chars_per_second = 16;
	std::chrono::milliseconds min_dwell{3000};
	std::chrono::milliseconds max_dwell{6000};
	std::chrono::milliseconds max_age{10000};
	size_t capacity = 8;
};

inline size_t visible_characters(const std::string &text)
{
	size_t count = 0;
	for (unsigned char c : text)
		if ((c & 0xc0) != 0x80 && c != '\n' && c != '\r')
			++count;
	return count;
}

// Never split inside a UTF-8 sequence. Prefer whitespace; preserve every non-space byte.
// Counting is by Unicode scalar, not terminal bytes or UTF-16 code units.
inline std::vector<std::string> caption_pages(const std::string &text, size_t width)
{
	width = std::max<size_t>(1, width);
	std::vector<std::string> lines;
	size_t begin = 0;
	while (begin < text.size()) {
		while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\n' ||
					       text[begin] == '\r' || text[begin] == '\t'))
			++begin;
		if (begin == text.size())
			break;
		size_t end = begin, space = std::string::npos, count = 0;
		while (end < text.size() && count < width && text[end] != '\n') {
			if (text[end] == ' ' || text[end] == '\t')
				space = end;
			++end;
			while (end < text.size() &&
			       (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
				++end;
			++count;
		}
		if (end < text.size() && text[end] != '\n' && text[end] != ' ' &&
		    space != std::string::npos)
			end = space;
		std::string line = text.substr(begin, end - begin);
		while (!line.empty() &&
		       (line.back() == ' ' || line.back() == '\r' || line.back() == '\t'))
			line.pop_back();
		if (!line.empty())
			lines.push_back(std::move(line));
		begin = end;
	}
	std::vector<std::string> pages;
	for (size_t i = 0; i < lines.size(); i += 2)
		pages.push_back(lines[i] + (i + 1 < lines.size() ? "\n" + lines[i + 1] : ""));
	return pages;
}

inline std::chrono::milliseconds caption_dwell(const std::string &page,
					       const CaptionOptions &options)
{
	const auto reading = std::chrono::milliseconds(
		(visible_characters(page) * 1000 + std::max(1u, options.chars_per_second) - 1) /
		std::max(1u, options.chars_per_second));
	return std::max(options.min_dwell,
			std::min(std::max(options.min_dwell, options.max_dwell), reading));
}

inline bool backend_requires_reload(int old_device, int new_device, bool old_flash, bool new_flash)
{
	return old_device != new_device || old_flash != new_flash;
}
} // namespace localvocal
