#pragma once

#include "caption-layout.h"
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace localvocal {
// One translator preserves submission order. Presentation has its own thread so reading
// time never holds up inference/network work. Neither worker needs an OBS filter pointer.
template<class Context> class CloudCaptionPipeline {
public:
	using Clock = std::chrono::steady_clock;
	using Cancel = std::shared_ptr<std::atomic<bool>>;
	using Publish = std::function<void(const std::string &)>;
	struct Request {
		std::string cache_key;
		std::function<std::string(Cancel, long)> translate;
		Publish publish;
		Context context;
		bool final = true;
	};
	struct Completion {
		uint64_t sequence;
		Context context;
		std::string text;
		std::chrono::milliseconds queue_time, translation_time;
	};
	struct Stats {
		uint64_t submitted = 0, translated = 0, cached = 0, failed = 0, expired = 0,
			 rejected = 0, archive_overflow = 0;
		size_t pending = 0, ready = 0;
	};

	CloudCaptionPipeline()
	{
		worker_ = std::thread([this] { translate_loop(); });
		try {
			presenter_ = std::thread([this] { present_loop(); });
		} catch (...) {
			{
				std::lock_guard<std::mutex> lock(mutex_);
				stopping_ = true;
			}
			cv_.notify_all();
			worker_.join();
			throw;
		}
	}
	~CloudCaptionPipeline() { stop(); }
	CloudCaptionPipeline(const CloudCaptionPipeline &) = delete;
	CloudCaptionPipeline &operator=(const CloudCaptionPipeline &) = delete;

	void configure(CaptionOptions options)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		invalidate_locked();
		options.capacity = std::max<size_t>(1, options.capacity);
		options_ = options;
	}
	void reset()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		invalidate_locked();
	}
	void stop()
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (stopping_)
				return;
			stopping_ = true;
			invalidate_locked();
		}
		cv_.notify_all();
		if (worker_.joinable())
			worker_.join();
		if (presenter_.joinable())
			presenter_.join();
	}
	bool submit(Request request)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (stopping_ || pending_.size() >= options_.capacity) {
			++stats_.rejected;
			return false;
		}
		// Only pending partials are coalesced. Never discard a final as a duplicate phrase.
		if (!request.final) {
			for (auto it = pending_.begin(); it != pending_.end();) {
				if (!it->request.final)
					it = pending_.erase(it);
				else
					++it;
			}
		}
		pending_.push_back(
			{++next_sequence_, generation_, Clock::now(), std::move(request)});
		++stats_.submitted;
		cv_.notify_all();
		return true;
	}
	std::vector<Completion> take_completed()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		std::vector<Completion> result;
		while (!completed_.empty()) {
			result.push_back(std::move(completed_.front()));
			completed_.pop_front();
		}
		return result;
	}
	Stats stats() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		auto result = stats_;
		result.pending = pending_.size();
		result.ready = ready_.size();
		return result;
	}

private:
	struct Work {
		uint64_t sequence, generation;
		Clock::time_point submitted;
		Request request;
	};
	struct Ready {
		uint64_t generation;
		Clock::time_point submitted;
		std::vector<std::string> pages;
		Publish publish;
	};
	void invalidate_locked()
	{
		cancel_->store(true);
		cancel_ = std::make_shared<std::atomic<bool>>(false);
		++generation_;
		pending_.clear();
		ready_.clear();
		completed_.clear();
		last_key_.clear();
		last_result_.clear();
		if (clear_) {
			try {
				clear_("");
			} catch (...) {
				++stats_.failed;
			}
			clear_ = {};
		}
		cv_.notify_all();
	}
	void translate_loop()
	{
		std::unique_lock<std::mutex> lock(mutex_);
		while (!stopping_) {
			cv_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
			if (stopping_)
				break;
			auto work = std::move(pending_.front());
			pending_.pop_front();
			const auto started = Clock::now();
			const auto options = options_;
			const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
				started - work.submitted);
			if (age >= options.max_age) {
				++stats_.expired;
				continue;
			}
			auto cancel = cancel_;
			std::string result;
			if (!work.request.cache_key.empty() &&
			    work.request.cache_key == last_key_) {
				result = last_result_;
				++stats_.cached;
			} else {
				lock.unlock();
				try {
					result = work.request.translate(
						cancel,
						static_cast<long>((options.max_age - age).count()));
				} catch (
					...) { /* failed sequence is terminal; next request must progress */
				}
				lock.lock();
			}
			if (stopping_ || work.generation != generation_)
				continue;
			if (result.empty()) {
				++stats_.failed;
				continue;
			}
			// Commit the key/result together only after success. Reset invalidates both.
			last_key_ = work.request.cache_key;
			last_result_ = result;
			++stats_.translated;
			if (work.request.final) {
				if (completed_.size() == options.capacity) {
					++stats_.archive_overflow;
				} else {
					completed_.push_back({work.sequence,
							      std::move(work.request.context),
							      result, age,
							      std::chrono::duration_cast<
								      std::chrono::milliseconds>(
								      Clock::now() - started)});
				}
			}
			if (Clock::now() - work.submitted >= options.max_age ||
			    ready_.size() == options.capacity) {
				++stats_.expired;
			} else {
				auto pages = options.readable
						     ? caption_pages(result, options.line_chars)
						     : std::vector<std::string>{result};
				ready_.push_back({work.generation, work.submitted, std::move(pages),
						  std::move(work.request.publish)});
			}
			cv_.notify_all();
		}
	}
	void present_loop()
	{
		std::unique_lock<std::mutex> lock(mutex_);
		while (!stopping_) {
			cv_.wait(lock, [this] { return stopping_ || !ready_.empty(); });
			if (stopping_)
				break;
			auto ready = std::move(ready_.front());
			ready_.pop_front();
			const auto options = options_;
			for (const auto &page : ready.pages) {
				if (stopping_ || ready.generation != generation_)
					break;
				if (Clock::now() - ready.submitted >= options.max_age) {
					++stats_.expired;
					break;
				}
				// Publish/clear are serialized with reset. These callbacks must not re-enter this object.
				try {
					ready.publish(page);
				} catch (...) {
					++stats_.failed;
					break;
				}
				clear_ = ready.publish;
				const auto until = Clock::now() +
						   (options.readable ? caption_dwell(page, options)
								     : options.max_dwell);
				cv_.wait_until(lock, until, [this, &ready, &options] {
					return stopping_ || ready.generation != generation_ ||
					       (!options.readable && !ready_.empty());
				});
			}
			if (ready.generation == generation_ && clear_ &&
			    (options.readable || ready_.empty())) {
				try {
					clear_("");
				} catch (...) {
					++stats_.failed;
				}
				clear_ = {};
			}
		}
	}

	mutable std::mutex mutex_;
	std::condition_variable cv_;
	bool stopping_ = false;
	uint64_t generation_ = 0, next_sequence_ = 0;
	Cancel cancel_ = std::make_shared<std::atomic<bool>>(false);
	CaptionOptions options_;
	Stats stats_;
	std::deque<Work> pending_;
	std::deque<Ready> ready_;
	std::deque<Completion> completed_;
	std::string last_key_, last_result_;
	Publish clear_;
	std::thread worker_, presenter_;
};
} // namespace localvocal
