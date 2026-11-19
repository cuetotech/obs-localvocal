#include "../../src/text/caption-text.h"
#include "../../src/transcription-utils.h"
#include "../../src/model-utils/model-integrity.h"
#include <iostream>
#include <random>
#include <thread>
#include <future>

using namespace std::chrono_literals;
int checks = 0;
void check(bool ok, const char *name)
{
	++checks;
	if (!ok)
		throw std::runtime_error(name);
}
std::string read(const std::filesystem::path &p)
{
	std::ifstream s(p, std::ios::binary);
	return {std::istreambuf_iterator<char>(s), {}};
}
void write(const std::filesystem::path &p, const std::string &value)
{
	std::ofstream s(p, std::ios::binary);
	s << value;
	if (!s)
		throw std::runtime_error("fixture write");
}
template<class F> void fails(F action, const char *name)
{
	bool failed = false;
	try {
		action();
	} catch (const std::exception &) {
		failed = true;
	}
	check(failed, name);
}

int main()
{
	try {
		for (const std::string &text :
		     {std::string("God"), std::string("Amen!"), std::string("7"),
		      std::string("John 3:16"), std::string("\"Do not fear.\""),
		      std::string("하나님 😀"), std::string("I")}) {
			check(remove_leading_trailing_nonalpha(text) == text,
			      "preserve caption content");
			check(remove_leading_trailing_nonalpha(" \t" + text + "\r\n") == text,
			      "trim only surrounding whitespace");
			check(localvocal::repair_windows_utf8(text) == text, "preserve valid UTF8");
		}
		check(remove_leading_trailing_nonalpha(" \r\n\t").empty(), "whitespace only");
		check(localvocal::repair_windows_utf8("").empty(), "empty UTF8");
		check(localvocal::utf8_sequence_size("", 0) == 0 &&
			      localvocal::utf8_sequence_size("a", 1) == 0,
		      "decoder rejects end offsets");
		check(localvocal::repair_windows_utf8(std::string("a\0b", 3)) ==
			      std::string("a\0b", 3),
		      "embedded null preserved");
		const auto original = std::string("\xf0\xff\x98\x80", 4);
		check(localvocal::repair_windows_utf8(original) == "😀" &&
			      static_cast<unsigned char>(original[1]) == 0xff,
		      "bounded historical repair without input mutation");
		check(localvocal::repair_windows_utf8(std::string(1, static_cast<char>(0xff))) ==
			      "\xef\xbf\xbd",
		      "invalid suffix replacement");
		std::mt19937 generator(42);
		for (unsigned trial = 0; trial < 2000; ++trial) {
			std::string input(trial % 80, 'x');
			for (char &c : input)
				c = static_cast<char>(generator() & 0xff);
			const auto saved = input;
			const auto output = localvocal::repair_windows_utf8(input);
			bool valid = true;
			for (size_t offset = 0; offset < output.size();) {
				auto size = localvocal::utf8_sequence_size(output, offset);
				if (!size) {
					valid = false;
					break;
				}
				offset += size;
			}
			check(valid && input == saved,
			      "malformed bytes yield valid UTF8 without mutation");
		}
		for (unsigned lead = 0x80; lead <= 0xff; ++lead) {
			std::string input(32, 'A');
			input.resize(input.capacity(), 'A');
			input.back() = static_cast<char>(lead);
			check(!localvocal::repair_windows_utf8(input).empty(),
			      "allocation-edge suffix");
		}
		localvocal::DownloadStaging root(std::filesystem::temp_directory_path());
		const auto destination = root.file();
		const std::string payload = "verified model";
		const auto hash = SHA256()(payload);
		auto cancel = std::make_shared<std::atomic<bool>>(false);
		auto validate = [&](const auto &p) {
			return localvocal::model_file_valid(p, hash, cancel);
		};
		int transfers = 0;
		auto transfer = [&](const auto &p) {
			++transfers;
			write(p, payload);
		};
		localvocal::ensure_model_file(destination, cancel, validate, transfer);
		check(validate(destination) && transfers == 1, "verified file promoted");
		localvocal::ensure_model_file(destination, cancel, validate, transfer);
		check(transfers == 1, "verified cache reused");
		write(destination, "corrupt");
		fails(
			[&] {
				localvocal::ensure_model_file(
					destination, cancel, validate, [&](const auto &p) {
						write(p, "partial");
						throw std::runtime_error("interrupted");
					});
			},
			"transfer failure aborts");
		check(read(destination) == "corrupt", "failed transfer never promoted");
		fails(
			[&] {
				localvocal::ensure_model_file(destination, cancel, validate,
							      [&](const auto &p) {
								      write(p, "wrong hash");
							      });
			},
			"checksum failure aborts");
		check(read(destination) == "corrupt", "bad checksum never promoted");
		localvocal::ensure_model_file(destination, cancel, validate, transfer);
		check(validate(destination) && transfers == 2,
		      "corrupt cache replaced after validation");
		cancel->store(true);
		fails(
			[&] {
				localvocal::ensure_model_file(destination, cancel, validate,
							      transfer);
			},
			"pre-cancelled job rejected");
		cancel->store(false);
		std::filesystem::remove(destination);
		fails(
			[&] {
				localvocal::ensure_model_file(destination, cancel, validate,
							      [&](const auto &p) {
								      write(p, payload);
								      cancel->store(true);
							      });
			},
			"cancellation before promotion");
		check(!std::filesystem::exists(destination), "cancelled file not published");
		check(std::filesystem::is_empty(destination.parent_path()),
		      "temporary files cleaned on every failure");
		cancel->store(false);
		std::thread first([&] {
			localvocal::ensure_model_file(destination, cancel, validate, transfer);
		});
		std::thread second([&] {
			localvocal::ensure_model_file(destination, cancel, validate, transfer);
		});
		first.join();
		second.join();
		check(validate(destination) && transfers == 3,
		      "concurrent same-file request transfers once");
		check(localvocal::model_filename("https://example.com/model.bin?download=1") ==
			      "model.bin",
		      "URL filename");
		check(localvocal::model_filename(
			      "https://example.com/model.bin?next=/other/file") == "model.bin",
		      "ignore slashes in URL query");
		fails([&] { localvocal::model_filename("https://example.com/.."); },
		      "reject traversal filename");
		struct Owner {
			int loads = 0;
		} owner;
		auto lifetime = std::make_shared<localvocal::ModelCallbackLifetime<Owner>>(&owner);
		auto old = lifetime->begin(0);
		auto fresh = lifetime->begin(0);
		check(old->load() && !lifetime->run(old, [](auto &o) { ++o.loads; }),
		      "superseded completion rejected");
		check(lifetime->run(fresh, [](auto &o) { ++o.loads; }) && owner.loads == 1,
		      "current completion accepted");
		lifetime->cancel(0);
		check(!lifetime->run(fresh, [](auto &o) { ++o.loads; }),
		      "disabled completion rejected");
		fresh = lifetime->begin(1);
		lifetime->close();
		check(fresh->load() && !lifetime->run(fresh, [](auto &o) { ++o.loads; }),
		      "destroyed owner completion rejected");
		check(owner.loads == 1, "no stale model load");
		localvocal::ModelCallbackLifetime<Owner> in_progress(&owner);
		auto pending = in_progress.begin(0);
		std::promise<void> entered, release;
		auto permitted = release.get_future();
		auto completion = std::async(std::launch::async, [&] {
			return in_progress.run(pending, [&](auto &o) {
				entered.set_value();
				permitted.wait();
				++o.loads;
			});
		});
		entered.get_future().wait();
		std::promise<void> closing;
		auto shutdown = std::async(std::launch::async, [&] {
			closing.set_value();
			in_progress.close();
		});
		closing.get_future().wait();
		const auto waiting = shutdown.wait_for(20ms) == std::future_status::timeout;
		release.set_value();
		check(completion.get(), "in-progress completion finishes safely");
		shutdown.get();
		check(waiting && pending->load() && owner.loads == 2,
		      "shutdown waits for active owner lease");
		check(!in_progress.run(pending, [](auto &o) { ++o.loads; }),
		      "shutdown rejects later completions");
		std::cout << checks << " text/download/lifetime checks passed\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
