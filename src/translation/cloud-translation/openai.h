#pragma once

#include "ITranslator.h"
#include "translation-cloud.h"
#include <string>

class OpenAITranslator : public ITranslator {
public:
	explicit OpenAITranslator(CloudTranslatorConfig config);
	~OpenAITranslator() override = default;

	std::string translate(const std::string &text, const std::string &target_lang,
			      const std::string &source_lang = "auto") override;

private:
	std::string instructions(const std::string &target_lang,
			 const std::string &source_lang) const;
	CloudTranslatorConfig config_;
};
