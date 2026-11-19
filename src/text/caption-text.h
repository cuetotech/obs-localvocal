#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace localvocal {
inline size_t utf8_sequence_size(std::string_view text, size_t offset)
{
	if (offset >= text.size())
		return 0;
	const auto first = static_cast<unsigned char>(text[offset]);
	size_t length = first < 0x80                     ? 1
			: first >= 0xc2 && first <= 0xdf ? 2
			: first >= 0xe0 && first <= 0xef ? 3
			: first >= 0xf0 && first <= 0xf4 ? 4
							 : 0;
	if (!length || length > text.size() - offset)
		return 0;
	uint32_t scalar = first & (length == 1   ? 0x7f
				   : length == 2 ? 0x1f
				   : length == 3 ? 0x0f
						 : 0x07);
	for (size_t i = 1; i < length; ++i) {
		const auto byte = static_cast<unsigned char>(text[offset + i]);
		if ((byte & 0xc0) != 0x80)
			return 0;
		scalar = (scalar << 6) | (byte & 0x3f);
	}
	if ((length == 2 && scalar < 0x80) || (length == 3 && scalar < 0x800) ||
	    (length == 4 && scalar < 0x10000) || scalar > 0x10ffff ||
	    (scalar >= 0xd800 && scalar <= 0xdfff))
		return 0;
	return length;
}

// Preserve valid UTF-8 first. Apply the historical Windows repair only when the
// complete candidate is valid. Unrepairable bytes become U+FFFD, never speculative reads.
inline std::string repair_windows_utf8(std::string_view text)
{
	std::string output;
	output.reserve(text.size());
	for (size_t i = 0; i < text.size();) {
		if (const auto size = utf8_sequence_size(text, i)) {
			output.append(text.data() + i, size);
			i += size;
			continue;
		}
		std::string candidate(text.substr(i, 4));
		if (candidate.size() >= 2 && static_cast<unsigned char>(candidate[1]) == 0xff)
			candidate[1] = static_cast<char>(0x9f);
		auto length = utf8_sequence_size(candidate, 0);
		if (!length && static_cast<unsigned char>(candidate[0]) >= 0xe0) {
			candidate[0] =
				static_cast<char>(static_cast<unsigned char>(candidate[0]) - 0x20);
			length = utf8_sequence_size(candidate, 0);
		}
		if (length) {
			output.append(candidate.data(), length);
			i += length;
		} else {
			output += "\xef\xbf\xbd";
			++i;
		}
	}
	return output;
}

inline bool caption_space(unsigned char ch)
{
	return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

// Caption punctuation, numbers, names and non-ASCII text are meaningful content.
inline std::string trim_caption_text(std::string_view text)
{
	size_t first = 0, end = text.size();
	while (first < end && caption_space(static_cast<unsigned char>(text[first])))
		++first;
	while (end > first && caption_space(static_cast<unsigned char>(text[end - 1])))
		--end;
	return std::string(text.substr(first, end - first));
}
} // namespace localvocal
