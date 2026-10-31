// Script commands (CFunctions) of the game: interpolating parameters, a texture's name, power-ups (see
// game_util_cfuncs.h).

#include "engine/script/game_util_cfuncs.h"

#include <cstdint>
#include <cmath>
#include <cstring>

#include "game/park_editing/park_editor.h"
#include "engine/gfx/debug_lines.h"
#include "engine/gfx/texture_cfuncs.h"
#include "engine/maths/vec4.h"
#include "engine/objects/compositemgr.h"
#include "engine/core/checksums.h"
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

namespace commands {

float g_game_util_whole_turn{360.0f};
float g_game_util_half_turn{180.0f};
float g_game_util_minus_degrees_to_radians{-0.017453292f};

namespace {

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
void* get_game_mode() { return static_cast<void*>(goals::g_game_mode); }
uint32_t active_ruleset_label() { return static_cast<goals::MatchRules*>(get_game_mode())->get_name_key(); }
// The rail editor (0x004F4970 finds it).
park_editing::RailAuthoring* get_rail_editor() { return park_editing::RailAuthoring::sole(); }

// 0x005A9E80 (commands::wrap_angle, texture_cfuncs.cpp): an angle in degrees brought into 0..360. The original reads
// its x87 result unrounded; ours is the float it returns (the last bits can differ when it adds or takes a turn).
double wrapped_degrees(float angle) { return commands::wrap_angle(angle); }
// The half and whole turns compared and turned with (0x0064B278 180, 0x00645EA8 360).
float half_turn() { return g_game_util_half_turn; }
float whole_turn() { return g_game_util_whole_turn; }

// Whether `array` (if any) has the element `name`.
// The C runtime's _ftol (0x006236FC): truncated to a 64-bit integer, the low half kept.
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

bool array_has(Array* array, uint32_t name)
{
    if (!array)
        return false;
    for (uint32_t i = 0; i < array->extent_; ++i)
        if (array->fetch_part(i) == name)
            return true;
    return false;
}

} // namespace

// 0x005B1B60: InterpolateParameters A={...} B={...} 0x404E690D=t [ignore=[names]] [Angles=[names]]: 0xFF6F3872, a new
// structure with each named integer or float of A (unless ignored) interpolated towards B's value of that name (an
// integer or a float; skipped if B has neither): A + (B - A) * t - for the Angles the shorter way round (the
// difference and the result wrapped into 0-360 degrees). An integer stays an integer (truncated) only if both were integers. Always
// true.
bool __cdecl cfunc_interpolate_parameters(script::Struct* params, script::Script* script)
{
    Struct* memory = script::spawn_param_bag();
    Struct* result = memory ? memory->prepare_all() : nullptr;
    Struct* a = nullptr;
    params->read_sub_struct(kA, &a, 1);
    Struct* b = nullptr;
    params->read_sub_struct(kB, &b, 1);
    float t = 0.0f;
    params->fetch_float(kFraction, &t, 0);
    Array* ignore = nullptr;
    params->fetch_list(kIgnore, &ignore, 0);
    Array* angles = nullptr;
    params->fetch_list(kAngles, &angles, 0);

    for (Component* component = a->step_to_entry(nullptr); component;
         // (the original asks the command's params for the next one: the same, the component isn't null)
         component = params->step_to_entry(component)) {
        if (array_has(ignore, component->name))
            continue;
        bool integer = true;
        float from;
        uint8_t type = component->type_and_flag_ >> 1;
        if (type == script::kQbvalInteger) {
            from = static_cast<float>(component->integer);
        } else if (type == script::kQbvalFloat) {
            from = component->float_value_;
            integer = false;
        } else {
            continue;
        }
        uint32_t name = component->name;
        if (!name)
            continue;
        float to = 0.0f;
        int to_integer = 0;
        if (b->fetch_int_value(name, &to_integer, 0)) {
            to = static_cast<float>(to_integer);
        } else if (b->fetch_float(name, &to, 0)) {
            integer = false;
        } else {
            continue;
        }
        float value;
        if (array_has(angles, name)) {
            // x87: the wrapped difference, less a whole turn above pi, scaled and added in double
            double difference = wrapped_degrees(static_cast<float>(static_cast<double>(to) - from));
            if (difference > half_turn())
                difference = -(whole_turn() - difference);
            value = static_cast<float>(wrapped_degrees(static_cast<float>(difference * t + from)));
        } else {
            value = static_cast<float>((static_cast<double>(to) - from) * t + from);
        }
        if (integer)
            result->put_int(name, ftol(value));
        else
            result->push_float(name, value);
    }
    script->args_->append_struct_ref(kInterpolated, result);
    return true;
}

// 0x005B1E00: GetTextureFromPath path="dir/name": texture, the path's last part (after the last / or \). The name is
// copied into a 32-byte buffer, as in the original (a longer one overruns it). Always true.
bool __cdecl cfunc_get_texture_from_path(script::Struct* params, script::Script* script)
{
    const char* path;
    params->fetch_text(kPath, &path, 1);
    char name[32];
    int length = static_cast<int>(std::strlen(path));
    int i = length;
    for (; i >= 0; --i)
        if (path[i] == '/' || path[i] == '\\')
            break;
    for (int from = i + 1; from <= length; ++from)
        name[from - (i + 1)] = path[from];
    script->args_->append_word(kTexture, name);
    return true;
}

// 0x005B1EA0: ClearPowerups: for each of the skate module's skaters 0-7 that exists, the power-ups of the first
// SkaterState component the object manager has cleared - the same component each time (as in the original). Always
// true.
bool __cdecl cfunc_clear_powerups(script::Struct*, script::Script*)
{
    modules::Skate* skate = modules::Skate::sole();
    for (int i = 0; i < 8; ++i) {
        if (skate->get_rider_at(i)) {
            auto* state = static_cast<objects::RiderPoseState*>(
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
bool __cdecl cfunc_find_nearest_rail_point(script::Struct* params, script::Script* script)
{
    maths::Vec4 pos;
    params->fetch_vec(kPos, reinterpret_cast<script::VectorValue*>(&pos), 1);
    float distance = 150.0f;
    params->fetch_float(kRailDistance, &distance, 0);
    float max_distance_squared = static_cast<float>(static_cast<double>(distance) * distance);
    uint32_t ignore_rail = 0;
    params->read_hash_value(kIgnoreRailId, &ignore_rail, 0);
    int ignore_index = -1;
    params->fetch_int_value(kIgnoreRailPointIndex, &ignore_index, 0);

    float rail_distance_squared = 1.0e8f;
    uint32_t rail = 0;
    int rail_index = 0;
    maths::Vec4 rail_point;
    get_rail_editor()->find_nearest_point(&pos, &rail_point, &rail_distance_squared, &rail, &rail_index, ignore_rail,
                                          ignore_index);
    float geometry_distance_squared = 1.0e8f;
    maths::Vec4 geometry_point;
    if (params->has_bare_flag(kCheckLevelGeometry))
        park_editing::ParkBuilderMode::find_restart(&pos, &geometry_point, &geometry_distance_squared);
    if (!(rail_distance_squared < max_distance_squared) && !(geometry_distance_squared < max_distance_squared))
        return false;
    maths::Vec4 nearest;
    if (rail_distance_squared < geometry_distance_squared) {
        nearest = rail_point;
        script->args_->put_name_key(kRailId, rail);
        script->args_->put_int(kRailPointIndex, rail_index);
    } else {
        nearest = geometry_point;
    }
    script->args_->put_vec3(kNearestPos, nearest.x, nearest.y, nearest.z);
    return true;
}

// 0x005B3160: GetParkName: 0xCC23BDDB, the park's name (the buffer at 0x007CED58). Returns false.
bool __cdecl cfunc_get_park_name(script::Struct*, script::Script* script)
{
    script->args_->append_word(kParkName, commands::g_park_name);
    return false;
}

// 0x005B3180: SetSpecialBarColors Skater=n: skater n's score takes the special bar's colours from the global array
// 0x2E8B9F1C (0x005612D0; the skater isn't checked for null). Always true.
bool __cdecl cfunc_set_special_bar_colors(script::Struct* params, script::Script*)
{
    int index;
    params->fetch_int_value(kSkater, &index, 1);
    auto* skater = static_cast<objects::Rider*>(get_skate()->get_rider_at(index));
    static_cast<modules::PointTally*>(skater->get_points_total())->set_special_bar_colors();
    return true;
}

// 0x005B31C0: GetInitialsString: string, the records' default initials. Always true.
bool __cdecl cfunc_get_initials_string(script::Struct*, script::Script* script)
{
    auto* records = highscores::g_game_records;
    // (the initials through two getters folded with others: 0x005480C0, then 0x005902E0 - returns its argument)
    script->args_->append_word(kString, script::kQbvalString, records->default_initials_);
    return true;
}

// 0x005B31F0: SetInitialsString [string="ABC" ("ERR")]: the records' default initials. Always true.
bool __cdecl cfunc_set_initials_string(script::Struct* params, script::Script*)
{
    auto* records = highscores::g_game_records;
    const char* initials = "ERR"; // "ERR"
    params->fetch_text(kString, &initials, 0);
    records->set_stock_initials(initials);
    return true;
}

// 0x005B3230: GetFireballLevel: level, the game mode's fireball level. Always true.
bool __cdecl cfunc_get_fireball_level(script::Struct*, script::Script* script)
{
    script->args_->put_int(kLevel, static_cast<goals::MatchRules*>(get_game_mode())->get_fireball_level());
    return true;
}

// 0x005B3260: GetSkaterLastNameString <name>: last_name, the master_skater_list entry's of that name. False if there
// is no such entry or it has no last_name.
bool __cdecl cfunc_get_skater_last_name_string(script::Struct* params, script::Script* script)
{
    uint32_t name;
    params->read_hash_value(0u, &name, 1);
    Array* skaters = script::fetch_list(kMasterSkaterList);
    int count = static_cast<int>(skaters->extent_);
    for (int i = 0; i < count; ++i) {
        auto* skater = reinterpret_cast<Struct*>(skaters->fetch_part(i));
        uint32_t entry;
        skater->read_hash_value(kName, &entry, 1);
        if (entry != name)
            continue;
        const char* last_name;
        if (!skater->fetch_text(kLastName, &last_name, 0))
            return false;
        script->args_->append_word(kLastName, last_name);
        return true;
    }
    return false;
}

// 0x005B3300: DrawDirectionalLightLines heading=degrees pitch=degrees r= g= b=: a debug line from 75 above the local
// skater 50 long towards the light - compiled out of this build (the drawing call is empty); the local skater isn't
// checked for null. Always true.
bool __cdecl cfunc_draw_directional_light_lines(script::Struct* params, script::Script*)
{
    float heading;
    params->fetch_float(kHeading, &heading, 1);
    float pitch;
    params->fetch_float(kPitch, &pitch, 1);
    pitch = static_cast<float>(pitch * static_cast<double>(g_game_util_minus_degrees_to_radians)); // -pi / 180
    heading = static_cast<float>((90.0 - heading) * g_degree_to_radian_ratio);                     // pi / 180
    int r;
    params->fetch_int_value(kR, &r, 1);
    int g;
    params->fetch_int_value(kG, &g, 1);
    int b;
    params->fetch_int_value(kBlue, &b, 1);
    auto* skater = reinterpret_cast<uint8_t*>(get_skate()->get_home_rider());
    const maths::Vec4& position = *reinterpret_cast<maths::Vec4*>(skater + 0x4C);
    double above = static_cast<double>(position.y) + 75.0;
    float horizontal = static_cast<float>(std::cos(static_cast<double>(pitch)) * 50.0);
    maths::Vec4 from;
    from.x = position.x;
    from.y = static_cast<float>(above);
    from.z = position.z;
    from.w = 0.0f;
    maths::Vec4 to;
    to.x = static_cast<float>(std::cos(static_cast<double>(heading)) * horizontal + position.x);
    to.y = static_cast<float>(std::sin(static_cast<double>(pitch)) * 50.0 + above);
    to.z = static_cast<float>(std::sin(static_cast<double>(heading)) * horizontal + position.z);
    to.w = 0.0f;
    uint32_t colour = 0xFF000000u | static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16 |
                      static_cast<uint32_t>(static_cast<uint8_t>(g)) << 8 | static_cast<uint8_t>(r);
    gfx::push_diag_line(&from, &to, colour, colour, 1);
    return true;
}

// 0x005B3470: InSlapGame: whether the game mode is netslap or slap.
bool __cdecl cfunc_in_slap_game(script::Struct*, script::Script*)
{
    return active_ruleset_label() == kNetSlap || active_ruleset_label() == kSlap;
}

// 0x005B46A0: SetPreferencesFromUI <which preferences> string="..." field="name" [checksum=<value>] [time=n]
// [score=n]: the preference `field` set to {ui_string, checksum, time (time_limit / horse_time_limit), score (target
// score: score, else time * 1000)} in the preferences the params name (0x00565D40). Always true.
bool __cdecl cfunc_set_preferences_from_ui(script::Struct* params, script::Script*)
{
    auto* preferences = modules::Skate::get_preferences_from_params(params);
    const char* string;
    params->fetch_text(kString, &string, 1);
    const char* field;
    params->fetch_text("field", &field, 1);
    Struct* memory = script::spawn_param_bag();
    Struct* value = memory ? memory->prepare_all() : nullptr;
    value->append_word(kUiString, script::kQbvalString, string);
    uint32_t checksum = 0;
    params->read_hash_value("checksum", &checksum, 0);
    value->add_component_of_type(kChecksum, script::kQbvalName, checksum);
    if (std::strcmp(field, "time_limit") == 0 || std::strcmp(field, "horse_time_limit") == 0) {
        int time = 0; // (uninitialised in the original without time)
        params->fetch_int_value("time", &time, 0);
        value->add_component_of_type(kTime, script::kQbvalInteger, static_cast<uint32_t>(time));
    }
    if (std::strcmp(field, "target_score") == 0) {
        int score;
        if (!params->fetch_int_value(kScore, &score, 0)) {
            params->fetch_int_value(kTime, &score, 1);
            score *= 1000;
        }
        value->add_component_of_type(kScore, script::kQbvalInteger, static_cast<uint32_t>(score));
    }
    preferences->change_setting(checksums::hash_from_text(field), value);
    if (value) {
        value->get_clear();
        script::delete_struct(value);
    }
    return true;
}

} // namespace commands
