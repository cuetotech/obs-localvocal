#pragma once

#include <string>
#include <atomic>
#include <memory>
#include <vector>

struct CloudTranslatorConfig {
	std::string provider;
	std::string access_key;         // Main API key/Client ID
	std::string secret_key;         // Secret key/Client secret
	std::string region;             // For AWS / Azure
	std::string model;              // For Claude
	bool free = true;               // For Deepl
	std::string endpoint;           // For Custom API
	std::string body;               // For Custom API
	std::string response_json_path; // For Custom API
	std::shared_ptr<std::atomic<bool>> cancelled;
	long timeout_ms = 5000;
};

inline std::string cloud_cache_key(const CloudTranslatorConfig &config, const std::string &source,
				   const std::string &target, const std::string &text)
{
	std::string key;
	for (const auto &value : std::vector<std::string>{
		     config.provider, config.access_key, config.secret_key, config.region,
		     config.model, config.free ? "free" : "paid", config.endpoint, config.body,
		     config.response_json_path, source, target, text})
		key += std::to_string(value.size()) + ":" + value;
	return key; // In-memory identity only. Never log this value (it includes credentials).
}

std::string translate_cloud(const CloudTranslatorConfig &config, const std::string &text,
			    const std::string &target_lang, const std::string &source_lang);
