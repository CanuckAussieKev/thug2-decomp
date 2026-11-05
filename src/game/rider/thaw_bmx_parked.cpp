// NOT ORIGINAL - THAW's parked bikes (game/rider/thaw_bmx_parked.h).

#include "game/rider/thaw_bmx_parked.h"

#include <cmath>
#include <array>
#include <cstdint>
#include <span>
#include <numbers>

#include "base/core/checksums.h"
#include "base/maths/vec4.h"
#include "game/front_end/front_end.h"
#include "engine/ext/extension.h"
#include "engine/gfx/screen.h"
#include "base/maths/mat4.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "engine/script/context.h"
#include "game/skate/skate_module.h"
#include "game/skate/skate_module_calls.h"
#include "game/rider/rider.h"
#include "game/rider/thaw_bmx.h"
#include "platform/run_log.h"
#include "game/rider/core_physics.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"

namespace skater::thaw_bmx {

namespace {
constexpr float kHalfTurnF = 180.0F;
constexpr float kTurnF = 360.0F;
constexpr double kHalfTurnD = 180.0;


uint32_t name_hash(const char* name) { return checksums::hash_from_text(name); }

// The scripts and globals (tools/pipeline/thaw_bmx/ride.ts, convert.ts).
constexpr const char* kHomeBike = "thaw_bmx_home";                // the level's bike's object
constexpr const char* kHomeScript = "thaw_bmx_home_bike";         // makes it (the modes it's in decided there)
constexpr const char* kLevelStartScript = "thaw_bmx_level_start"; // a new level: the last level's bikes gone
constexpr const char* kHomeBikes = "thaw_bmx_home_bikes"; // THAW's places: [{level=<name> pos=(...) heading=<deg>}]
// The skate module's level structure name (+0x380, SetLevelStructureName) and the structure's `level` string.

constexpr int kSettleFrames = 30;   // (in a new level, unpaused, before the start spot is noted)
constexpr int kCheckFrames = 30;    // (how often the level's bike is looked for)
constexpr float kEyeHeight = 36.0F; // (the line between the skater and a bike, this far up)
constexpr float kGroundAbove = 48.0F, kGroundBelow = 200.0F, kGroundStep = 60.0F; // (a start spot's ground)

struct Kickoff {
    bool valid = false;
    Point pos{};
    float heading = 0.0F;
};

struct StageMap {
    uint32_t structure = 0;
    int frames = 0;
    bool noted = false;
    Kickoff start;
};

StageMap& stage_map()
{
    static core::NoDestroy<StageMap> state;
    return state.get();
}

objects::Rider* local_skater()
{
    if (!modules::Skate::sole()) {
        return nullptr;
    }
    return core::down_cast<objects::Rider>(skate_calls::get_home_rider());
}

Point point_of(const maths::Vec4& v) { return {.x = v.x, .y = v.y, .z = v.z}; }

// The game's collision ray (static collision, the nearest hit): true and the point when something is hit.
bool get_hit(Point from, Point to, Point* at)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* feeler = &ray_storage;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0F};
    const maths::Vec4 end = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0F};
    if (feeler->get_collision_along(&start, &end, false) == 0) {
        return false;
    }
    if (at) {
        *at = point_of(feeler->contact_pos());
    }
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
    start_spot_candidates(pos, {.heading = heading, .distance = kStartSpotDistance}, candidates);
    for (const Point& candidate : candidates) {
        Point ground{};
        if (!get_hit({.x = candidate.x, .y = pos.y + kGroundAbove, .z = candidate.z},
                     {.x = candidate.x, .y = pos.y - kGroundBelow, .z = candidate.z}, &ground)) {
            continue;
        }
        if (std::fabs(ground.y - pos.y) > kGroundStep || !get_clear(pos, ground)) {
            continue;
        }
        return {.valid = true, .pos = ground, .heading = heading};
    }
    return {};
}

script::Struct* level_params_ref()
{
    const uint32_t name = modules::Skate::sole()->level_params();
    return (name != 0U) ? script::read_sub_struct(name) : nullptr;
}

