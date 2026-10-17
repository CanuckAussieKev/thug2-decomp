// NOT ORIGINAL - the menu_backdrop feature (ours, added at the user's request; not the game's and not PartyMod's).
// See menu_backdrop.h.

#include "game/front/menu_backdrop.h"

#include <cmath>
#include <cstring>

#include "game/front/frontend_objects.h"
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

// The views the user picked with /where in Los Angeles (level 15).
constexpr View kViews[] = {
    {{-1808.255f, -342.621f, 5804.185f}, {0.618382f, 0.178992f, -0.765223f}, {-0.112357f, 0.983850f, 0.139335f}},
    {{-1995.710f, -342.599f, 6016.290f}, {-0.683391f, 0.179143f, 0.707732f}, {0.124546f, 0.983823f, -0.128765f}},
};

// The front-end menu cameras (skater camera animations, levels\mainmenu\*.ska and the network menu's).
constexpr uint32_t kMenuCameras[] = {
    0x4C17CD39, // mainmenu_camera
    0xFCC018D3, // mainmenu_camera02: the main menu (the spinning skater)
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

} // namespace

bool BackdropLoaded()
{
    for (SceneNode* node = GameVariable<SceneNode*>(0x006FA7EC); node && !IsEnd(node); node = node->next) {
        if (SceneName(node->scene) == kBackdropSceneName)
            return true;
    }
    return false;
}

uint32_t PlayingMenuCamera()
{
    uint32_t anim = PlayingCameraAnim();
    return IsMenuCamera(anim) ? anim : 0;
}

// (0x004A09C0, cdecl)
Gfx::Camera* ViewportCamera(int viewport) { return GameFunction<Gfx::Camera*(__cdecl*)(int)>(0x004A09C0)(viewport); }

int& Setting()
{
    static int setting = ConfigInt("features", "menu_backdrop", 1);
    return setting;
}

const View* ViewFor(int setting)
{
    if (setting < 1 || setting > static_cast<int>(sizeof(kViews) / sizeof(kViews[0])))
        return nullptr;
    return &kViews[setting - 1];
}

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
    if (!On(Setting()) || !name || _stricmp(name, "mainmenu") != 0)
        return;
    Log("menu_backdrop: loading the scenes %s and %s behind the front end (view %d)", kBackdropSky, kBackdropScene,
        Setting());
    for (const char* scene : {kBackdropSky, kBackdropScene}) {
        alignas(4) uint8_t memory[8];
        Script::CStruct* params = reinterpret_cast<Script::CStruct*>(memory)->Init();
        params->AddString(kScene, scene);
        if (isNet)
            params->AddChecksum(0u, kIsNet);
        Nx::CFunc_LoadScene(params, nullptr);
        params->Clear();
    }
    // The front-end objects may be moved into the backdrop again (frontend_objects.h).
    FrontEndObjects::OnBackdropLoaded();
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

bool ShouldHideScene(uint32_t sceneName, int setting, bool frontEnd, bool backdropLoaded, bool holding)
{
    if (!On(setting) || !frontEnd || !backdropLoaded)
        return false;
    if (holding)
        return sceneName == kFrontEndSceneName;
    return sceneName == kBackdropSceneName || sceneName == kBackdropSkyName;
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
    bool holding = IsMenuCamera(PlayingCameraAnim());
    return ShouldHideScene(sceneName, setting, true, true, holding);
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

namespace {
// The pose the game last gave the viewport camera while it was held (before the view replaced it).
struct GamePose {
    bool pos, matrix;
    Mth::Vector p;
    Mth::Matrix m;
};
GamePose g_gamePose;
} // namespace

void NoteCameraPos(Gfx::Camera* camera, const Mth::Vector& pos)
{
    if (!camera || !Holding() || camera != ViewportCamera(0))
        return;
    g_gamePose.p = pos;
    g_gamePose.pos = true;
}

void NoteCameraMatrix(Gfx::Camera* camera, const Mth::Matrix& matrix)
{
    if (!camera || !Holding() || camera != ViewportCamera(0))
        return;
    g_gamePose.m = matrix;
    g_gamePose.matrix = true;
}

bool GameCameraPose(Mth::Vector* pos, Mth::Matrix* matrix)
{
    if (!g_gamePose.pos || !g_gamePose.matrix)
        return false;
    *pos = g_gamePose.p;
    *matrix = g_gamePose.m;
    return true;
}

void ForgetGameCameraPose() { g_gamePose.pos = g_gamePose.matrix = false; }

void HoldCamera(Gfx::Camera* camera)
{
    if (!camera || !Holding() || camera != ViewportCamera(0))
        return;
    ApplyView(camera, *ViewFor(Setting()));
}

} // namespace Front::MenuBackdrop
