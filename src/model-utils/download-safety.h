#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <map>
#include <thread>
#include <stdexcept>

namespace localvocal {
using DownloadCancel = std::shared_ptr<std::atomic<bool>>;
inline void check_download_cancel(const DownloadCancel &cancel)
{
	if (cancel && cancel->load())
		throw std::runtime_error("Model download cancelled");
}

// Filter destruction closes this gate before releasing any filter state. Network
// transfers never hold it; the completion/model-load lease uses the owner.
template<class Owner> class ModelCallbackLifetime {
public:
	explicit ModelCallbackLifetime(Owner *owner) : owner_(owner) {}
	DownloadCancel begin(size_t channel)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (sessions_.at(channel))
			sessions_[channel]->store(true);
		return sessions_[channel] = std::make_shared<std::atomic<bool>>(owner_ == nullptr);
	}
	void cancel(size_t channel)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (sessions_.at(channel))
			sessions_[channel]->store(true);
	}
	void close()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		owner_ = nullptr;
		for (const auto &session : sessions_)
			if (session)
				session->store(true);
	}
	template<class F> bool run(const DownloadCancel &cancel, F action)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!owner_ || !cancel || cancel->load())
			return false;
		action(*owner_);
		return true;
	}

private:
	std::mutex mutex_;
	Owner *owner_;
	std::array<DownloadCancel, 2> sessions_{}; // Whisper and native translation.
};

class DownloadStaging {
public:
	explicit DownloadStaging(const std::filesystem::path &parent)
	{
		static std::atomic<unsigned long long> serial{0};
		for (unsigned attempt = 0; attempt < 16; ++attempt) {
			folder_ = parent / (".localvocal-download-" +
					    std::to_string(std::chrono::steady_clock::now()
								   .time_since_epoch()
								   .count()) +
					    "-" + std::to_string(++serial));
			if (std::filesystem::create_directory(folder_))
				return;
		}
		throw std::runtime_error("Could not create a model staging directory");
	}
	~DownloadStaging()
	{
		std::error_code error;
		std::filesystem::remove_all(folder_, error);
	}
	DownloadStaging(const DownloadStaging &) = delete;
	DownloadStaging &operator=(const DownloadStaging &) = delete;
	std::filesystem::path file() const { return folder_ / "model.part"; }

private:
	std::filesystem::path folder_;
};

// Transaction used by the real downloader and fault-injection tests. A known-bad
// destination is replaced only after the new file is closed and validated.
inline std::shared_ptr<std::mutex> model_file_mutex(const std::filesystem::path &path)
{
	static std::mutex registry_mutex;
	static std::map<std::filesystem::path, std::weak_ptr<std::mutex>> registry;
	std::lock_guard<std::mutex> lock(registry_mutex);
	for (auto it = registry.begin(); it != registry.end();)
		if (it->second.expired())
			it = registry.erase(it);
		else
			++it;
	auto &entry = registry[std::filesystem::absolute(path).lexically_normal()];
	auto mutex = entry.lock();
	if (!mutex) {
		mutex = std::make_shared<std::mutex>();
		entry = mutex;
	}
	return mutex;
}

template<class Validate, class Transfer>
void ensure_model_file(const std::filesystem::path &destination, const DownloadCancel &cancel,
		       Validate validate, Transfer transfer)
{
	auto mutex = model_file_mutex(destination);
	std::unique_lock<std::mutex> lock(*mutex, std::defer_lock);
	while (!lock.try_lock()) {
		check_download_cancel(cancel);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	check_download_cancel(cancel);
	if (std::filesystem::exists(destination) && validate(destination))
		return;
	DownloadStaging staging(destination.parent_path());
	transfer(staging.file());
	check_download_cancel(cancel);
	if (!validate(staging.file()))
		throw std::runtime_error("Downloaded model failed checksum validation");
	check_download_cancel(cancel);
	if (std::filesystem::exists(destination)) {
		if (validate(destination))
			return;                       // Another filter completed the same file.
		std::filesystem::remove(destination); // Only a known-invalid file is removed.
	}
	std::error_code error;
	std::filesystem::rename(staging.file(), destination, error);
	if (error && !(std::filesystem::exists(destination) && validate(destination)))
		throw std::runtime_error("Could not commit verified model file: " +
					 error.message());
}
} // namespace localvocal
