#pragma once

namespace fs {
class Source;
} // namespace fs

// NOT ORIGINAL - the game's data file sources (fs::Source, engine/file/file_system.h), made on first use and never
// destroyed; app/ adds them to the file system in this order (app/main.cpp):
//   - the data overlay's folder (engine/file/data_overlay.h - ours, looked in first);
//   - the loaded .pre archives (engine/file/archive.h), while the pre manager exists;
//   - the Data folder on disk (file::open_on_disk's path), which handles every call that reaches it.
namespace file {

fs::Source& overlay_source();
fs::Source& archive_source();
fs::Source& data_folder_source();

} // namespace file
