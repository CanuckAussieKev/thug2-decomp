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

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/file/file_load.h"
#include "engine/file/archive.h"
#include "frontend/data_overlay.h"
#include "engine/script/token_reader.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "platform/run_log.h"
#include "engine/util/unique_handle.h"

namespace objects::thugpro_cas {

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
std::set<std::string>& our_keys()
{
    static std::set<std::string> value; // the path keys our own parts name (never served from THUGPRO's install)
    return value;
}
std::set<std::string>& their_files()
{
    static std::set<std::string> value; // the path keys of THUGPRO's loose files the added parts name (while loading)
    return value;
}
// What file_path serves: their_files() once the load is done (published once - the file system's threads read it).
std::atomic<const std::set<std::string>*> s_served{nullptr};
bool s_done;
int s_frames;

bool is_file(const std::string& path)
{
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// A path as the game folder keeps it: lower case, '\', without a leading "data\".
std::string get_relative(const char* path)
{
    std::string out;
    for (const char* c = path; *c; ++c)
        out += *c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
    if (out.starts_with("data\\"))
        out.erase(0, 5);
    return out;
}

bool is_path(const char* text) { return std::strchr(text, '/') || std::strchr(text, '\\'); }

// The ways the game asks for a part's file: as named, .xbx after it, an image's .img.xbx.
std::vector<std::string> get_candidates(const char* path)
{
    const std::string relative = get_relative(path);
    const std::string key = path_key(path);
    return {relative, relative + ".xbx", key + ".img.xbx", relative + ".img.xbx"};
}

// Whether the game has a file: in the data overlay, a loaded archive, or its data folder. (Not FileLoad::is_present: on disk
// it opens the file, and any error but "not found" - a folder that isn't there - is the game's fatal file error.)
bool game_has(const std::string& name)
{
    char overlay[260];
    if (frontend::data_overlay::get_overlay_path(name.c_str(), overlay))
        return true;
    if (pre::hub() && pre::is_present(name.c_str()))
        return true;
    char disk[256];
    file::make_disk_path(disk, name.c_str());
    return is_file(game_folder_path(disk));
}

// Whether a part's path can be loaded: the game's own file (or one of ours), or a loose file of THUGPRO's (then noted
// as one the overlay serves from the install).
bool get_found(const char* path)
{
    const std::string key = path_key(path);
    if (their_files().count(key))
        return true;
    for (const std::string& candidate : get_candidates(path)) {
        if (game_has(candidate))
            return true;
    }
    if (our_keys().count(key))
        return false; // (one of our own parts' files: never taken from THUGPRO's install)
    for (const std::string& candidate : get_candidates(path)) {
        if (is_file(s_data + candidate)) {
            their_files().insert(key);
            return true;
        }
    }
    return false;
}

// Every path a part's structure names (strings with a slash), its structures and arrays included.
void collect_paths(script::Struct* part, std::vector<const char*>* out, int depth = 0);
void collect_array_paths(script::Array* array, std::vector<const char*>* out, int depth)
{
    if (!array || depth > 4)
        return;
    for (uint32_t i = 0; i < array->extent_; ++i) {
        uint32_t element = array->fetch_part(i);
        if (!element)
            continue;
        if (array->kind_tag_ == script::kQbvalString && is_path(reinterpret_cast<const char*>(element)))
            out->push_back(reinterpret_cast<const char*>(element));
        else if (array->kind_tag_ == script::kQbvalStruct)
            collect_paths(reinterpret_cast<script::Struct*>(element), out, depth + 1);
        else if (array->kind_tag_ == script::kQbvalArray)
            collect_array_paths(reinterpret_cast<script::Array*>(element), out, depth + 1);
    }
}
void collect_paths(script::Struct* part, std::vector<const char*>* out, int depth)
{
    if (!part || depth > 4)
        return;
    for (script::Component* c = part->step_to_entry(nullptr); c; c = part->step_to_entry(c)) {
        switch (c->kind_tag()) {
        case script::kQbvalString:
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
            if (c->string && is_path(c->string))
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
                out->push_back(c->string);
            break;
        case script::kQbvalStruct:
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
            collect_paths(c->structure, out, depth + 1);
            break;
        case script::kQbvalArray:
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access): the script values' tagged 32-bit cells (param_struct.h), read by their tag
            collect_array_paths(c->array, out, depth + 1);
            break;
        default:
            break;
        }
    }
}

// One of our part lists: a global array of structures whose first has a desc_id.
bool is_part_list(script::Array* array)
{
    if (!array || array->kind_tag_ != script::kQbvalStruct || array->extent_ == 0)
        return false;
    auto* first = reinterpret_cast<script::Struct*>(array->fetch_part(0));
    uint32_t desc = 0;
    return first && first->read_hash_value(kDescId, &desc, 0);
}

std::vector<uint8_t> read_whole_file(const std::string& path)
{
    std::vector<uint8_t> data;
    const util::UniqueHandle file(
        CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!file)
        return data;
    const DWORD size = GetFileSize(file.get(), nullptr);
    if (size != INVALID_FILE_SIZE && size < 64u << 20) {
        data.resize(size);
        DWORD read = 0;
        if (!ReadFile(file.get(), data.data(), size, &read, nullptr) || read != size)
            data.clear();
    }
    return data;
}

std::string install_folder()
{
    char folder[MAX_PATH] = {};
    GetPrivateProfileStringA("features", "thugpro_folder", "", folder, sizeof(folder),
                             game_folder_path("thug2-decomp.ini").c_str());
    if (folder[0])
        return folder;
    char local[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableA("LOCALAPPDATA", local, sizeof(local));
    if (!length || length >= sizeof(local))
        return "";
    return std::string(local) + "\\THUG Pro";
}

} // namespace

std::vector<uint8_t> expand_lzss(const uint8_t* in, size_t length, size_t size)
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
            const uint8_t c = in[i++];
            out.push_back(c);
            window[r] = c;
            r = (r + 1) & 4095;
        } else {
            if (i + 1 >= length)
                break;
            const uint32_t position = in[i] | ((in[i + 1] & 0xF0u) << 4);
            const uint32_t last = (in[i + 1] & 0x0Fu) + 2;
            i += 2;
            for (uint32_t k = 0; k <= last && out.size() < size; ++k) {
                const uint8_t c = window[(position + k) & 4095];
                out.push_back(c);
                window[r] = c;
                r = (r + 1) & 4095;
            }
        }
    }
    return out;
}

