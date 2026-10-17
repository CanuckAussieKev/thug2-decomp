// NOT ORIGINAL - the menu_level feature (ours, added at the user's request; not the game's and not PartyMod's).
// See menu_level.h.

#include "game/front/menu_level.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <string>

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

const std::string& Folder()
{
    static const std::string folder = GameFolderPath("thug2-decomp-data");
    return folder;
}

// The camera matrix rows 0-2 we last wrote (to tell a matrix set again in the shop's coordinates from ours).
Mth::Matrix g_written;
bool g_haveWritten;

} // namespace

bool Active()
{
    if (MenuBackdrop::Setting() != kSetting)
        return false;
    static const bool exists = [] {
        DWORD attributes = GetFileAttributesA(Folder().c_str());
        bool found = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
        Log(found ? "menu_level: the front end is a level in Los Angeles (files from %s)"
                  : "menu_level: menu_backdrop=3 but %s doesn't exist - run tools/menu_level/make_menu_level.py; "
                    "the skate shop is used",
            Folder().c_str());
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
    if (!name || !Active())
        return false;
    char relative[260];
    OverlayName(name, relative);
    std::string full = Folder() + "\\" + relative;
    if (full.size() >= sizeof(path))
        return false;
    DWORD attributes = GetFileAttributesA(full.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return false;
    std::memcpy(path, full.c_str(), full.size() + 1);
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

void PlaceCamera(Gfx::Camera* camera)
{
    if (!camera || !Active() || Nx::CurrentLevel() != 0 || camera != MenuBackdrop::ViewportCamera(0))
        return;
    Move move;
    if (!CurrentMove(&move))
        return;
    float pos[3] = {camera->m_pos.x, camera->m_pos.y, camera->m_pos.z};
    bool shopPos = InShop(pos);
    bool newMatrix = !g_haveWritten || std::memcmp(&camera->m_matrix, &g_written, sizeof(Mth::Vector) * 3) != 0;
    if (shopPos) {
        float moved[3];
        MovePoint(move, pos, moved);
        camera->m_pos.x = moved[0];
        camera->m_pos.y = moved[1];
        camera->m_pos.z = moved[2];
    }
    // The orientation when the game set it again since ours (with the position or alone).
    if (newMatrix) {
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
