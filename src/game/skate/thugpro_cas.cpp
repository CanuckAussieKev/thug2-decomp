// NOT ORIGINAL - THUGPRO's own CAS parts and boards, read from the user's THUGPRO install by reference. See
// thugpro_cas.h.

#include "game/skate/thugpro_cas.h"

#include <windows.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/file/file.h"
#include "game/file/pre.h"
#include "game/front/data_overlay.h"
#include "game/script/parse.h"
#include "game/script/qb.h"
#include "game/script/struct.h"
#include "hook/log.h"

namespace Obj::ThugproCas {

namespace {

constexpr uint32_t kDescId = 0x4BB2084E;             // desc_id
constexpr uint32_t kHidden = 0x77A21642;             // hidden
constexpr uint32_t kNoRandom = 0xF6F8E158;           // no_random
constexpr uint32_t kMasterEditableList = 0x839B2AD5; // master_editable_list (the CAS lists are loaded once it is)
constexpr int kWaitFrames = 600;                     // how long the main loop waits for it

// THUGPRO's files with CAS lists (its thugpro_qb.prx).
const char* const kFiles[] = {
    "qb\\game\\cas_skater_m.qb", "qb\\game\\cas_skater_f.qb", "qb\\game\\cas_skater_shared.qb",
    "qb\\game\\cas_logos.qb",    "qb\\game\\decks.qb",        "qb\\thugpro\\thugpro_boards.qb",
};

std::string s_data;                  // THUGPRO's data folder, ending in '\'
std::set<std::string> s_ourKeys;     // the path keys our own parts name (never served from THUGPRO's install)
std::set<std::string> s_theirFiles;  // the path keys of THUGPRO's loose files the added parts name (while loading)
// What FilePath serves: s_theirFiles once the load is done (published once - the file system's threads read it).
std::atomic<const std::set<std::string>*> s_served{nullptr};
bool s_done;
int s_frames;

bool IsFile(const std::string& path)
{
    DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// A path as the game folder keeps it: lower case, '\', without a leading "data\".
std::string Relative(const char* path)
{
    std::string out;
    for (const char* c = path; *c; ++c)
        out += *c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
    if (out.rfind("data\\", 0) == 0)
        out.erase(0, 5);
    return out;
}

bool IsPath(const char* text) { return std::strchr(text, '/') || std::strchr(text, '\\'); }

// The ways the game asks for a part's file: as named, .xbx after it, an image's .img.xbx.
std::vector<std::string> Candidates(const char* path)
{
    std::string relative = Relative(path);
    std::string key = PathKey(path);
    return {relative, relative + ".xbx", key + ".img.xbx", relative + ".img.xbx"};
}

// Whether the game has a file: in the data overlay, a loaded archive, or its data folder. (Not File::Exist: on disk
// it opens the file, and any error but "not found" - a folder that isn't there - is the game's fatal file error.)
bool GameHas(const std::string& name)
{
    char overlay[260];
    if (Front::DataOverlay::OverlayPath(name.c_str(), overlay))
        return true;
    if (Pre::Manager() && Pre::Exist(name.c_str()))
        return true;
    char disk[256];
    File::MakeDiskPath(disk, name.c_str());
    return IsFile(GameFolderPath(disk));
}

// Whether a part's path can be loaded: the game's own file (or one of ours), or a loose file of THUGPRO's (then noted
// as one the overlay serves from the install).
bool Found(const char* path)
{
    std::string key = PathKey(path);
    if (s_theirFiles.count(key))
        return true;
    for (const std::string& candidate : Candidates(path)) {
        if (GameHas(candidate))
            return true;
    }
    if (s_ourKeys.count(key))
        return false; // (one of our own parts' files: never taken from THUGPRO's install)
    for (const std::string& candidate : Candidates(path)) {
        if (IsFile(s_data + candidate)) {
            s_theirFiles.insert(key);
            return true;
        }
    }
    return false;
}

// Every path a part's structure names (strings with a slash), its structures and arrays included.
void CollectPaths(Script::CStruct* part, std::vector<const char*>* out, int depth = 0);
void CollectArrayPaths(Script::CArray* array, std::vector<const char*>* out, int depth)
{
    if (!array || depth > 4)
        return;
    for (uint32_t i = 0; i < array->m_size; ++i) {
        uint32_t element = array->GetElement(i);
        if (!element)
            continue;
        if (array->m_type == Script::ESYMBOLTYPE_STRING && IsPath(reinterpret_cast<const char*>(element)))
            out->push_back(reinterpret_cast<const char*>(element));
        else if (array->m_type == Script::ESYMBOLTYPE_STRUCTURE)
            CollectPaths(reinterpret_cast<Script::CStruct*>(element), out, depth + 1);
        else if (array->m_type == Script::ESYMBOLTYPE_ARRAY)
            CollectArrayPaths(reinterpret_cast<Script::CArray*>(element), out, depth + 1);
    }
}
void CollectPaths(Script::CStruct* part, std::vector<const char*>* out, int depth)
{
    if (!part || depth > 4)
        return;
    for (Script::CComponent* c = part->GetNextComponent(nullptr); c; c = part->GetNextComponent(c)) {
        switch (c->Type()) {
        case Script::ESYMBOLTYPE_STRING:
            if (c->string && IsPath(c->string))
                out->push_back(c->string);
            break;
        case Script::ESYMBOLTYPE_STRUCTURE: CollectPaths(c->structure, out, depth + 1); break;
        case Script::ESYMBOLTYPE_ARRAY: CollectArrayPaths(c->array, out, depth + 1); break;
        default: break;
        }
    }
}

// One of our part lists: a global array of structures whose first has a desc_id.
bool IsPartList(Script::CArray* array)
{
    if (!array || array->m_type != Script::ESYMBOLTYPE_STRUCTURE || array->m_size == 0)
        return false;
    auto* first = reinterpret_cast<Script::CStruct*>(array->GetElement(0));
    uint32_t desc;
    return first && first->GetChecksum(kDescId, &desc, 0);
}

std::vector<uint8_t> ReadWholeFile(const std::string& path)
{
    std::vector<uint8_t> data;
    HANDLE file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return data;
    DWORD size = GetFileSize(file, nullptr);
    if (size != INVALID_FILE_SIZE && size < 64u << 20) {
        data.resize(size);
        DWORD read = 0;
        if (!ReadFile(file, data.data(), size, &read, nullptr) || read != size)
            data.clear();
    }
    CloseHandle(file);
    return data;
}

std::string InstallFolder()
{
    char folder[MAX_PATH] = {};
    GetPrivateProfileStringA("features", "thugpro_folder", "", folder, sizeof(folder),
                             GameFolderPath("thug2-decomp.ini").c_str());
    if (folder[0])
        return folder;
    char local[MAX_PATH] = {};
    DWORD length = GetEnvironmentVariableA("LOCALAPPDATA", local, sizeof(local));
    if (!length || length >= sizeof(local))
        return "";
    return std::string(local) + "\\THUG Pro";
}

} // namespace

std::vector<uint8_t> DecodeLzss(const uint8_t* in, size_t length, size_t size)
{
    uint8_t window[4096];
    std::memset(window, ' ', sizeof(window));
    uint32_t r = 4096 - 18;
    std::vector<uint8_t> out;
    out.reserve(size);
    size_t i = 0;
    uint32_t flags = 0;
    while (out.size() < size && i < length) {
        flags >>= 1;
        if (!(flags & 0x100))
            flags = in[i++] | 0xFF00u;
        if (flags & 1) {
            if (i >= length)
                break;
            uint8_t c = in[i++];
            out.push_back(c);
            window[r] = c;
            r = (r + 1) & 4095;
        } else {
            if (i + 1 >= length)
                break;
            uint32_t position = in[i] | ((in[i + 1] & 0xF0u) << 4);
            uint32_t last = (in[i + 1] & 0x0Fu) + 2;
            i += 2;
            for (uint32_t k = 0; k <= last && out.size() < size; ++k) {
                uint8_t c = window[(position + k) & 4095];
                out.push_back(c);
                window[r] = c;
                r = (r + 1) & 4095;
            }
        }
    }
    return out;
}

bool ReadArchive(const std::vector<uint8_t>& archive, const std::vector<std::string>& names,
                 std::vector<std::pair<std::string, std::vector<uint8_t>>>* out)
{
    // {total size, version, file count}, then each file: {size, compressed size (0: stored), name size (int16), use
    // count, checksum}, its name, its data padded to 4 bytes (src/game/file/pip.cpp NextRawEntry).
    if (archive.size() < 12)
        return false;
    auto u32 = [&archive](size_t at) {
        uint32_t value;
        std::memcpy(&value, archive.data() + at, 4);
        return value;
    };
    uint32_t count = u32(8);
    size_t offset = 12;
    for (uint32_t k = 0; k < count; ++k) {
        if (offset + 16 > archive.size())
            return false;
        uint32_t size = u32(offset), compressed = u32(offset + 4);
        uint16_t nameSize;
        std::memcpy(&nameSize, archive.data() + offset + 8, 2);
        size_t body = offset + 16 + nameSize;
        size_t stored = compressed ? compressed : size;
        if (body > archive.size() || stored > archive.size() - body)
            return false;
        std::string name(reinterpret_cast<const char*>(archive.data() + offset + 16),
                         strnlen(reinterpret_cast<const char*>(archive.data() + offset + 16), nameSize));
        std::string key = Relative(name.c_str());
        for (const std::string& wanted : names) {
            if (key != wanted)
                continue;
            const uint8_t* data = archive.data() + body;
            std::vector<uint8_t> file = compressed ? DecodeLzss(data, stored, size) : std::vector<uint8_t>(data, data + size);
            if (file.size() == size) // (a file that doesn't decompress to its size is left out)
                out->emplace_back(key, std::move(file));
        }
        offset = body + ((stored + 3) & ~static_cast<size_t>(3));
    }
    return true;
}

std::vector<std::pair<uint32_t, const uint8_t*>> TopLevelArrays(const uint8_t* qb, size_t size)
{
    std::vector<std::pair<uint32_t, const uint8_t*>> arrays;
    const uint8_t* p = qb;
    const uint8_t* end = qb + size;
    while (p < end) {
        p = Script::SkipEndOfLines(p);
        const uint8_t* before = p;
        switch (*p) {
        case 0x00: return arrays;
        case 0x16: { // name = value
            uint32_t name = Script::Read4Bytes(p + 1);
            const uint8_t* q = Script::SkipEndOfLines(p + 5);
            if (*q != 0x07) {
                p = Script::SkipToken(p);
                break;
            }
            q = Script::SkipEndOfLines(q + 1);
            if (*q == 0x05)
                arrays.emplace_back(name, q);
            p = Script::SkipValue(q);
            break;
        }
        case 0x23: p = Script::SkipToEndOfScript(p + 6); break; // script name ... endscript
        case 0x45: p += 6; break;                               // a section marker
        default: p = Script::SkipToken(p); break;
        }
        if (p <= before)
            return arrays; // (a token we can't move past: stop)
    }
    return arrays;
}

std::string PathKey(const char* path)
{
    std::string key = Relative(path);
    static const char* const kExtensions[] = {".xbx", ".skin", ".tex", ".cas", ".col", ".img", ".png",
                                              ".mdl", ".ske",  ".geom", ".txx", ".dds", ".bmp"};
    for (bool again = true; again;) {
        again = false;
        for (const char* extension : kExtensions) {
            size_t length = std::strlen(extension);
            if (key.size() > length && key.compare(key.size() - length, length, extension) == 0) {
                key.erase(key.size() - length);
                again = true;
            }
        }
    }
    return key;
}

std::vector<uint32_t> PickNew(Script::CArray* ours, Script::CArray* theirs, bool (*found)(const char* path))
{
    std::vector<uint32_t> picked;
    if (!ours || !theirs || theirs->m_type != Script::ESYMBOLTYPE_STRUCTURE)
        return picked;
    std::set<uint32_t> descs;
    for (uint32_t i = 0; i < ours->m_size; ++i) {
        auto* part = reinterpret_cast<Script::CStruct*>(ours->GetElement(i));
        uint32_t desc;
        if (part && part->GetChecksum(kDescId, &desc, 0))
            descs.insert(desc);
    }
    for (uint32_t i = 0; i < theirs->m_size; ++i) {
        auto* part = reinterpret_cast<Script::CStruct*>(theirs->GetElement(i));
        uint32_t desc;
        if (!part || !part->GetChecksum(kDescId, &desc, 0) || descs.count(desc))
            continue;
        std::vector<const char*> paths;
        CollectPaths(part, &paths);
        bool all = true;
        for (const char* path : paths)
            all = all && found(path);
        if (!all)
            continue;
        descs.insert(desc);
        picked.push_back(i);
    }
    return picked;
}

bool FilePath(const char* relative, char (&path)[260])
{
    const std::set<std::string>* served = s_served.load();
    if (!served || !relative || !served->count(PathKey(relative)))
        return false;
    std::string full = s_data + relative;
    if (full.size() >= sizeof(path) || !IsFile(full))
        return false;
    std::memcpy(path, full.c_str(), full.size() + 1);
    static std::set<std::string> logged;
    if (logged.size() < 1000 && logged.insert(full).second)
        Log("THUGPRO CAS (TP-R10): %s read from THUGPRO's install (%s)", relative, full.c_str());
    return true;
}

void LoadOnce()
{
    if (s_done)
        return;
    if (!Script::GetArray(kMasterEditableList)) {
        if (++s_frames > kWaitFrames)
            s_done = true;
        return;
    }
    s_done = true;
    int setting = ConfigInt("features", "thugpro_parts", 1);
    if (!setting) {
        Log("THUGPRO CAS (TP-R10): off ([features] thugpro_parts=0) - a THUGPRO player's own parts fall back to THUG2's");
        return;
    }
    std::string folder = InstallFolder();
    std::string archivePath = folder + "\\data\\pre\\thugpro_qb.prx";
    std::vector<uint8_t> archive = ReadWholeFile(archivePath);
    if (archive.empty()) {
        Log("THUGPRO CAS (TP-R10): no THUGPRO install at %s - a THUGPRO player's own parts fall back to THUG2's "
            "([features] thugpro_folder= names another)",
            folder.c_str());
        return;
    }
    s_data = folder + "\\data\\";
    std::vector<std::string> names(std::begin(kFiles), std::end(kFiles));
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    if (!ReadArchive(archive, names, &files)) {
        Log("THUGPRO CAS (TP-R10): %s isn't an archive we can read - THUGPRO's parts not loaded", archivePath.c_str());
        return;
    }
    // The arrays that are also our part lists, and every path our own parts name (never served from THUGPRO's).
    struct Target {
        uint32_t name;
        Script::CArray* ours;
        const uint8_t* theirs;
    };
    std::vector<Target> targets;
    for (auto& [name, data] : files) {
        size_t size = data.size();
        data.resize(size + 16, 0); // (the walk stops at a 0 token)
        for (auto [array, at] : TopLevelArrays(data.data(), size)) {
            Script::CArray* ours = Script::GetArray(array);
            if (IsPartList(ours))
                targets.push_back({array, ours, at});
        }
    }
    for (const Target& target : targets) {
        for (uint32_t i = 0; i < target.ours->m_size; ++i) {
            std::vector<const char*> paths;
            CollectPaths(reinterpret_cast<Script::CStruct*>(target.ours->GetElement(i)), &paths);
            for (const char* path : paths)
                s_ourKeys.insert(PathKey(path));
        }
    }
    int added = 0, lists = 0, skipped = 0;
    Mem::Manager* manager = Mem::Manager::Get();
    Mem::PushContext(manager->m_scriptHeap);
    for (const Target& target : targets) {
        Script::CArray* theirs = Script::NewArray();
        if (!theirs)
            break;
        theirs->Init();
        Script::ParseArray(theirs, target.theirs, nullptr);
        std::vector<uint32_t> picked = PickNew(target.ours, theirs, Found);
        int theirNew = 0;
        if (theirs->m_type == Script::ESYMBOLTYPE_STRUCTURE) {
            std::set<uint32_t> descs;
            for (uint32_t i = 0; i < target.ours->m_size; ++i) {
                uint32_t desc;
                auto* part = reinterpret_cast<Script::CStruct*>(target.ours->GetElement(i));
                if (part && part->GetChecksum(kDescId, &desc, 0))
                    descs.insert(desc);
            }
            for (uint32_t i = 0; i < theirs->m_size; ++i) {
                uint32_t desc;
                auto* part = reinterpret_cast<Script::CStruct*>(theirs->GetElement(i));
                if (part && part->GetChecksum(kDescId, &desc, 0) && !descs.count(desc))
                    ++theirNew;
            }
        }
        if (!picked.empty()) {
            uint32_t old = target.ours->m_size;
            target.ours->Resize(old + static_cast<uint32_t>(picked.size()));
            uint32_t* elements = theirs->GetArrayPointer();
            for (size_t k = 0; k < picked.size(); ++k) {
                auto* part = reinterpret_cast<Script::CStruct*>(elements[picked[k]]);
                elements[picked[k]] = 0; // (now ours: not freed with THUGPRO's array)
                if (setting == 1 && !part->ContainsFlag(kHidden))
                    part->AddChecksum(0u, kHidden);
                if (setting == 1 && !part->ContainsFlag(kNoRandom))
                    part->AddChecksum(0u, kNoRandom);
                target.ours->SetElement(old + static_cast<uint32_t>(k), reinterpret_cast<uint32_t>(part));
            }
            ++lists;
        }
        added += static_cast<int>(picked.size());
        skipped += theirNew - static_cast<int>(picked.size());
        Script::CleanUpArray(theirs);
        theirs->Clear();
        Script::DeleteArray(theirs);
    }
    Mem::PopContext();
    s_served.store(new std::set<std::string>(s_theirFiles)); // (kept for the game's life)
    Log("THUGPRO CAS (TP-R10): %d of THUGPRO's parts added to %d of our lists from %s (%s); %d left out (files not in "
        "the install's loose data); %d of its loose files to read from there when asked for",
        added, lists, archivePath.c_str(),
        setting == 1 ? "hidden from our menus" : "offered in our CAS menus too: thugpro_parts=2", skipped,
        static_cast<int>(s_theirFiles.size()));
}

} // namespace Obj::ThugproCas