bool read_archive(const std::vector<uint8_t>& archive, const std::vector<std::string>& names,
                  std::vector<std::pair<std::string, std::vector<uint8_t>>>* out)
{
    // {total size, version, file count}, then each file: {size, compressed size (0: stored), name size (int16), use
    // count, checksum}, its name, its data padded to 4 bytes (src/engine/file/packs.cpp next_raw_entry).
    if (archive.size() < 12)
        return false;
    auto u32 = [&archive](size_t at) {
        uint32_t value = 0;
        std::memcpy(&value, archive.data() + at, 4);
        return value;
    };
    const uint32_t count = u32(8);
    size_t offset = 12;
    for (uint32_t k = 0; k < count; ++k) {
        if (offset + 16 > archive.size())
            return false;
        const uint32_t size = u32(offset);
        const uint32_t compressed = u32(offset + 4);
        uint16_t name_size = 0;
        std::memcpy(&name_size, archive.data() + offset + 8, 2);
        const size_t body = offset + 16 + name_size;
        const size_t stored = compressed ? compressed : size;
        if (body > archive.size() || stored > archive.size() - body)
            return false;
        const std::string name(reinterpret_cast<const char*>(archive.data() + offset + 16),
                               strnlen(reinterpret_cast<const char*>(archive.data() + offset + 16), name_size));
        const std::string key = get_relative(name.c_str());
        for (const std::string& wanted : names) {
            if (key != wanted)
                continue;
            const uint8_t* data = archive.data() + body;
            std::vector<uint8_t> file =
                compressed ? expand_lzss(data, stored, size) : std::vector<uint8_t>(data, data + size);
            if (file.size() == size) // (a file that doesn't decompress to its size is left out)
                out->emplace_back(key, std::move(file));
        }
        offset = body + ((stored + 3) & ~static_cast<size_t>(3));
    }
    return true;
}

