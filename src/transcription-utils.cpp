#include "transcription-utils.h"
#include "text/caption-text.h"
#include <sstream>

std::string fix_utf8(const std::string &str)
{
#ifdef _WIN32
	return localvocal::repair_windows_utf8(str);
#else
	return str;
#endif
}

// Historical name retained for callers; preserve punctuation and numbers.
std::string remove_leading_trailing_nonalpha(const std::string &str)
{
	return localvocal::trim_caption_text(str);
}

std::vector<std::string> split(const std::string &string, char delimiter)
{
	std::vector<std::string> tokens;
	std::string token;
	std::istringstream tokenStream(string);
	while (std::getline(tokenStream, token, delimiter)) {
		if (!token.empty()) {
			tokens.push_back(token);
		}
	}
	return tokens;
}

std::vector<std::string> split_words(const std::string &str_copy)
{
	std::vector<std::string> words;
	std::string word;
	for (char c : str_copy) {
		if (std::isspace(static_cast<unsigned char>(c))) {
			if (!word.empty()) {
				words.push_back(word);
				word.clear();
			}
		} else {
			word += c;
		}
	}
	if (!word.empty()) {
		words.push_back(word);
	}
	return words;
}
