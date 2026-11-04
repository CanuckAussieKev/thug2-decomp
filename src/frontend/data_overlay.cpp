// NOT ORIGINAL - the data overlay (ours, added at the user's request; not the game's and not PartyMod's).
// See data_overlay.h.

#include "frontend/data_overlay.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <array>

#include "frontend/new_frontend.h"
#include "engine/gfx/hd_ui.h"
#include "game/skate/thugpro_cas.h"
#include "platform/run_log.h"
#include "base/core/checked_index.h"
#include "engine/file/file_load.h"

namespace frontend::data_overlay {

namespace {

constexpr int kOverlayNameLimit = 259; // the longest name that fits with its terminator
constexpr std::size_t kOverlayPrefixLength = 5;

bool is_folder(const std::string& path)
{
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U);
}
bool is_file(const std::string& path)
{
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U);
}

} // namespace

const std::string& get_folder()
{
    static const std::string kFolder = game_folder_path("thug2-decomp-data");
    return kFolder;
}

void overlay_name(const char* name, std::array<char, file::kOverlayPathSize>& out)
{
    int length = 0;
    for (const char character : std::string_view(name)) {
        if (length >= kOverlayNameLimit) {
            break;
        }
        core::at(out, length++) =
            character == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    core::at(out, length) = '\0';
    // Without anything up to the last "data\" (the development machine's paths, or the game's own "data\" prefix).
    const std::string_view whole(out.data());
    size_t rest = 0;
    for (size_t found = whole.find("data\\"); found != std::string_view::npos; found = whole.find("data\\", found + 1)) {
        if (found == 0 || core::at(whole, found - 1) == '\\') {
            rest = found + kOverlayPrefixLength;
        }
    }
    if (rest != 0) {
        std::memmove(out.data(), whole.substr(rest).data(), whole.size() - rest + 1);
    }
}

int search_folders(bool anteara, bool front_end, std::array<const char*, 4>& folders)
{
    int count = 0;
    if (anteara && front_end) {
        core::at(folders, count++) = "anteara\\front_end\\";
    }
    if (anteara) {
        core::at(folders, count++) = "anteara\\";
    }
    if (front_end) {
        core::at(folders, count++) = "front_end\\";
    }
    core::at(folders, count++) = "";
    return count;
}

bool anteara_mod()
{
    static const bool kOn = [] {
        const bool setting = config_int("features", "anteara_mod", 0) != 0;
        if (setting) {
            const std::string folder = get_folder() + "\\anteara";
            if (is_folder(folder)) {
                get_log("anteara_mod: on - Anteara Mod's scripts from %s (tools/anteara)", folder.c_str());
            } else {
                get_log("anteara_mod: on, but %s not found - run tools/pipeline/menu_level/make_menu_level.ts; the "
                        "game's scripts are used",
                        folder.c_str());
            }
        }
        return setting;
    }();
    return kOn;
}

bool get_overlay_path(const char* name, std::array<char, file::kOverlayPathSize>& path)
{
    static const bool kOverlay = is_folder(get_folder());
    if (!name) {
        return false;
    }
    std::array<char, file::kOverlayPathSize> relative{};
    overlay_name(name, relative);
    std::string full;
    // First, while [features] hd_ui is on: the upscaled menu images and fonts (engine/gfx/hd_ui.h), in the hd_ui folder
    // under the same names (also the ones inside the .prx packs, which the overlay serves by name).
    if (kOverlay && render::hd_ui::is_switched_on()) {
        const std::string candidate = get_folder() + "\\hd_ui\\" + relative.data();
        if (is_file(candidate)) {
            full = candidate;
        }
    }
    std::array<const char*, 4> folders{};
    const int count =
        kOverlay && full.empty() ? search_folders(anteara_mod(), new_frontend::is_switched_on(), folders) : 0;
    for (int i = 0; i < count && full.empty(); ++i) {
        const std::string candidate = get_folder() + "\\" + core::at(folders, i) + relative.data();
        if (is_file(candidate)) {
            full = candidate;
        }
    }
    // Last: a THUGPRO part's file from the user's THUGPRO install (TP-R10, game/skate/thugpro_cas.h).
    if (full.empty()) {
        return objects::thugpro_cas::file_path(relative.data(), path);
    }
    if (full.size() >= sizeof(path)) {
        return false;
    }
    std::memcpy(path.data(), full.c_str(), full.size() + 1);
    // Each file the overlay serves, logged the first time (and how the game asked for it).
    static std::vector<std::string> logged;
    if (std::ranges::find(logged, full) == logged.end()) {
        logged.push_back(full);
        get_log("data overlay: %s read from %s (asked for as %s)", relative.data(), full.c_str(), name);
    }
    return true;
}

} // namespace frontend::data_overlay