std::vector<std::pair<uint32_t, const uint8_t*>> top_level_arrays(const uint8_t* qb, size_t size)
{
    std::vector<std::pair<uint32_t, const uint8_t*>> arrays;
    const uint8_t* p = qb;
    const uint8_t* end = qb + size;
    while (p < end) {
        p = script::skip_newline_tokens(p);
        const uint8_t* before = p;
        switch (*p) {
        case 0x00:
            return arrays;
        case 0x16: { // name = value
            const uint32_t name = script::fetch_word32(p + 1);
            const uint8_t* q = script::skip_newline_tokens(p + 5);
            if (*q != 0x07) {
                p = script::step_over_token(p);
                break;
            }
            q = script::skip_newline_tokens(q + 1);
            if (*q == 0x05)
                arrays.emplace_back(name, q);
            p = script::skip_value(q);
            break;
        }
        case 0x23:
            p = script::skip_to_end_of_script(p + 6);
            break; // script name ... endscript
        case 0x45:
            p += 6;
            break; // a section marker
        default:
            p = script::step_over_token(p);
            break;
        }
        if (p <= before)
            return arrays; // (a token we can't move past: stop)
    }
    return arrays;
}

std::string path_key(const char* path)
{
    std::string key = get_relative(path);
    static const char* const kExtensions[] = {".xbx", ".skin", ".tex",  ".cas", ".col", ".img", ".png",
                                              ".mdl", ".ske",  ".geom", ".txx", ".dds", ".bmp"};
    for (bool again = true; again;) {
        again = false;
        for (const char* extension : kExtensions) {
            const size_t length = std::strlen(extension);
            if (key.size() > length && key.compare(key.size() - length, length, extension) == 0) {
                key.erase(key.size() - length);
                again = true;
            }
        }
    }
    return key;
}

