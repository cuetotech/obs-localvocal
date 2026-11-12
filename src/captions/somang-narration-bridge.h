#pragma once

// EXPERIMENTAL / opt-in only. No change to production captions when not configured.
// Exports completed, unpaginated translations to a localhost narration sidecar.
#include <obs.h>
#include <curl/curl.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace somang_narration {

class Bridge {
public:
    static Bridge &instance()
    {
        static Bridge bridge;
        return bridge;
    }

    void translated(const std::string &source, uint64_t sequence, const std::string &text)
    {
        if (!enabled_for(source) || text.empty())
            return;
        obs_data_t *data = obs_data_create();
        obs_data_set_string(data, "source", source.c_str());
        obs_data_set_string(data, "run", run_id_.c_str());
        obs_data_set_int(data, "seq", static_cast<long long>(sequence));
        obs_data_set_int(data, "produced_unix_ms", unix_ms());
        obs_data_set_string(data, "text", text.c_str());
        const std::string body = obs_data_get_json(data);
        obs_data_release(data);
        enqueue("/event", body, false);
    }

    void control(const char *action)
    {
        if (!enabled_ || !action)
            return;
        const std::string name(action);
        if (name != "pause" && name != "resume" && name != "stop" && name != "flush")
            return;
        obs_data_t *data = obs_data_create();
        obs_data_set_string(data, "action", name.c_str());
        obs_data_set_string(data, "source", source_.c_str());
        const std::string body = obs_data_get_json(data);
        obs_data_release(data);
        enqueue("/control", body, name != "resume");
    }

    bool enabled_for(const std::string &source) const
    {
        return enabled_ && source == source_;
    }

private:
    struct Item { std::string path, body; };
    static long long unix_ms()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }
    static bool safe_token(const std::string &token)
    {
        if (token.size() < 16 || token.size() > 128)
            return false;
        for (unsigned char c : token)
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                  (c >= 'a' && c <= 'z') || c == '_' || c == '-' || c == '.'))
                return false;
        return true;
    }

    Bridge()
    {
        const char *source = std::getenv("SOMANG_NARRATION_OUTPUT_SOURCE");
        const char *token = std::getenv("SOMANG_NARRATION_TOKEN");
        if (!source || !*source || !token || !safe_token(token))
            return;
        source_ = source;
        token_ = token;
        run_id_ = std::to_string(unix_ms()) + "-" +
                  std::to_string(static_cast<unsigned long long>(
                      reinterpret_cast<uintptr_t>(this)));
        try {
            worker_ = std::thread([this] { run(); });
            enabled_ = true;
            blog(LOG_INFO, "Somang narration experimental bridge enabled (localhost only)");
        } catch (...) {
            blog(LOG_WARNING, "Somang narration bridge failed to start; captions unaffected");
        }
    }

    ~Bridge()
    {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            stopping_ = true;
            queue_.clear();
        }
        cv_.notify_all();
        if (worker_.joinable())
            worker_.join();
    }
    Bridge(const Bridge &) = delete;
    Bridge &operator=(const Bridge &) = delete;

    void enqueue(const std::string &path, const std::string &body, bool preempt)
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (stopping_)
            return;
        if (preempt)
            queue_.clear();
        if (queue_.size() >= 16) {
            // Never silently let narration lag indefinitely; leave OBS captions untouched.
            ++dropped_;
            if (dropped_ <= 3 || dropped_ % 10 == 0)
                blog(LOG_WARNING, "Somang narration bridge queue overflow; dropped=%llu",
                     static_cast<unsigned long long>(dropped_));
            return;
        }
        queue_.push_back({path, body});
        cv_.notify_one();
    }

    static size_t discard_reply(char *, size_t size, size_t count, void *)
    {
        return size * count;
    }

    void run()
    {
        for (;;) {
            Item item;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (stopping_)
                    return;
                item = std::move(queue_.front());
                queue_.pop_front();
            }
            CURL *curl = curl_easy_init();
            if (!curl) {
                report_failure("curl init");
                continue;
            }
            const std::string url = "http://127.0.0.1:8765" + item.path;
            const std::string auth = "X-Somang-Token: " + token_;
            struct curl_slist *headers = nullptr;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, auth.c_str());
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, item.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(item.body.size()));
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 150L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 600L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_reply);
            long status = 0;
            const CURLcode result = curl_easy_perform(curl);
            if (result == CURLE_OK)
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            if (result != CURLE_OK || status < 200 || status >= 300)
                report_failure("sidecar unavailable/rejected");
        }
    }

    void report_failure(const char *why)
    {
        ++failed_;
        if (failed_ <= 3 || failed_ % 10 == 0)
            blog(LOG_WARNING, "Somang narration %s; failed deliveries=%llu",
                 why, static_cast<unsigned long long>(failed_));
    }

    std::string source_, token_, run_id_;
    bool enabled_ = false, stopping_ = false;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Item> queue_;
    std::thread worker_;
    uint64_t dropped_ = 0, failed_ = 0;
};

} // namespace somang_narration
