// NOT ORIGINAL - texture sets (engine/gfx/texture_sets.h).

#include "engine/gfx/texture_sets.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <ratio>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "engine/ext/extension.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "engine/gfx/texture_dict.h"
#include "engine/memory/memory_report.h"
#include "formats/texture_set_record.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"

namespace render::texture_sets {

namespace {

constexpr std::size_t kSettingSize = 64;
constexpr std::size_t kReadChunk = 4096;
constexpr std::string_view kDictionarySuffix = ".tex.xbx";
constexpr std::string_view kLevelsPrefix = "levels\\";

struct LoadedDictionary {
    TextureSet* dictionary = nullptr;
    uint32_t key = 0; // its key in the loaded dictionaries' table (checked before a reload: still that dictionary)
    std::string file; // as load_tex_dict opened it ("levels\bo\bo.tex.xbx")
};

struct TextureSetsState {
    bool read = false;
    std::string active = kOriginal; // the set the data overlay serves
    std::string wanted;             // the set to switch to at the next frame ("" none)
    std::string last_switch;        // the last switch's line (status)
    std::vector<LoadedDictionary> dictionaries;
    Reloader reloader = nullptr;
    // the sets' records as read, by set name (read again after a switch: a set installed anew while the game runs)
    std::mutex records_mutex;
    std::map<std::string, texture_set_record::Record, std::less<>> records;
};

TextureSetsState& state()
{
    static core::NoDestroy<TextureSetsState> value;
    return value.get();
}

std::string lower(std::string_view text)
{
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](char ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); });
    return out;
}

bool is_file(const std::string& path) { return fs::is_file_remembered(path); }

// A set's name as the ini and the console give it: lower case, trimmed; "" is the original.
std::string set_name(std::string_view text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return kOriginal;
    }
    const size_t last = text.find_last_not_of(" \t\r\n");
    return lower(text.substr(first, last - first + 1));
}

// [Graphics] texture_set as the ini has it now.
std::string ini_setting()
{
    std::array<char, kSettingSize> setting{};
    build_info::fetch_text(build_info::kGraphics, "texture_set", "", setting.data(), setting.size());
    return set_name(setting.data());
}

// The set to use for `wanted`: itself when installed, else the original (logged with why).
std::string usable(const std::string& wanted)
{
    if (wanted == kOriginal || installed(wanted)) {
        return wanted;
    }
    get_log("texture sets: [Graphics] texture_set=%s isn't installed (no %s\\%s\\set.json - "
            "tools/pipeline/hd_textures/base.ts --model %s) - the original textures",
            wanted.c_str(), sets_folder().c_str(), wanted.c_str(), wanted.c_str());
    return kOriginal;
}

void read_once()
{
    TextureSetsState& st = state();
    if (st.read) {
        return;
    }
    st.read = true;
    const std::string setting = ini_setting();
    st.active = usable(setting);
    const std::vector<std::string> sets = installed_sets();
    std::string listed;
    for (const std::string& set : sets) {
        listed += (listed.empty() ? "" : ", ") + set;
    }
    if (!sets.empty() || st.active != kOriginal) {
        get_log("texture sets: %s in use (installed: %s)", st.active.c_str(), listed.empty() ? "none" : listed.c_str());
    }
}

class TextureSetsExtension final : public ext::Extension {
public:
    void menu_frame(modules::FrontEnd* /*front_end*/, bool /*game_owned*/) override { tick(); }
};

} // namespace

const std::string& sets_folder()
{
    static const std::string kFolder = game_folder_path("thug2-decomp-textures");
    return kFolder;
}

bool installed(std::string_view set)
{
    const std::string name = set_name(set);
    if (name == kOriginal) {
        return true;
    }
    // (a plain name: no separators, no "..")
    if (name.find_first_of("\\/:.") != std::string::npos) {
        return false;
    }
    return is_file(sets_folder() + "\\" + name + "\\set.json");
}

std::vector<std::string> installed_sets()
{
    std::vector<std::string> sets;
    for (const char* set : kKnownSets) {
        if (installed(set)) {
            sets.emplace_back(set);
        }
    }
    return sets;
}

namespace {

// A set's set.json read (empty for the original, a set not installed, and a record that can't be read - logged).
texture_set_record::Record read_record(const std::string& name)
{
    texture_set_record::Record record;
    if (name == kOriginal || !installed(name)) {
        return record;
    }
    const std::string path = sets_folder() + "\\" + name + "\\set.json";
    const fs::OwnedStream file = fs::open_stream(path.c_str(), "rb", fs::StreamSharing::kSecure);
    if (!file) {
        return record;
    }
    std::string text;
    std::array<char, kReadChunk> buffer{};
    for (size_t got = 0; (got = fs::disk().stream_read(file.get(), buffer.data(), 1, buffer.size())) > 0;) {
        text.append(buffer.data(), got);
    }
    if (!texture_set_record::read(text, &record)) {
        get_log("texture sets: %s isn't a set record base.ts wrote (or is damaged) - its textures taken as not de-lit",
                path.c_str());
    }
    return record;
}

// What `use` gives for a set's record (read the first time it is wanted; the records' lock held).
template <class Use>
auto with_record(std::string_view set, const Use& use)
{
    const std::string name = set_name(set);
    TextureSetsState& st = state();
    const std::scoped_lock lock(st.records_mutex);
    auto found = st.records.find(name);
    if (found == st.records.end()) {
        found = st.records.emplace(name, read_record(name)).first;
    }
    return use(found->second);
}

} // namespace

