#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace util {

namespace fs = std::filesystem;

/// Extract all entries from an in-memory ZIP archive to dest_dir.
/// Returns true on success, false on any error.
bool extract_zip(const std::vector<uint8_t>& data, const fs::path& dest_dir);

} // namespace util
