// NOT ORIGINAL - THAW's parked bikes (game/skater/thaw_bmx_parked.h).

#include "game/skater/thaw_bmx_parked.h"

#include <cmath>

#include "game/core/crc.h"
#include "game/front/front_end.h"
#include "game/front/level_editor.h"
#include "game/gfx/screen.h"
#include "game/mth/matrix.h"
#include "game/nx/feeler.h"
#include "game/obj/object.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/skate/skate_module_calls.h"
#include "game/skater/skater.h"
#include "game/skater/thaw_bmx.h"
#include "game/sys/log.h"

namespace Skater::ThawBmx {

namespace {

constexpr double kPi = 3.14159265358979323846;

uint32_t Checksum(const char* name) { return Crc::GenerateCRCFromString(name); }

// The scripts and globals (tools/pipeline/thaw_bmx/ride.ts, convert.ts).
constexpr const char* kHomeBike = "thaw_bmx_home";          // the level's bike's object
constexpr const char* kHomeScript = "thaw_bmx_home_bike";   // makes it (the modes it's in decided there)
constexpr const char* kLevelScript = "thaw_bmx_level_start"; // a new level: the last level's bikes gone
constexpr const char* kHomeBikes = "thaw_bmx_home_bikes";   // THAW's places: [{level=<name> pos=(...) heading=<deg>}]
// The skate module's level structure name (+0x380, SetLevelStructureName) and the structure's `level` string.
constexpr uint32_t kLevelStructure = 0x380;

constexpr int kSettleFrames = 30; // (in a new level, unpaused, before the start spot is noted)
constexpr int kCheckFrames = 30;  // (how often the level's bike is looked for)
constexpr float kEyeHeight = 36.0f; // (the line between the skater and a bike, this far up)
constexpr float kGroundAbove = 48.0f, kGroundBelow = 200.0f, kGroundStep = 60.0f; // (a start spot's ground)

struct Start {
    bool valid = false;
    Point pos{};
    float heading = 0.0f;
};

struct Level {
    uint32_t structure = 0;
    int frames = 0;
    bool noted = false;
    Start start;
} g_level;

Obj::CSkater* LocalSkater()
{
    if (!Mdl::Skate::Instance())
        return nullptr;
    return static_cast<Obj::CSkater*>(SkateCalls::GetLocalSkater());
}

Point PointOf(const Mth::Vector& v) { return {v.x, v.y, v.z}; }

// The game's collision ray (static collision, the nearest hit): true and the point when something is hit.
bool Hit(Point from, Point to, Point* at)
{
    alignas(16) uint8_t memory[sizeof(Nx::CFeeler)] = {};
    auto* feeler = reinterpret_cast<Nx::CFeeler*>(memory);
    feeler->Construct();
    Mth::Vector start = {from.x, from.y, from.z, 1.0f}, end = {to.x, to.y, to.z, 1.0f};
    if (!feeler->GetCollisionAlong(&start, &end, false))
        return false;
    if (at)
        *at = PointOf(feeler->m_point);
    return true;
}

bool Clear(Point a, Point b)
{
    return !Hit({a.x, a.y + kEyeHeight, a.z}, {b.x, b.y + kEyeHeight, b.z}, nullptr);
}

// The start spot: the first candidate with ground near the start's height and a clear line from the start.
Start FindStartSpot(Point pos, float heading)
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

Script::CStruct* LevelStructure()
{
    uint32_t name = SkateCalls::SkateField<uint32_t>(kLevelStructure);
    return name ? Script::GetStructure(name) : nullptr;
}

// THAW's place for the current level (thaw_bmx_home_bikes, by the level structure's `level`).
bool ThawSpot(Point* pos, float* heading)
{
    Script::CStruct* level = LevelStructure();
    const char* name = nullptr;
    if (!level || !level->GetString("level", &name, 0) || !name)
        return false;
    Script::CArray* bikes = Script::GetArray(kHomeBikes);
    if (!bikes || bikes->m_type != Script::ESYMBOLTYPE_STRUCTURE)
        return false;
    const uint32_t want = Checksum(name);
    for (uint32_t i = 0; i < bikes->m_size; ++i) {
        auto* bike = reinterpret_cast<Script::CStruct*>(bikes->GetElement(i));
        uint32_t levelName = 0;
        Script::VectorValue p = {};
        if (!bike || !bike->GetChecksum("level", &levelName, 0) || levelName != want || !bike->GetVector("pos", &p, 0))
            continue;
        *pos = {p.x, p.y, p.z};
        *heading = 0.0f;
        bike->GetFloat("heading", heading, 0);
        return true;
    }
    return false;
}

Obj::CCompositeObject* ObjectOf(Script::CStruct* params)
{
    uint32_t id = 0;
    if (!params || !params->GetChecksum("id", &id, 0))
        return nullptr;
    return static_cast<Obj::CCompositeObject*>(Obj::FindObject(id));
}

// The skater put at `pos` facing `heading` (standing upright), its drawn matrix and the core physics' with it.
void PlaceSkater(Obj::CSkater* skater, Point pos, float heading)
{
    double h = static_cast<double>(heading) * kPi / 180.0;
    float s = static_cast<float>(std::sin(h)), c = static_cast<float>(std::cos(h));
    for (Mth::Matrix* m : {&skater->m_matrix, &skater->m_displayMatrix}) {
        m->row[0] = {c, 0.0f, -s, 0.0f};
        m->row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
        m->row[2] = {s, 0.0f, c, 0.0f};
    }
    if (skater->m_corePhysics)
        *reinterpret_cast<Mth::Matrix*>(skater->m_corePhysics + 0x18) = skater->m_matrix; // (its display matrix)
    skater->m_pos = {pos.x, pos.y, pos.z, 1.0f};
    skater->m_oldPos = skater->m_pos;
}

// ThawBmx_HomeSpot: pos / heading of the level's bike.
bool __cdecl CFunc_HomeSpot(Script::CStruct*, Script::CScript* script)
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
    script->m_params->AddVector("pos", pos.x, pos.y, pos.z);
    script->m_params->AddFloat("heading", heading);
    Log("thaw_bmx: the level's bike at (%.0f, %.0f, %.0f) heading %.0f (%s)", pos.x, pos.y, pos.z, heading, from);
    return true;
}

// ThawBmx_WalkToBike id=<object>
bool __cdecl CFunc_WalkToBike(Script::CStruct* params, Script::CScript*)
{
    Obj::CSkater* skater = LocalSkater();
    Obj::CCompositeObject* bike = ObjectOf(params);
    if (!skater || !bike)
        return true;
    Point from = PointOf(skater->m_pos), to = PointOf(bike->m_pos);
    float heading = HeadingOf(skater->m_matrix.row[2].x, skater->m_matrix.row[2].z);
    Point next;
    if (StepToward(from, to, kWalkStep, &next)) {
        PlaceSkater(skater, to, HeadingOf(bike->m_matrix.row[2].x, bike->m_matrix.row[2].z));
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
bool __cdecl CFunc_BikeReachable(Script::CStruct* params, Script::CScript*)
{
    Obj::CSkater* skater = LocalSkater();
    Obj::CCompositeObject* bike = ObjectOf(params);
    return skater && bike && Clear(PointOf(skater->m_pos), PointOf(bike->m_pos));
}

void RunScript(const char* name)
{
    uint32_t checksum = Checksum(name);
    if (Script::ScriptExists(checksum))
        Script::RunScript(checksum, nullptr, nullptr, false, name);
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
    static const Script::CFunctionEntry functions[] = {
        {"ThawBmx_HomeSpot", reinterpret_cast<void*>(&CFunc_HomeSpot)},
        {"ThawBmx_WalkToBike", reinterpret_cast<void*>(&CFunc_WalkToBike)},
        {"ThawBmx_BikeReachable", reinterpret_cast<void*>(&CFunc_BikeReachable)},
    };
    Script::RegisterCFunctions(functions, sizeof(functions) / sizeof(functions[0]));
}

void UpdateParked(Mdl::FrontEnd* frontEnd)
{
    if ((frontEnd && frontEnd->m_paused) || Front::LevelEditor::Active())
        return;
    Obj::CSkater* skater = LocalSkater();
    uint32_t structure = Mdl::Skate::Instance() ? SkateCalls::SkateField<uint32_t>(kLevelStructure) : 0;
    if (!skater || !structure || Nx::CurrentLevel() == 0) {
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
        Point pos = PointOf(skater->m_pos);
        float heading = HeadingOf(skater->m_matrix.row[2].x, skater->m_matrix.row[2].z);
        g_level.start = FindStartSpot(pos, heading);
        g_level.noted = true;
        if (!g_level.start.valid)
            Log("thaw_bmx: no room for the level's bike near the start (%.0f, %.0f, %.0f)", pos.x, pos.y, pos.z);
        RunScript(kLevelScript);
    }
    if (g_level.frames % kCheckFrames != 0 || OnBike(skater->m_corePhysics) || Obj::FindObject(Checksum(kHomeBike)))
        return;
    RunScript(kHomeScript);
}

} // namespace Skater::ThawBmx
