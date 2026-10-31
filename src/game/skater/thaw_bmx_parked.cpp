// NOT ORIGINAL - THAW's parked bikes (game/skater/thaw_bmx_parked.h).

#include "game/skater/thaw_bmx_parked.h"

#include <cmath>

#include "game/core/checksums.h"
#include "game/front/front_end.h"
#include "game/front/level_editor.h"
#include "game/gfx/screen.h"
#include "game/maths/mat4.h"
#include "game/engine3d/probe_ray.h"
#include "game/obj/entity.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/param_struct.h"
#include "game/skate/skate_module_calls.h"
#include "game/skater/rider.h"
#include "game/skater/thaw_bmx.h"
#include "game/sys/run_log.h"

namespace Skater::ThawBmx {

namespace {

constexpr double kPi = 3.14159265358979323846;

uint32_t NameHash(const char* name) { return Checksums::HashFromText(name); }

// The scripts and globals (tools/pipeline/thaw_bmx/ride.ts, convert.ts).
constexpr const char* kHomeBike = "thaw_bmx_home";          // the level's bike's object
constexpr const char* kHomeScript = "thaw_bmx_home_bike";   // makes it (the modes it's in decided there)
constexpr const char* kLevelStartScript = "thaw_bmx_level_start"; // a new level: the last level's bikes gone
constexpr const char* kHomeBikes = "thaw_bmx_home_bikes";   // THAW's places: [{level=<name> pos=(...) heading=<deg>}]
// The skate module's level structure name (+0x380, SetLevelStructureName) and the structure's `level` string.
constexpr uint32_t kLevelDataKey = 0x380;

constexpr int kSettleFrames = 30; // (in a new level, unpaused, before the start spot is noted)
constexpr int kCheckFrames = 30;  // (how often the level's bike is looked for)
constexpr float kEyeHeight = 36.0f; // (the line between the skater and a bike, this far up)
constexpr float kGroundAbove = 48.0f, kGroundBelow = 200.0f, kGroundStep = 60.0f; // (a start spot's ground)

struct Kickoff {
    bool valid = false;
    Point pos{};
    float heading = 0.0f;
};

struct StageMap {
    uint32_t structure = 0;
    int frames = 0;
    bool noted = false;
    Kickoff start;
} g_level;

GameObjects::CRider* LocalSkater()
{
    if (!GameModules::Skate::Sole())
        return nullptr;
    return static_cast<GameObjects::CRider*>(SkateCalls::GetHomeRider());
}

Point PointOf(const Maths::Vec4& v) { return {v.x, v.y, v.z}; }

// The game's collision ray (static collision, the nearest hit): true and the point when something is hit.
bool Hit(Point from, Point to, Point* at)
{
    alignas(16) uint8_t memory[sizeof(Engine3D::CProbeRay)] = {};
    auto* feeler = reinterpret_cast<Engine3D::CProbeRay*>(memory);
    feeler->MakeNew();
    Maths::Vec4 start = {from.x, from.y, from.z, 1.0f}, end = {to.x, to.y, to.z, 1.0f};
    if (!feeler->GetCollisionAlong(&start, &end, false))
        return false;
    if (at)
        *at = PointOf(feeler->m_contactPos);
    return true;
}

bool Clear(Point a, Point b)
{
    return !Hit({a.x, a.y + kEyeHeight, a.z}, {b.x, b.y + kEyeHeight, b.z}, nullptr);
}

// The start spot: the first candidate with ground near the start's height and a clear line from the start.
Kickoff FindStartSpot(Point pos, float heading)
{
    Point candidates[4];
    StartSpotCandidates(pos, heading, kStartSpotDistance, candidates);
    for (const Point& c : candidates) {
        Point ground;
        if (!Hit({c.x, pos.y + kGroundAbove, c.z}, {c.x, pos.y - kGroundBelow, c.z}, &ground))
            continue;
        if (std::fabs(ground.y - pos.y) > kGroundStep || !Clear(pos, ground))
            continue;
        return {true, ground, heading};
    }
    return {};
}

Scripts::CStruct* LevelParamsRef()
{
    uint32_t name = SkateCalls::SkateField<uint32_t>(kLevelDataKey);
    return name ? Scripts::ReadSubStruct(name) : nullptr;
}

// THAW's place for the current level (thaw_bmx_home_bikes, by the level structure's `level`).
bool ThawSpot(Point* pos, float* heading)
{
    Scripts::CStruct* level = LevelParamsRef();
    const char* name = nullptr;
    if (!level || !level->FetchText("level", &name, 0) || !name)
        return false;
    Scripts::CArray* bikes = Scripts::FetchList(kHomeBikes);
    if (!bikes || bikes->m_kindTag != Scripts::QBVAL_STRUCT)
        return false;
    const uint32_t want = NameHash(name);
    for (uint32_t i = 0; i < bikes->m_extent; ++i) {
        auto* bike = reinterpret_cast<Scripts::CStruct*>(bikes->FetchPart(i));
        uint32_t levelName = 0;
        Scripts::VectorValue p = {};
        if (!bike || !bike->ReadHashValue("level", &levelName, 0) || levelName != want || !bike->FetchVec("pos", &p, 0))
            continue;
        *pos = {p.x, p.y, p.z};
        *heading = 0.0f;
        bike->FetchFloat("heading", heading, 0);
        return true;
    }
    return false;
}

GameObjects::CGameEntity* ObjectOf(Scripts::CStruct* params)
{
    uint32_t id = 0;
    if (!params || !params->ReadHashValue("id", &id, 0))
        return nullptr;
    return static_cast<GameObjects::CGameEntity*>(GameObjects::FindObject(id));
}

// The skater put at `pos` facing `heading` (standing upright), its drawn matrix and the core physics' with it.
void PlaceSkater(GameObjects::CRider* skater, Point pos, float heading)
{
    double h = static_cast<double>(heading) * kPi / 180.0;
    float s = static_cast<float>(std::sin(h)), c = static_cast<float>(std::cos(h));
    for (Maths::Mat4* m : {&skater->m_basisFrame, &skater->m_displayMatrix}) {
        m->row[0] = {c, 0.0f, -s, 0.0f};
        m->row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
        m->row[2] = {s, 0.0f, c, 0.0f};
    }
    if (skater->m_corePhysics)
        *reinterpret_cast<Maths::Mat4*>(skater->m_corePhysics + 0x18) = skater->m_basisFrame; // (its display matrix)
    skater->m_atPoint = {pos.x, pos.y, pos.z, 1.0f};
    skater->m_oldPos = skater->m_atPoint;
}

// ThawBmx_HomeSpot: pos / heading of the level's bike.
bool __cdecl CFunc_HomeSpot(Scripts::CStruct*, Scripts::CScript* script)
{
    Point pos{};
    float heading = 0.0f;
    const char* from = "THAW's place";
    if (!ThawSpot(&pos, &heading)) {
        if (!g_level.start.valid)
            return false;
        pos = g_level.start.pos;
        heading = g_level.start.heading;
        from = "near the start";
    }
    script->m_args->PutVec3("pos", pos.x, pos.y, pos.z);
    script->m_args->PushFloat("heading", heading);
    Log("thaw_bmx: the level's bike at (%.0f, %.0f, %.0f) heading %.0f (%s)", pos.x, pos.y, pos.z, heading, from);
    return true;
}

// ThawBmx_WalkToBike id=<object>
bool __cdecl CFunc_WalkToBike(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CRider* skater = LocalSkater();
    GameObjects::CGameEntity* bike = ObjectOf(params);
    if (!skater || !bike)
        return true;
    Point from = PointOf(skater->m_atPoint), to = PointOf(bike->m_atPoint);
    float heading = HeadingOf(skater->m_basisFrame.row[2].x, skater->m_basisFrame.row[2].z);
    Point next;
    if (StepToward(from, to, kWalkStep, &next)) {
        PlaceSkater(skater, to, HeadingOf(bike->m_basisFrame.row[2].x, bike->m_basisFrame.row[2].z));
        return true;
    }
    // (turned towards it first, then walked)
    float turned = heading;
    if (!TurnToward(heading, HeadingOf(to.x - from.x, to.z - from.z), kWalkTurnDegrees, &turned))
        next = from;
    PlaceSkater(skater, next, turned);
    return false;
}

// ThawBmx_BikeReachable id=<object>
bool __cdecl CFunc_BikeReachable(Scripts::CStruct* params, Scripts::CScript*)
{
    GameObjects::CRider* skater = LocalSkater();
    GameObjects::CGameEntity* bike = ObjectOf(params);
    return skater && bike && Clear(PointOf(skater->m_atPoint), PointOf(bike->m_atPoint));
}

void LaunchScript(const char* name)
{
    uint32_t checksum = NameHash(name);
    if (Scripts::ScriptExists(checksum))
        Scripts::LaunchScript(checksum, nullptr, nullptr, false, name);
}

} // namespace

bool StepToward(Point from, Point to, float step, Point* out)
{
    float dx = to.x - from.x, dz = to.z - from.z;
    float distance = std::sqrt(dx * dx + dz * dz);
    if (distance <= step) {
        *out = to;
        return true;
    }
    float t = step / distance;
    *out = {from.x + dx * t, from.y + (to.y - from.y) * t, from.z + dz * t};
    return false;
}

bool TurnToward(float heading, float target, float maxStep, float* out)
{
    auto wrap = [](float a) {
        a = std::fmod(a + 180.0f, 360.0f);
        if (a < 0.0f)
            a += 360.0f;
        return a - 180.0f;
    };
    float difference = wrap(target - heading);
    if (std::fabs(difference) <= maxStep) {
        *out = wrap(target);
        return true;
    }
    *out = wrap(heading + (difference > 0.0f ? maxStep : -maxStep));
    return false;
}

float HeadingOf(float x, float z)
{
    if (x == 0.0f && z == 0.0f)
        return 0.0f;
    return static_cast<float>(std::atan2(static_cast<double>(x), static_cast<double>(z)) * 180.0 / kPi);
}

void StartSpotCandidates(Point pos, float heading, float distance, Point out[4])
{
    double h = static_cast<double>(heading) * kPi / 180.0;
    float ax = static_cast<float>(std::sin(h)), az = static_cast<float>(std::cos(h)); // (at)
    float rx = az, rz = -ax;                                                           // (right: up x at)
    out[0] = {pos.x + rx * distance, pos.y, pos.z + rz * distance};
    out[1] = {pos.x - rx * distance, pos.y, pos.z - rz * distance};
    out[2] = {pos.x - ax * distance, pos.y, pos.z - az * distance};
    out[3] = {pos.x + ax * distance, pos.y, pos.z + az * distance};
}

void RegisterParkedFunctions()
{
    static const Scripts::CNativeCommand functions[] = {
        {"ThawBmx_HomeSpot", reinterpret_cast<void*>(&CFunc_HomeSpot)},
        {"ThawBmx_WalkToBike", reinterpret_cast<void*>(&CFunc_WalkToBike)},
        {"ThawBmx_BikeReachable", reinterpret_cast<void*>(&CFunc_BikeReachable)},
    };
    Scripts::InstallScriptFunctions(functions, sizeof(functions) / sizeof(functions[0]));
}

void UpdateParked(GameModules::FrontEnd* frontEnd)
{
    if ((frontEnd && frontEnd->m_isHalted) || MenuSystem::LevelEditor::LiveNow())
        return;
    GameObjects::CRider* skater = LocalSkater();
    uint32_t structure = GameModules::Skate::Sole() ? SkateCalls::SkateField<uint32_t>(kLevelDataKey) : 0;
    if (!skater || !structure || Engine3D::CurrentLevel() == 0) {
        g_level.frames = 0;
        return;
    }
    if (structure != g_level.structure) {
        g_level = {};
        g_level.structure = structure;
    }
    ++g_level.frames;
    if (!g_level.noted) {
        if (g_level.frames < kSettleFrames)
            return;
        // (where the skater is now: where the level started it)
        Point pos = PointOf(skater->m_atPoint);
        float heading = HeadingOf(skater->m_basisFrame.row[2].x, skater->m_basisFrame.row[2].z);
        g_level.start = FindStartSpot(pos, heading);
        g_level.noted = true;
        if (!g_level.start.valid)
            Log("thaw_bmx: no room for the level's bike near the start (%.0f, %.0f, %.0f)", pos.x, pos.y, pos.z);
        LaunchScript(kLevelStartScript);
    }
    if (g_level.frames % kCheckFrames != 0 || OnBike(skater->m_corePhysics) || GameObjects::FindObject(NameHash(kHomeBike)))
        return;
    LaunchScript(kHomeScript);
}

} // namespace Skater::ThawBmx