// THAW's place for the current level (thaw_bmx_home_bikes, by the level structure's `level`).
bool thaw_spot(Point* pos, float* heading)
{
    script::Struct* level = level_params_ref();
    const char* name = nullptr;
    if (!level || !level->fetch_text("level", &name, 0) || !name) {
        return false;
    }
    script::Array* bikes = script::fetch_list(kHomeBikes);
    if (!bikes || bikes->kind_tag() != script::QbValueKind::kStruct) {
        return false;
    }
    const uint32_t want = name_hash(name);
    for (uint32_t i = 0; i < bikes->extent(); ++i) {
        auto* bike = bikes->struct_at(i);
        uint32_t level_name = 0;
        script::VectorValue stored_position = {};
        if (!bike || !bike->read_hash_value("level", &level_name, 0) || level_name != want ||
            !bike->fetch_vec("pos", &stored_position, 0)) {
            continue;
        }
        *pos = {.x = stored_position.x, .y = stored_position.y, .z = stored_position.z};
        *heading = 0.0F;
        bike->fetch_float("heading", heading, 0);
        return true;
    }
    return false;
}

objects::GameEntity* object_of(script::Struct* params)
{
    uint32_t id = 0;
    if (!params || !params->read_hash_value("id", &id, 0)) {
        return nullptr;
    }
    return core::down_cast<objects::GameEntity>(objects::find_object(id));
}

// The skater put at `pos` facing `heading` (standing upright), its drawn matrix and the core physics' with it.
void place_skater(objects::Rider* skater, Point pos, float heading)
{
    const double heading_radians = static_cast<double>(heading) * std::numbers::pi / 180.0;
    auto sine = static_cast<float>(std::sin(heading_radians));
    auto cosine = static_cast<float>(std::cos(heading_radians));
    for (maths::Mat4* matrix : {&skater->basis_frame(), &skater->display_matrix()}) {
        core::at(matrix->row, 0) = {.x = cosine, .y = 0.0F, .z = -sine, .w = 0.0F};
        core::at(matrix->row, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 0.0F};
        core::at(matrix->row, 2) = {.x = sine, .y = 0.0F, .z = cosine, .w = 0.0F};
    }
    if (skater->core_physics()) {
        skater->core_physics()->display_matrix() = skater->basis_frame(); // (its display matrix)
    }
    skater->at_point() = {.x = pos.x, .y = pos.y, .z = pos.z, .w = 1.0F};
    skater->old_pos() = skater->at_point();
}

// ThawBmx_HomeSpot: pos / heading of the level's bike.
bool cfunc_home_spot(script::Struct* /*params*/, script::Context context)
{
    Point pos{};
    float heading = 0.0F;
    const char* from = "THAW's place";
    if (!thaw_spot(&pos, &heading)) {
        if (!stage_map().start.valid) {
            return false;
        }
        pos = stage_map().start.pos;
        heading = stage_map().start.heading;
        from = "near the start";
    }
    context.args()->put_vec3("pos", {.x = pos.x, .y = pos.y, .z = pos.z});
    context.args()->push_float("heading", heading);
    get_log("thaw_bmx: the level's bike at (%.0f, %.0f, %.0f) heading %.0f (%s)", pos.x, pos.y, pos.z, heading, from);
    return true;
}

// ThawBmx_WalkToBike id=<object>
bool cfunc_walk_to_bike(script::Struct* params, script::Context /*context*/)
{
    objects::Rider* skater = local_skater();
    const objects::GameEntity* bike = object_of(params);
    if (!skater || !bike) {
        return true;
    }
    const Point from = point_of(skater->at_point());
    const Point to = point_of(bike->at_point());
    const float heading = get_heading_of(core::at(skater->basis_frame().row, 2).x, core::at(skater->basis_frame().row, 2).z);
    Point next{};
    if (step_toward(from, to, kWalkStep, &next)) {
        place_skater(skater, to, get_heading_of(core::at(bike->basis_frame().row, 2).x, core::at(bike->basis_frame().row, 2).z));
        return true;
    }
    // (turned towards it first, then walked)
    float turned = heading;
    if (!turn_toward(
            {.heading = heading, .target = get_heading_of(to.x - from.x, to.z - from.z), .max_step_ = kWalkTurnDegrees},
            &turned)) {
        next = from;
    }
    place_skater(skater, next, turned);
    return false;
}

