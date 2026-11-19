#include "translation.h"
#include "plugin-support.h"
#include "model-utils/model-find-utils.h"
#include "transcription-filter-data.h"
#include "language_codes.h"
#include "translation-language-utils.h"
#include "translation-safety.h"

#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>
#include <obs-module.h>
#include <chrono>
#include <regex>
#include <stdexcept>

void build_and_enable_translation(struct transcription_filter_data *gf,
				  const std::string &model_file_path)
{
	std::lock_guard<std::mutex> lock(gf->whisper_ctx_mutex);

	gf->translation_ctx.local_model_folder_path = model_file_path;
	if (build_translation_context(gf->translation_ctx) ==
	    OBS_POLYGLOT_TRANSLATION_INIT_SUCCESS) {
		obs_log(LOG_INFO, "Enable translation");
		gf->translate = true;
	} else {
		obs_log(LOG_ERROR, "Failed to load CT2 model");
		gf->translate = false;
	}
}

int build_translation_context(struct translation_context &translation_ctx)
{
	std::string local_model_path = translation_ctx.local_model_folder_path;
	obs_log(LOG_INFO, "Building translation context from '%s'...", local_model_path.c_str());
	std::string local_spm_path = find_file_in_folder_by_regex_expression(
		local_model_path, "(sentencepiece|spm|spiece|source).*?\\.(model|spm)");
	std::string target_spm_path =
		find_file_in_folder_by_regex_expression(local_model_path, "target.*?\\.spm");

	try {
		// Release the old model before allocating its replacement. reset(new ...)
		// would construct the new model first, briefly keeping both in GPU memory.
		translation_ctx.translator.reset();
		translation_ctx.last_input_tokens.clear();
		translation_ctx.last_translation_tokens.clear();

		obs_log(LOG_INFO, "Loading SPM from %s", local_spm_path.c_str());
		translation_ctx.processor.reset(new sentencepiece::SentencePieceProcessor());
		const auto status = translation_ctx.processor->Load(local_spm_path);
		if (!status.ok()) {
			obs_log(LOG_ERROR, "Failed to load SPM: %s", status.ToString().c_str());
			return OBS_POLYGLOT_TRANSLATION_INIT_FAIL;
		}

		if (!target_spm_path.empty()) {
			obs_log(LOG_INFO, "Loading target SPM from %s", target_spm_path.c_str());
			translation_ctx.target_processor.reset(
				new sentencepiece::SentencePieceProcessor());
			const auto target_status =
				translation_ctx.target_processor->Load(target_spm_path);
			if (!target_status.ok()) {
				obs_log(LOG_ERROR, "Failed to load target SPM: %s",
					target_status.ToString().c_str());
				return OBS_POLYGLOT_TRANSLATION_INIT_FAIL;
			}
		} else {
			obs_log(LOG_INFO, "Target SPM not found, using source SPM for target");
			translation_ctx.target_processor.reset();
		}

		translation_ctx.tokenizer = [&translation_ctx](const std::string &text) {
			std::vector<std::string> tokens;
			translation_ctx.processor->Encode(text, &tokens);
			return tokens;
		};
		translation_ctx.detokenizer =
			[&translation_ctx](const std::vector<std::string> &tokens) {
				std::string text;
				if (translation_ctx.target_processor) {
					translation_ctx.target_processor->Decode(tokens, &text);
				} else {
					translation_ctx.processor->Decode(tokens, &text);
				}
				return std::regex_replace(text, std::regex("<unk>"), "UNK");
			};

		obs_log(LOG_INFO, "Loading CT2 model from %s", local_model_path.c_str());

#ifdef POLYGLOT_WITH_CUDA
		ctranslate2::Device device = ctranslate2::Device::CUDA;
		obs_log(LOG_INFO, "CT2 Using CUDA");
#else
		ctranslate2::Device device = ctranslate2::Device::CPU;
		obs_log(LOG_INFO, "CT2 Using CPU");
#endif

		translation_ctx.translator.reset(new ctranslate2::Translator(
			local_model_path, device, ctranslate2::ComputeType::AUTO));
		obs_log(LOG_INFO, "CT2 Model loaded");

		translation_ctx.options.reset(new ctranslate2::TranslationOptions);
		translation_ctx.options->beam_size = 1;
		translation_ctx.options->max_decoding_length = 64;
		translation_ctx.options->repetition_penalty = 2.0f;
		translation_ctx.options->no_repeat_ngram_size = 1;
		translation_ctx.options->max_input_length = 64;
		translation_ctx.options->sampling_temperature = 0.1f;
	} catch (std::exception &e) {
		translation_ctx.translator.reset();
		obs_log(LOG_ERROR, "Failed to load CT2 model: %s", e.what());
		return OBS_POLYGLOT_TRANSLATION_INIT_FAIL;
	}
	return OBS_POLYGLOT_TRANSLATION_INIT_SUCCESS;
}

