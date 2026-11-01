// NOT ORIGINAL - the data overlay (ours, added at the user's request; not the game's and not PartyMod's).
// See data_overlay.h.

#include "frontend/data_overlay.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

#include "frontend/new_frontend.h"
#include "engine/gfx/hd_ui.h"
#include "game/skate/thugpro_cas.h"
#include "platform/run_log.h"

namespace frontend::data_overlay {

namespace {

bool is_folder(const std::string& path)
{
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}
bool is_file(const std::string& path)
{
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

} // namespace

const std::string& get_folder()
{
    static const std::string kFolder = game_folder_path("thug2-decomp-data");
    return kFolder;
}

void overlay_name(const char* name, char (&out)[260])
{
    int length = 0;
    for (const char* c = name; *c && length < 259; ++c)
        out[length++] = *c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
    out[length] = '\0';
    // Without anything up to the last "data\" (the development machine's paths, or the game's own "data\" prefix).
    const char* rest = out;
    for (const char* found = std::strstr(out, "data\\"); found; found = std::strstr(found + 1, "data\\")) {
        if (found == out || found[-1] == '\\')
            rest = found + 5;
    }
    if (rest != out)
        std::memmove(out, rest, std::strlen(rest) + 1);
}

int search_folders(bool anteara, bool front_end, const char* (&folders)[4])
{
    int count = 0;
    if (anteara && front_end)
        folders[count++] = "anteara\\front_end\\";
    if (anteara)
        folders[count++] = "anteara\\";
    if (front_end)
        folders[count++] = "front_end\\";
    folders[count++] = "";
    return count;
}

bool anteara_mod()
{
    static const bool kOn = [] {
        const bool setting = config_int("features", "anteara_mod", 0) != 0;
        if (setting) {
            const std::string folder = get_folder() + "\\anteara";
            get_log(is_folder(folder)
                        ? "anteara_mod: on - Anteara Mod's scripts from %s (tools/anteara)"
                        : "anteara_mod: on, but %s not found - run tools/pipeline/menu_level/make_menu_level.ts; the "
                          "game's scripts are used",
                    folder.c_str());
        }
        return setting;
    }();
    return kOn;
}

bool get_overlay_path(const char* name, char (&path)[260])
{
    static const bool kOverlay = is_folder(get_folder());
    if (!name)
        return false;
    char relative[260];
    overlay_name(name, relative);
    std::string full;
    // First, while [features] hd_ui is on: the upscaled menu images and fonts (engine/gfx/hd_ui.h), in the hd_ui folder
    // under the same names (also the ones inside the .prx packs, which the overlay serves by name).
    if (kOverlay && render::hd_ui::is_switched_on()) {
        const std::string candidate = get_folder() + "\\hd_ui\\" + relative;
        if (is_file(candidate))
            full = candidate;
    }
    const char* folders[4];
    const int count =
        kOverlay && full.empty() ? search_folders(anteara_mod(), new_frontend::is_switched_on(), folders) : 0;
    for (int i = 0; i < count && full.empty(); ++i) {
        const std::string candidate = get_folder() + "\\" + folders[i] + relative;
        if (is_file(candidate))
            full = candidate;
    }
    // Last: a THUGPRO part's file from the user's THUGPRO install (TP-R10, game/skate/thugpro_cas.h).
    if (full.empty())
        return objects::thugpro_cas::file_path(relative, path);
    if (full.size() >= sizeof(path))
        return false;
    std::memcpy(path, full.c_str(), full.size() + 1);
    // Each file the overlay serves, logged the first time (and how the game asked for it).
    static std::vector<std::string> logged;
    if (std::find(logged.begin(), logged.end(), full) == logged.end()) {
        logged.push_back(full);
        get_log("data overlay: %s read from %s (asked for as %s)", relative, full.c_str(), name);
    }
    return true;
}

} // namespace frontend::data_overlay