// ThawBmx_BikeReachable id=<object>
bool cfunc_bike_reachable(script::Struct* params, script::Context /*context*/)
{
    const objects::Rider* skater = local_skater();
    const objects::GameEntity* bike = object_of(params);
    return (skater != nullptr) && (bike != nullptr) &&
           get_clear(point_of(skater->at_point()), point_of(bike->at_point()));
}

void launch_script(const char* name)
{
    const uint32_t checksum = name_hash(name);
    if (script::script_exists(checksum)) {
        script::launch_script(checksum, nullptr, nullptr, false, name);
    }
}

} // namespace

bool step_toward(Point from, Point to, float step, Point* out)
{
    const float dx = to.x - from.x;
    const float dz = to.z - from.z;
    const float distance = std::sqrt((dx * dx) + (dz * dz));
    if (distance <= step) {
        *out = to;
        return true;
    }
    const float fraction = step / distance;
    *out = {.x = from.x + (dx * fraction), .y = from.y + ((to.y - from.y) * fraction), .z = from.z + (dz * fraction)};
    return false;
}

bool turn_toward(HeadingTurn heading_turn, float* out)
{
    const auto heading = heading_turn.heading;
    const auto target = heading_turn.target;
    const auto max_step = heading_turn.max_step_;
    auto wrap = [](float a) {
        a = std::fmod(a + kHalfTurnF, kTurnF);
        if (a < 0.0F) {
            a += kTurnF;
        }
        return a - kHalfTurnF;
    };
    const float difference = wrap(target - heading);
    if (std::fabs(difference) <= max_step) {
        *out = wrap(target);
        return true;
    }
    *out = wrap(heading + (difference > 0.0F ? max_step : -max_step));
    return false;
}

float get_heading_of(float x, float z)
{
    if (x == 0.0F && z == 0.0F) {
        return 0.0F;
    }
    return static_cast<float>(std::atan2(static_cast<double>(x), static_cast<double>(z)) * kHalfTurnD / std::numbers::pi);
}

void start_spot_candidates(Point pos, HeadingReach heading_reach, std::span<Point, 4> out)
{
    const auto heading = heading_reach.heading;
    const auto distance = heading_reach.distance;
    const double heading_radians = static_cast<double>(heading) * std::numbers::pi / 180.0;
    auto ax = static_cast<float>(std::sin(heading_radians));
    auto az = static_cast<float>(std::cos(heading_radians)); // (at)
    const float rx = az;
    const float rz = -ax; // (right: up x at)
    core::at(out, 0) = {.x = pos.x + (rx * distance), .y = pos.y, .z = pos.z + (rz * distance)};
    core::at(out, 1) = {.x = pos.x - (rx * distance), .y = pos.y, .z = pos.z - (rz * distance)};
    core::at(out, 2) = {.x = pos.x - (ax * distance), .y = pos.y, .z = pos.z - (az * distance)};
    core::at(out, 3) = {.x = pos.x + (ax * distance), .y = pos.y, .z = pos.z + (az * distance)};
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
    if ((front_end && front_end->is_halted()) || (ext::game_owner() != nullptr)) {
        return;
    }
    const objects::Rider* skater = local_skater();
    const uint32_t structure = modules::Skate::sole() ? modules::Skate::sole()->level_params() : 0;
    if (!skater || (structure == 0U) || render::current_level() == 0) {
        stage_map().frames = 0;
        return;
    }
    if (structure != stage_map().structure) {
        stage_map() = {};
        stage_map().structure = structure;
    }
    ++stage_map().frames;
    if (!stage_map().noted) {
        if (stage_map().frames < kSettleFrames) {
            return;
        }
        // (where the skater is now: where the level started it)
        const Point pos = point_of(skater->at_point());
        const float heading = get_heading_of(core::at(skater->basis_frame().row, 2).x, core::at(skater->basis_frame().row, 2).z);
        stage_map().start = find_start_spot(pos, heading);
        stage_map().noted = true;
        if (!stage_map().start.valid) {
            get_log("thaw_bmx: no room for the level's bike near the start (%.0f, %.0f, %.0f)", pos.x, pos.y, pos.z);
        }
        launch_script(kLevelStartScript);
    }
    if (stage_map().frames % kCheckFrames != 0 || on_bike(skater->core_physics()) ||
        objects::find_object(name_hash(kHomeBike))) {
        return;
    }
    launch_script(kHomeScript);
}

} // namespace skater::thaw_bmx