std::vector<uint32_t> pick_new(script::Array* ours, script::Array* theirs, bool (*found)(const char* path))
{
    std::vector<uint32_t> picked;
    if (!ours || !theirs || theirs->kind_tag_ != script::kQbvalStruct)
        return picked;
    std::set<uint32_t> descs;
    for (uint32_t i = 0; i < ours->extent_; ++i) {
        auto* part = reinterpret_cast<script::Struct*>(ours->fetch_part(i));
        uint32_t desc = 0;
        if (part && part->read_hash_value(kDescId, &desc, 0))
            descs.insert(desc);
    }
    for (uint32_t i = 0; i < theirs->extent_; ++i) {
        auto* part = reinterpret_cast<script::Struct*>(theirs->fetch_part(i));
        uint32_t desc = 0;
        if (!part || !part->read_hash_value(kDescId, &desc, 0) || descs.count(desc))
            continue;
        std::vector<const char*> paths;
        collect_paths(part, &paths);
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

bool file_path(const char* relative, char (&path)[260])
{
    const std::set<std::string>* served = s_served.load();
    if (!served || !relative || !served->count(path_key(relative)))
        return false;
    const std::string full = s_data + relative;
    if (full.size() >= sizeof(path) || !is_file(full))
        return false;
    std::memcpy(path, full.c_str(), full.size() + 1);
    static std::set<std::string> logged;
    if (logged.size() < 1000 && logged.insert(full).second)
        get_log("THUGPRO CAS (TP-R10): %s read from THUGPRO's install (%s)", relative, full.c_str());
    return true;
}

void load_once()
{
    if (s_done)
        return;
    if (!script::fetch_list(kMasterEditableList)) {
        if (++s_frames > kWaitFrames)
            s_done = true;
        return;
    }
    s_done = true;
    const int setting = config_int("features", "thugpro_parts", 1);
    if (!setting) {
        get_log("THUGPRO CAS (TP-R10): off ([features] thugpro_parts=0) - a THUGPRO player's own parts fall back to "
                "THUG2's");
        return;
    }
    const std::string folder = install_folder();
    const std::string archive_path = folder + R"(\data\pre\thugpro_qb.prx)";
    const std::vector<uint8_t> archive = read_whole_file(archive_path);
    if (archive.empty()) {
        get_log("THUGPRO CAS (TP-R10): no THUGPRO install at %s - a THUGPRO player's own parts fall back to THUG2's "
                "([features] thugpro_folder= names another)",
                folder.c_str());
        return;
    }
    s_data = folder + "\\data\\";
    const std::vector<std::string> names(std::begin(kFiles), std::end(kFiles));
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
    if (!read_archive(archive, names, &files)) {
        get_log("THUGPRO CAS (TP-R10): %s isn't an archive we can read - THUGPRO's parts not loaded",
                archive_path.c_str());
        return;
    }
    // The arrays that are also our part lists, and every path our own parts name (never served from THUGPRO's).
    struct Target {
        uint32_t name;
        script::Array* ours;
        const uint8_t* theirs;
    };
    std::vector<Target> targets;
    for (auto& [name, data] : files) {
        const size_t size = data.size();
        data.resize(size + 16, 0); // (the walk stops at a 0 token)
        for (auto [array, at] : top_level_arrays(data.data(), size)) {
            script::Array* ours = script::fetch_list(array);
            if (is_part_list(ours))
                targets.push_back({array, ours, at});
        }
    }
    for (const Target& target : targets) {
        for (uint32_t i = 0; i < target.ours->extent_; ++i) {
            std::vector<const char*> paths;
            collect_paths(reinterpret_cast<script::Struct*>(target.ours->fetch_part(i)), &paths);
            for (const char* path : paths)
                our_keys().insert(path_key(path));
        }
    }
    int added = 0, lists = 0, skipped = 0;
    const memory::Hub* manager = memory::Hub::get_pull();
    memory::enter_heap_scope(manager->script_heap_);
    for (const Target& target : targets) {
        script::Array* theirs = script::alloc_script_array();
        if (!theirs)
            break;
        new (theirs) script::Array;
        script::parse_array(theirs, target.theirs, nullptr);
        std::vector<uint32_t> picked = pick_new(target.ours, theirs, get_found);
        int their_new = 0;
        if (theirs->kind_tag_ == script::kQbvalStruct) {
            std::set<uint32_t> descs;
            for (uint32_t i = 0; i < target.ours->extent_; ++i) {
                uint32_t desc = 0;
                auto* part = reinterpret_cast<script::Struct*>(target.ours->fetch_part(i));
                if (part && part->read_hash_value(kDescId, &desc, 0))
                    descs.insert(desc);
            }
            for (uint32_t i = 0; i < theirs->extent_; ++i) {
                uint32_t desc = 0;
                auto* part = reinterpret_cast<script::Struct*>(theirs->fetch_part(i));
                if (part && part->read_hash_value(kDescId, &desc, 0) && !descs.count(desc))
                    ++their_new;
            }
        }
        if (!picked.empty()) {
            const uint32_t old = target.ours->extent_;
            target.ours->rescale(old + static_cast<uint32_t>(picked.size()));
            uint32_t* elements = theirs->find_array_value();
            for (size_t k = 0; k < picked.size(); ++k) {
                auto* part = reinterpret_cast<script::Struct*>(elements[picked[k]]);
                elements[picked[k]] = 0; // (now ours: not freed with THUGPRO's array)
                if (setting == 1 && !part->has_bare_flag(kHidden))
                    part->put_name_key(0u, kHidden);
                if (setting == 1 && !part->has_bare_flag(kNoRandom))
                    part->put_name_key(0u, kNoRandom);
                target.ours->set_element(old + static_cast<uint32_t>(k), reinterpret_cast<uint32_t>(part));
            }
            ++lists;
        }
        added += static_cast<int>(picked.size());
        skipped += their_new - static_cast<int>(picked.size());
        script::release_array_storage(theirs);
        theirs->get_clear();
        script::delete_array(theirs);
    }
    memory::leave_heap_scope();
    s_served.store(new std::set<std::string>(their_files())); // (kept for the game's life)
    get_log(
        "THUGPRO CAS (TP-R10): %d of THUGPRO's parts added to %d of our lists from %s (%s); %d left out (files not in "
        "the install's loose data); %d of its loose files to read from there when asked for",
        added, lists, archive_path.c_str(),
        setting == 1 ? "hidden from our menus" : "offered in our CAS menus too: thugpro_parts=2", skipped,
        static_cast<int>(their_files().size()));
}

} // namespace objects::thugpro_cas
