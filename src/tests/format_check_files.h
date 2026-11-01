#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

// NOT ORIGINAL - thug2_format_check's data file checks (format_check_files.cpp).
namespace format_check_files {

// A check of one file's bytes (from an archive, unpacked, or loose).
using FileCheck = void (*)(const std::string& name, const uint8_t* data, size_t size);

// Run `check_function` on every file with this lower-case extension (".scn"...).
void register_check(const std::string& extension, FileCheck check_function);
// Count a failure (for the per-format checks).
void fail(const std::string& what);

// Check the Data folder `data` (with its pre\ folder). Returns the number of failures.
int run(const std::filesystem::path& data);

} // namespace format_check_files
