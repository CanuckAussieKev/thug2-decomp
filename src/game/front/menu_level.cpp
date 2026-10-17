// NOT ORIGINAL - the menu_level feature (ours, added at the user's request; not the game's and not PartyMod's).
// See menu_level.h.

#include "game/front/menu_level.h"

#include <windows.h>

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "game/front/menu_backdrop.h"
#include "game/gfx/camera.h"
#include "game/gfx/screen.h"
#include "game/script/struct.h"
#include "hook/log.h"

namespace Front::MenuLevel {

namespace {

constexpr int kSetting = 3;
constexpr uint32_t kLevelMainmenu = 0x24E3054C;   // level_Mainmenu
constexpr uint32_t kMenuLevelYaw = 0xD36043EC;    // menu_level_yaw
constexpr uint32_t kMenuLevelOffset = 0xA041A52B; // menu_level_offset
constexpr float kShopRadius = 3000.0f;

// The data overlay: thug2-decomp-data next to the exe (files always used: the playable skate shop...) and its
// menu_level folder (the front end built in LA: used before the others while the setting is 3).
const std::string& Folder()
{
    static const std::string folder = GameFolderPath("thug2-decomp-data");
    return folder;
}
const std::string& MenuFolder()
{
    static const std::string folder = Folder() + "\\menu_level";
    return folder;
}
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

// The camera matrix rows 0-2 we last wrote (to tell a matrix set again in the shop's coordinates from ours).
Mth::Matrix g_written;
bool g_haveWritten;
// The camera position we last wrote.
float g_writtenPos[3];
bool g_haveWrittenPos;

} // namespace

bool Active()
{
    if (MenuBackdrop::Setting() != kSetting)
        return false;
    static const bool exists = [] {
        bool found = IsFolder(MenuFolder());
        // (logged only when the ini asks for it: self-tests set the setting for their run)
        if (ConfigInt("features", "menu_backdrop", 1) == kSetting)
            Log(found ? "menu_level: the front end is a level in Los Angeles (files from %s)"
                      : "menu_level: menu_backdrop=3 but %s doesn't exist - run tools/menu_level/make_menu_level.py; "
                        "the skate shop is used",
                MenuFolder().c_str());
        return found;
    }();
    return exists;
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

bool OverlayPath(const char* name, char (&path)[260])
{
    static const bool overlay = IsFolder(Folder());
    if (!name || !overlay)
        return false;
    char relative[260];
    OverlayName(name, relative);
    std::string full;
    if (Active() && IsFile(MenuFolder() + "\\" + relative))
        full = MenuFolder() + "\\" + relative;
    else if (IsFile(Folder() + "\\" + relative))
        full = Folder() + "\\" + relative;
    else
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

bool CurrentMove(Move* move)
{
    Script::CStruct* level = Script::GetStructure(kLevelMainmenu);
    if (!level)
        return false;
    float yaw = 0.0f;
    Script::VectorValue offset{};
    if (!level->GetFloat(kMenuLevelYaw, &yaw, 0) || !level->GetVector(kMenuLevelOffset, &offset, 0))
        return false;
    move->yaw = yaw;
    move->offset[0] = offset.x;
    move->offset[1] = offset.y;
    move->offset[2] = offset.z;
    return true;
}

bool InShop(const float pos[3])
{
    return pos[0] * pos[0] + pos[1] * pos[1] + pos[2] * pos[2] < kShopRadius * kShopRadius;
}

void TurnDirection(const Move& move, const float in[3], float out[3])
{
    // in * RotateY(yaw): rows (c, 0, -s), (0, 1, 0), (s, 0, c).
    double c = std::cos(static_cast<double>(move.yaw)), s = std::sin(static_cast<double>(move.yaw));
    double x = in[0], y = in[1], z = in[2];
    out[0] = static_cast<float>(x * c + z * s);
    out[1] = static_cast<float>(y);
    out[2] = static_cast<float>(-x * s + z * c);
}

void MovePoint(const Move& move, const float in[3], float out[3])
{
    float turned[3];
    TurnDirection(move, in, turned);
    for (int i = 0; i < 3; ++i)
        out[i] = turned[i] + move.offset[i];
}

namespace {

constexpr uint32_t kMainMenuCamera = 0xFCC018D3; // mainmenu_camera02

float& Zoom()
{
    static float zoom = [] {
        int percent = ConfigInt("features", "menu_zoom", 100);
        return percent >= 10 && percent <= 1000 ? percent / 100.0f : 1.0f;
    }();
    return zoom;
}

// The field of view the game gave the camera, and the one we wrote over it.
float g_gameFov, g_writtenFov;
bool g_haveWrittenFov;

// Page Up / Page Down held: zoom in / out (1% a frame), Home: back to 100%. The value is logged when the key goes
// up.
void UpdateZoomKeys()
{
    static bool changing = false;
    const uint8_t* keys = SDL_GetKeyboardState(nullptr);
    if (!keys)
        return;
    float& zoom = Zoom();
    bool in = keys[SDL_SCANCODE_PAGEUP] != 0, out = keys[SDL_SCANCODE_PAGEDOWN] != 0, reset = keys[SDL_SCANCODE_HOME];
    if (in)
        zoom = std::min(zoom * 1.01f, 10.0f);
    if (out)
        zoom = std::max(zoom / 1.01f, 0.1f);
    if (reset)
        zoom = 1.0f;
    if (in || out || reset) {
        changing = true;
    } else if (changing) {
        changing = false;
        Log("menu_level: main menu zoom %d%% (to keep it: menu_zoom=%d in thug2-decomp.ini [features])",
            static_cast<int>(zoom * 100.0f + 0.5f), static_cast<int>(zoom * 100.0f + 0.5f));
    }
}

// The main menu camera's field of view divided by the zoom (as a narrower or wider lens: the tangent of half the
// angle scaled).
void ApplyZoom(Gfx::Camera* camera)
{
    bool ours = g_haveWrittenFov && camera->m_hFov == g_writtenFov;
    if (!ours)
        g_gameFov = camera->m_hFov;
    if (MenuBackdrop::PlayingMenuCamera() != kMainMenuCamera) {
        if (ours) { // (the zoom is the main menu's only)
            camera->m_hFov = g_gameFov;
            camera->UpdateAdjustedHFOV();
            g_haveWrittenFov = false;
        }
        return;
    }
    UpdateZoomKeys();
    constexpr double kHalfRadians = 3.14159265358979 / 360.0;
    double half = std::atan(std::tan(g_gameFov * kHalfRadians) / Zoom());
    float fov = static_cast<float>(half / kHalfRadians);
    if (camera->m_hFov != fov) {
        camera->m_hFov = fov;
        camera->UpdateAdjustedHFOV();
    }
    g_writtenFov = fov;
    g_haveWrittenFov = true;
}

} // namespace

void PlaceCamera(Gfx::Camera* camera)
{
    if (!camera || !Active() || Nx::CurrentLevel() != 0 || camera != MenuBackdrop::ViewportCamera(0))
        return;
    Move move;
    if (!CurrentMove(&move))
        return;
    // Only a camera still in the shop's coordinates gets the move - position and orientation together. Cameras
    // placed from the skater (the main menu's mainmenu_camera02: targetOffset / positionOffset turn with the skater)
    // are already right. A position still as we last wrote it wasn't set again: its matrix is turned when the game
    // set it again since.
    float pos[3] = {camera->m_pos.x, camera->m_pos.y, camera->m_pos.z};
    bool ourPos = g_haveWrittenPos && std::memcmp(pos, g_writtenPos, sizeof(pos)) == 0;
    bool newMatrix = !g_haveWritten || std::memcmp(&camera->m_matrix, &g_written, sizeof(Mth::Vector) * 3) != 0;
    bool shop = !ourPos && InShop(pos);
    // [features] debug_backdrop: each new camera position the game gives (up to 40), and what became of it.
    static const bool debug = ConfigInt("features", "debug_backdrop", 0) != 0;
    static int logged = 0;
    static float lastLogged[3];
    if (debug && !ourPos && logged < 40 &&
        (logged == 0 || std::fabs(pos[0] - lastLogged[0]) + std::fabs(pos[1] - lastLogged[1]) +
                                std::fabs(pos[2] - lastLogged[2]) > 1.0f)) {
        ++logged;
        std::memcpy(lastLogged, pos, sizeof(pos));
        float out[3];
        MovePoint(move, pos, out);
        const Mth::Vector& back = camera->m_matrix.row[2];
        Log("menu_level debug: camera at (%.1f, %.1f, %.1f) looking along (%.3f, %.3f, %.3f): %s", pos[0], pos[1],
            pos[2], -back.x, -back.y, -back.z, shop ? "in the shop, moved" : "left as it is");
    }
    if (shop) {
        float out[3];
        MovePoint(move, pos, out);
        camera->m_pos.x = out[0];
        camera->m_pos.y = out[1];
        camera->m_pos.z = out[2];
        std::memcpy(g_writtenPos, out, sizeof(out));
        g_haveWrittenPos = true;
    }
    if (shop || (ourPos && newMatrix)) {
        for (int row = 0; row < 3; ++row) {
            Mth::Vector& r = camera->m_matrix.row[row];
            float in[3] = {r.x, r.y, r.z}, out[3];
            TurnDirection(move, in, out);
            r.x = out[0];
            r.y = out[1];
            r.z = out[2];
        }
        g_written = camera->m_matrix;
        g_haveWritten = true;
    }
    ApplyZoom(camera);
}

void TurnOrientation(Mth::Matrix* orientation)
{
    if (!orientation || !Active() || Nx::CurrentLevel() != 0)
        return;
    Move move;
    if (!CurrentMove(&move))
        return;
    for (int row = 0; row < 3; ++row) {
        Mth::Vector& r = orientation->row[row];
        float in[3] = {r.x, r.y, r.z}, out[3];
        TurnDirection(move, in, out);
        r.x = out[0];
        r.y = out[1];
        r.z = out[2];
    }
}

bool HideScene(uint32_t sceneName)
{
    return sceneName == MenuBackdrop::kFrontEndSceneName && Active() && Nx::CurrentLevel() == 0;
}

} // namespace Front::MenuLevel
