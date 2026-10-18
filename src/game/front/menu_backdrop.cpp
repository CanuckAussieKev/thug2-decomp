// NOT ORIGINAL - the menu_backdrop feature (ours, added at the user's request; not the game's and not PartyMod's).
// See menu_backdrop.h.

#include "game/front/menu_backdrop.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <string>

#include "game/front/data_overlay.h"
#include "game/gfx/camera.h"
#include "game/gfx/scene.h"
#include "game/gfx/screen.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Front::MenuBackdrop {

namespace {

constexpr uint32_t kScene = 0x26861025; // scene
constexpr uint32_t kIsNet = 0x417149C8; // is_net

// The view the user took with the level editor's P key in THPS4's skate shop (level 11, scene 072538EB): looking
// over the shop's desk.
constexpr View kView = {
    {564.790f, 74.075f, -1114.151f}, {0.007642f, 0.023744f, 0.999689f}, {-0.000181f, 0.999718f, -0.023743f}};

// The backdrop's scene file in the data overlay.
constexpr const char* kBackdropFile = "levels\\thps4_skateshop\\thps4_skateshop.scn.xbx";

// The front-end menu cameras (skater camera animations, levels\mainmenu\*.ska and the network menu's).
constexpr uint32_t kMenuCameras[] = {
    0x4C17CD39, // mainmenu_camera
    0xFCC018D3, // mainmenu_camera02: the main menu
    0x8BC72845, // mainmenu_camera03: play_no_skater_cam (options, network, memory card... menus)
    0x89ECAF5C, // SS_MenuCam: the network select menu
    0x7A99B2EB, // level_select_cam: the level select (the skater hidden)
};

// The loaded scenes: a list (head 0x006FA7E0) of nodes {?, scene +4, ?, next +0xC} ending at the head (scene -1).
struct SceneNode {
    uint32_t unknown0;
    uint8_t* scene; // its name checksum at +0x84
    uint32_t unknown8;
    SceneNode* next;
};
bool IsEnd(const SceneNode* node) { return reinterpret_cast<intptr_t>(node->scene) == -1; }
uint32_t SceneName(const uint8_t* scene) { return *reinterpret_cast<const uint32_t*>(scene + 0x84); }

// The name of the skater camera animation playing (the skate module's movie manager +0x368: 0x0057B030,
// thiscall), 0 if none (or one without a name).
uint32_t PlayingCameraAnim()
{
    uint8_t* skate = GameVariable<uint8_t*>(0x007CE478);
    if (!skate)
        return 0;
    void* movies = *reinterpret_cast<void**>(skate + 0x368);
    if (!movies)
        return 0;
    return GameFunction<uint32_t(__thiscall*)(void*)>(0x0057B030)(movies);
}

bool On(int setting) { return ViewFor(setting) != nullptr; }

int g_forcedInstalled = -1;

} // namespace

bool BackdropLoaded()
{
    for (SceneNode* node = GameVariable<SceneNode*>(0x006FA7EC); node && !IsEnd(node); node = node->next) {
        if (SceneName(node->scene) == kBackdropSceneName)
            return true;
    }
    return false;
}

// (0x004A09C0, cdecl)
Gfx::Camera* ViewportCamera(int viewport) { return GameFunction<Gfx::Camera*(__cdecl*)(int)>(0x004A09C0)(viewport); }

int& Setting()
{
    static int setting = ConfigInt("features", "menu_backdrop", 1);
    return setting;
}

bool Installed()
{
    if (g_forcedInstalled >= 0)
        return g_forcedInstalled != 0;
    static const bool installed = [] {
        std::string path = DataOverlay::Folder() + "\\" + kBackdropFile;
        DWORD attributes = GetFileAttributesA(path.c_str());
        bool found = attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
        // (logged only when the ini asks for the backdrop: self-tests set the setting for their run)
        if (ConfigInt("features", "menu_backdrop", 1) != 0)
            Log(found ? "menu_backdrop: the front end in THPS4's skate shop (%s)"
                      : "menu_backdrop: %s not found - convert the THPS4 levels (tools/thps4_levels) and run "
                        "tools/menu_level/make_menu_level.py; the original front end is used",
                path.c_str());
        return found;
    }();
    return installed;
}

