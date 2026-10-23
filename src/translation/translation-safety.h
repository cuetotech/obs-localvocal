#pragma once

#include <cstddef>
#include <algorithm>
#include <string>
#include <vector>

namespace localvocal {
// CT2 includes a hard target prefix in its returned hypothesis. Check it before
// advancing an iterator; truncated/mismatched output must fail, not corrupt memory.
inline bool strip_translation_prefix(const std::vector<std::string> &tokens,
				     const std::vector<std::string> &prefix,
				     std::vector<std::string> &output)
{
	output.clear();
	if (tokens.size() < prefix.size() ||
	    !std::equal(prefix.begin(), prefix.end(), tokens.begin()))
		return false;
	output.assign(tokens.begin() + static_cast<std::ptrdiff_t>(prefix.size()), tokens.end());
	return true;
}
} // namespace localvocal
