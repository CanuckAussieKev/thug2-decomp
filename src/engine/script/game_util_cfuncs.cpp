// Script commands (CFunctions) of the game: interpolating parameters, a texture's name, power-ups (see
// game_util_cfuncs.h).

#include "engine/script/game_util_cfuncs.h"

#include <cstdint>
#include <cmath>
#include <cstring>
#include <array>

#include "game/park_editing/park_editor.h"
#include "engine/gfx/debug_lines.h"
#include "engine/gfx/texture_cfuncs.h"
#include "base/maths/vec4.h"
#include "engine/objects/compositemgr.h"
#include "base/core/checksums.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "game/skate/highscores.h"
#include "game/skate/skate_module.h"
#include "game/rider/rider.h"
#include "game/rider/skater_state.h"
#include "game/park_editing/rail_editor.h"
#include "game/skate/game_mode.h"
#include "game/skate/point_tally.h"
#include "engine/gfx/cag_cfuncs.h"
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "base/core/span_copy.h"
#include "game/online/player_info.h"
#include <gsl/pointers>
#include <span>
#include <string_view>

namespace commands {

namespace {
// Search defaults: how far a rail point is looked for, and the start of the nearest-distance searches (squared).
constexpr float kDefaultRailDistance = 150.0F;
constexpr float kFarthestSquaredDistance = 1.0e8F;
// The aim line: 90 degrees to the heading's start, 50 feet along, 75 feet above the skater.
constexpr double kQuarterTurnDegrees = 90.0;
constexpr double kAimLineLength = 50.0;
constexpr int kMillisecondsPerSecond = 1000;
constexpr float kGameUtilWholeTurn{360.0F};
constexpr float kGameUtilHalfTurn{180.0F};
constexpr float kGameUtilMinusDegreesToRadians{-0.017453292F};

using script::Array;
using script::Component;
using script::Struct;

constexpr uint32_t kA = 0x174841BC;                    // "A"
constexpr uint32_t kB = 0x8E411006;                    // "B"
constexpr uint32_t kFraction = 0x404E690D;             // (name unknown) InterpolateParameters: how far from A to B
constexpr uint32_t kIgnore = 0xF277291D;               // "ignore"
constexpr uint32_t kAngles = 0x9D2D0915;               // "Angles"
constexpr uint32_t kInterpolated = 0xFF6F3872;         // (name unknown) InterpolateParameters: the result
constexpr uint32_t kPath = 0xF4AB74F0;                 // "path"
constexpr uint32_t kTexture = 0x7D99F28D;              // "texture"
constexpr uint32_t kSkaterState = 0x43686585;          // "SkaterState"
constexpr uint32_t kPos = 0x7F261953;                  // "pos"
constexpr uint32_t kRailDistance = 0x213395F1;         // (name unknown) FindNearestRailPoint: how far to look (150)
constexpr uint32_t kIgnoreRailId = 0x288A6EFE;         // "IgnoreRailId"
constexpr uint32_t kIgnoreRailPointIndex = 0xE4D305A7; // "IgnoreRailPointIndex"
constexpr uint32_t kCheckLevelGeometry = 0x9C247A94;   // "CheckLevelGeometry"
constexpr uint32_t kRailId = 0xA61E7CD9;               // "rail_id"
constexpr uint32_t kRailPointIndex = 0x00AB3C14;       // "rail_point_index"
constexpr uint32_t kNearestPos = 0xDF970A05;           // "NearestPos"
constexpr uint32_t kParkName = 0xCC23BDDB;             // (name unknown) GetParkName: the park's name
constexpr uint32_t kSkater = 0x5B8AB877;               // "Skater"
constexpr uint32_t kString = 0x61414D56;               // "string"
constexpr uint32_t kLevel = 0x651533EC;                // "level"
constexpr uint32_t kMasterSkaterList = 0xF31E8FF8;     // "master_skater_list"
constexpr uint32_t kName = 0xA1DC81F9;                 // "name"
constexpr uint32_t kLastName = 0x37F745A5;             // "last_name"
constexpr uint32_t kHeading = 0xFD4BC03E;              // "heading"
constexpr uint32_t kPitch = 0xD8604126;                // "pitch"
constexpr uint32_t kR = 0x93F60062;                    // "r"
constexpr uint32_t kG = 0xFE2BE489;                    // "g"
constexpr uint32_t kBlue = 0x8E411006;                 // "b" (the same checksum as "B")
constexpr uint32_t kNetSlap = 0xF9D5D933;              // "netslap"
constexpr uint32_t kSlap = 0xCA1F360F;                 // "slap"
constexpr uint32_t kUiString = 0x96875C0F;             // "ui_string"
constexpr uint32_t kChecksum = 0x21902065;             // "checksum"
constexpr uint32_t kTime = 0x906B67BA;                 // "time"
constexpr uint32_t kScore = 0xCD66C8AE;                // "score"

modules::Skate* get_skate() { return modules::Skate::sole(); }
// The game mode (0x007CE470): its name (0x005532A0) and fireball level (0x005531F0) - neither has source yet.
void* get_game_mode() { return static_cast<void*>(goals::game_mode()); }
uint32_t active_ruleset_label() { return static_cast<goals::MatchRules*>(get_game_mode())->get_name_key(); }
// The rail editor (0x004F4970 finds it).
park_editing::RailAuthoring* get_rail_editor() { return park_editing::RailAuthoring::sole(); }

// 0x005A9E80 (commands::wrap_angle, texture_cfuncs.cpp): an angle in degrees brought into 0..360. The original reads
// its x87 result unrounded; ours is the float it returns (the last bits can differ when it adds or takes a turn).
double wrapped_degrees(float angle) { return commands::wrap_angle(angle); }
// The half and whole turns compared and turned with (0x0064B278 180, 0x00645EA8 360).
float half_turn() { return kGameUtilHalfTurn; }
float whole_turn() { return kGameUtilWholeTurn; }

// Whether `array` (if any) has the element `name`.
// The C runtime's _ftol (0x006236FC): truncated to a 64-bit integer, the low half kept.
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

bool array_has(Array* array, uint32_t name)
{
    if (!array) {
        return false;
    }
    for (uint32_t i = 0; i < array->extent(); ++i) {
        if (array->fetch_part(i) == name) {
            return true;
        }
    }
    return false;
}

} // namespace

// 0x005B1B60: InterpolateParameters A={...} B={...} 0x404E690D=t [ignore=[names]] [Angles=[names]]: 0xFF6F3872, a new
// structure with each named integer or float of A (unless ignored) interpolated towards B's value of that name (an
// integer or a float; skipped if B has neither): A + (B - A) * t - for the Angles the shorter way round (the
// difference and the result wrapped into 0-360 degrees). An integer stays an integer (truncated) only if both were integers. Always
// true.
bool cfunc_interpolate_parameters(script::Struct* params, script::Script* script)
{
    gsl::owner<Struct*> result = script::new_struct();
    Struct* from_struct = nullptr;
    params->read_sub_struct(kA, &from_struct, 1);
    Struct* to_struct = nullptr;
    params->read_sub_struct(kB, &to_struct, 1);
    float fraction = 0.0F;
    params->fetch_float(kFraction, &fraction, 0);
    Array* ignore = nullptr;
    params->fetch_list(kIgnore, &ignore, 0);
    Array* angles = nullptr;
    params->fetch_list(kAngles, &angles, 0);

    for (Component* component = from_struct->step_to_entry(nullptr); component;
         // (the original asks the command's params for the next one: the same, the component isn't null)
         component = params->step_to_entry(component)) {
        if (array_has(ignore, component->name)) {
            continue;
        }
        bool integer = true;
        float from = 0.0F;
        const script::QbValueKind type = value_kind(*component);
        if (type == script::QbValueKind::kInteger) {
            from = static_cast<float>(component->value.integer());
        } else if (type == script::QbValueKind::kFloat) {
            from = component->value.real();
            integer = false;
        } else {
            continue;
        }
        const uint32_t name = component->name;
        if (name == 0U) {
            continue;
        }
        float to = 0.0F;
        int to_integer = 0;
        if (to_struct->fetch_int_value(name, &to_integer, 0)) {
            to = static_cast<float>(to_integer);
        } else if (to_struct->fetch_float(name, &to, 0)) {
            integer = false;
        } else {
            continue;
        }
        float value = 0.0F;
        if (array_has(angles, name)) {
            // x87: the wrapped difference, less a whole turn above pi, scaled and added in double
            double difference = wrapped_degrees(static_cast<float>(static_cast<double>(to) - from));
            if (difference > half_turn()) {
                difference = -(whole_turn() - difference);
            }
            value = static_cast<float>(wrapped_degrees(static_cast<float>((difference * fraction) + from)));
        } else {
            value = static_cast<float>(((static_cast<double>(to) - from) * fraction) + from);
        }
        if (integer) {
            result->put_int(checksums::Name{name}, ftol(value));
        } else {
            result->push_float(checksums::Name{name}, value);
        }
    }
    script->args()->append_struct_ref(kInterpolated, result);
    return true;
}

// 0x005B1E00: GetTextureFromPath path="dir/name": texture, the path's last part (after the last / or \). The name is
// copied into a 32-byte buffer, as in the original (a longer one overruns it). Always true.
bool cfunc_get_texture_from_path(script::Struct* params, script::Script* script)
{
    const char* path = nullptr;
    params->fetch_text(kPath, &path, 1);
    constexpr size_t kNameSize = 32;
    std::array<char, kNameSize> name{};
    const std::string_view full(path);
    const std::size_t slash = full.find_last_of("\\/");
    const std::string_view base = slash == std::string_view::npos ? full : full.substr(slash + 1);
    core::copy_into(std::span<const char>(base.data(), base.size()), std::span<char>(name).first(base.size()));
    core::at(name, base.size()) = '\0';
    script->args()->append_word(kTexture, name.data());
    return true;
}

// 0x005B1EA0: ClearPowerups: for each of the skate module's skaters 0-7 that exists, the power-ups of the first
// SkaterState component the object manager has cleared - the same component each time (as in the original). Always
// true.
bool cfunc_clear_powerups(script::Struct* /*params*/, script::Script* /*script*/)
{
    modules::Skate* skate = modules::Skate::sole();
    for (int i = 0; i < online::kPlayerSlotCount; ++i) {
        if (skate->get_rider_at(i)) {
            auto* state = core::down_cast<objects::RiderPoseState>(
                objects::object_hub_slot()->find_first_module_of_kind(kSkaterState));
            state->clear_powerups();
        }
    }
    return true;
}

// 0x005B2FC0: FindNearestRailPoint pos=(x,y,z) [0x213395F1=distance (150)] [IgnoreRailId=<rail>]
// [IgnoreRailPointIndex=n] [CheckLevelGeometry]: NearestPos, the nearest rail point (also rail_id and
// rail_point_index) or - with CheckLevelGeometry and when nearer - the park generator's nearest point, within the
// distance. False if neither is within it.
bool cfunc_find_nearest_rail_point(script::Struct* params, script::Script* script)
{
    maths::Vec4 pos;
    params->fetch_vec(kPos, &pos, 1);
    float distance = kDefaultRailDistance;
    params->fetch_float(kRailDistance, &distance, 0);
    auto max_distance_squared = static_cast<float>(static_cast<double>(distance) * distance);
    uint32_t ignore_rail = 0;
    params->read_hash_value(kIgnoreRailId, &ignore_rail, 0);
    int ignore_index = -1;
    params->fetch_int_value(kIgnoreRailPointIndex, &ignore_index, 0);

    float rail_distance_squared = kFarthestSquaredDistance;
    uint32_t rail = 0;
    int rail_index = 0;
    maths::Vec4 rail_point;
    get_rail_editor()->find_nearest_point(&pos, &rail_point, &rail_distance_squared, &rail, &rail_index, ignore_rail,
                                          ignore_index);
    float geometry_distance_squared = kFarthestSquaredDistance;
    maths::Vec4 geometry_point;
    if (params->has_bare_flag(kCheckLevelGeometry)) {
        park_editing::ParkBuilderMode::find_restart(&pos, &geometry_point, &geometry_distance_squared);
    }
    if (!(rail_distance_squared < max_distance_squared) && !(geometry_distance_squared < max_distance_squared)) {
        return false;
    }
    maths::Vec4 nearest;
    if (rail_distance_squared < geometry_distance_squared) {
        nearest = rail_point;
        script->args()->put_name_key(checksums::Name{kRailId}, rail);
        script->args()->put_int(checksums::Name{kRailPointIndex}, rail_index);
    } else {
        nearest = geometry_point;
    }
    script->args()->put_vec3(checksums::Name{kNearestPos}, {.x = nearest.x, .y = nearest.y, .z = nearest.z});
    return true;
}

// 0x005B3160: GetParkName: 0xCC23BDDB, the park's name (the buffer at 0x007CED58). Returns false.
bool cfunc_get_park_name(script::Struct* /*params*/, script::Script* script)
{
    script->args()->append_word(kParkName, commands::park_name().data());
    return false;
}

// 0x005B3180: SetSpecialBarColors Skater=n: skater n's score takes the special bar's colours from the global array
// 0x2E8B9F1C (0x005612D0; the skater isn't checked for null). Always true.
bool cfunc_set_special_bar_colors(script::Struct* params, script::Script* /*script*/)
{
    int index = 0;
    params->fetch_int_value(kSkater, &index, 1);
    auto* skater = core::down_cast<objects::Rider>(get_skate()->get_rider_at(index));
    skater->get_points_total()->set_special_bar_colors();
    return true;
}

// 0x005B31C0: GetInitialsString: string, the records' default initials. Always true.
bool cfunc_get_initials_string(script::Struct* /*params*/, script::Script* script)
{
    auto* records = highscores::game_records();
    // (the initials through two getters folded with others: 0x005480C0, then 0x005902E0 - returns its argument)
    script->args()->append_word(kString, script::QbValueKind::kString, records->default_initials());
    return true;
}

// 0x005B31F0: SetInitialsString [string="ABC" ("ERR")]: the records' default initials. Always true.
bool cfunc_set_initials_string(script::Struct* params, script::Script* /*script*/)
{
    auto* records = highscores::game_records();
    const char* initials = "ERR"; // "ERR"
    params->fetch_text(kString, &initials, 0);
    records->set_stock_initials(initials);
    return true;
}

// 0x005B3230: GetFireballLevel: level, the game mode's fireball level. Always true.
bool cfunc_get_fireball_level(script::Struct* /*params*/, script::Script* script)
{
    script->args()->put_int(checksums::Name{kLevel}, static_cast<goals::MatchRules*>(get_game_mode())->get_fireball_level());
    return true;
}

// 0x005B3260: GetSkaterLastNameString <name>: last_name, the master_skater_list entry's of that name. False if there
// is no such entry or it has no last_name.
bool cfunc_get_skater_last_name_string(script::Struct* params, script::Script* script)
{
    uint32_t name = 0;
    params->read_hash_value(0U, &name, 1);
    Array* skaters = script::fetch_list(kMasterSkaterList);
    const int count = static_cast<int>(skaters->extent());
    for (int i = 0; i < count; ++i) {
        auto* skater = skaters->struct_at(i);
        uint32_t entry = 0;
        skater->read_hash_value(kName, &entry, 1);
        if (entry != name) {
            continue;
        }
        const char* last_name = nullptr;
        if (!skater->fetch_text(kLastName, &last_name, 0)) {
            return false;
        }
        script->args()->append_word(kLastName, last_name);
        return true;
    }
    return false;
}

// 0x005B3300: DrawDirectionalLightLines heading=degrees pitch=degrees r= g= b=: a debug line from 75 above the local
// skater 50 long towards the light - compiled out of this build (the drawing call is empty); the local skater isn't
// checked for null. Always true.
bool cfunc_draw_directional_light_lines(script::Struct* params, script::Script* /*script*/)
{
    float heading = 0.0F;
    params->fetch_float(kHeading, &heading, 1);
    float pitch = 0.0F;
    params->fetch_float(kPitch, &pitch, 1);
    pitch = static_cast<float>(pitch * static_cast<double>(kGameUtilMinusDegreesToRadians)); // -pi / 180
    heading = static_cast<float>((kQuarterTurnDegrees - heading) * kDegreeToRadianRatio);                     // pi / 180
    int red = 0;
    params->fetch_int_value(kR, &red, 1);
    int green = 0;
    params->fetch_int_value(kG, &green, 1);
    int blue = 0;
    params->fetch_int_value(kBlue, &blue, 1);
    const objects::GameEntity* skater = get_skate()->get_home_rider();
    const maths::Vec4& position = skater->at_point();
    const double above = static_cast<double>(position.y) + 75.0;
    auto horizontal = static_cast<float>(std::cos(static_cast<double>(pitch)) * kAimLineLength);
    maths::Vec4 from;
    from.x = position.x;
    from.y = static_cast<float>(above);
    from.z = position.z;
    from.w = 0.0F;
    maths::Vec4 to;
    to.x = static_cast<float>((std::cos(static_cast<double>(heading)) * horizontal) + position.x);
    to.y = static_cast<float>((std::sin(static_cast<double>(pitch)) * kAimLineLength) + above);
    to.z = static_cast<float>((std::sin(static_cast<double>(heading)) * horizontal) + position.z);
    to.w = 0.0F;
    const uint32_t colour = 0xFF000000U | static_cast<uint32_t>(static_cast<uint8_t>(blue)) << 16 |
                            static_cast<uint32_t>(static_cast<uint8_t>(green)) << 8 | static_cast<uint8_t>(red);
    gfx::push_diag_line(&from, &to, colour, colour, 1);
    return true;
}

// 0x005B3470: InSlapGame: whether the game mode is netslap or slap.
bool cfunc_in_slap_game(script::Struct* /*params*/, script::Script* /*script*/)
{
    return active_ruleset_label() == kNetSlap || active_ruleset_label() == kSlap;
}

// 0x005B46A0: SetPreferencesFromUI <which preferences> string="..." field="name" [checksum=<value>] [time=n]
// [score=n]: the preference `field` set to {ui_string, checksum, time (time_limit / horse_time_limit), score (target
// score: score, else time * 1000)} in the preferences the params name (0x00565D40). Always true.
bool cfunc_set_preferences_from_ui(script::Struct* params, script::Script* /*script*/)
{
    auto* preferences = modules::Skate::get_preferences_from_params(params);
    const char* string = nullptr;
    params->fetch_text(kString, &string, 1);
    const char* field = nullptr;
    params->fetch_text("field", &field, 1);
    gsl::owner<Struct*> value = script::new_struct();
    value->append_word(kUiString, script::QbValueKind::kString, string);
    uint32_t checksum = 0;
    params->read_hash_value("checksum", &checksum, 0);
    value->add_component_of_type(kChecksum, script::QbValueKind::kName, checksum);
    if (std::strcmp(field, "time_limit") == 0 || std::strcmp(field, "horse_time_limit") == 0) {
        int time = 0; // (uninitialised in the original without time)
        params->fetch_int_value("time", &time, 0);
        value->add_component_of_type(kTime, script::QbValueKind::kInteger, static_cast<uint32_t>(time));
    }
    if (std::strcmp(field, "target_score") == 0) {
        int score = 0;
        if (!params->fetch_int_value(kScore, &score, 0)) {
            params->fetch_int_value(kTime, &score, 1);
            score *= kMillisecondsPerSecond;
        }
        value->add_component_of_type(kScore, script::QbValueKind::kInteger, static_cast<uint32_t>(score));
    }
    preferences->change_setting(checksums::hash_from_text(field), value);
    if (value) {
        value->get_clear();
        script::delete_struct(value);
    }
    return true;
}

} // namespace commands