void ForceInstalled(int installed) { g_forcedInstalled = installed; }

bool Active() { return On(Setting()) && Installed(); }

const View* ViewFor(int setting) { return setting != 0 ? &kView : nullptr; }

Mth::Matrix ViewMatrix(const View& view)
{
    auto normalized = [](double x, double y, double z) {
        double length = std::sqrt(x * x + y * y + z * z);
        return Mth::Vector{static_cast<float>(x / length), static_cast<float>(y / length),
                           static_cast<float>(z / length), 0.0f};
    };
    const float* b = view.back;
    const float* u = view.up;
    Mth::Matrix m;
    m.row[0] = normalized(static_cast<double>(u[1]) * b[2] - static_cast<double>(u[2]) * b[1],
                          static_cast<double>(u[2]) * b[0] - static_cast<double>(u[0]) * b[2],
                          static_cast<double>(u[0]) * b[1] - static_cast<double>(u[1]) * b[0]);
    m.row[1] = normalized(u[0], u[1], u[2]);
    m.row[2] = normalized(b[0], b[1], b[2]);
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    return m;
}

void OnSceneLoaded(const char* name, bool isNet)
{
    // (only the front end's: the playable skate shop - the data overlay's - uses the same scene)
    if (!name || _stricmp(name, "mainmenu") != 0 || Nx::CurrentLevel() != 0 || !Active())
        return;
    Log("menu_backdrop: loading the scenes %s and %s behind the front end", kBackdropSky, kBackdropScene);
    for (const char* scene : {kBackdropSky, kBackdropScene}) {
        alignas(4) uint8_t memory[8];
        Script::CStruct* params = reinterpret_cast<Script::CStruct*>(memory)->Init();
        params->AddString(kScene, scene);
        if (isNet)
            params->AddChecksum(0u, kIsNet);
        Nx::CFunc_LoadScene(params, nullptr);
        params->Clear();
    }
}

bool IsMenuCamera(uint32_t animName)
{
    for (uint32_t camera : kMenuCameras) {
        if (animName == camera)
            return true;
    }
    return false;
}

bool ShouldHold(int setting, bool frontEnd, bool backdropLoaded, bool menuCamera)
{
    return On(setting) && frontEnd && backdropLoaded && menuCamera;
}

bool ShouldHideScene(uint32_t sceneName, int setting, bool frontEnd, bool backdropLoaded)
{
    return On(setting) && frontEnd && backdropLoaded && sceneName == kFrontEndSceneName;
}

bool Holding()
{
    int setting = Setting();
    if (!On(setting) || Nx::CurrentLevel() != 0 || !BackdropLoaded())
        return false;
    return ShouldHold(setting, true, true, IsMenuCamera(PlayingCameraAnim()));
}

bool HideScene(uint32_t sceneName)
{
    int setting = Setting();
    if (!On(setting) || Nx::CurrentLevel() != 0 || !BackdropLoaded())
        return false;
    return ShouldHideScene(sceneName, setting, true, true);
}

void ApplyView(Gfx::Camera* camera, const View& view)
{
    camera->m_pos = {view.pos[0], view.pos[1], view.pos[2], 1.0f};
    Mth::Matrix m = ViewMatrix(view);
    for (int row = 0; row < 3; ++row)
        camera->m_matrix.row[row] = m.row[row];
    float fov = Nx::GetScreenAngle();
    if (camera->m_hFov != fov) {
        camera->m_hFov = fov;
        camera->UpdateAdjustedHFOV();
    }
}

void HoldCamera(Gfx::Camera* camera)
{
    if (!camera || !Holding() || camera != ViewportCamera(0))
        return;
    ApplyView(camera, *ViewFor(Setting()));
}

} // namespace Front::MenuBackdrop
