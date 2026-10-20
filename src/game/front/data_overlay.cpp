// NOT ORIGINAL - the data overlay (ours, added at the user's request; not the game's and not PartyMod's).
// See data_overlay.h.

#include "game/front/data_overlay.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

#include "game/front/menu_backdrop.h"
#include "hook/log.h"

namespace Front::DataOverlay {

namespace {

bool IsFolder(const std::string& path)
{
    DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}
bool IsFile(const std::string& path)
{
    DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

} // namespace

const std::string& Folder()
{
    static const std::string folder = GameFolderPath("thug2-decomp-data");
    return folder;
}

void OverlayName(const char* name, char (&out)[260])
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

int SearchFolders(bool anteara, bool frontEnd, const char* (&folders)[4])
{
    int count = 0;
    if (anteara && frontEnd)
        folders[count++] = "anteara\\front_end\\";
    if (anteara)
        folders[count++] = "anteara\\";
    if (frontEnd)
        folders[count++] = "front_end\\";
    folders[count++] = "";
    return count;
}

bool AntearaMod()
{
    static const bool on = [] {
        bool setting = ConfigInt("features", "anteara_mod", 0) != 0;
        if (setting) {
            std::string folder = Folder() + "\\anteara";
            Log(IsFolder(folder) ? "anteara_mod: on - the Anteara Mod's scripts from %s (tools/anteara)"
                                 : "anteara_mod: on, but %s not found - run tools/menu_level/make_menu_level.ts; the "
                                   "game's scripts are used",
                folder.c_str());
        }
        return setting;
    }();
    return on;
}

bool OverlayPath(const char* name, char (&path)[260])
{
    static const bool overlay = IsFolder(Folder());
    if (!name || !overlay)
        return false;
    char relative[260];
    OverlayName(name, relative);
    std::string full;
    const char* folders[4];
    int count = SearchFolders(AntearaMod(), MenuBackdrop::Active(), folders);
    for (int i = 0; i < count && full.empty(); ++i) {
        std::string candidate = Folder() + "\\" + folders[i] + relative;
        if (IsFile(candidate))
            full = candidate;
    }
    if (full.empty())
        return false;
    if (full.size() >= sizeof(path))
        return false;
    std::memcpy(path, full.c_str(), full.size() + 1);
    // Each file the overlay serves, logged the first time (and how the game asked for it).
    static std::vector<std::string> logged;
    if (std::find(logged.begin(), logged.end(), full) == logged.end()) {
        logged.push_back(full);
        Log("data overlay: %s read from %s (asked for as %s)", relative, full.c_str(), name);
    }
    return true;
}

} // namespace Front::DataOverlay
