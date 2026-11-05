#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace localvocal {

// Finalized Korean recognition is the only input. A snapshot is attached to each
// translation request before the current utterance is remembered.
class RollingKoreanContext {
public:
	void clear() { sentences_.clear(); }

	void remember(const std::string &sentence)
	{
		if (sentence.empty() || sentence.size() > 4096)
			return;
		if (!sentences_.empty() && sentences_.back() == sentence)
			return;
		sentences_.push_back(sentence);
		while (sentences_.size() > 4)
			sentences_.pop_front();
	}

	std::string snapshot(size_t max_sentences, size_t max_bytes = 1200) const
	{
		max_sentences = std::min<size_t>(max_sentences, 3);
		std::vector<std::string> selected;
		size_t total = 0;
		for (auto it = sentences_.rbegin(); it != sentences_.rend() &&
						 selected.size() < max_sentences; ++it) {
			const size_t additional = it->size() + (selected.empty() ? 0 : 1);
			// Discard oversized utterances rather than truncate a UTF-8 sequence.
			if (additional > max_bytes || total > max_bytes - additional)
				break;
			selected.push_back(*it);
			total += additional;
		}
		std::string text;
		for (auto it = selected.rbegin(); it != selected.rend(); ++it) {
			if (!text.empty())
				text += "\n";
			text += *it;
		}
		return text;
	}

	size_t size() const { return sentences_.size(); }

private:
	std::deque<std::string> sentences_;
};

} // namespace localvocal
