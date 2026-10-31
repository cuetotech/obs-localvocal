#include "../../src/captions/cloud-caption-pipeline.h"
#include "../../src/translation/cloud-translation/translation-cloud.h"
#include <iostream>
#include <stdexcept>

using namespace std::chrono_literals;
using Pipeline = localvocal::CloudCaptionPipeline<int>;
int checks = 0;
void check(bool condition, const char *message)
{
	++checks;
	if (!condition)
		throw std::runtime_error(message);
}
template<class F> void eventually(F condition)
{
	const auto until = Pipeline::Clock::now() + 2s;
	while (!condition()) {
		if (Pipeline::Clock::now() > until)
			throw std::runtime_error("asynchronous test deadline");
		std::this_thread::sleep_for(1ms);
	}
}
struct Screen {
	std::mutex mutex;
	std::vector<std::pair<std::string, Pipeline::Clock::time_point>> shown;
	void publish(const std::string &text)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!text.empty())
			shown.push_back({text, Pipeline::Clock::now()});
	}
	size_t count()
	{
		std::lock_guard<std::mutex> lock(mutex);
		return shown.size();
	}
};
Pipeline::Request request(int id, Screen &screen, std::string text)
{
	Pipeline::Request r;
	r.context = id;
	r.cache_key = text;
	r.translate = [text](auto, long) {
		return text;
	};
	r.publish = [&screen](const auto &caption) {
		screen.publish(caption);
	};
	return r;
}
std::string without_space(std::string text)
{
	text.erase(std::remove_if(text.begin(), text.end(),
				  [](char c) {
					  return c == ' ' || c == '\n' || c == '\r' || c == '\t';
				  }),
		   text.end());
	return text;
}
int main()
{
	try {
		localvocal::CaptionOptions o;
		check(localvocal::caption_dwell("short", o) == 3s, "minimum dwell");
		check(localvocal::caption_dwell(std::string(80, 'x'), o) == 5s,
		      "length-based dwell");
		check(localvocal::caption_dwell(std::string(200, 'x'), o) == 6s, "maximum dwell");
		check(localvocal::caption_pages(" \t\r\n", 42).empty(), "whitespace caption");
		for (const std::string &text :
		     {std::string(125, 'x'),
		      std::string(
			      "God has prepared a path for us, even when we cannot see it. Do not fear."),
		      std::string(
			      "하나님은 우리와 함께 계십니다. Jesus said: ‘Do not fear.’ 😀😀😀")}) {
			std::string rebuilt;
			for (const auto &page : localvocal::caption_pages(text, 24)) {
				check(std::count(page.begin(), page.end(), '\n') <= 1,
				      "two-line page");
				size_t start = 0;
				do {
					auto end = page.find('\n', start);
					check(localvocal::visible_characters(
						      page.substr(start, end - start)) <= 24,
					      "Unicode line width");
					if (end == std::string::npos)
						break;
					start = end + 1;
				} while (start < page.size());
				rebuilt += page;
			}
			check(without_space(rebuilt) == without_space(text),
			      "preserve words and Unicode bytes");
		}
		check(!localvocal::backend_requires_reload(0, 0, false, false),
		      "unchanged backend");
		check(localvocal::backend_requires_reload(-1, 0, false, false), "CPU to CUDA");
		check(localvocal::backend_requires_reload(0, -1, false, false), "CUDA to CPU");
		check(localvocal::backend_requires_reload(0, 0, false, true), "flash change");
		CloudTranslatorConfig config;
		config.provider = "google";
		config.access_key = "test-only";
		auto key = cloud_cache_key(config, "ko", "en", "same");
		check(key != cloud_cache_key(config, "ko", "es", "same"), "target-aware cache");
		config.access_key = "another-test-key";
		check(key != cloud_cache_key(config, "ko", "en", "same"), "credential-aware cache");

		{
			Screen screen;
			Pipeline p;
			o.readable = true;
			o.min_dwell = 50ms;
			o.max_dwell = 80ms;
			p.configure(o);
			std::atomic<bool> release{false}, started{false}, second_started{false};
			auto first = request(1, screen, "first");
			first.translate = [&](auto cancelled, long) {
				started = true;
				while (!release && !cancelled->load())
					std::this_thread::sleep_for(1ms);
				return "first";
			};
			auto second = request(2, screen, "second");
			second.translate = [&](auto, long) {
				second_started = true;
				return "second";
			};
			check(p.submit(std::move(first)), "submit first");
			eventually([&] { return started.load(); });
			check(p.submit(std::move(second)), "submit second");
			check(!second_started, "one in-flight request; no completion reordering");
			release = true;
			eventually([&] { return screen.count() == 2; });
			{
				std::lock_guard<std::mutex> lock(screen.mutex);
				check(screen.shown[0].first == "first" &&
					      screen.shown[1].first == "second",
				      "sermon order");
				check(screen.shown[1].second - screen.shown[0].second >= 50ms,
				      "new response cannot shorten dwell");
			}
			auto completed = p.take_completed();
			check(completed.size() == 2 && completed[0].context == 1 &&
				      completed[1].context == 2,
			      "archive independent of dwell");
		}
		{
			Screen screen;
			Pipeline p;
			o.readable = false;
			p.configure(o);
			std::atomic<int> calls{0};
			auto repeated = request(1, screen, "same");
			repeated.translate = [&](auto, long) -> std::string {
				if (++calls == 1)
					throw std::runtime_error("injected failure");
				return "correct repeated phrase";
			};
			p.submit(repeated);
			eventually([&] { return p.stats().failed == 1; });
			p.submit(repeated);
			eventually([&] { return p.stats().translated == 1; });
			repeated.context = 2;
			p.submit(repeated);
			eventually([&] { return p.stats().translated == 2; });
			check(calls == 2 && p.stats().cached == 1,
			      "failure then repeat retries; success cached atomically");
			check(p.take_completed().size() == 2,
			      "legitimate repeated finals retained");
			p.reset();
			p.submit(repeated);
			eventually([&] { return calls == 3; });
			check(calls == 3, "reset invalidates cache");
		}
		{
			Screen screen;
			Pipeline p;
			o.capacity = 2;
			p.configure(o);
			std::atomic<bool> started{false};
			auto blocked = request(1, screen, "old session");
			blocked.translate = [&](auto cancelled, long) {
				started = true;
				while (!cancelled->load())
					std::this_thread::sleep_for(1ms);
				return "stale response";
			};
			p.submit(std::move(blocked));
			eventually([&] { return started.load(); });
			check(p.submit(request(2, screen, "pending 1")), "first pending");
			check(p.submit(request(3, screen, "pending 2")), "second pending");
			check(!p.submit(request(4, screen, "overflow")),
			      "bounded work rejects overflow");
			check(p.stats().pending == 2 && p.stats().rejected == 1,
			      "overload counted");
			p.reset();
			p.submit(request(5, screen, "new session"));
			eventually([&] { return screen.count() == 1; });
			check(screen.shown[0].first == "new session",
			      "late response cannot repaint after reset");
			check(p.take_completed().size() == 1, "stale archive invalidated");
			const auto before = Pipeline::Clock::now();
			p.stop();
			check(Pipeline::Clock::now() - before < 500ms,
			      "owned workers join promptly");
			check(!p.submit(request(6, screen, "after stop")),
			      "stop prevents submission");
		}
		{
			Screen screen;
			Pipeline p;
			o.max_age = 30ms;
			p.configure(o);
			std::atomic<bool> started{false}, release{false};
			auto old = request(1, screen, "old");
			old.translate = [&](auto cancelled, long) {
				started = true;
				while (!release && !cancelled->load())
					std::this_thread::sleep_for(1ms);
				return "old";
			};
			p.submit(std::move(old));
			eventually([&] { return started.load(); });
			p.submit(request(2, screen, "queued old"));
			std::this_thread::sleep_for(40ms);
			release = true;
			eventually([&] { return p.stats().expired == 2; });
			check(screen.count() == 0, "expired display suppressed");
			check(p.take_completed().size() == 1,
			      "successful full translation survives display expiry");
			p.submit(request(3, screen, "fresh"));
			eventually([&] { return screen.count() == 1; });
			check(screen.shown[0].first == "fresh", "progress after expired head");
		}
		std::cout << checks << " caption/backend/cache/worker checks passed\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "FAILED: " << e.what() << '\n';
		return 1;
	}
}
