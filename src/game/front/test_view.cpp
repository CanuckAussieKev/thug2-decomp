// Test views: teleport standing, the camera lock (game/front/test_view.h). NOT ORIGINAL.

#include "game/front/test_view.h"

#include <cmath>
#include <cstdint>

#include "game/gfx/camera.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/nx/feeler.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/skate/skate_module.h"
#include "game/skater/skater.h"
#include "game/sys/mlp.h"
#include "hook/game.h"
#include "hook/log.h"

namespace Front::TestView {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kProbeAbove = 12.0f;  // the ground ray starts this far above the point...
constexpr float kProbeBelow = 4000.0f; // ...and ends this far below it

Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Length(Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
Vec3 Scale(Vec3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }

struct Lock {
    bool on;
    Vec3 pos, right, up, back, drift;
    uint32_t frame; // (the game frame it was locked at)
} g_lock;

Mth::Vector Row(Vec3 v, float w) { return {v.x, v.y, v.z, w}; }

// The local skater (the skate module's 0x00566520; null with none).
Obj::CSkater* LocalSkater()
{
    uint8_t* skate = reinterpret_cast<uint8_t*>(Mdl::g_skateModule);
    return skate ? reinterpret_cast<Mdl::Skate*>(skate)->GetLocalSkater()
                 : nullptr;
}

// The game's collision ray (Nx::CFeeler: static collision, the nearest hit) from `from` to `to`.
bool GroundBelow(Vec3 point, float* y)
{
    // (the game's memory layout, as level_editor_carry.cpp's FirstHit: static collision only, nothing to release)
    alignas(16) uint8_t memory[sizeof(Nx::CFeeler)] = {};
    auto* feeler = reinterpret_cast<Nx::CFeeler*>(memory);
    feeler->Construct();
    Mth::Vector start = {point.x, point.y + kProbeAbove, point.z, 1.0f};
    Mth::Vector end = {point.x, point.y - kProbeBelow, point.z, 1.0f};
    if (!feeler->GetCollisionAlong(&start, &end, false))
        return false;
    *y = feeler->m_point.y;
    return true;
}

// TeleportSkaterStanding pos=(x, y, z) [heading=<degrees>]
bool __cdecl CFunc_TeleportSkaterStanding(Script::CStruct* params, Script::CScript*)
{
    Script::VectorValue pos = {};
    if (!params || !params->GetVector("pos", &pos, 0)) {
        Log("TeleportSkaterStanding: pos=(x, y, z) needed");
        return false;
    }
    float heading = 0.0f;
    bool hasHeading = params->GetFloat("heading", &heading, 0);
    Vec3 at = {};
    std::string why = Teleport({pos.x, pos.y, pos.z}, hasHeading, heading, &at);
    if (!why.empty()) {
        Log("TeleportSkaterStanding: %s", why.c_str());
        return false;
    }
    return true;
}

} // namespace

// --- the pieces -------------------------------------------------------------------------------------------------

bool CameraRows(Vec3 direction, Vec3* right, Vec3* up, Vec3* back)
{
    float length = Length(direction);
    if (!(length > 1e-6f))
        return false;
    Vec3 b = Scale(direction, -1.0f / length);
    Vec3 r = Cross({0.0f, 1.0f, 0.0f}, b);
    float rl = Length(r);
    if (!(rl > 1e-4f))
        return false;
    r = Scale(r, 1.0f / rl);
    Vec3 u = Cross(b, r);
    u = Scale(u, 1.0f / Length(u));
    *right = r;
    *up = u;
    *back = b;
    return true;
}

void StandingRows(float headingDegrees, Vec3* right, Vec3* up, Vec3* at)
{
    double h = static_cast<double>(headingDegrees) * kPi / 180.0;
    float s = static_cast<float>(std::sin(h)), c = static_cast<float>(std::cos(h));
    *at = {s, 0.0f, c};
    *up = {0.0f, 1.0f, 0.0f};
    *right = Cross(*up, *at); // (cos, 0, -sin): heading 0 gives the identity
}

// --- the game ---------------------------------------------------------------------------------------------------

std::string Teleport(Vec3 position, bool hasHeading, float headingDegrees, Vec3* outPosition, bool onBoard)
{
    Obj::CSkater* skater = LocalSkater();
    if (!skater)
        return "no local skater";
    if (!hasHeading) {
        const Mth::Vector& at = skater->m_matrix.row[2];
        headingDegrees = static_cast<float>(std::atan2(static_cast<double>(at.x), static_cast<double>(at.z)) * 180.0 / kPi);
    }
    float ground = 0.0f;
    bool grounded = GroundBelow(position, &ground);
    if (grounded)
        position.y = ground;
    Vec3 right, up, at;
    StandingRows(headingDegrees, &right, &up, &at);
    // (the skater's own custom restart kept and put back: only the restart's code is borrowed)
    Mth::Vector savedPos = skater->m_customRestartPos;
    Mth::Matrix savedMatrix = skater->m_customRestartMatrix;
    bool savedWalking = skater->m_customRestartWalking;
    skater->m_customRestartPos = Row(position, 1.0f);
    skater->m_customRestartMatrix.row[0] = Row(right, 0.0f);
    skater->m_customRestartMatrix.row[1] = Row(up, 0.0f);
    skater->m_customRestartMatrix.row[2] = Row(at, 0.0f);
    skater->m_customRestartMatrix.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    skater->m_customRestartWalking = !onBoard;
    skater->SkipToCustomRestart();
    skater->m_customRestartPos = savedPos;
    skater->m_customRestartMatrix = savedMatrix;
    skater->m_customRestartWalking = savedWalking;
    Log("test view: skater teleported %s to (%.1f, %.1f, %.1f) heading %.1f degrees%s", onBoard ? "on its board" : "standing",
        position.x, position.y, position.z, headingDegrees, grounded ? " (on the ground below)" : " (no ground below: as given)");
    if (outPosition)
        *outPosition = position;
    return {};
}

bool LockCamera(Vec3 position, Vec3 direction, Vec3 drift)
{
    Vec3 right, up, back;
    if (!CameraRows(direction, &right, &up, &back))
        return false;
    g_lock = {true, position, right, up, back, drift, Mlp::FramesRun()};
    Log("test view: camera locked at (%.1f, %.1f, %.1f) looking along (%.3f, %.3f, %.3f), drifting (%.3f, %.3f, %.3f) a "
        "frame",
        position.x, position.y, position.z, -back.x, -back.y, -back.z, drift.x, drift.y, drift.z);
    return true;
}

void UnlockCamera()
{
    if (g_lock.on)
        Log("test view: camera unlocked");
    g_lock.on = false;
}

bool CameraLocked() { return g_lock.on; }

void PlaceCamera(Gfx::Camera* camera, int viewport)
{
    if (!g_lock.on || !camera || viewport != 0)
        return;
    float frames = static_cast<float>(Mlp::FramesRun() - g_lock.frame);
    camera->m_pos = Row({g_lock.pos.x + g_lock.drift.x * frames, g_lock.pos.y + g_lock.drift.y * frames,
                         g_lock.pos.z + g_lock.drift.z * frames},
                        1.0f);
    camera->m_matrix.row[0] = Row(g_lock.right, 0.0f);
    camera->m_matrix.row[1] = Row(g_lock.up, 0.0f);
    camera->m_matrix.row[2] = Row(g_lock.back, 0.0f);
}

void RegisterScriptFunctions()
{
    static const Script::CFunctionEntry functions[] = {
        {"TeleportSkaterStanding", reinterpret_cast<void*>(&CFunc_TeleportSkaterStanding)},
    };
    Script::RegisterCFunctions(functions, sizeof(functions) / sizeof(functions[0]));
}

} // namespace Front::TestView