int translate(struct translation_context &translation_ctx, const std::string &text,
	      const std::string &source_lang, const std::string &target_lang, std::string &result)
{
	const auto start = std::chrono::steady_clock::now();
	result.clear();
	try {
		if (!translation_ctx.translator || !translation_ctx.options)
			throw std::runtime_error("CT2 translation model is not loaded");
		std::vector<ctranslate2::TranslationResult> results;
		std::vector<std::string> target_prefix;
		std::vector<std::string> new_input_tokens;

		if (translation_ctx.input_tokenization_style == INPUT_TOKENIZAION_M2M100) {
			std::vector<std::string> input_tokens = {source_lang, "<s>"};
			if (translation_ctx.add_context > 0) {
				for (const auto &tokens : translation_ctx.last_input_tokens) {
					input_tokens.insert(input_tokens.end(), tokens.begin(), tokens.end());
				}
			}
			new_input_tokens = translation_ctx.tokenizer(text);
			input_tokens.insert(input_tokens.end(), new_input_tokens.begin(), new_input_tokens.end());
			input_tokens.push_back("</s>");
			const std::vector<std::vector<std::string>> batch = {input_tokens};

			target_prefix = {target_lang};
			if (translation_ctx.add_context > 0) {
				for (const auto &tokens : translation_ctx.last_translation_tokens) {
					target_prefix.insert(target_prefix.end(), tokens.begin(), tokens.end());
				}
			}
			const std::vector<std::vector<std::string>> target_prefix_batch = {target_prefix};
			results = translation_ctx.translator->translate_batch(
				batch, target_prefix_batch, *translation_ctx.options);
		} else {
			const auto tokens = translation_ctx.tokenizer(
				"<2" + language_codes_to_whisper[target_lang] + "> " + text);
			const std::vector<std::vector<std::string>> batch = {tokens};
			results = translation_ctx.translator->translate_batch(batch, {}, *translation_ctx.options);
		}

		if (results.empty())
			throw std::runtime_error("CT2 returned no translation result");
		std::vector<std::string> translation_tokens;
		if (!localvocal::strip_translation_prefix(results[0].output(), target_prefix, translation_tokens))
			throw std::runtime_error("CT2 returned a truncated or mismatched target prefix");

		const std::string result_ = translation_ctx.detokenizer(translation_tokens);
		result = remove_start_punctuation(result_);

		// Commit paired context only after successful inference and decoding.
		if (translation_ctx.input_tokenization_style == INPUT_TOKENIZAION_M2M100)
			translation_ctx.last_input_tokens.push_back(new_input_tokens);
		translation_ctx.last_translation_tokens.push_back(translation_tokens);
		const size_t context_limit = translation_ctx.add_context > 0
			? static_cast<size_t>(translation_ctx.add_context) : 0;
		while (translation_ctx.last_input_tokens.size() > context_limit)
			translation_ctx.last_input_tokens.pop_front();
		while (translation_ctx.last_translation_tokens.size() > context_limit)
			translation_ctx.last_translation_tokens.pop_front();

		// Stage timing only: this excludes ASR, sentence buffering, and rendering.
		const double elapsed_ms = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - start).count();
		obs_log(LOG_INFO, "CT2 translation: %.1f ms, input_bytes=%zu, output_bytes=%zu",
			elapsed_ms, text.size(), result.size());
	} catch (std::exception &e) {
		result.clear();
		obs_log(LOG_ERROR, "CT2 translation failed: %s", e.what());
		return OBS_POLYGLOT_TRANSLATION_FAIL;
	}
	return OBS_POLYGLOT_TRANSLATION_SUCCESS;
}
