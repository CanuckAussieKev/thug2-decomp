#include "engine/file/archive_folder.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

#include "base/core/checksums.h"
#include "base/core/language.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "engine/file/file_load.h"

namespace archive_folder {
namespace {

constexpr uint32_t kArchiveVersion = 0xABCD0003U; // (THUG2's archives' version word)

// Every file under `folder` (a disk path), by its path in the folder ("levels\bo\bo.scn.xbx").
class Collect final : public fs::FoundVisitor {
public:
    explicit Collect(std::vector<std::string>* names) : names_(names) {}
    std::vector<std::string> folders;
    bool found(const fs::Found& file) override
    {
        const std::string_view name = file.name;
        if (name == "." || name == "..") {
            return true;
        }
        (file.folder ? folders : *names_).emplace_back(name);
        return true;
    }

private:
    std::vector<std::string>* names_;
};

void collect(const std::string& folder, const std::string& prefix, std::vector<std::string>* names)
{
    std::vector<std::string> here;
    Collect visitor(&here);
    fs::disk().list((folder + "\\*").c_str(), visitor);
    for (const std::string& name : here) {
        names->push_back(prefix + name);
    }
    for (const std::string& sub : visitor.folders) {
        collect(folder + "\\" + sub, prefix + sub + "\\", names);
    }
}

void put32(std::vector<uint8_t>& out, uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<uint8_t>(value >> shift));
    }
}

void put16(std::vector<uint8_t>& out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

// "pre\bo_scripts.prx" -> "data\pre\bo_scripts" (any extension; "" when the name has none).
std::string folder_of(const char* path)
{
    std::string folder = std::string("data\\") + path;
    const size_t slash = folder.find_last_of('\\');
    const size_t dot = folder.find_last_of('.');
    if (dot == std::string::npos || dot < slash) {
        return "";
    }
    folder.resize(dot);
    return folder;
}

} // namespace

bool build(const char* path, int language, std::vector<uint8_t>* image)
{
    if (!path || file::is_present(path)) {
        return false; // (the archive itself: read as always)
    }
    const std::string english = folder_of(path);
    if (english.empty()) {
        return false;
    }
    std::string folder = english;
    if (language == core::kLanguageFrench || language == core::kLanguageGerman) {
        const std::string localised = english + (language == core::kLanguageFrench ? ".fr" : ".de");
        if (fs::disk().kind(localised.c_str()) == fs::PathKind::kFolder) {
            folder = localised;
        }
    }
    if (fs::disk().kind(folder.c_str()) != fs::PathKind::kFolder) {
        return false;
    }
    std::vector<std::string> names;
    collect(folder, "", &names);
    std::ranges::sort(names,
                      [](const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) < 0; });

    image->clear();
    image->resize(12);
    int32_t count = 0;
    std::string bytes;
    for (const std::string& name : names) {
        if (!fs::read_text(folder + "\\" + name, &bytes)) {
            continue;
        }
        const auto name_size = static_cast<uint16_t>((name.size() + 4) & ~size_t{3});
        put32(*image, static_cast<uint32_t>(bytes.size()));
        put32(*image, 0); // (stored, not packed)
        put16(*image, name_size);
        put16(*image, 0);
        put32(*image, checksums::hash_from_text(name.c_str()));
        const size_t at = image->size();
        image->resize(at + name_size, 0);
        std::memcpy(image->data() + at, name.data(), name.size());
        image->insert(image->end(), bytes.begin(), bytes.end());
        image->resize((image->size() + 3) & ~size_t{3}, 0);
        ++count;
    }
    const auto total = static_cast<uint32_t>(image->size());
    std::memcpy(image->data(), &total, 4);
    std::memcpy(image->data() + 4, &kArchiveVersion, 4);
    std::memcpy(image->data() + 8, &count, 4);
    return true;
}

} // namespace archive_folder
