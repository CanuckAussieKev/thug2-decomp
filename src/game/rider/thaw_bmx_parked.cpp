// NOT ORIGINAL - THAW's parked bikes (game/rider/thaw_bmx_parked.h).

#include "game/rider/thaw_bmx_parked.h"

#include <cmath>
#include <array>
#include <span>

#include "engine/core/checksums.h"
#include "frontend/front_end.h"
#include "editor/level_editor.h"
#include "engine/gfx/screen.h"
#include "engine/maths/mat4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "game/skate/skate_module_calls.h"
#include "game/rider/rider.h"
#include "game/rider/thaw_bmx.h"
#include "platform/run_log.h"
#include "game/rider/core_physics.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"

namespace skater::thaw_bmx {

namespace {

constexpr double kPi = 3.14159265358979323846;

uint32_t name_hash(const char* name) { return checksums::hash_from_text(name); }

// The scripts and globals (tools/pipeline/thaw_bmx/ride.ts, convert.ts).
constexpr const char* kHomeBike = "thaw_bmx_home";                // the level's bike's object
constexpr const char* kHomeScript = "thaw_bmx_home_bike";         // makes it (the modes it's in decided there)
constexpr const char* kLevelStartScript = "thaw_bmx_level_start"; // a new level: the last level's bikes gone
constexpr const char* kHomeBikes = "thaw_bmx_home_bikes"; // THAW's places: [{level=<name> pos=(...) heading=<deg>}]
// The skate module's level structure name (+0x380, SetLevelStructureName) and the structure's `level` string.

constexpr int kSettleFrames = 30;   // (in a new level, unpaused, before the start spot is noted)
constexpr int kCheckFrames = 30;    // (how often the level's bike is looked for)
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

objects::Rider* local_skater()
{
    if (!modules::Skate::sole())
        return nullptr;
    return core::down_cast<objects::Rider>(skate_calls::get_home_rider());
}

Point point_of(const maths::Vec4& v) { return {.x = v.x, .y = v.y, .z = v.z}; }

// The game's collision ray (static collision, the nearest hit): true and the point when something is hit.
bool get_hit(Point from, Point to, Point* at)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* feeler = &ray_storage;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0f};
    const maths::Vec4 end = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0f};
    if (!feeler->get_collision_along(&start, &end, false))
        return false;
    if (at)
        *at = point_of(feeler->contact_pos_);
    return true;
}

bool get_clear(Point a, Point b)
{
    return !get_hit({.x = a.x, .y = a.y + kEyeHeight, .z = a.z}, {.x = b.x, .y = b.y + kEyeHeight, .z = b.z}, nullptr);
}

// The start spot: the first candidate with ground near the start's height and a clear line from the start.
Kickoff find_start_spot(Point pos, float heading)
{
    std::array<Point, 4> candidates{};
    start_spot_candidates(pos, heading, kStartSpotDistance, candidates);
    for (const Point& c : candidates) {
        Point ground{};
        if (!get_hit({.x = c.x, .y = pos.y + kGroundAbove, .z = c.z}, {.x = c.x, .y = pos.y - kGroundBelow, .z = c.z}, &ground))
            continue;
        if (std::fabs(ground.y - pos.y) > kGroundStep || !get_clear(pos, ground))
            continue;
        return {.valid = true, .pos = ground, .heading = heading};
    }
    return {};
}

script::Struct* level_params_ref()
{
    const uint32_t name = modules::Skate::sole()->level_params_;
    return name ? script::read_sub_struct(name) : nullptr;
}

