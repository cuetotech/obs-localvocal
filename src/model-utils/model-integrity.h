#pragma once
#include "download-safety.h"
#include "sha256.h"
#include <fstream>
#include <algorithm>

namespace localvocal {
inline bool model_file_valid(const std::filesystem::path &path, std::string expected,
			     const DownloadCancel &cancel = {})
{
	check_download_cancel(cancel);
	if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) == 0)
		return false;
	// Some legacy catalog entries have no checksum. Keep compatibility, without
	// representing existence/nonempty validation as cryptographic verification.
	if (expected.empty())
		return true;
	if (expected.size() != 64 ||
	    expected.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
		return false;
	std::transform(expected.begin(), expected.end(), expected.begin(), [](unsigned char c) {
		return c >= 'A' && c <= 'F' ? static_cast<char>(c + ('a' - 'A'))
					    : static_cast<char>(c);
	});
	std::ifstream stream(path, std::ios::binary);
	if (!stream)
		return false;
	SHA256 hash;
	char buffer[64 * 1024];
	while (stream) {
		check_download_cancel(cancel);
		stream.read(buffer, sizeof(buffer));
		hash.add(buffer, static_cast<size_t>(stream.gcount()));
	}
	return !stream.bad() && hash.getHash() == expected;
}
inline std::string model_filename(const std::string &url)
{
	const auto path = url.substr(0, url.find_first_of("?#"));
	const auto slash = path.find_last_of('/');
	const auto name = path.substr(slash == std::string::npos ? 0 : slash + 1);
	if (name.empty() || name == "." || name == ".." ||
	    name.find_first_of("/\\") != std::string::npos)
		throw std::runtime_error("Invalid model filename");
	return name;
}
} // namespace localvocal