int delight_version(std::string_view set)
{
    return with_record(set, [](const texture_set_record::Record& record) { return record.delight; });
}

std::vector<uint32_t> delit_textures(std::string_view set, std::string_view overlay_name)
{
    return with_record(set, [&](const texture_set_record::Record& record) {
        return record.delight > 0 ? texture_set_record::delit_of(record, overlay_name) : std::vector<uint32_t>{};
    });
}

const std::string& active()
{
    read_once();
    return state().active;
}

namespace {
int& originals_wanted()
{
    thread_local int wanted = 0;
    return wanted;
}
} // namespace

OriginalFiles::OriginalFiles() { ++originals_wanted(); }
OriginalFiles::~OriginalFiles() { --originals_wanted(); }

bool file_for(const char* overlay_name, std::string* path)
{
    if (overlay_name == nullptr || originals_wanted() > 0) {
        return false;
    }
    const std::string_view name(overlay_name);
    if (name.size() <= kDictionarySuffix.size() || !name.ends_with(kDictionarySuffix)) {
        return false;
    }
    const std::string& set = active();
    if (set == kOriginal) {
        return false;
    }
    std::string candidate = sets_folder() + "\\" + set + "\\" + std::string(name);
    if (!is_file(candidate)) {
        return false;
    }
    *path = std::move(candidate);
    return true;
}

std::string request(std::string_view set)
{
    read_once();
    const std::string name = set_name(set);
    if (!installed(name)) {
        return name + ": not installed (tools/pipeline/hd_textures/base.ts --game <folder> --model " + name + ")";
    }
    if (name == state().active && state().wanted.empty()) {
        return name + " already in use";
    }
    state().wanted = name;
    return name + ": switching (the next frame)";
}

bool request_from_ini()
{
    read_once();
    const std::string wanted = usable(ini_setting());
    get_log("texture sets: %s", request(wanted).c_str());
    return true;
}

bool tick()
{
    TextureSetsState& st = state();
    if (st.wanted.empty()) {
        return false;
    }
    const std::string to = st.wanted;
    st.wanted.clear();
    if (to == st.active) {
        return false;
    }
    const std::string from = st.active;
    st.active = to; // (the data overlay serves the new set's files from here on)
    {
        const std::scoped_lock lock(st.records_mutex);
        st.records.clear(); // (read again: the dictionaries read below take their de-lit lists from them)
    }
    const auto start = std::chrono::steady_clock::now();
    int dictionaries = 0;
    int textures = 0;
    int failed = 0;
    if (st.reloader != nullptr) {
        // (a copy: a reload adds or removes no dictionary, but the list is the loader's)
        const std::vector<LoadedDictionary> loaded = st.dictionaries;
        for (const LoadedDictionary& entry : loaded) {
            if (!entry.file.starts_with(kLevelsPrefix) || find_tex_dict(entry.key) != entry.dictionary) {
                continue; // (the level's only: a model's next load takes the set)
            }
            const int swapped = st.reloader(entry.dictionary, entry.file.c_str());
            if (swapped < 0) {
                ++failed;
                continue;
            }
            ++dictionaries;
            textures += swapped;
        }
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    st.last_switch = text::format("%s -> %s: %d dictionaries read again (%d textures) in %.0f ms%s", from.c_str(),
                                 to.c_str(), dictionaries, textures, ms,
                                 failed != 0 ? text::format(", %d couldn't be read", failed).c_str() : "");
    get_log("texture sets: %s", st.last_switch.c_str());
    memory::log_memory_use((std::string("after the texture set ") + to).c_str());
    return true;
}

std::string status_text()
{
    read_once();
    std::string sets;
    for (const std::string& set : installed_sets()) {
        const int delight = delight_version(set);
        sets += " " + set + (delight > 0 ? text::format(" (de-lit, v%d)", delight) : std::string());
    }
    std::string text = "textures: " + state().active + " in use; installed: original" + sets;
    if (!state().wanted.empty()) {
        text += "; switching to " + state().wanted;
    }
    if (!state().last_switch.empty()) {
        text += "\nlast switch: " + state().last_switch;
    }
    return text;
}

void dictionary_loaded(TextureSet* dictionary, const char* file)
{
    if (dictionary == nullptr || file == nullptr) {
        return;
    }
    std::vector<LoadedDictionary>& list = state().dictionaries;
    const auto found = std::ranges::find(list, dictionary, &LoadedDictionary::dictionary);
    if (found != list.end()) {
        found->key = dictionary->checksum();
        found->file = lower(file);
        return;
    }
    list.push_back({.dictionary = dictionary, .key = dictionary->checksum(), .file = lower(file)});
}

void dictionary_unloaded(const TextureSet* dictionary)
{
    std::erase_if(state().dictionaries, [dictionary](const LoadedDictionary& entry) { return entry.dictionary == dictionary; });
}

void set_reloader(Reloader reloader) { state().reloader = reloader; }

std::string console_command(const std::string& arguments)
{
    std::istringstream in(arguments);
    std::string set;
    in >> set;
    if (set.empty()) {
        return status_text();
    }
    return request(set);
}

ext::Extension& extension()
{
    static TextureSetsExtension the_extension;
    return the_extension;
}

} // namespace render::texture_sets