// THAW's place for the current level (thaw_bmx_home_bikes, by the level structure's `level`).
bool thaw_spot(Point* pos, float* heading)
{
    script::Struct* level = level_params_ref();
    const char* name = nullptr;
    if (!level || !level->fetch_text("level", &name, 0) || !name)
        return false;
    script::Array* bikes = script::fetch_list(kHomeBikes);
    if (!bikes || bikes->kind_tag_ != script::kQbvalStruct)
        return false;
    const uint32_t want = name_hash(name);
    for (uint32_t i = 0; i < bikes->extent_; ++i) {
        auto* bike = bikes->struct_at(i);
        uint32_t level_name = 0;
        script::VectorValue p = {};
        if (!bike || !bike->read_hash_value("level", &level_name, 0) || level_name != want ||
            !bike->fetch_vec("pos", &p, 0))
            continue;
        *pos = {.x = p.x, .y = p.y, .z = p.z};
        *heading = 0.0f;
        bike->fetch_float("heading", heading, 0);
        return true;
    }
    return false;
}

objects::GameEntity* object_of(script::Struct* params)
{
    uint32_t id = 0;
    if (!params || !params->read_hash_value("id", &id, 0))
        return nullptr;
    return core::down_cast<objects::GameEntity>(objects::find_object(id));
}

// The skater put at `pos` facing `heading` (standing upright), its drawn matrix and the core physics' with it.
void place_skater(objects::Rider* skater, Point pos, float heading)
{
    const double h = static_cast<double>(heading) * kPi / 180.0;
    auto s = static_cast<float>(std::sin(h)), c = static_cast<float>(std::cos(h));
    for (maths::Mat4* m : {&skater->basis_frame_, &skater->display_matrix_}) {
        core::at(m->row, 0) = {.x = c, .y = 0.0f, .z = -s, .w = 0.0f};
        core::at(m->row, 1) = {.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 0.0f};
        core::at(m->row, 2) = {.x = s, .y = 0.0f, .z = c, .w = 0.0f};
    }
    if (skater->core_physics_)
        skater->core_physics_->display_matrix_ = skater->basis_frame_; // (its display matrix)
    skater->at_point_ = {.x = pos.x, .y = pos.y, .z = pos.z, .w = 1.0f};
    skater->old_pos_ = skater->at_point_;
}

// ThawBmx_HomeSpot: pos / heading of the level's bike.
bool cfunc_home_spot(script::Struct*, script::Script* script)
{
    Point pos{};
    float heading = 0.0f;
    const char* from = "THAW's place";
    if (!thaw_spot(&pos, &heading)) {
        if (!g_level.start.valid)
            return false;
        pos = g_level.start.pos;
        heading = g_level.start.heading;
        from = "near the start";
    }
    script->args_->put_vec3("pos", pos.x, pos.y, pos.z);
    script->args_->push_float("heading", heading);
    get_log("thaw_bmx: the level's bike at (%.0f, %.0f, %.0f) heading %.0f (%s)", pos.x, pos.y, pos.z, heading, from);
    return true;
}

// ThawBmx_WalkToBike id=<object>
bool cfunc_walk_to_bike(script::Struct* params, script::Script*)
{
    objects::Rider* skater = local_skater();
    const objects::GameEntity* bike = object_of(params);
    if (!skater || !bike)
        return true;
    const Point from = point_of(skater->at_point_);
    const Point to = point_of(bike->at_point_);
    const float heading = get_heading_of(core::at(skater->basis_frame_.row, 2).x, core::at(skater->basis_frame_.row, 2).z);
    Point next{};
    if (step_toward(from, to, kWalkStep, &next)) {
        place_skater(skater, to, get_heading_of(core::at(bike->basis_frame_.row, 2).x, core::at(bike->basis_frame_.row, 2).z));
        return true;
    }
    // (turned towards it first, then walked)
    float turned = heading;
    if (!turn_toward(heading, get_heading_of(to.x - from.x, to.z - from.z), kWalkTurnDegrees, &turned))
        next = from;
    place_skater(skater, next, turned);
    return false;
}

// ThawBmx_BikeReachable id=<object>
bool cfunc_bike_reachable(script::Struct* params, script::Script*)
{
    const objects::Rider* skater = local_skater();
    const objects::GameEntity* bike = object_of(params);
    return skater && bike && get_clear(point_of(skater->at_point_), point_of(bike->at_point_));
}

