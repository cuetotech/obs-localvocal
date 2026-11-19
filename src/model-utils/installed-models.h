#pragma once

#include "model-downloader.h"
#include "model-integrity.h"

#include <filesystem>
#include <system_error>

namespace localvocal {

// A fast, *offline* presence check for populating an OBS dropdown.
// Full SHA-256 validation is performed by the explicit download manager.
// Never hash multi-gigabyte model files on every OBS launch.
inline bool model_installed_locally(const ModelInfo &model)
{
	if (model.local_folder_name.empty() || model.files.empty())
		return false;
	const auto folder = find_model_folder(model);
	if (!folder)
		return false;
	for (const auto &file : model.files) {
		if (file.url.empty())
			return false;
		std::error_code error;
		std::filesystem::path filename;
		try {
			filename = std::filesystem::u8path(model_filename(file.url));
		} catch (...) {
			return false;
		}
		const auto path = *folder / filename;
		if (!std::filesystem::is_regular_file(path, error) || error)
			return false;
		if (std::filesystem::file_size(path, error) == 0 || error)
			return false;
	}
	return true;
}

} // namespace localvocal
