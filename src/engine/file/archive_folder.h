#pragma once
// NOT ORIGINAL - an archive unpacked into a folder (tools/qbjs/unpack_game.ts): Data\pre\bo_scripts\ in place of
// Data\pre\bo_scripts.prx, its files loose at their names in the archive (levels\bo\bo_scripts.ts...). Both archive
// loaders (pre::ArchiveManager::load_pre, packs::load_pre) take the folder when the archive file isn't there: the same
// archive built in memory from it - every file in the folder, stored as it is (not packed) - so everything after reads
// it as it read the archive, and an archive is in and out of memory exactly when it was.
#include <cstdint>
#include <vector>

namespace archive_folder {

// The archive the loaders open as `path` ("pre\bo_scripts.prx", under Data) built from its folder, when the archive
// file isn't there and the folder is (Data\pre\bo_scripts; French and German: Data\pre\bo_scripts.fr / .de first - the
// localised archive, made with its patch applied): its bytes in `image`. False when there's no folder for it.
bool build(const char* path, int language, std::vector<uint8_t>* image);

} // namespace archive_folder