void launch_script(const char* name)
{
    const uint32_t checksum = name_hash(name);
    if (script::script_exists(checksum))
        script::launch_script(checksum, nullptr, nullptr, false, name);
}

} // namespace

bool step_toward(Point from, Point to, float step, Point* out)
{
    const float dx = to.x - from.x;
    const float dz = to.z - from.z;
    const float distance = std::sqrt(dx * dx + dz * dz);
    if (distance <= step) {
        *out = to;
        return true;
    }
    const float t = step / distance;
    *out = {.x = from.x + dx * t, .y = from.y + (to.y - from.y) * t, .z = from.z + dz * t};
    return false;
}

bool turn_toward(float heading, float target, float max_step, float* out)
{
    auto wrap = [](float a) {
        a = std::fmod(a + 180.0f, 360.0f);
        if (a < 0.0f)
            a += 360.0f;
        return a - 180.0f;
    };
    const float difference = wrap(target - heading);
    if (std::fabs(difference) <= max_step) {
        *out = wrap(target);
        return true;
    }
    *out = wrap(heading + (difference > 0.0f ? max_step : -max_step));
    return false;
}

float get_heading_of(float x, float z)
{
    if (x == 0.0f && z == 0.0f)
        return 0.0f;
    return static_cast<float>(std::atan2(static_cast<double>(x), static_cast<double>(z)) * 180.0 / kPi);
}

void start_spot_candidates(Point pos, float heading, float distance, std::span<Point, 4> out)
{
    const double h = static_cast<double>(heading) * kPi / 180.0;
    auto ax = static_cast<float>(std::sin(h)), az = static_cast<float>(std::cos(h));  // (at)
    const float rx = az;
    const float rz = -ax; // (right: up x at)
    core::at(out, 0) = {.x = pos.x + rx * distance, .y = pos.y, .z = pos.z + rz * distance};
    core::at(out, 1) = {.x = pos.x - rx * distance, .y = pos.y, .z = pos.z - rz * distance};
    core::at(out, 2) = {.x = pos.x - ax * distance, .y = pos.y, .z = pos.z - az * distance};
    core::at(out, 3) = {.x = pos.x + ax * distance, .y = pos.y, .z = pos.z + az * distance};
}

void register_parked_functions()
{
    static const auto kFunctions = std::to_array<script::NativeCommand>({
        {.name = "ThawBmx_HomeSpot", .function = &cfunc_home_spot},
        {.name = "ThawBmx_WalkToBike", .function = &cfunc_walk_to_bike},
        {.name = "ThawBmx_BikeReachable", .function = &cfunc_bike_reachable},
    });
    script::install_script_functions(kFunctions);
}

void update_parked(modules::FrontEnd* front_end)
{
    if ((front_end && front_end->is_halted_) || frontend::level_editor::live_now())
        return;
    const objects::Rider* skater = local_skater();
    const uint32_t structure = modules::Skate::sole() ? modules::Skate::sole()->level_params_ : 0;
    if (!skater || !structure || render::current_level() == 0) {
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
        const Point pos = point_of(skater->at_point_);
        const float heading = get_heading_of(core::at(skater->basis_frame_.row, 2).x, core::at(skater->basis_frame_.row, 2).z);
        g_level.start = find_start_spot(pos, heading);
        g_level.noted = true;
        if (!g_level.start.valid)
            get_log("thaw_bmx: no room for the level's bike near the start (%.0f, %.0f, %.0f)", pos.x, pos.y, pos.z);
        launch_script(kLevelStartScript);
    }
    if (g_level.frames % kCheckFrames != 0 || on_bike(skater->core_physics_) ||
        objects::find_object(name_hash(kHomeBike)))
        return;
    launch_script(kHomeScript);
}

} // namespace skater::thaw_bmx
