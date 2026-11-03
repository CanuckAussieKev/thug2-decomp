// NOT ORIGINAL - the level editor's trigger volumes, kill zones, teleports and gaps (ours). See level_editor_triggers.h.

#include "editor/level_editor_triggers.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <map>
#include <new>
#include <span>
#include <sstream>
#include <string>
#include <vector>
#include <utility>

#include "engine/core/enum_ops.h"
#include "editor/level_editor.h"
#include "editor/level_editor_ui.h"
#include "engine/maths/vec2.h"
#include "engine/render/coll_static.h"
#include "engine/core/checksums.h"
#include "editor/level_editor_cameras.h"
#include "editor/level_editor_chains.h"
#include "editor/level_editor_hud.h"
#include "editor/level_editor_layout.h"
#include "editor/level_editor_node_create.h"
#include "editor/level_editor_node_edit.h"
#include "editor/level_editor_nodes.h"
#include "editor/level_editor_objects.h"
#include "editor/level_editor_picking.h"
#include "editor/level_editor_scenery.h"
#include "editor/level_editor_walls.h"
#include "engine/gfx/sector.h"
#include "engine/maths/mat4.h"
#include "engine/render/coll_cache.h"
#include "engine/render/coll_movable.h"
#include "engine/render/coll_tri.h"
#include "engine/render/coll_world.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/entity.h"
#include "engine/objects/proximity.h"
#include "engine/script/load.h"
#include "engine/script/node_manager.h"
#include "engine/script/nodes.h"
#include "engine/script/qb.h"
#include "engine/script/param_struct.h"
#include "game/skate/gap_checklist.h"
#include "game/skate/skate_module.h"
#include "platform/run_log.h"
#include <gsl/pointers>
#include <array>
#include "engine/core/ints.h"
#include "engine/core/parse_number.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include "engine/core/mem.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/maths/vec4.h"
#include "engine/render/coll_obj.h"

namespace frontend::level_editor::triggers {

namespace {

constexpr std::uint32_t kParseNodeArrayIgnoreGaps = 0xF7ECD937u; // ParseNodeArrayIgnoreGaps
constexpr float kHalf = 0.5f; // half

} // namespace

// --- the pure parts ---------------------------------------------------------------------------------------------

namespace {

constexpr int kBitsPerByte = 8;
constexpr std::size_t kHexNameLength = 10; // "0x" and 8 hex digits
constexpr std::size_t kHexNameDigits = 8;
constexpr std::size_t kTripleTextSize = 96; // "x,y,z" with room
constexpr std::size_t kNumberTextSize = 16;
constexpr std::size_t kHexTextSize = 16;
constexpr std::size_t kRadiusTextSize = 48;
constexpr std::size_t kScriptNameSize = 32;
constexpr std::size_t kGapNameSize = 32;
constexpr std::size_t kStatusTextSize = 160;
constexpr std::size_t kBoostTextSize = 48;
constexpr std::size_t kSpeedUpTextSize = 64;
constexpr std::size_t kMaxCleanTextLength = 60; // the longest gap name written into a script
constexpr int kMaxGapScore = 1000000;
constexpr float kMinTriggerSize = 10.0f; // a trigger box side limits
constexpr float kMaxTriggerSize = 50000.0f;
constexpr float kMinPieceTriggerSize = 20.0f; // the least a trigger copied from a piece box measures
// Script tokens (see engine/script/qb.h), named after what the decompiler writes for each.
// The trigger kinds overlay colours (ARGB).
constexpr uint32_t kKillColour = 0xFFFF4848;
constexpr uint32_t kJumpToColour = 0xFFB27CFF;
constexpr uint32_t kScriptsColour = 0xFF3FD4E6;
constexpr uint32_t kGapStartColour = 0xFF62E26A;
constexpr uint32_t kGapEndColour = 0xFFF2D24A;
constexpr uint32_t kOtherColour = 0xFF8C9AAA;
constexpr uint32_t kBoostColour = 0xFF3F9BFF;
constexpr uint32_t kBreakColour = 0xFFD8EEFF;
constexpr uint32_t kViewCamColour = 0xFFFFA040;
constexpr uint32_t kHandleOutlineColour = 0xFF101010u;
// A breakable Shatter call.
constexpr int kShatterArea = 5000;
constexpr float kShatterSpread = 1.5f;
constexpr float kShatterScale = 0.4f;
constexpr int kShatterLiftSpeed = 60;
constexpr float kRailAccelerateRate = 0.05f; // THAW default
constexpr float kBreakBoxGrowth = 12.0f; // a breakable box is its piece box grown by this on each axis
// Placing a new trigger.
constexpr float kGroundProbeDepth = 5000.0f;
constexpr float kPlaceAheadReach = 4000.0f; // how far ahead of the camera the trigger is looked for
constexpr float kPlaceAheadFallback = 600.0f; // and where it goes if nothing is hit
constexpr float kPlaceAboveGround = 200.0f; // the start of the ground probe above the placement
constexpr float kWaterKillWidth = 800.0f; // the water kill zone width and depth
constexpr float kTeleportSize = 200.0f;
constexpr float kScriptTriggerWidth = 300.0f;
constexpr float kScriptTriggerHeight = 200.0f;
constexpr float kGapGateWidth = 300.0f; // a gap start and end gates
constexpr float kGapGateHeight = 300.0f;
constexpr float kGapGateDepth = 60.0f;
constexpr float kGapGateSpacing = 600.0f; // the end gate is put this far ahead of the start
constexpr float kBoostWidth = 250.0f;
constexpr float kBoostHeight = 60.0f;
constexpr float kDefaultBoostMultiplier = 7.5f; // shown when the call has none
// The overlay.
constexpr float kDefaultSegmentThickness = 1.5f;
constexpr float kOverlayLineThickness = 2.0f;
constexpr float kMinArrowLength = 4.0f; // shorter arrows are not drawn
constexpr uint32_t kLinkArrowAlpha = 0xE0;
constexpr uint32_t kKillArrowAlpha = 0x80;
constexpr uint32_t kGapArrowAlpha = 0xB0;
constexpr uint32_t kLevelBoostArrowAlpha = 0xA0;
constexpr float kMinSideLengthSquared = 1e-4f;
constexpr float kArrowHeadLength = 9.0f;
constexpr float kArrowHeadWidth = 7.0f;
constexpr float kSpeedUpLabelDistance = 1500.0f; // how close the camera is for a speed-up label
constexpr float kSpeedUpLabelLift = 14.0f;
constexpr float kProximDrawDistance = 6000.0f; // how far away an unselected proximity node is still drawn
constexpr float kProximBoxThickness = 1.5f;
constexpr float kProximLabelLift = 10.0f;
constexpr float kBreakDrawDistance = 4000.0f;
constexpr int kCrackRays = 5;
constexpr float kCrackThickness = 1.5f;
constexpr float kBreakLabelLift = 12.0f;
constexpr float kBoostArrowScale = 0.15f; // arrow length per unit of boost strength
constexpr float kBoostDrawDistance = 3000.0f;
constexpr float kDiameterFactor = 2.0f;
constexpr int kHandleCount = 6; // a box resize handles: one for each face
constexpr int kLastHandle = 5;
constexpr float kLitHandleRadius = 7.0f;
constexpr float kHandleRadius = 5.5f;
constexpr float kResizeSnapGrid = 25.0f;

bool same_text(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = core::at(a, i), y = core::at(b, i);
        if (x >= 'A' && x <= 'Z')
            x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z')
            y = static_cast<char>(y - 'A' + 'a');
        if (x != y)
            return false;
    }
    return true;
}

uint32_t get_checksums(const char* text) { return checksums::hash_from_text(text); }

// The names the scripts use.
uint32_t start_gap()
{
    static const uint32_t kC = get_checksums("StartGap");
    return kC;
}
uint32_t end_gap()
{
    static const uint32_t kC = get_checksums("EndGap");
    return kC;
}
uint32_t get_gap_id()
{
    static const uint32_t kC = get_checksums("GapID");
    return kC;
}
uint32_t current_flags()
{
    static const uint32_t kC = get_checksums("flags");
    return kC;
}
uint32_t get_score()
{
    static const uint32_t kC = get_checksums("score");
    return kC;
}
uint32_t get_text()
{
    static const uint32_t kC = get_checksums("text");
    return kC;
}
uint32_t current_node_name()
{
    static const uint32_t kC = get_checksums("nodename");
    return kC;
}
uint32_t kill_skater()
{
    static const uint32_t kC = get_checksums("SK3_KillSkater");
    return kC;
}
uint32_t kill_skater_water()
{
    static const uint32_t kC = get_checksums("SK3_KillSkater_Water");
    return kC;
}
uint32_t get_teleport()
{
    static const uint32_t kC = get_checksums("TeleportSkaterToNode");
    return kC;
}

void get_put4(std::vector<uint8_t>* out, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        out->push_back(static_cast<uint8_t>(value >> (kBitsPerByte * i)));
}
void put_name(std::vector<uint8_t>* out, uint32_t checksum)
{
    out->push_back(script::token_byte(script::QbToken::kName));
    get_put4(out, checksum);
}
// name=value (a name)
void put_name_value(std::vector<uint8_t>* out, uint32_t key, uint32_t value)
{
    put_name(out, key);
    out->push_back(script::token_byte(script::QbToken::kEquals));
    put_name(out, value);
}

// "0x" and 8 hex digits.
bool is_hex_form(const std::string& name, uint32_t* value)
{
    if (name.size() != kHexNameLength || core::at(name, 0) != '0' || (core::at(name, 1) != 'x' && core::at(name, 1) != 'X'))
        return false;
    uint32_t v = 0;
    for (size_t i = 2; i < kHexNameLength; ++i) {
        const char c = core::at(name, i);
        const int d = c >= '0' && c <= '9'   ? c - '0'
                      : c >= 'a' && c <= 'f' ? c - 'a' + 10
                      : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                             : -1;
        if (d < 0)
            return false;
        v = v << 4 | static_cast<uint32_t>(d);
    }
    *value = v;
    return true;
}

bool valid_name(const std::string& name) { return node_create::valid_name(name); }

bool read_number(const std::string& text, float* out)
{
    char* end = nullptr;
    *out = std::strtof(text.c_str(), &end);
    return !text.empty() && end && *end == '\0' && std::isfinite(*out);
}

bool read_triple(const std::string& text, V3* out)
{
    std::array<float, 3> v{};
    size_t at = 0;
    for (int i = 0; i < 3; ++i) {
        const size_t comma = i < 2 ? text.find(',', at) : text.size();
        if (comma == std::string::npos)
            return false;
        if (!read_number(text.substr(at, comma - at), &core::at(v, i)))
            return false;
        at = comma + 1;
    }
    *out = {.x = core::at(v, 0), .y = core::at(v, 1), .z = core::at(v, 2)};
    return true;
}

std::string triple(V3 v)
{
    std::array<char, kTripleTextSize> text{};
    text::format_to(text, "%.9g,%.9g,%.9g", v.x, v.y, v.z);
    return text.data();
}

} // namespace

const char* kind_name(Kind kind)
{
    switch (kind) {
    case Kind::kKill:
        return "Kill zone";
    case Kind::kJumpTo:
        return "Teleport";
    case Kind::kScripts:
        return "Runs a script";
    case Kind::kGapStart:
        return "Gap start";
    case Kind::kGapEnd:
        return "Gap end";
    case Kind::kOther:
        return "Trigger (no script)";
    case Kind::kBoost:
        return "Jump boost";
    case Kind::kBreak:
        return "Breakable";
    case Kind::kViewCam:
        return "Camera";
    }
    return "?";
}

const char* kind_word(Kind kind)
{
    switch (kind) {
    case Kind::kKill:
        return "kill";
    case Kind::kJumpTo:
        return "teleport";
    case Kind::kScripts:
        return "script";
    case Kind::kGapStart:
        return "gapstart";
    case Kind::kGapEnd:
        return "gapend";
    case Kind::kOther:
        return "other";
    case Kind::kBoost:
        return "boost";
    case Kind::kBreak:
        return "break";
    case Kind::kViewCam:
        return "camera";
    }
    return "other";
}

bool kind_from_word(const std::string& word, Kind* kind)
{
    for (int i = 0; i < kKinds; ++i) {
        if (word == kind_word(static_cast<Kind>(i))) {
            *kind = static_cast<Kind>(i);
            return true;
        }
    }
    return false;
}

uint32_t kind_colour(Kind kind)
{
    switch (kind) {
    case Kind::kKill:
        return kKillColour;
    case Kind::kJumpTo:
        return kJumpToColour;
    case Kind::kScripts:
        return kScriptsColour;
    case Kind::kGapStart:
        return kGapStartColour;
    case Kind::kGapEnd:
        return kGapEndColour;
    case Kind::kOther:
        return kOtherColour;
    case Kind::kBoost:
        return kBoostColour;
    case Kind::kBreak:
        return kBreakColour;
    case Kind::kViewCam:
        return kViewCamColour;
    }
    return kOtherColour;
}

Kind kind_of_calls(const Calls& calls, bool has_script)
{
    if (calls.kill)
        return Kind::kKill;
    if (calls.teleport)
        return Kind::kJumpTo;
    if (calls.end_gap_)
        return Kind::kGapEnd;
    if (calls.start_gap_)
        return Kind::kGapStart;
    if (calls.boost)
        return Kind::kBoost;
    if (calls.shatter)
        return Kind::kBreak;
    if (calls.camera)
        return Kind::kViewCam;
    return has_script ? Kind::kScripts : Kind::kOther;
}

const std::vector<const char*>& break_sounds()
{
    static const std::vector<const char*> kSounds = {"HitGlassPane2x", "HitMetalCan", "HitTrafficBarrier", "BonkBush"};
    return kSounds;
}

const std::vector<const char*>& gap_option_labels()
{
    static const std::vector<const char*> kFlags = {
        "PURE_AIR",    "PURE_GRIND",   "PURE_RAIL",        "PURE_MANUAL",  "CANCEL_GROUND",   "CANCEL_AIR",
        "CANCEL_RAIL", "CANCEL_GRIND", "CANCEL_MANUAL",    "CANCEL_WALL",  "CANCEL_WALLRIDE", "CANCEL_LIP",
        "CANCEL_HANG", "CANCEL_WALK",  "REQUIRE_AIR",      "REQUIRE_RAIL", "REQUIRE_GRIND",   "REQUIRE_MANUAL",
        "REQUIRE_LIP", "REQUIRE_WALL", "REQUIRE_WALLRIDE", "REQUIRE_WALK"};
    return kFlags;
}

bool same_spec(const Spec& a, const Spec& b)
{
    return a.name == b.name && a.kind == b.kind && a.size.x == b.size.x && a.size.y == b.size.y &&
           a.size.z == b.size.z && a.water == b.water && a.target == b.target && a.script == b.script &&
           a.gap == b.gap && a.flags == b.flags && a.text == b.text && a.score == b.score && a.replaces == b.replaces &&
           a.exit == b.exit && a.strength == b.strength && a.sound == b.sound && a.seconds == b.seconds;
}

uint32_t checksum_of(const std::string& name)
{
    uint32_t value = 0;
    if (is_hex_form(name, &value))
        return value;
    return name.empty() ? 0u : get_checksums(name.c_str());
}

std::string name_for(uint32_t checksum, const char* known)
{
    if (known && *known && valid_name(known) && get_checksums(known) == checksum)
        return known;
    std::array<char, kNumberTextSize> text{};
    text::format_to(text, "0x%08X", checksum);
    return text.data();
}

std::string get_script_name(const Spec& spec) { return spec.name + "Script"; }

float clamp_seconds(float seconds)
{
    if (!std::isfinite(seconds))
        return kDefaultCameraShotSeconds;
    return std::max(kCameraMinSeconds, std::min(kCameraMaxSeconds, seconds));
}

const char* get_problem(const Spec& spec)
{
    switch (spec.kind) {
    case Kind::kKill:
        return spec.target.empty() ? "Choose the restart the skater goes back to" : nullptr;
    case Kind::kJumpTo:
        return spec.target.empty() ? "Choose where it sends the skater" : nullptr;
    case Kind::kScripts:
        return spec.script.empty() ? "Choose the script it runs" : nullptr;
    case Kind::kGapStart:
        return spec.gap.empty() ? "It has no gap" : nullptr;
    case Kind::kGapEnd:
        if (spec.gap.empty())
            return "It has no gap";
        if (spec.text.empty() || spec.score <= 0)
            return "A gap needs a name and a score";
        return nullptr;
    case Kind::kOther:
        return "It has no script";
    case Kind::kBoost:
        return nullptr;
    case Kind::kBreak:
        return spec.target.empty() ? "Choose the piece it breaks" : nullptr;
    case Kind::kViewCam:
        return spec.target.empty() ? "Choose the camera it plays" : nullptr;
    }
    return "?";
}

std::string clean_text(const std::string& text)
{
    std::string out;
    for (char c : text) {
        auto u = static_cast<unsigned char>(c);
        if (u < kFirstPrintableChar || u == kDeleteChar)
            continue;
        if (c == '"')
            c = '\'';
        if (c == '#')
            continue;
        out.push_back(c);
        if (out.size() >= kMaxCleanTextLength)
            break;
    }
    // (no spaces at either end)
    const size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos)
        return "";
    const size_t last = out.find_last_not_of(' ');
    return out.substr(first, last - first + 1);
}

V3 clamp_size(V3 size)
{
    auto clamp = [](float v) { return std::isfinite(v) ? std::max(kMinTriggerSize, std::min(kMaxTriggerSize, v)) : kMinTriggerSize; };
    return {.x = clamp(size.x), .y = clamp(size.y), .z = clamp(size.z)};
}

OrientedBox box_of(V3 pos, float yaw, V3 size)
{
    OrientedBox box{};
    box.origin = pos;
    core::at(box.axes, 0) = turn_about_up({.x = 1.0f, .y = 0.0f, .z = 0.0f}, yaw);
    core::at(box.axes, 1) = {.x = 0.0f, .y = 1.0f, .z = 0.0f};
    core::at(box.axes, 2) = turn_about_up({.x = 0.0f, .y = 0.0f, .z = 1.0f}, yaw);
    box.min = size * -kHalf;
    box.max = size * kHalf;
    return box;
}

bool get_inside(const OrientedBox& box, V3 p)
{
    const V3 d = p - box.origin;
    const float x = get_dot(d, core::at(box.axes, 0));
    const float y = get_dot(d, core::at(box.axes, 1));
    const float z = get_dot(d, core::at(box.axes, 2));
    return x >= box.min.x && x <= box.max.x && y >= box.min.y && y <= box.max.y && z >= box.min.z && z <= box.max.z;
}

V3 handle_direction(HandleOf handle_of)
{
    const auto yaw = handle_of.yaw;
    const auto handle = handle_of.handle;
    const int axis = (handle >> 1) % 3;
    const float sign = (handle & 1) ? 1.0f : -1.0f;
    const V3 unit = axis == 0 ? V3{.x = 1.0f, .y = 0.0f, .z = 0.0f} : axis == 1 ? V3{.x = 0.0f, .y = 1.0f, .z = 0.0f} : V3{.x = 0.0f, .y = 0.0f, .z = 1.0f};
    return turn_about_up(unit, yaw) * sign;
}

V3 handle_point(V3 pos, float yaw, V3 size, int handle)
{
    const int axis = (handle >> 1) % 3;
    const float half = (axis == 0 ? size.x : axis == 1 ? size.y : size.z) * 0.5f;
    return pos + handle_direction({.yaw = yaw, .handle = handle}) * half;
}

void resize_face(V3 pos, float yaw, V3 size, FaceDrag face_drag, ResizedFace resized)
{
    auto* const new_pos = resized.new_pos_;
    auto* const new_size = resized.new_size_;
    const auto handle = face_drag.handle;
    const auto along = face_drag.along;
    const int axis = (handle >> 1) % 3;
    V3 grown = size;
    float* side = axis == 0 ? &grown.x : axis == 1 ? &grown.y : &grown.z;
    const float before = *side;
    *side = before + (std::isfinite(along) ? along : 0.0f);
    grown = clamp_size(grown);
    const float change = (axis == 0 ? grown.x : axis == 1 ? grown.y : grown.z) - before;
    *new_size = grown;
    *new_pos = pos + handle_direction({.yaw = yaw, .handle = handle}) * (change * kHalf);
}

std::vector<uint8_t> get_body(const Spec& spec)
{
    std::vector<uint8_t> out;
    if (get_problem(spec))
        return out;
    switch (spec.kind) {
    case Kind::kKill:
        put_name(&out, spec.water ? kill_skater_water() : kill_skater());
        put_name_value(&out, current_node_name(), checksum_of(spec.target));
        out.push_back(0x01);
        break;
    case Kind::kJumpTo:
        put_name(&out, get_teleport());
        put_name_value(&out, current_node_name(), checksum_of(spec.target));
        out.push_back(0x01);
        break;
    case Kind::kScripts:
        put_name(&out, checksum_of(spec.script));
        out.push_back(0x01);
        break;
    case Kind::kGapStart:
        put_name(&out, start_gap());
        put_name_value(&out, get_gap_id(), checksum_of(spec.gap));
        if (!spec.flags.empty()) {
            put_name(&out, current_flags());
            out.push_back(script::token_byte(script::QbToken::kEquals));
            out.push_back(script::token_byte(script::QbToken::kArrayStart));
            for (size_t i = 0; i < spec.flags.size(); ++i) {
                if (i)
                    out.push_back(script::token_byte(script::QbToken::kComma));
                put_name(&out, checksum_of(core::at(spec.flags, i)));
            }
            out.push_back(script::token_byte(script::QbToken::kArrayEnd));
        }
        out.push_back(0x01);
        break;
    case Kind::kGapEnd: {
        put_name(&out, end_gap());
        put_name_value(&out, get_gap_id(), checksum_of(spec.gap));
        put_name(&out, get_score());
        out.push_back(script::token_byte(script::QbToken::kEquals));
        out.push_back(script::token_byte(script::QbToken::kInteger));
        get_put4(&out, static_cast<uint32_t>(spec.score));
        put_name(&out, get_text());
        out.push_back(script::token_byte(script::QbToken::kEquals));
        out.push_back(script::token_byte(script::QbToken::kString));
        std::string text = clean_text(spec.text);
        get_put4(&out, static_cast<uint32_t>(text.size() + 1));
        out.insert(out.end(), text.begin(), text.end());
        out.push_back(0x00);
        out.push_back(0x01);
        break;
    }
    case Kind::kOther:
        break;
    case Kind::kBoost:
        // trigger_jump_boost speed=<n> (skutils.qb: the skater's upward velocity set to n)
        put_name(&out, get_checksums("trigger_jump_boost"));
        put_name(&out, get_checksums("speed"));
        out.push_back(script::token_byte(script::QbToken::kEquals));
        out.push_back(script::token_byte(script::QbToken::kInteger));
        get_put4(&out, static_cast<uint32_t>(std::max(kBoostMin, std::min(kBoostMax, spec.strength))));
        out.push_back(0x01);
        break;
    case Kind::kBreak: {
        // [emit_sound <sound>] Shatter name=<piece> area=5000 variance=1.0 spread=1.5 life=3 bounce=0 scale=0.4
        // vel_x=0 vel_y=60 vel_z=0, Kill name=<piece> - BO_ShatteringGlass's (without its texture swap - the piece's
        // own textures shatter)
        const uint32_t piece = checksum_of(spec.target);
        if (!spec.sound.empty()) {
            put_name(&out, get_checksums("PlaySound"));
            put_name(&out, checksum_of(spec.sound));
            out.push_back(0x01);
        }
        auto integer = [&](const char* key, int32_t value) {
            put_name(&out, get_checksums(key));
            out.push_back(script::token_byte(script::QbToken::kEquals));
            out.push_back(script::token_byte(script::QbToken::kInteger));
            get_put4(&out, static_cast<uint32_t>(value));
        };
        auto real = [&](const char* key, float value) {
            put_name(&out, get_checksums(key));
            out.push_back(script::token_byte(script::QbToken::kEquals));
            out.push_back(script::token_byte(script::QbToken::kFloat));
            auto bits = std::bit_cast<uint32_t>(value);
            get_put4(&out, bits);
        };
        put_name(&out, get_checksums("Shatter"));
        put_name_value(&out, get_checksums("name"), piece);
        integer("area", kShatterArea);
        real("variance", 1.0f);
        real("spread", kShatterSpread);
        integer("life", 3);
        integer("bounce", 0);
        real("scale", kShatterScale);
        integer("vel_x", 0);
        integer("vel_y", kShatterLiftSpeed);
        integer("vel_z", 0);
        out.push_back(0x01);
        put_name(&out, get_checksums("Kill"));
        put_name_value(&out, get_checksums("name"), piece);
        out.push_back(0x01);
        break;
    }
    case Kind::kViewCam: {
        // LevelEditor_PlayCamera camera=<node> time=<seconds> (level_editor_cameras.h: the game's virtual_cam)
        put_name(&out, get_checksums("LevelEditor_PlayCamera"));
        put_name_value(&out, get_checksums("camera"), checksum_of(spec.target));
        put_name(&out, get_checksums("time"));
        out.push_back(script::token_byte(script::QbToken::kEquals));
        out.push_back(script::token_byte(script::QbToken::kFloat));
        const float seconds = clamp_seconds(spec.seconds);
        auto bits = std::bit_cast<uint32_t>(seconds);
        get_put4(&out, bits);
        out.push_back(0x01);
        break;
    }
    }
    return out;
}

std::vector<uint8_t> qb_file(const std::vector<Spec>& specs)
{
    std::vector<uint8_t> out;
    std::vector<std::pair<uint32_t, std::string>> names;
    auto note = [&](const std::string& name) {
        uint32_t value = 0;
        if (name.empty() || is_hex_form(name, &value))
            return;
        const uint32_t checksum = get_checksums(name.c_str());
        for (const auto& n : names) {
            if (n.first == checksum)
                return;
        }
        names.emplace_back(checksum, name);
    };
    for (const Spec& spec : specs) {
        out.push_back(script::token_byte(script::QbToken::kScriptStart));
        const std::string script = get_script_name(spec);
        put_name(&out, get_checksums(script.c_str()));
        out.push_back(0x01);
        std::vector<uint8_t> body = get_body(spec);
        out.insert(out.end(), body.begin(), body.end());
        out.push_back(script::token_byte(script::QbToken::kScriptEnd));
        out.push_back(0x01);
        note(script);
        if (!body.empty()) {
            if (spec.kind == Kind::kKill || spec.kind == Kind::kJumpTo)
                note(spec.target);
            if (spec.kind == Kind::kScripts)
                note(spec.script);
            if (spec.kind == Kind::kGapStart || spec.kind == Kind::kGapEnd)
                note(spec.gap);
            if (spec.kind == Kind::kBreak) {
                note(spec.target);
                note(spec.sound);
            }
            if (spec.kind == Kind::kViewCam) {
                note("LevelEditor_PlayCamera");
                note(spec.target);
            }
        }
    }
    for (const auto& n : names) {
        out.push_back(script::token_byte(script::QbToken::kChecksumName));
        get_put4(&out, n.first);
        out.insert(out.end(), n.second.begin(), n.second.end());
        out.push_back(0x00);
    }
    out.push_back(0x00);
    return out;
}

std::string link_record(const Placed& placed)
{
    const Spec& s = placed.spec;
    std::ostringstream out;
    out << s.name << ' ' << kind_word(s.kind) << " pos=" << triple(placed.pos);
    std::array<char, kNumberTextSize> number{};
    text::format_to(number, " yaw=%.9g", placed.yaw);
    out << number.data() << " size=" << triple(s.size);
    if (!s.target.empty())
        out << " target=" << s.target;
    if (s.water)
        out << " water=1";
    if (!s.script.empty())
        out << " script=" << s.script;
    if (!s.gap.empty())
        out << " gap=" << s.gap;
    if (!s.flags.empty()) {
        out << " flags=";
        for (size_t i = 0; i < s.flags.size(); ++i)
            out << (i ? "," : "") << core::at(s.flags, i);
    }
    if (s.kind == Kind::kGapStart || s.kind == Kind::kGapEnd)
        out << " score=" << s.score;
    if (s.replaces) {
        text::format_to(number, " replaces=%08X", s.replaces);
        out << number.data();
    }
    if (s.exit)
        out << " exit=1";
    if (s.kind == Kind::kBoost)
        out << " strength=" << s.strength;
    if (!s.sound.empty())
        out << " sound=" << s.sound;
    if (s.kind == Kind::kViewCam) {
        text::format_to(number, " seconds=%.9g", clamp_seconds(s.seconds));
        out << number.data();
    }
    const std::string text = clean_text(s.text);
    if (!text.empty())
        out << " text=\"" << text << '"';
    return out.str();
}

bool read_record(const std::string& line, Placed* placed)
{
    *placed = Placed{};
    Spec& s = placed->spec;
    std::string text = line;
    // text="..." (last: it may hold spaces)
    const size_t quote = text.find(" text=\"");
    if (quote != std::string::npos) {
        const size_t start = quote + 7;
        const size_t end = text.find('"', start);
        if (end == std::string::npos || text.find_first_not_of(" \t\r", end + 1) != std::string::npos)
            return false;
        s.text = clean_text(text.substr(start, end - start));
        text.resize(quote);
    }
    std::istringstream in(text);
    std::string word;
    if (!(in >> s.name >> word) || !valid_name(s.name) || !kind_from_word(word, &s.kind) || s.kind == Kind::kOther)
        return false;
    bool have_pos = false, have_yaw = false, have_size = false, have_target = false, have_water = false,
         have_script = false, have_gap = false, have_flags = false, have_score = false, have_replaces = false,
         have_exit = false, have_strength = false, have_sound = false, have_seconds = false;
    s.score = kDefaultGapScore;
    auto once = [](bool* seen) {
        if (*seen)
            return false;
        *seen = true;
        return true;
    };
    std::string field;
    while (in >> field) {
        const size_t equals = field.find('=');
        if (equals == std::string::npos || equals == 0)
            return false;
        const std::string key = field.substr(0, equals);
        const std::string value = field.substr(equals + 1);
        bool ok = true;
        if (key == "pos") {
            ok = once(&have_pos) && read_triple(value, &placed->pos);
        } else if (key == "yaw") {
            ok = once(&have_yaw) && read_number(value, &placed->yaw);
        } else if (key == "size") {
            ok = once(&have_size) && read_triple(value, &s.size);
            s.size = clamp_size(s.size);
        } else if (key == "target") {
            ok = once(&have_target) && valid_name(value);
            s.target = value;
        } else if (key == "water") {
            ok = once(&have_water) && (value == "0" || value == "1");
            s.water = value == "1";
        } else if (key == "script") {
            ok = once(&have_script) && valid_name(value);
            s.script = value;
        } else if (key == "gap") {
            ok = once(&have_gap) && valid_name(value);
            s.gap = value;
        } else if (key == "flags") {
            ok = once(&have_flags);
            size_t at = 0;
            while (ok && at <= value.size()) {
                size_t comma = value.find(',', at);
                if (comma == std::string::npos)
                    comma = value.size();
                const std::string flag = value.substr(at, comma - at);
                ok = valid_name(flag);
                s.flags.push_back(flag);
                at = comma + 1;
            }
        } else if (key == "score") {
            char* end = nullptr;
            const long score = std::strtol(value.c_str(), &end, 10);
            ok = once(&have_score) && !value.empty() && end && *end == '\0' && score >= 0 && score <= kMaxGapScore;
            s.score = static_cast<int>(score);
        } else if (key == "replaces") {
            char* end = nullptr;
            const unsigned long checksum = std::strtoul(value.c_str(), &end, 16);
            ok = once(&have_replaces) && !value.empty() && value.size() <= kHexNameDigits && end && *end == '\0';
            s.replaces = static_cast<uint32_t>(checksum);
        } else if (key == "exit") {
            ok = once(&have_exit) && (value == "0" || value == "1");
            s.exit = value == "1";
        } else if (key == "strength") {
            char* end = nullptr;
            const long strength = std::strtol(value.c_str(), &end, 10);
            ok = once(&have_strength) && !value.empty() && end && *end == '\0' && strength >= kBoostMin &&
                 strength <= kBoostMax;
            s.strength = static_cast<int>(strength);
        } else if (key == "sound") {
            ok = once(&have_sound) && valid_name(value);
            s.sound = value;
        } else if (key == "seconds") {
            float seconds = 0.0f;
            ok = once(&have_seconds) && read_number(value, &seconds) && seconds >= kCameraMinSeconds &&
                 seconds <= kCameraMaxSeconds;
            s.seconds = seconds;
        } // (another key: a later version's - skipped)
        if (!ok)
            return false;
    }
    return have_pos;
}

int partner(const std::vector<Spec>& specs, int index)
{
    if (index < 0 || std::cmp_greater_equal(index, specs.size()))
        return -1;
    const Spec& s = core::at(specs, index);
    Kind other{};
    if (s.kind == Kind::kGapStart)
        other = Kind::kGapEnd;
    else if (s.kind == Kind::kGapEnd)
        other = Kind::kGapStart;
    else
        return -1;
    for (size_t i = 0; i < specs.size(); ++i) {
        if (ints::as_signed(i) != index && core::at(specs, i).kind == other && !s.gap.empty() && same_text(core::at(specs, i).gap, s.gap))
            return static_cast<int>(i);
    }
    return -1;
}

// --- proximity nodes ---

bool same_proxim(const ProximEdit& a, const ProximEdit& b)
{
    return a.node == b.node && a.enter == b.enter && a.exit == b.exit && a.shape == b.shape && a.box == b.box &&
           a.radius == b.radius && a.size.x == b.size.x && a.size.y == b.size.y && a.size.z == b.size.z &&
           a.offset.x == b.offset.x && a.offset.y == b.offset.y && a.offset.z == b.offset.z;
}

bool proxim_edited(const ProximEdit& edit) { return !edit.enter.empty() || !edit.exit.empty() || edit.shape; }

std::string proxim_record(const ProximEdit& e, const char* known)
{
    std::ostringstream out;
    std::array<char, kHexTextSize> hex{};
    text::format_to(hex, "%08X", e.node);
    out << hex.data();
    if (!e.enter.empty())
        out << " enter=" << e.enter;
    if (!e.exit.empty())
        out << " exit=" << e.exit;
    if (e.shape) {
        out << " shape=" << (e.box ? "box" : "sphere");
        if (e.box) {
            out << " size=" << triple(e.size) << " offset=" << triple(e.offset);
        } else {
            std::array<char, kRadiusTextSize> radius{};
            text::format_to(radius, " radius=%.9g", e.radius);
            out << radius.data();
        }
    }
    if (known && *known)
        out << "  # " << known;
    return out.str();
}

bool read_proxim_record(const std::string& line, ProximEdit* edit)
{
    *edit = ProximEdit{};
    const std::string text = line.substr(0, line.find('#'));
    std::istringstream in(text);
    std::string word;
    if (!(in >> word) || word.size() != kHexNameDigits || word.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return false;
    edit->node = static_cast<uint32_t>(std::strtoul(word.c_str(), nullptr, kHexBase));
    bool have_enter = false, have_exit = false, have_shape = false, have_radius = false, have_size = false,
         have_offset = false;
    auto once = [](bool* seen) {
        if (*seen)
            return false;
        *seen = true;
        return true;
    };
    std::string field;
    while (in >> field) {
        const size_t equals = field.find('=');
        if (equals == std::string::npos || equals == 0)
            return false;
        const std::string key = field.substr(0, equals);
        const std::string value = field.substr(equals + 1);
        bool ok = true;
        if (key == "enter") {
            ok = once(&have_enter) && valid_name(value);
            edit->enter = value;
        } else if (key == "exit") {
            ok = once(&have_exit) && valid_name(value);
            edit->exit = value;
        } else if (key == "shape") {
            ok = once(&have_shape) && (value == "sphere" || value == "box");
            edit->shape = true;
            edit->box = value == "box";
        } else if (key == "radius") {
            ok = once(&have_radius) && read_number(value, &edit->radius) && edit->radius >= kProximMin &&
                 edit->radius <= kProximMax;
        } else if (key == "size") {
            ok = once(&have_size) && read_triple(value, &edit->size) && edit->size.x >= kProximMin &&
                 edit->size.y >= kProximMin && edit->size.z >= kProximMin && edit->size.x <= kProximMax &&
                 edit->size.y <= kProximMax && edit->size.z <= kProximMax;
        } else if (key == "offset") {
            ok = once(&have_offset) && read_triple(value, &edit->offset);
        } // (another key: a later version's - skipped)
        if (!ok)
            return false;
    }
    // (a shape needs its measure: a sphere its radius, a box its size)
    return !(have_shape && (edit->box ? !have_size : !have_radius));
}

std::string proxim_script_name(uint32_t node)
{
    std::array<char, kScriptNameSize> text{};
    text::format_to(text, "ed_proxim_%08X", node);
    return text.data();
}

std::vector<uint8_t> proxim_body(ProximScripts scripts)
{
    const auto enter = scripts.enter;
    const auto exit = scripts.exit;
    const auto own = scripts.own;
    std::vector<uint8_t> out;
    if (!enter && !exit)
        return out;
    auto branch = [&](uint32_t script) {
        if (script) {
            put_name(&out, script);
            out.push_back(0x01);
        } else if (own) {
            put_name(&out, own);
            out.push_back(script::token_byte(script::QbToken::kAllArguments)); // (<...>: its arguments passed on)
            out.push_back(0x01);
        }
    };
    // if Inside {enter} else {exit}: 47 <offset> ... 48 <offset> 01 ... 28 01; each offset counted from its own first
    // byte - the if's to the else's body, the else's to the end of line after endif.
    out.push_back(script::token_byte(script::QbToken::kShortIf));
    const size_t if_offset = out.size();
    out.push_back(0);
    out.push_back(0);
    put_name(&out, get_checksums("Inside"));
    out.push_back(0x01);
    branch(enter);
    out.push_back(script::token_byte(script::QbToken::kShortElse));
    const size_t else_offset = out.size();
    out.push_back(0);
    out.push_back(0);
    out.push_back(0x01);
    auto to_else = static_cast<uint16_t>(else_offset + 2 - if_offset);
    core::at(out, if_offset) = static_cast<uint8_t>(to_else);
    core::at(out, if_offset + 1) = static_cast<uint8_t>(to_else >> kBitsPerByte);
    branch(exit);
    out.push_back(script::token_byte(script::QbToken::kEndIf));
    auto to_end = static_cast<uint16_t>(out.size() - else_offset);
    core::at(out, else_offset) = static_cast<uint8_t>(to_end);
    core::at(out, else_offset + 1) = static_cast<uint8_t>(to_end >> kBitsPerByte);
    out.push_back(0x01);
    return out;
}

std::vector<uint8_t> proxim_qb_file(const std::vector<ProximScript>& scripts)
{
    std::vector<uint8_t> out;
    for (const ProximScript& s : scripts) {
        out.push_back(script::token_byte(script::QbToken::kScriptStart));
        put_name(&out, get_checksums(proxim_script_name(s.node).c_str()));
        out.push_back(0x01);
        std::vector<uint8_t> body = proxim_body({.enter = s.enter, .exit = s.exit, .own = s.own});
        out.insert(out.end(), body.begin(), body.end());
        out.push_back(script::token_byte(script::QbToken::kScriptEnd));
        out.push_back(0x01);
    }
    for (const ProximScript& s : scripts) {
        std::string name = proxim_script_name(s.node);
        out.push_back(script::token_byte(script::QbToken::kChecksumName));
        get_put4(&out, get_checksums(name.c_str()));
        out.insert(out.end(), name.begin(), name.end());
        out.push_back(0x00);
    }
    out.push_back(0x00);
    return out;
}

// --- the game side ----------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kTriggerScript = 0x2CA8A299; // TriggerScript
constexpr uint16_t kVolumeFlags = 0x50;         // a trigger face the skater passes through
constexpr auto kFileName = std::to_array("editor\\thug2_decomp_triggers.qb");
constexpr float kDrawRadius = 8000.0f; // (level triggers drawn within this of the camera)
constexpr float kLabelRadius = 3000.0f;
constexpr int kMaxLevelDrawn = 300, kMaxLabels = 48, kMaxFacesKept = 64;

// NOT ORIGINAL grouping - level_editor_triggers.cpp's state (were globals). Made on first use, never destroyed
// (level_editor_triggers_state()).
struct LevelEditorTriggersState {
    // The editor's triggers (alive or not: a deleted one's spec is kept for its undo), what Tick saw of them.
    std::vector<Spec> specs_;
    std::vector<Added> added_;
    // The level's triggers (scanned when the editor starts in a level); those switched off / on here (their walls' states
    // go in the undo steps); those switched off while an editable copy stands in for them (on again when it goes).
    std::vector<LevelTrigger> level_;
    bool scanned_{};
    std::vector<uint32_t> switched_;
    std::vector<uint32_t> turned_off_;
    uint32_t select_wall_{};
    uint32_t level_array_{};
    uint32_t inspected_{};
};

LevelEditorTriggersState& level_editor_triggers_state()
{
    static core::NoDestroy<LevelEditorTriggersState> state;
    return state.get();
}

// The collision boxes.
struct Cuboid {
    uint32_t name;
    render::HitMovingBox* coll;
    V3 pos, size;
    float yaw;
};
// NOT ORIGINAL grouping - the trigger boxes, the checklist, the gaps, the undo step, the view (were globals). Made on
// first use, never destroyed (trigger_edit()).
struct TriggerEdit {
    std::vector<Cuboid> boxes_;
    // The scripts last defined, the gaps the editor put in the checklist (their names and scores) and the checklist.
    std::vector<uint8_t> defined_;
    std::vector<std::pair<std::string, int>> gaps_added_;
    goals::GapTally* checklist_{};
    std::vector<std::string> level_gap_names_;
    bool level_gaps_read_{};
    // Undo, selection, the UI's actions.
    bool has_undo_{};
    State undo_before_{};
    int select_ = -1;
    std::vector<std::function<void()>> actions_;
    bool shown_{};
    bool setting_read_{};
    V3 camera_ = {};
    V3 forward_ = {.x = 0.0f, .y = 0.0f, .z = 1.0f};
    float yaw_{};
    Calls* calls_{};
};

TriggerEdit& trigger_edit()
{
    static core::NoDestroy<TriggerEdit> state;
    return state.get();
}

uint32_t hash_of_name(const std::string& name) { return node_create::name_hash(name); }

std::string get_label_of(uint32_t checksum) { return nodes::caption(checksum); }

// The overlay's trigger nodes now.
bool node_alive(const std::string& name) { return node_create::is_added(hash_of_name(name)); }

bool node_place(const std::string& name, V3* pos, float* yaw)
{
    script::Struct* node =
        level_nodes::node_manager_instance() ? level_nodes::fetch_vertex(hash_of_name(name), false) : nullptr;
    if (!node)
        return false;
    maths::Vec4 p = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f}, a = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
    level_nodes::fetch_place(node, &p);
    level_nodes::read_turn_angles(node, &a);
    *pos = {.x = p.x, .y = p.y, .z = p.z};
    *yaw = a.y;
    return true;
}

// --- what a script calls ---

void on_kill(script::Struct* params, const uint8_t*)
{
    trigger_edit().calls_->kill = true;
    uint32_t target = 0;
    if (params && params->read_hash_value(current_node_name(), &target, 0) && target)
        trigger_edit().calls_->target = target;
}
void on_water(script::Struct* params, const uint8_t* call)
{
    on_kill(params, call);
    trigger_edit().calls_->water = true;
}
void on_teleport(script::Struct* params, const uint8_t*)
{
    trigger_edit().calls_->teleport = true;
    uint32_t target = 0;
    if (params && params->read_hash_value(current_node_name(), &target, 0) && target)
        trigger_edit().calls_->target = target;
}
void on_start_gap(script::Struct* params, const uint8_t*)
{
    trigger_edit().calls_->start_gap_ = true;
    uint32_t id = 0;
    if (params && params->read_hash_value(get_gap_id(), &id, 0) && !trigger_edit().calls_->gap_id_)
        trigger_edit().calls_->gap_id_ = id;
}
void on_end_gap(script::Struct* params, const uint8_t*)
{
    trigger_edit().calls_->end_gap_ = true;
    uint32_t id = 0;
    if (params && params->read_hash_value(get_gap_id(), &id, 0))
        trigger_edit().calls_->gap_id_ = id;
    const char* text = nullptr;
    if (params && params->fetch_text(get_text(), &text, 0) && text)
        trigger_edit().calls_->gap_text_ = text;
    int score = 0;
    if (params && params->fetch_int_value(get_score(), &score, 0))
        trigger_edit().calls_->gap_score_ = score;
}

void on_boost(script::Struct* params, const uint8_t*)
{
    trigger_edit().calls_->boost = true;
    int speed = 0;
    float value = 0.0f;
    if (params && params->fetch_int_value(get_checksums("speed"), &speed, 0))
        trigger_edit().calls_->boost_speed_ = static_cast<float>(speed);
    else if (params && params->fetch_float(get_checksums("speed"), &value, 0))
        trigger_edit().calls_->boost_speed_ = value;
    if (params && params->fetch_float(get_checksums("multi"), &value, 0))
        trigger_edit().calls_->boost_multi_ = value;
}
void on_shatter(script::Struct*, const uint8_t*) { trigger_edit().calls_->shatter = true; }
void on_camera(script::Struct* params, const uint8_t*)
{
    trigger_edit().calls_->camera = true;
    uint32_t node = 0;
    if (params && params->read_hash_value(get_checksums("camera"), &node, 0))
        trigger_edit().calls_->camera_node_ = node;
}
void on_rail_accelerate(script::Struct* params, const uint8_t*)
{
    trigger_edit().calls_->rail_speed_ = true;
    trigger_edit().calls_->rail_rate_ = kRailAccelerateRate; // (THAW's default)
    float rate = 0.0f;
    if (params && params->fetch_float(get_checksums("rate"), &rate, 0))
        trigger_edit().calls_->rail_rate_ = rate;
}

Calls get_walk(uint32_t script);

// What a script calls, walked once a level (a trigger that runs a script: shown every frame).
std::vector<std::pair<uint32_t, Calls>>& calls_cache()
{
    static core::NoDestroy<std::vector<std::pair<uint32_t, Calls>>> calls_cache;
    return calls_cache.get();
}
const Calls& calls_of(uint32_t script)
{
    for (const auto& c : calls_cache()) {
        if (c.first == script)
            return c.second;
    }
    calls_cache().emplace_back(script, get_walk(script));
    return calls_cache().back().second;
}

bool is_script(uint32_t name)
{
    const script::SymbolTableEntry* symbol = name ? script::resolve_symbol(name) : nullptr;
    return symbol && symbol->type() == script::QbValueKind::kScriptBody;
}

Calls get_walk(uint32_t script)
{
    Calls calls;
    if (!is_script(script))
        return calls;
    trigger_edit().calls_ = &calls;
    static const auto kKills = std::to_array<uint32_t>({kill_skater(), get_checksums("SK3_KillSkater_Pungee"),
                                      get_checksums("SK3_Killskater_Finish"), get_checksums("KillSkater")});
    script::clear_script_flag_6f_2564(); // (as for_each_node_script_call walks them)
    for (const uint32_t target : kKills)
        level_nodes::for_each_script_call({.script = script, .target = target}, &on_kill, nullptr, 0);
    level_nodes::for_each_script_call({.script = script, .target = kill_skater_water()}, &on_water, nullptr, 0);
    level_nodes::for_each_script_call({.script = script, .target = get_teleport()}, &on_teleport, nullptr, 0);
    level_nodes::for_each_script_call({.script = script, .target = start_gap()}, &on_start_gap, nullptr, 0);
    level_nodes::for_each_script_call({.script = script, .target = end_gap()}, &on_end_gap, nullptr, 0);
    // (the feature map: jump boosts, breakables, THAW's rail speed-ups - and the editor's own)
    level_nodes::for_each_script_call({.script = script, .target = get_checksums("trigger_jump_boost")}, &on_boost, nullptr, 0);
    level_nodes::for_each_script_call({.script = script, .target = get_checksums("Shatter")}, &on_shatter, nullptr, 0);
    level_nodes::for_each_script_call({.script = script, .target = get_checksums("ShatterAndDie")}, &on_shatter, nullptr, 0);
    level_nodes::for_each_script_call({.script = script, .target = get_checksums("RailAccelerate")}, &on_rail_accelerate, nullptr, 0);
    level_nodes::for_each_script_call({.script = script, .target = get_checksums("ed_RailSpeed")}, &on_rail_accelerate, nullptr, 0);
    level_nodes::for_each_script_call({.script = script, .target = get_checksums("LevelEditor_PlayCamera")}, &on_camera, nullptr,
                                      0); // (map4)
    script::set_script_flag_6f_2564();
    trigger_edit().calls_ = nullptr;
    return calls;
}

// --- the level's triggers ---

render::HitMesh* coll_of(render::WorldCell* sector)
{
    render::HitMesh* coll = sector ? sector->hit_body() : nullptr;
    return render::is_static_collider(coll) ? coll : nullptr;
}
bool turned_off_by_us(uint32_t sector) { return std::ranges::find(level_editor_triggers_state().turned_off_, sector) != level_editor_triggers_state().turned_off_.end(); }

V3 to_v3(const maths::Vec4& v) { return {.x = v.x, .y = v.y, .z = v.z}; }

// A sector's trigger faces read into `t` (false: none) - its faces' box, the first ones' corners, whether every face is
// a trigger's; where its collision is now (a wall moved: read again - RefreshMoved).
bool read_faces(render::WorldCell* sector, LevelTrigger* t)
{
    const render::HitMesh* coll = coll_of(sector);
    auto* tri = coll ? coll->geometry() : nullptr;
    if (!tri || !tri->faces() || !tri->vertices() || tri->tri_count() == 0)
        return false;
    t->faces.clear();
    t->face_count_ = 0;
    t->all_trigger_ = true;
    t->wall = static_cast<render::PcGeom*>(sector->geom_handle()) == nullptr; // (no geometry: collision only)
    t->bounds_low_ = {.x = tri->bounds().min.x, .y = tri->bounds().min.y, .z = tri->bounds().min.z};
    t->bounds_high_ = {.x = tri->bounds().max.x, .y = tri->bounds().max.y, .z = tri->bounds().max.z};
    bool any = false;
    for (int f = 0; std::cmp_less(f, tri->tri_count()); ++f) {
        const render::FaceFlag flags = tri->face_flags(f);
        if (!core::any(flags & render::FaceFlag::kTrigger)) {
            t->all_trigger_ = false;
            continue;
        }
        std::array<V3, 3> p{};
        bool ok = true;
        for (int k = 0; k < 3 && ok; ++k) {
            const uint32_t index = tri->face_vertex(f, k);
            ok = index < tri->num_vertices();
            if (ok)
                core::at(p, k) = to_v3(tri->read_stored_vertex(static_cast<int>(index)));
        }
        if (!ok)
            continue;
        for (const V3& q : p) {
            grow_box(&t->min, &t->max, any, q, q);
            any = true;
        }
        if (t->face_count_ < kMaxFacesKept)
            t->faces.insert(t->faces.end(), p.begin(), p.end());
        ++t->face_count_;
    }
    return any;
}

void scan_sector(void*, uint32_t checksum, render::WorldCell* sector)
{
    LevelTrigger t;
    if (!read_faces(sector, &t))
        return;
    t.sector = checksum;
    if (script::Struct* node = level_nodes::fetch_vertex(checksum, false)) {
        t.has_node_ = true;
        node->read_hash_value(kTriggerScript, &t.script, 0);
    }
    level_editor_triggers_state().level_.push_back(std::move(t));
}

void scan_level()
{
    level_editor_triggers_state().level_.clear();
    level_editor_triggers_state().scanned_ = true;
    if (!level_nodes::node_manager_instance() || !scenery::main_scene())
        return;
    scenery::for_each_level_sector(&scan_sector, nullptr);
    for (LevelTrigger& t : level_editor_triggers_state().level_) {
        t.calls = get_walk(t.script);
        t.kind = kind_of_calls(t.calls, t.script != 0);
    }
    get_log("level editor: %u trigger volumes in the level", static_cast<unsigned>(level_editor_triggers_state().level_.size()));
}

// The level's own gaps' names (every EndGap its nodes' TriggerScripts reach - not the editor's): the gaps the editor
// never takes out of the checklist.
std::vector<std::string>*& stored_names()
{
    static std::vector<std::string>* names{};
    return names;
}
void on_level_gap(script::Struct* params, const uint8_t*)
{
    const char* text = nullptr;
    if (params && params->fetch_text(get_text(), &text, 0) && text)
        stored_names()->emplace_back(text);
}
void read_level_gaps()
{
    trigger_edit().level_gap_names_.clear();
    trigger_edit().level_gaps_read_ = true;
    std::vector<uint32_t> scripts;
    for (const nodes::Elem& node : nodes::get_all()) {
        if (node.added || !node.trigger_script_)
            continue;
        script::Struct* s = nodes::struct_of(node);
        uint32_t script = 0;
        if (s && s->read_hash_value(kTriggerScript, &script, 0) && script &&
            std::ranges::find(scripts, script) == scripts.end())
            scripts.push_back(script);
    }
    stored_names() = &trigger_edit().level_gap_names_;
    script::clear_script_flag_6f_2564();
    for (const uint32_t script : scripts) {
        if (is_script(script))
            level_nodes::for_each_script_call({.script = script, .target = end_gap()}, &on_level_gap, nullptr, 0);
    }
    script::set_script_flag_6f_2564();
    stored_names() = nullptr;
}

// --- the boxes ---

gsl::owner<render::HitMovingBox*> make_box(uint32_t name)
{
    auto box = memory::make_owned_value<render::HitMovingBox>();
    if (!box)
        return nullptr;
    box->assign_checksum(name);
    box->set_poly_attr_bits(kVolumeFlags);
    box->set_surface_type(0);
    render::register_movable(box);
    return box;
}

void free_box(render::HitMovingBox* box)
{
    render::unregister_movable(box);
    gsl::owner<decltype(box)> owned = memory::owned_element(box); // (the editor's)
    delete owned; // (out of the collision caches too)
}

void place_box(Cuboid* b, V3 pos, float yaw, V3 size)
{
    const OrientedBox placed = box_of(pos, yaw, size);
    b->coll->box().min = {.x = placed.min.x, .y = placed.min.y, .z = placed.min.z, .w = 1.0f};
    b->coll->box().max = {.x = placed.max.x, .y = placed.max.y, .z = placed.max.z, .w = 1.0f};
    b->coll->set_anchor_point({.x = pos.x, .y = pos.y, .z = pos.z, .w = 1.0f});
    maths::Mat4 m;
    for (int r = 0; r < 3; ++r)
        core::at(m.row, r) = {.x = core::at(placed.axes, r).x, .y = core::at(placed.axes, r).y, .z = core::at(placed.axes, r).z, .w = 0.0f};
    core::at(m.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    b->coll->change_facing(&m);
    b->pos = pos;
    b->yaw = yaw;
    b->size = size;
}

void drop_boxes()
{
    for (const Cuboid& b : trigger_edit().boxes_)
        free_box(b.coll);
    trigger_edit().boxes_.clear();
}

// --- the gap checklist ---

goals::GapTally* get_checklist()
{
    modules::Skate* skate = modules::Skate::sole();
    return skate ? static_cast<goals::GapTally*>(skate->fetch_gap_list()) : nullptr;
}

bool level_gap(const std::string& name)
{
    return std::ranges::find(trigger_edit().level_gap_names_, name) != trigger_edit().level_gap_names_.end();
}

// The editor's gaps that are no longer wanted out of `checklist` (a level's own gap of that name stays).
void take_out_gaps(goals::GapTally* checklist, const std::vector<std::pair<std::string, int>>& wanted)
{
    if (!checklist)
        return;
    for (const auto& gap : trigger_edit().gaps_added_) {
        if (std::ranges::find(wanted, gap) != wanted.end() || level_gap(gap.first))
            continue;
        const int index = checklist->find_gap(gap.first.c_str());
        goals::GapEntry* check = index >= 0 ? checklist->get_gap(index) : nullptr;
        if (check && check->points_held() == gap.second) {
            check->teardown(1);
            get_log("level editor: gap \"%s\" taken out of the gap list", gap.first.c_str());
        }
    }
}

// --- syncing what the specs say with the game ---

const char* game_problem(const Spec& spec)
{
    if (const char* problem = get_problem(spec))
        return problem;
    if ((spec.kind == Kind::kKill || spec.kind == Kind::kJumpTo) &&
        !level_nodes::fetch_vertex(checksum_of(spec.target), false))
        return "That node isn't in this level";
    if (spec.kind == Kind::kScripts && !is_script(checksum_of(spec.script)))
        return "Script not found";
    return nullptr;
}

// The level's triggers as they are now: switched off or on (Walls::Disengaged - the editor's switch, or a level script's Kill),
// and moved (an invisible wall moved by level_editor_walls.h: its faces read again when its collision's box changed).
void refresh_level()
{
    for (LevelTrigger& t : level_editor_triggers_state().level_) {
        render::WorldCell* sector = scenery::sector_of(t.sector);
        const render::HitMesh* coll = sector ? coll_of(sector) : nullptr;
        auto* tri = coll ? coll->geometry() : nullptr;
        if (!tri)
            continue;
        t.off = walls::disengaged(t.sector) || (coll->flag_bits() & 4) != 0;
        if (tri->bounds().min.x != t.bounds_low_.x || tri->bounds().min.y != t.bounds_low_.y ||
            tri->bounds().min.z != t.bounds_low_.z || tri->bounds().max.x != t.bounds_high_.x ||
            tri->bounds().max.y != t.bounds_high_.y || tri->bounds().max.z != t.bounds_high_.z) {
            LevelTrigger now = t;
            if (read_faces(sector, &now)) {
                t.faces = now.faces;
                t.min = now.min;
                t.max = now.max;
            }
            t.bounds_low_ = now.bounds_low_;
            t.bounds_high_ = now.bounds_high_;
        }
    }
}

int spec_index(const std::string& name)
{
    for (size_t i = 0; i < level_editor_triggers_state().specs_.size(); ++i) {
        if (same_text(core::at(level_editor_triggers_state().specs_, i).name, name))
            return static_cast<int>(i);
    }
    return -1;
}

// --- the feature map: breakables, proximity nodes ---

// A scenery piece's sector, and whether it's there (WorldCell::is_operative 0x0049E9E0: its geometry's active flag - a Kill,
// the editor's hiding - else its collision's).
bool is_sector_enabled(uint32_t checksum)
{
    render::WorldCell* sector = scenery::sector_of(checksum);
    return sector && sector->is_operative();
}

// Where a breakable goes: its position and its size.
struct BreakPlace {
    V3* pos{};
    V3* size{};
};

// A breakable's box: the piece's box where it is now (axis-aligned), grown 6 each way so the skater meets it before
// the piece (false: the piece isn't there).
bool break_place(const Spec& spec, BreakPlace place)
{
    auto* const pos = place.pos;
    auto* const size = place.size;
    const uint32_t piece = checksum_of(spec.target);
    OrientedBox box{};
    if (!piece || !scenery::cuboid(piece, &box))
        return false;
    V3 min{}, max{};
    bounds_of(box, &min, &max);
    *pos = (min + max) * kHalf;
    *size = clamp_size((max - min) + V3{.x = kBreakBoxGrowth, .y = kBreakBoxGrowth, .z = kBreakBoxGrowth});
    return true;
}

// Proximity nodes: the edits, what each ProximNode had before the editor touched it (put back), the scripts defined.
std::vector<ProximEdit>& stored_proxims()
{
    static core::NoDestroy<std::vector<ProximEdit>> proxims;
    return proxims.get();
}
struct ProximOriginal {
    uint32_t script;
    objects::ProximShape shape;
    float radius;
    V3 min, max;
};
std::map<uint32_t, ProximOriginal>& proxim_originals()
{
    static std::map<uint32_t, ProximOriginal> value;
    return value;
}
std::vector<uint8_t>& proxim_defined()
{
    static core::NoDestroy<std::vector<uint8_t>> proxim_defined;
    return proxim_defined.get();
}

bool have_proxim_manager() { return objects::proximity_hub() != nullptr; }

int proxim_index(uint32_t node)
{
    for (size_t i = 0; i < stored_proxims().size(); ++i) {
        if (core::at(stored_proxims(), i).node == node)
            return static_cast<int>(i);
    }
    return -1;
}

void put_proxim_back(objects::ProximNode* p, const ProximOriginal& o)
{
    p->own_script() = o.script;
    p->set_form(o.shape);
    p->radius() = o.radius;
    p->set_reach_sq(o.radius * o.radius);
    p->box().min = {.x = o.min.x, .y = o.min.y, .z = o.min.z, .w = 1.0f};
    p->box().max = {.x = o.max.x, .y = o.max.y, .z = o.max.z, .w = 1.0f};
}

// Every frame (Guard): each edited node's ProximNode as its edit says - its script (defined here), its shape - and those
// no longer edited (or in a network session) put back.
void sync_proxims()
{
    if (!have_proxim_manager() || (stored_proxims().empty() && proxim_originals().empty()))
        return;
    const bool refused = layout::get_refused();
    std::vector<ProximScript> scripts;
    std::vector<std::pair<const ProximEdit*, objects::ProximNode*>> live;
    for (const ProximEdit& e : stored_proxims()) {
        objects::ProximNode* p = refused || !proxim_edited(e) ? nullptr : objects::proxim_get_node(e.node);
        if (!p)
            continue;
        auto o = proxim_originals().find(e.node);
        if (o == proxim_originals().end()) {
            const ProximOriginal original = {.script = p->own_script(),
                                             .shape = p->form(),
                                             .radius = p->radius(),
                                             .min = {.x = p->box().min.x, .y = p->box().min.y, .z = p->box().min.z},
                                             .max = {.x = p->box().max.x, .y = p->box().max.y, .z = p->box().max.z}};
            o = proxim_originals().emplace(e.node, original).first;
        }
        if (!e.enter.empty() || !e.exit.empty())
            scripts.push_back({.node = e.node, .enter = checksum_of(e.enter), .exit = checksum_of(e.exit), .own = o->second.script});
        live.emplace_back(&e, p);
    }
    // (put back: those not edited now)
    for (auto it = proxim_originals().begin(); it != proxim_originals().end();) {
        const bool kept = std::ranges::any_of(live, [&](const auto& l) { return l.first->node == it->first; });
        if (!kept) {
            if (objects::ProximNode* p = objects::proxim_get_node(it->first))
                put_proxim_back(p, it->second);
            it = proxim_originals().erase(it);
        } else {
            ++it;
        }
    }
    std::vector<uint8_t> file = proxim_qb_file(scripts);
    if (!scripts.empty() && file != proxim_defined()) {
        script::load_qb_data("editor\\thug2_decomp_proxim.qb", file, 0, 0, 0);
        proxim_defined() = file;
    }
    for (const auto& l : live) {
        const ProximEdit& e = *l.first;
        objects::ProximNode* p = l.second;
        const ProximOriginal& o = proxim_originals()[e.node];
        const uint32_t script =
            !e.enter.empty() || !e.exit.empty() ? get_checksums(proxim_script_name(e.node).c_str()) : o.script;
        if (p->own_script() != script)
            p->own_script() = script;
        if (!e.shape) {
            p->set_form(o.shape);
            p->radius() = o.radius;
            p->set_reach_sq(o.radius * o.radius);
            p->box().min = {.x = o.min.x, .y = o.min.y, .z = o.min.z, .w = 1.0f};
            p->box().max = {.x = o.max.x, .y = o.max.y, .z = o.max.z, .w = 1.0f};
        } else if (e.box) {
            // (objects::ProximShape::kBox: the world's axes; its middle kept at the node's place plus the offset)
            const V3 centre = V3{.x = p->at_point().x, .y = p->at_point().y, .z = p->at_point().z} + e.offset;
            const V3 half = e.size * 0.5f;
            p->set_form(objects::ProximShape::kBox);
            p->box().min = {.x = centre.x - half.x, .y = centre.y - half.y, .z = centre.z - half.z, .w = 1.0f};
            p->box().max = {.x = centre.x + half.x, .y = centre.y + half.y, .z = centre.z + half.z, .w = 1.0f};
        } else {
            p->set_form(objects::ProximShape::kSphere);
            p->radius() = e.radius;
            p->set_reach_sq(e.radius * e.radius);
        }
    }
}

void get_sync()
{
    // What each spec is now.
    level_editor_triggers_state().added_.clear();
    for (const Spec& spec : level_editor_triggers_state().specs_) {
        Added a;
        a.spec = spec;
        a.alive = node_alive(spec.name) && node_place(spec.name, &a.pos, &a.yaw);
        a.problem = game_problem(spec);
        if (spec.kind == Kind::kScripts && !a.problem)
            a.calls = calls_of(checksum_of(spec.script));
        if (spec.kind == Kind::kBreak && a.alive) {
            // (its box is its piece's, wherever the piece is now; none while the piece is gone)
            V3 pos{}, size{};
            if (break_place(spec, {.pos = &pos, .size = &size})) {
                a.pos = pos;
                a.yaw = 0.0f;
                a.spec.size = size;
            }
            a.broken = !is_sector_enabled(checksum_of(spec.target));
        }
        level_editor_triggers_state().added_.push_back(a);
    }
    // Their scripts (every spec's: a deleted one's undo brings its node back naming it). The game's loader defines
    // them (an unchanged script is left as it is).
    std::vector<Spec> scripted;
    for (const Added& a : level_editor_triggers_state().added_) {
        Spec s = a.spec;
        if (a.problem)
            s.kind = Kind::kOther; // (an empty script: it does nothing yet)
        scripted.push_back(s);
    }
    std::vector<uint8_t> file = scripted.empty() ? std::vector<uint8_t>() : qb_file(scripted);
    bool redefined = false;
    if (file != trigger_edit().defined_ && !file.empty()) {
        script::load_qb_data(kFileName.data(), file, 0, 0, 0);
        trigger_edit().defined_ = file;
        redefined = true;
    }
    // The boxes: one for each alive trigger, where its node is (none in a network session or a career-type mode -
    // Guard).
    const bool network = layout::get_refused();
    for (size_t i = 0; i < trigger_edit().boxes_.size();) {
        int at = -1;
        for (size_t k = 0; k < level_editor_triggers_state().added_.size(); ++k) {
            if (core::at(level_editor_triggers_state().added_, k).alive && !core::at(level_editor_triggers_state().added_, k).broken && hash_of_name(core::at(level_editor_triggers_state().added_, k).spec.name) == core::at(trigger_edit().boxes_, i).name)
                at = static_cast<int>(k);
        }
        if (at < 0 || network) {
            free_box(core::at(trigger_edit().boxes_, i).coll);
            trigger_edit().boxes_.erase(trigger_edit().boxes_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    for (const Added& a : level_editor_triggers_state().added_) {
        if (!a.alive || a.broken || network)
            continue;
        uint32_t name = hash_of_name(a.spec.name);
        auto it = std::ranges::find_if(trigger_edit().boxes_, [&](const Cuboid& b) { return b.name == name; });
        if (it == trigger_edit().boxes_.end()) {
            Cuboid b = {.name = name, .coll = make_box(name), .pos = {}, .size = {}, .yaw = 0.0f};
            if (!b.coll)
                continue;
            place_box(&b, a.pos, a.yaw, a.spec.size);
            trigger_edit().boxes_.push_back(b);
            get_log("level editor: trigger %s (%s) in place", a.spec.name.c_str(), kind_name(a.spec.kind));
        } else if (it->pos.x != a.pos.x || it->pos.y != a.pos.y || it->pos.z != a.pos.z || it->yaw != a.yaw ||
                   it->size.x != a.spec.size.x || it->size.y != a.spec.size.y || it->size.z != a.spec.size.z) {
            place_box(&*it, a.pos, a.yaw, a.spec.size);
        }
    }
    // The level's triggers an alive editable copy stands in for: switched off (through level_editor_walls.h - the
    // collision switch's one owner: WorldCell::change_live for a wall), on again once no copy stands in for them.
    std::vector<uint32_t> replaced;
    for (const Added& a : level_editor_triggers_state().added_) {
        if (a.alive && a.spec.replaces && scenery::sector_of(a.spec.replaces))
            replaced.push_back(a.spec.replaces);
    }
    for (size_t i = 0; i < level_editor_triggers_state().turned_off_.size();) {
        if (std::ranges::find(replaced, core::at(level_editor_triggers_state().turned_off_, i)) == replaced.end()) {
            walls::set_off(core::at(level_editor_triggers_state().turned_off_, i), false);
            level_editor_triggers_state().turned_off_.erase(level_editor_triggers_state().turned_off_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    for (const uint32_t sector : replaced) {
        if (turned_off_by_us(sector))
            continue;
        if (!walls::disengaged(sector))
            walls::set_off(sector, true);
        level_editor_triggers_state().turned_off_.push_back(sector);
    }
    refresh_level();
    // The gap list: built again as the level's load builds it; the editor's gaps no longer there taken out.
    std::vector<std::pair<std::string, int>> wanted;
    for (const Added& a : level_editor_triggers_state().added_) {
        if (a.alive && !a.problem && a.spec.kind == Kind::kGapEnd)
            wanted.emplace_back(clean_text(a.spec.text), a.spec.score);
    }
    std::ranges::sort(wanted);
    if (redefined || wanted != trigger_edit().gaps_added_) {
        goals::GapTally* checklist = get_checklist();
        if (checklist && !script::fetch_int_value(kParseNodeArrayIgnoreGaps)) {
            if (!trigger_edit().level_gaps_read_)
                read_level_gaps();
            checklist->get_build();
            take_out_gaps(checklist, wanted);
            trigger_edit().checklist_ = checklist;
            if (wanted != trigger_edit().gaps_added_)
                get_log("level editor: gap list built again - %u of the editor's gaps",
                        static_cast<unsigned>(wanted.size()));
            trigger_edit().gaps_added_ = wanted;
        }
    }
    sync_proxims();
}

} // namespace

bool get_editor_gaps(const goals::GapTally* checklist, std::vector<std::string>* names,
                     std::vector<std::string>* level_names)
{
    names->clear();
    level_names->clear();
    if (!checklist || checklist != trigger_edit().checklist_ || trigger_edit().gaps_added_.empty())
        return false;
    for (const auto& gap : trigger_edit().gaps_added_)
        names->push_back(gap.first);
    *level_names = trigger_edit().level_gap_names_;
    return true;
}

namespace {

// The level's array going (node_create's listener): everything of this level forgotten - its boxes unregistered, the
// editor's gaps out of its checklist (kept by the career for the level).
void get_forget()
{
    drop_boxes();
    take_out_gaps(trigger_edit().checklist_, {});
    trigger_edit().gaps_added_.clear();
    trigger_edit().checklist_ = nullptr;
    level_editor_triggers_state().specs_.clear();
    level_editor_triggers_state().added_.clear();
    level_editor_triggers_state().switched_.clear();
    level_editor_triggers_state().turned_off_.clear(); // (their collision goes with the level)
    level_editor_triggers_state().select_wall_ = 0;
    level_editor_triggers_state().level_.clear();
    level_editor_triggers_state().scanned_ = false;
    trigger_edit().level_gap_names_.clear();
    trigger_edit().level_gaps_read_ = false;
    calls_cache().clear();
    level_editor_triggers_state().inspected_ = 0;
    trigger_edit().has_undo_ = false;
    trigger_edit().select_ = -1;
    trigger_edit().actions_.clear();
    level_editor_triggers_state().level_array_ = 0;
    // (proximity nodes: put back while the level's are still there - none once nearby_zone_shutdown has run)
    if (have_proxim_manager()) {
        for (const auto& o : proxim_originals()) {
            if (objects::ProximNode* p = objects::proxim_get_node(o.first))
                put_proxim_back(p, o.second);
        }
    }
    stored_proxims().clear();
    proxim_originals().clear();
}

void on_array_removed(uint32_t name)
{
    if (!name || name != level_editor_triggers_state().level_array_)
        return;
    if (!level_editor_triggers_state().specs_.empty())
        get_log("level editor: the level's node array left - its %u triggers go with it",
                static_cast<unsigned>(level_editor_triggers_state().specs_.size()));
    get_forget();
}

bool array_loaded(level_nodes::NodeManager* manager, uint32_t name)
{
    for (uint32_t i = 0; i < manager->num() && i < static_cast<uint32_t>(level_nodes::NodeManager::kMaxArrays); ++i) {
        if (core::at(manager->arrays(), i).label_ == name)
            return true;
    }
    return false;
}

// The level's node array noted (the triggers belong to it): another array added after it (a cutscene's) doesn't
// change it; the level's gone and another there forgets them.
void note_level()
{
    node_create::set_array_removed_listener(&on_array_removed);
    if (level_nodes::NodeManager* manager = level_nodes::node_manager_instance().get()) {
        if (level_editor_triggers_state().level_array_ && array_loaded(manager, level_editor_triggers_state().level_array_))
            return;
        const uint32_t level = manager->get_last_node_array_name();
        if (level && level != level_editor_triggers_state().level_array_) {
            if (level_editor_triggers_state().level_array_)
                get_forget();
            level_editor_triggers_state().level_array_ = level;
        }
    }
}

void note_undo()
{
    if (!trigger_edit().has_undo_) {
        trigger_edit().undo_before_ = store_away();
        trigger_edit().has_undo_ = true;
    }
}

// A node for a spec at `pos` / `yaw` in the overlay (false: `message`).
node_create::Spec node_spec(const Spec& spec, V3 pos, float yaw)
{
    node_create::Spec n;
    n.name = spec.name;
    n.cls = "GenericNode";
    n.type = "ed_trigger";
    n.pos = pos;
    n.angles = {.x = 0.0f, .y = yaw, .z = 0.0f};
    node_create::Prop script = {.type = node_create::Prop::kName, .key = "TriggerScript"};
    script.text = get_script_name(spec);
    n.extra.push_back(script);
    n.owned = true;
    return n;
}

bool name_taken(const std::string& name) { return spec_index(name) >= 0 || node_create::name_taken(name); }

std::string fresh_gap()
{
    return node_create::fresh_name("ed_gap", [](const std::string& id) {
        return std::ranges::any_of(level_editor_triggers_state().specs_, [&](const Spec& s) { return same_text(s.gap, id); });
    });
}

// The nearest restart (a player or multiplayer one) to `pos` ("" none).
std::string nearest_restart(V3 pos)
{
    float best = 0.0f;
    std::string name;
    for (const nodes::Elem& node : nodes::get_all()) {
        if (node.family != nodes::Family::kSpawn || node.hidden || !node.has_pos_ ||
            !core::any(node.restart_types_ & (nodes::SpawnPointKind::kPlayer1 | nodes::SpawnPointKind::kMultiplayer)))
            continue;
        const V3 d = node.pos - pos;
        const float distance = get_dot(d, d);
        if (name.empty() || distance < best) {
            best = distance;
            name = name_for(node.name, script::kept_qb_name(node.name));
        }
    }
    return name;
}

// The game's collision ray (a ProbeRay: constructor 0x00417760; 0x00417710 casts it from `from` to `to` - thiscall, true
// on a hit, its point at +0x34), static collision only (as the editor's G): trigger faces (0x10) are passed.
bool get_cast(V3 from, V3 to, V3* hit)
{
    render::ProbeRay ray_storage;
    render::ProbeRay* ray = &ray_storage;
    const maths::Vec4 start = {.x = from.x, .y = from.y, .z = from.z, .w = 1.0f};
    const maths::Vec4 end = {.x = to.x, .y = to.y, .z = to.z, .w = 1.0f};
    if (!ray->get_collision_along(&start, &end, false))
        return false;
    const auto& point = ray->hit().point;
    *hit = {.x = point[0], .y = point[1], .z = point[2]};
    return true;
}
bool ground_below(V3 from, V3* hit) { return get_cast(from, {.x = from.x, .y = from.y - kGroundProbeDepth, .z = from.z}, hit); }

// Where a new trigger goes: the ground where the view looks (else 600 units ahead), its box standing on it.
V3 place_ahead(V3 size, float ahead)
{
    const V3 forward = make_unit(trigger_edit().forward_);
    V3 at{};
    if (!get_cast(trigger_edit().camera_, trigger_edit().camera_ + forward * kPlaceAheadReach, &at))
        at = trigger_edit().camera_ + forward * kPlaceAheadFallback;
    const V3 flat = make_unit({.x = forward.x, .y = 0.0f, .z = forward.z});
    at = at + flat * ahead;
    V3 ground{};
    if (ground_below(at + V3{.x = 0.0f, .y = kPlaceAboveGround, .z = 0.0f}, &ground))
        at.y = ground.y;
    at.y += size.y * kHalf;
    return at;
}

// New triggers made (their nodes in the overlay); the first selected. One undo step.
void fabricate(const std::vector<Placed>& made, const char* what)
{
    note_undo();
    std::string message;
    int first = -1;
    for (const Placed& p : made) {
        if (!node_create::fabricate(node_spec(p.spec, p.pos, p.yaw), -1, &message)) {
            hud::get_message(message, hud::kMessageSeconds);
            break;
        }
        level_editor_triggers_state().specs_.push_back(p.spec);
        if (first < 0)
            first = static_cast<int>(level_editor_triggers_state().specs_.size()) - 1;
        get_log("level editor: added trigger %s (%s) at (%.1f, %.1f, %.1f)", p.spec.name.c_str(),
                kind_name(p.spec.kind), p.pos.x, p.pos.y, p.pos.z);
    }
    get_sync();
    if (first >= 0) {
        trigger_edit().select_ = nodes::get_find(hash_of_name(core::at(level_editor_triggers_state().specs_, first).name));
        hud::get_message(std::string("Added ") + what, hud::kShortMessageSeconds);
    }
}

} // namespace

void get_guard()
{
    sync_proxims(); // (proximity nodes as their edits say - put back in a network session)
    // A breakable broken in play (its piece killed by its own script): its box goes, so it isn't shattered again.
    for (size_t i = 0; i < trigger_edit().boxes_.size();) {
        int at = -1;
        for (size_t k = 0; k < level_editor_triggers_state().specs_.size(); ++k) {
            if (core::at(level_editor_triggers_state().specs_, k).kind == Kind::kBreak && hash_of_name(core::at(level_editor_triggers_state().specs_, k).name) == core::at(trigger_edit().boxes_, i).name)
                at = static_cast<int>(k);
        }
        if (at >= 0 && !is_sector_enabled(checksum_of(core::at(level_editor_triggers_state().specs_, at).target))) {
            free_box(core::at(trigger_edit().boxes_, i).coll);
            trigger_edit().boxes_.erase(trigger_edit().boxes_.begin() + static_cast<std::ptrdiff_t>(i));
            get_log("level editor: breakable %s broken - its box taken out until the piece is back",
                    core::at(level_editor_triggers_state().specs_, at).name.c_str());
        } else {
            ++i;
        }
    }
    std::string why;
    if (trigger_edit().boxes_.empty() || !layout::get_refused(&why))
        return;
    get_log("level editor: the editor's %u trigger volumes taken out (%s)", static_cast<unsigned>(trigger_edit().boxes_.size()),
            why.c_str());
    drop_boxes();
}

const std::vector<LevelTrigger>& level_triggers() { return level_editor_triggers_state().level_; }
const std::vector<Added>& added_triggers() { return level_editor_triggers_state().added_; }

bool is_trigger(uint32_t name)
{
    return std::ranges::any_of(level_editor_triggers_state().specs_, [&](const Spec& s) { return hash_of_name(s.name) == name; });
}

bool get_shown()
{
    if (!trigger_edit().setting_read_) {
        trigger_edit().setting_read_ = true;
        trigger_edit().shown_ = config_int("editor", "triggers", 1) != 0;
    }
    return trigger_edit().shown_;
}

void set_shown(bool shown)
{
    get_shown();
    trigger_edit().shown_ = shown;
    config_set_int("editor", "triggers", shown ? 1 : 0);
}

uint32_t inspected_sector() { return level_editor_triggers_state().inspected_; }
void inspect(uint32_t sector) { level_editor_triggers_state().inspected_ = sector; }

void get_queue(std::function<void()> action) { trigger_edit().actions_.push_back(std::move(action)); }

void kickoff()
{
    note_level();
    scan_level();
    trigger_edit().level_gaps_read_ = false;
    // (breakables broken while playing: their pieces back to edit - and to break again)
    for (const Spec& spec : level_editor_triggers_state().specs_) {
        const uint32_t piece = spec.kind == Kind::kBreak ? checksum_of(spec.target) : 0;
        render::WorldCell* sector = piece ? scenery::sector_of(piece) : nullptr;
        if (sector && node_alive(spec.name) && !is_sector_enabled(piece) && !scenery::get_hidden(piece)) {
            sector->change_live(true);
            get_log("level editor: breakable %s's piece back (broken while playing)", spec.name.c_str());
        }
    }
    get_sync();
}

void halt() { trigger_edit().actions_.clear(); }

void tick(V3 camera, V3 forward, float yaw)
{
    trigger_edit().camera_ = camera;
    trigger_edit().forward_ = forward;
    trigger_edit().yaw_ = yaw;
    note_level();
    if (!level_editor_triggers_state().scanned_)
        scan_level();
    std::vector<std::function<void()>> actions;
    actions.swap(trigger_edit().actions_);
    for (auto& action : actions)
        action();
    get_sync();
}

// --- the UI's actions (level_editor_triggers_ui.cpp queues them) ---

void add_kill(bool water)
{
    Placed p;
    p.spec.name = node_create::fresh_name("ed_trigger", name_taken);
    p.spec.kind = Kind::kKill;
    p.spec.water = water;
    p.spec.size = water ? V3{.x = kWaterKillWidth, .y = 100.0f, .z = kWaterKillWidth} : V3{.x = kDefaultTriggerWidth, .y = kDefaultTriggerHeight, .z = kDefaultTriggerDepth};
    p.pos = place_ahead(p.spec.size, 0.0f);
    p.yaw = trigger_edit().yaw_;
    p.spec.target = nearest_restart(p.pos);
    fabricate({p}, water ? "a water kill zone" : "a kill zone");
}

void add_teleport()
{
    Placed p;
    p.spec.name = node_create::fresh_name("ed_trigger", name_taken);
    p.spec.kind = Kind::kJumpTo;
    p.spec.size = {.x = kTeleportSize, .y = kTeleportSize, .z = kTeleportSize};
    p.pos = place_ahead(p.spec.size, 0.0f);
    p.yaw = trigger_edit().yaw_;
    p.spec.target = nearest_restart(p.pos);
    fabricate({p}, "a teleport");
}

void append_script()
{
    Placed p;
    p.spec.name = node_create::fresh_name("ed_trigger", name_taken);
    p.spec.kind = Kind::kScripts;
    p.spec.size = {.x = kScriptTriggerWidth, .y = kScriptTriggerHeight, .z = kScriptTriggerWidth};
    p.pos = place_ahead(p.spec.size, 0.0f);
    p.yaw = trigger_edit().yaw_;
    fabricate({p}, "a script trigger");
}

void register_gap_entry()
{
    Placed start, end;
    const std::string gap = fresh_gap();
    start.spec.name = node_create::fresh_name("ed_trigger", name_taken);
    start.spec.kind = Kind::kGapStart;
    start.spec.size = {.x = kGapGateWidth, .y = kGapGateHeight, .z = kGapGateDepth};
    start.spec.gap = gap;
    start.spec.flags = {"CANCEL_GROUND"};
    const int number = text::to_int(std::next(gap.c_str(), 6));
    std::array<char, kGapNameSize> text{};
    text::format_to(text, "Editor Gap %d", number > 0 ? number : 1);
    start.spec.text = text.data();
    start.spec.score = kDefaultGapScore;
    start.pos = place_ahead(start.spec.size, 0.0f);
    start.yaw = trigger_edit().yaw_;
    end = start;
    end.spec.name = "";
    end.spec.kind = Kind::kGapEnd;
    // (the end 600 units further along the view, on the ground there)
    end.pos = place_ahead(end.spec.size, kGapGateSpacing);
    // (two fresh names: the first isn't taken until it's made)
    std::string first = start.spec.name;
    end.spec.name = node_create::fresh_name("ed_trigger",
                                            [&](const std::string& n) { return same_text(n, first) || name_taken(n); });
    fabricate({start, end}, "a gap");
}

// (UX9: the gap wizard - level_editor_gap_wizard.h) A gap's two ends as given, their names and GapID fresh.
void add_gap_pair(Placed start, Placed end)
{
    const std::string gap = fresh_gap();
    start.spec.name = node_create::fresh_name("ed_trigger", name_taken);
    std::string first = start.spec.name;
    end.spec.name = node_create::fresh_name("ed_trigger",
                                            [&](const std::string& n) { return same_text(n, first) || name_taken(n); });
    start.spec.kind = Kind::kGapStart;
    end.spec.kind = Kind::kGapEnd;
    for (Placed* p : {&start, &end}) {
        p->spec.gap = gap;
        p->spec.size = clamp_size(p->spec.size);
        p->spec.text = clean_text(p->spec.text);
    }
    end.spec.flags = start.spec.flags;
    end.spec.text = start.spec.text;
    end.spec.score = start.spec.score;
    fabricate({start, end}, "a gap");
}

void set_spec(const std::string& name, const Spec& spec, bool undo)
{
    const int i = spec_index(name);
    if (i < 0)
        return;
    if (undo)
        note_undo();
    Spec s = spec;
    s.name = core::at(level_editor_triggers_state().specs_, i).name;
    s.size = clamp_size(s.size);
    s.text = clean_text(s.text);
    // (a gap's name, score and flags are the same at both ends)
    if (s.kind == Kind::kGapStart || s.kind == Kind::kGapEnd) {
        for (Spec& other : level_editor_triggers_state().specs_) {
            if (&other != &core::at(level_editor_triggers_state().specs_, i) && !s.gap.empty() && same_text(other.gap, core::at(level_editor_triggers_state().specs_, i).gap) &&
                (other.kind == Kind::kGapStart || other.kind == Kind::kGapEnd)) {
                other.gap = s.gap;
                other.text = s.text;
                other.score = s.score;
                other.flags = s.flags;
            }
        }
    }
    core::at(level_editor_triggers_state().specs_, i) = s;
}

void begin_edit() { note_undo(); }

void turn_off(uint32_t sector, bool off)
{
    const LevelTrigger* t = nullptr;
    for (const LevelTrigger& l : level_editor_triggers_state().level_) {
        if (l.sector == sector)
            t = &l;
    }
    if (!t || !(t->wall || t->all_trigger_)) {
        hud::get_message("Has non-trigger faces; can't be disabled", hud::kMessageSeconds);
        return;
    }
    if (std::ranges::find(level_editor_triggers_state().switched_, sector) == level_editor_triggers_state().switched_.end())
        level_editor_triggers_state().switched_.push_back(sector); // (its wall state in the undo steps from now on)
    note_undo();
    if (!walls::set_off(sector, off)) {
        hud::get_message("Couldn't change the trigger", hud::kShortMessageSeconds);
        return;
    }
    hud::get_message(off ? "Trigger " + get_label_of(sector) + " disabled"
                         : "Trigger " + get_label_of(sector) + " enabled");
}

void select_wall(uint32_t sector) { level_editor_triggers_state().select_wall_ = sector; }

void make_editable(uint32_t sector)
{
    const LevelTrigger* t = nullptr;
    for (const LevelTrigger& l : level_editor_triggers_state().level_) {
        if (l.sector == sector)
            t = &l;
    }
    if (!t)
        return;
    if (!t->script) {
        hud::get_message("No TriggerScript to copy", hud::kShortMessageSeconds);
        return;
    }
    if (!t->wall && !t->all_trigger_) {
        hud::get_message("Has non-trigger faces; can't be replaced", hud::kMessageSeconds);
        return;
    }
    for (const Spec& s : level_editor_triggers_state().specs_) {
        if (s.replaces == sector && node_alive(s.name)) {
            hud::get_message("Already has an editable copy: " + s.name, hud::kShortMessageSeconds);
            return;
        }
    }
    Placed p;
    p.spec.name = node_create::fresh_name("ed_trigger", name_taken);
    p.spec.kind = Kind::kScripts;
    p.spec.script = name_for(t->script, script::kept_qb_name(t->script));
    p.spec.replaces = sector;
    p.spec.size = clamp_size({.x = std::max(kMinPieceTriggerSize, t->max.x - t->min.x), .y = std::max(kMinPieceTriggerSize, t->max.y - t->min.y),
                              .z = std::max(kMinPieceTriggerSize, t->max.z - t->min.z)});
    p.pos = (t->min + t->max) * kHalf;
    p.yaw = 0.0f;
    fabricate({p}, ("an editable copy of " + get_label_of(sector)).c_str());
}

void select_partner(const std::string& name)
{
    const int i = spec_index(name);
    const int other = partner(level_editor_triggers_state().specs_, i);
    if (other >= 0)
        trigger_edit().select_ = nodes::get_find(hash_of_name(core::at(level_editor_triggers_state().specs_, other).name));
}

void select_node_named(uint32_t name)
{
    const int index = nodes::get_find(name);
    if (index >= 0)
        trigger_edit().select_ = index;
}

// --- picking and drawing ---

bool get_pick(V3 origin, V3 unit_dir, int* node, uint32_t* sector, float max_t)
{
    *node = -1;
    *sector = 0;
    if (!get_shown())
        return false;
    float best = max_t;
    bool any = false;
    for (const Added& a : level_editor_triggers_state().added_) {
        if (!a.alive)
            continue;
        OrientedBox box = box_of(a.pos, a.yaw, a.spec.size);
        float t = 0.0f;
        if (ray_box({.origin = origin, .dir = unit_dir}, box.origin, box.axes, box.min, box.max, &t) && t < best) {
            best = t;
            *node = nodes::get_find(hash_of_name(a.spec.name));
            *sector = 0;
            any = *node >= 0;
        }
    }
    for (const LevelTrigger& l : level_editor_triggers_state().level_) {
        std::array<V3, 3> axes = {{{.x = 1.0f, .y = 0.0f, .z = 0.0f}, {.x = 0.0f, .y = 1.0f, .z = 0.0f}, {.x = 0.0f, .y = 0.0f, .z = 1.0f}}};
        const V3 grow = {.x = 2.0f, .y = 2.0f, .z = 2.0f};
        float t = 0.0f;
        if (!ray_box({.origin = origin, .dir = unit_dir}, {.x = 0.0f, .y = 0.0f, .z = 0.0f}, axes, l.min - grow, l.max + grow, &t) || t >= best)
            continue;
        // (the faces themselves when there are few; a big water plane by its box)
        float hit = t;
        bool found = l.face_count_ > kMaxFacesKept;
        for (size_t f = 0; !found && f + 2 < l.faces.size(); f += 3) {
            float ft = 0.0f;
            if (picking::ray_triangle({.origin = origin, .dir = unit_dir}, core::at(l.faces, f), core::at(l.faces, f + 1), core::at(l.faces, f + 2), &ft) && ft < best) {
                hit = ft;
                found = true;
            }
        }
        if (!found || hit >= best)
            continue;
        best = hit;
        *node = l.has_node_ ? nodes::get_find(l.sector) : -1;
        *sector = l.sector;
        any = true;
    }
    if (any && *sector)
        level_editor_triggers_state().inspected_ = *sector;
    return any;
}

namespace {

struct Painter {
    V3 camera{};
    const TickCycle* frame{};
    const Lens* lens{};
    std::span<const uint32_t, 4> viewport;
    float near_z_{};
    std::vector<ui::Seg>* lines{};

    bool get_to_pixel(V3 p, float* x, float* y) const
    {
        float nx = 0.0f, ny = 0.0f;
        if (!cast_onto(camera, *frame, *lens, p, near_z_, &nx, &ny))
            return false;
        ndc_to_pixel(viewport, nx, ny, x, y);
        return true;
    }
    void get_seg(V3 a, V3 b, uint32_t argb, float thickness = kDefaultSegmentThickness) const
    {
        if (!clip_to_near(camera, *frame, near_z_ + kNearPlaneMargin, &a, &b))
            return;
        float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
        if (get_to_pixel(a, &ax, &ay) && get_to_pixel(b, &bx, &by))
            lines->push_back({.ax = ax, .ay = ay, .bx = bx, .by = by, .argb = argb, .thickness = thickness});
    }
    void cuboid(const OrientedBox& box, uint32_t argb, float thickness) const
    {
        std::array<V3, kBoxCorners> c{};
        box_corners(box.origin, box.axes, box.min, box.max, c);
        for (int i = 0; i < kBoxCorners; ++i) {
            for (const int bit : {1, 2, 4}) {
                if (!(i & bit))
                    get_seg(core::at(c, i), core::at(c, i | bit), argb, thickness);
            }
        }
    }
    // An arrow from a to b, its head drawn on the screen.
    void get_arrow(V3 a, V3 b, uint32_t argb) const
    {
        get_seg(a, b, argb, kOverlayLineThickness);
        float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
        if (!get_to_pixel(a, &ax, &ay) || !get_to_pixel(b, &bx, &by))
            return;
        float dx = bx - ax, dy = by - ay, length = std::sqrt(dx * dx + dy * dy);
        if (length < kMinArrowLength)
            return;
        dx /= length;
        dy /= length;
        const float head = std::min(14.0f, length * 0.4f);
        for (const float side : {-1.0f, 1.0f}) {
            const float hx = -dx * 0.9f + side * -dy * 0.45f;
            const float hy = -dy * 0.9f + side * dx * 0.45f;
            lines->push_back({.ax = bx, .ay = by, .bx = bx + hx * head, .by = by + hy * head, .argb = argb, .thickness = kOverlayLineThickness});
        }
    }
};

uint32_t get_dim(uint32_t argb, uint32_t alpha) { return (argb & kRgbMask) | (alpha << kAlphaShift); }

float distance_to(V3 p, V3 min, V3 max)
{
    const float dx = std::max({min.x - p.x, 0.0f, p.x - max.x});
    const float dy = std::max({min.y - p.y, 0.0f, p.y - max.y});
    const float dz = std::max({min.z - p.z, 0.0f, p.z - max.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::string describe(const Calls& calls, Kind kind)
{
    std::string text = kind_name(kind);
    switch (kind) {
    case Kind::kKill:
        if (calls.water)
            text += " (water)";
        if (calls.target)
            text += " -> " + get_label_of(calls.target);
        break;
    case Kind::kJumpTo:
        if (calls.target)
            text += " -> " + get_label_of(calls.target);
        break;
    case Kind::kGapEnd:
        text += ": " + (calls.gap_text_.empty() ? get_label_of(calls.gap_id_) : calls.gap_text_);
        if (calls.gap_score_)
            text += " (" + std::to_string(calls.gap_score_) + ")";
        break;
    case Kind::kGapStart:
        if (calls.gap_id_)
            text += ": " + get_label_of(calls.gap_id_);
        break;
    case Kind::kBoost: {
        std::array<char, kBoostTextSize> value{};
        if (calls.boost_speed_ > 0.0f)
            text::format_to(value, " (speed %.0f)", calls.boost_speed_);
        else
            text::format_to(value, " (x%.1f)", calls.boost_multi_ > 0.0f ? calls.boost_multi_ : kDefaultBoostMultiplier);
        text += value.data();
        break;
    }
    case Kind::kViewCam:
        if (calls.camera_node_)
            text += " -> " + get_label_of(calls.camera_node_);
        break;
    default:
        break;
    }
    return text;
}

bool get_node_pos(uint32_t name, V3* pos)
{
    maths::Vec4 p;
    if (!name || !level_nodes::fetch_vertex(name, false) || !level_nodes::fetch_place(name, &p))
        return false;
    *pos = {.x = p.x, .y = p.y, .z = p.z};
    return true;
}

} // namespace

namespace {
void remember_view(V3 camera, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z);
} // namespace

void add_overlay(V3 camera, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z,
                 std::vector<ui::Seg>* lines, std::vector<ui::Overlay::OverlayText>* texts)
{
    remember_view(camera, frame, lens, viewport, near_z); // (the resize handles are drawn and picked with it)
    if (!get_shown())
        return;
    Painter paint = {.camera = camera, .frame = &frame, .lens = &lens, .viewport = viewport, .near_z_ = near_z, .lines = lines};
    int labels = 0;
    auto label = [&](V3 at, uint32_t argb, const std::string& text) {
        if (labels >= kMaxLabels)
            return;
        float x = 0.0f, y = 0.0f;
        if (paint.get_to_pixel(at, &x, &y)) {
            texts->push_back({.x = x, .y = y, .argb = argb, .text = text});
            ++labels;
        }
    };
    // The level's: nearest first.
    std::vector<std::pair<float, const LevelTrigger*>> near;
    for (const LevelTrigger& l : level_editor_triggers_state().level_) {
        const float d = distance_to(camera, l.min, l.max);
        if (d <= kDrawRadius)
            near.emplace_back(d, &l);
    }
    std::ranges::sort(near, [](const auto& a, const auto& b) { return a.first < b.first; });
    if (near.size() > static_cast<size_t>(kMaxLevelDrawn))
        near.resize(kMaxLevelDrawn);
    for (const auto& n : near) {
        const LevelTrigger& l = *n.second;
        const bool inspected = l.sector == level_editor_triggers_state().inspected_;
        const uint32_t colour = l.off ? 0x70909090u : get_dim(kind_colour(l.kind), inspected ? 0xFFu : 0xC0u);
        const float thickness = inspected ? 2.5f : 1.0f;
        if (l.face_count_ <= kMaxFacesKept) {
            for (size_t f = 0; f + 2 < l.faces.size(); f += 3) {
                paint.get_seg(core::at(l.faces, f), core::at(l.faces, f + 1), colour, thickness);
                paint.get_seg(core::at(l.faces, f + 1), core::at(l.faces, f + 2), colour, thickness);
                paint.get_seg(core::at(l.faces, f + 2), core::at(l.faces, f), colour, thickness);
            }
        } else {
            const OrientedBox box = {
                .origin = {.x = 0.0f, .y = 0.0f, .z = 0.0f}, .axes = {{{.x = 1.0f, .y = 0.0f, .z = 0.0f}, {.x = 0.0f, .y = 1.0f, .z = 0.0f}, {.x = 0.0f, .y = 0.0f, .z = 1.0f}}}, .min = l.min, .max = l.max};
            paint.cuboid(box, colour, thickness);
        }
        const V3 centre = (l.min + l.max) * 0.5f;
        V3 target{};
        if (!l.off && ((l.kind == Kind::kJumpTo && get_node_pos(l.calls.target, &target)) ||
                       (l.kind == Kind::kViewCam && get_node_pos(l.calls.camera_node_, &target)))) // (map4: the camera's)
            paint.get_arrow(centre, target, get_dim(kind_colour(l.kind), kLinkArrowAlpha));
        if (n.first <= kLabelRadius || inspected)
            label({.x = centre.x, .y = l.max.y, .z = centre.z}, colour,
                  (l.off ? "(off) " : "") + describe(l.calls, l.kind) + "  " + get_label_of(l.sector));
    }
    // The editor's.
    for (const Added& a : level_editor_triggers_state().added_) {
        if (!a.alive)
            continue;
        const OrientedBox box = box_of(a.pos, a.yaw, a.spec.size);
        const uint32_t colour = a.problem ? 0xC0A0A0A0u : kind_colour(a.spec.kind);
        paint.cuboid(box, colour, kOverlayLineThickness);
        V3 target{};
        if (!a.problem &&
            (a.spec.kind == Kind::kJumpTo || a.spec.kind == Kind::kKill || a.spec.kind == Kind::kViewCam) &&
            get_node_pos(checksum_of(a.spec.target), &target))
            paint.get_arrow(a.pos, target, get_dim(colour, a.spec.kind == Kind::kKill ? kKillArrowAlpha : kLinkArrowAlpha));
        if (a.spec.kind == Kind::kGapStart) {
            const int other = partner(level_editor_triggers_state().specs_, spec_index(a.spec.name));
            if (other >= 0) {
                for (const Added& b : level_editor_triggers_state().added_) {
                    if (b.alive && same_text(b.spec.name, core::at(level_editor_triggers_state().specs_, other).name))
                        paint.get_arrow(a.pos, b.pos, get_dim(kind_colour(Kind::kGapEnd), kGapArrowAlpha));
                }
            }
        }
        std::string text = a.spec.name + ": " + kind_name(a.spec.kind);
        if (a.spec.kind == Kind::kKill || a.spec.kind == Kind::kJumpTo)
            text += " -> " + a.spec.target;
        else if (a.spec.kind == Kind::kScripts)
            text += " " + a.spec.script;
        else if (a.spec.kind == Kind::kGapStart || a.spec.kind == Kind::kGapEnd)
            text += " \"" + a.spec.text + "\"";
        else if (a.spec.kind == Kind::kBoost)
            text += " " + std::to_string(a.spec.strength);
        else if (a.spec.kind == Kind::kBreak || a.spec.kind == Kind::kViewCam)
            text += " " + a.spec.target;
        if (a.problem)
            text += " (not active)";
        label({.x = a.pos.x, .y = a.pos.y + a.spec.size.y * kHalf, .z = a.pos.z}, colour, text);
    }
}

void add_extras(V3 camera, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, ExtrasPick extras_pick, std::vector<ui::Seg>* lines, std::vector<ui::Overlay::OverlayText>* texts)
{
    const auto near_z = extras_pick.near_z_;
    const auto selected_node = extras_pick.selected_node_;
    Painter paint = {.camera = camera, .frame = &frame, .lens = &lens, .viewport = viewport, .near_z_ = near_z, .lines = lines};
    auto label = [&](V3 at, uint32_t argb, const std::string& text) {
        float x = 0.0f, y = 0.0f;
        if (paint.get_to_pixel(at, &x, &y))
            texts->push_back({.x = x, .y = y, .argb = argb, .text = text});
    };
    const V3 up = {.x = 0.0f, .y = 1.0f, .z = 0.0f};
    // Rail speed-ups: chevrons along each segment pointing uphill (where they speed the skater up), 60 apart.
    for (const chains::SpeedSegment& s : chains::speed_segments(camera, 3000.0f)) {
        V3 a = s.a, b = s.b;
        if (b.y < a.y)
            std::swap(a, b);
        const V3 d = b - a;
        const float length = std::sqrt(get_dot(d, d));
        if (length < 1.0f)
            continue;
        const V3 dir = d * (1.0f / length);
        V3 side = perpendicular(dir, up);
        if (get_dot(side, side) < kMinSideLengthSquared)
            side = {.x = 1.0f, .y = 0.0f, .z = 0.0f};
        side = make_unit(side);
        const uint32_t colour = s.level ? 0xC0B4B4B4u : 0xFFFF9F1Au;
        const int count = std::max(1, std::min(24, static_cast<int>(length / 60.0f)));
        for (int i = 1; i <= count; ++i) {
            const V3 tip = a + d * (static_cast<float>(i) / static_cast<float>(count + 1)) + up * 4.0f;
            paint.get_seg(tip - dir * kArrowHeadLength + side * kArrowHeadWidth, tip, colour, kOverlayLineThickness);
            paint.get_seg(tip - dir * kArrowHeadLength - side * kArrowHeadWidth, tip, colour, kOverlayLineThickness);
        }
        const V3 middle = (a + b) * 0.5f;
        const V3 m = middle - camera;
        if (get_dot(m, m) < kSpeedUpLabelDistance * kSpeedUpLabelDistance) {
            std::array<char, kSpeedUpTextSize> text{};
            text::format_to(text, "%s+%.1f%%", s.level ? "level speed-up " : "speed-up ", s.rate * 100.0f);
            label(middle + up * kSpeedUpLabelLift, colour, text.data());
        }
    }
    // Proximity nodes: the edited ones and the selected one - a sphere's three rings, a box's edges.
    const std::vector<nodes::Elem>& nodes = nodes::get_all();
    const uint32_t selected =
        selected_node >= 0 && std::cmp_less(selected_node, nodes.size()) ? core::at(nodes, selected_node).name : 0;
    auto draw_proxim = [&](uint32_t node, bool is_selected) {
        const objects::ProximNode* p = is_proxim(node) ? objects::proxim_get_node(node) : nullptr;
        if (!p)
            return;
        const V3 at = {.x = p->at_point().x, .y = p->at_point().y, .z = p->at_point().z};
        const V3 d = at - camera;
        if (!is_selected && get_dot(d, d) > kProximDrawDistance * kProximDrawDistance)
            return;
        const uint32_t colour = is_selected ? 0xFFFFB060u : 0xC0F08A3Cu;
        V3 top = at;
        if (p->form() == objects::ProximShape::kSphere && p->radius() > 0.0f) {
            constexpr int kSides = 32;
            const std::array<std::array<V3, 2>, 3> axes = {{{{{.x = 1, .y = 0, .z = 0}, {.x = 0, .y = 0, .z = 1}}}, {{{.x = 1, .y = 0, .z = 0}, {.x = 0, .y = 1, .z = 0}}}, {{{.x = 0, .y = 1, .z = 0}, {.x = 0, .y = 0, .z = 1}}}}};
            for (const auto& plane : axes) {
                V3 previous = at + core::at(plane, 0) * p->radius();
                for (int i = 1; i <= kSides; ++i) {
                    const float angle = 2.0f * 3.14159265f * static_cast<float>(i) / kSides;
                    const V3 next =
                        at + core::at(plane, 0) * (p->radius() * std::cos(angle)) + core::at(plane, 1) * (p->radius() * std::sin(angle));
                    paint.get_seg(previous, next, colour, 1.0f);
                    previous = next;
                }
            }
            top = at + up * p->radius();
        } else if (p->form() == objects::ProximShape::kBox) {
            const V3 min = {.x = p->box().min.x, .y = p->box().min.y, .z = p->box().min.z};
            const V3 max = {.x = p->box().max.x, .y = p->box().max.y, .z = p->box().max.z};
            const OrientedBox box = {
                .origin = {.x = 0.0f, .y = 0.0f, .z = 0.0f}, .axes = {{{.x = 1.0f, .y = 0.0f, .z = 0.0f}, {.x = 0.0f, .y = 1.0f, .z = 0.0f}, {.x = 0.0f, .y = 0.0f, .z = 1.0f}}}, .min = min, .max = max};
            paint.cuboid(box, colour, kProximBoxThickness);
            top = {.x = (min.x + max.x) * kHalf, .y = max.y, .z = (min.z + max.z) * kHalf};
        }
        if (const ProximEdit* e = proxim_of(node)) {
            std::string text = "Proximity";
            if (!e->enter.empty())
                text += "  in: " + e->enter;
            if (!e->exit.empty())
                text += "  out: " + e->exit;
            label(top + up * kProximLabelLift, colour, text);
        }
    };
    for (const ProximEdit& e : stored_proxims()) {
        if (e.node != selected)
            draw_proxim(e.node, false);
    }
    if (selected && is_proxim(selected))
        draw_proxim(selected, true);
    // Breakables: cracks over the piece's box (each face's middle, five jagged rays); jump boosts: an arrow up as
    // strong as they are (the level's too).
    for (const Added& a : level_editor_triggers_state().added_) {
        if (!a.alive)
            continue;
        const V3 d = a.pos - camera;
        if (get_dot(d, d) > kBreakDrawDistance * kBreakDrawDistance)
            continue;
        if (a.spec.kind == Kind::kBreak) {
            const uint32_t colour = a.broken ? 0x90A0A0A0u : 0xFFE8F4FFu;
            const V3 half = a.spec.size * 0.5f;
            const std::array<V3, 4> normals = {{{.x = 1, .y = 0, .z = 0}, {.x = -1, .y = 0, .z = 0}, {.x = 0, .y = 0, .z = 1}, {.x = 0, .y = 0, .z = -1}}};
            for (const V3& n : normals) {
                const V3 across = {.x = n.z, .y = 0.0f, .z = -n.x};
                const float width = std::fabs(across.x) * half.x + std::fabs(across.z) * half.z;
                const V3 centre = a.pos + V3{.x = n.x * half.x, .y = 0.0f, .z = n.z * half.z};
                for (int ray = 0; ray < kCrackRays; ++ray) {
                    const float angle = 0.6f + static_cast<float>(ray) * 1.26f;
                    V3 previous = centre;
                    for (int step = 1; step <= 3; ++step) {
                        const float reach = static_cast<float>(step) / 3.0f;
                        const float jag = (step & 1) ? 0.18f : -0.12f;
                        const V3 next = centre + across * (std::cos(angle + jag) * width * 0.8f * reach) +
                                        up * (std::sin(angle + jag) * half.y * 0.8f * reach);
                        paint.get_seg(previous, next, colour, kCrackThickness);
                        previous = next;
                    }
                }
            }
            label(a.pos + up * (half.y + kBreakLabelLift), colour,
                  std::string(a.broken ? "broken (back in the editor)" : "breaks when hit") +
                      (a.spec.sound.empty() ? "" : "  " + a.spec.sound));
        } else if (a.spec.kind == Kind::kBoost) {
            const V3 base = a.pos + up * (a.spec.size.y * 0.5f);
            paint.get_arrow(base, base + up * (static_cast<float>(a.spec.strength) * kBoostArrowScale), kind_colour(Kind::kBoost));
        }
    }
    for (const LevelTrigger& l : level_editor_triggers_state().level_) {
        if (l.kind != Kind::kBoost || l.off)
            continue;
        const V3 centre = (l.min + l.max) * 0.5f;
        const V3 d = centre - camera;
        if (get_dot(d, d) > kBoostDrawDistance * kBoostDrawDistance)
            continue;
        const float strength = l.calls.boost_speed_ > 0.0f ? l.calls.boost_speed_ : 600.0f;
        const V3 base = {.x = centre.x, .y = l.max.y, .z = centre.z};
        paint.get_arrow(base, base + up * (strength * kBoostArrowScale), get_dim(kind_colour(Kind::kBoost), kLevelBoostArrowAlpha));
    }
}

// --- resize handles ---

namespace {

constexpr std::array<uint32_t, 3> kHandleColours = {{0xFFFF4040u, 0xFF40E040u, 0xFF4080FFu}}; // (x, y, z: as the move arrows)

// The selected editor trigger's node, the view add_overlay last drew with, the handles where they were drawn, the one
// under the cursor, and a resize under way.
uint32_t& stored_selected_node()
{
    static uint32_t selected_node{};
    return selected_node;
}
struct LastView {
    bool have = false;
    V3 camera = {};
    TickCycle frame = {};
    Lens lens = {};
    std::array<uint32_t, 4> viewport = {};
    float near_z_ = 1.0f;
};

LastView& last_view()
{
    static core::NoDestroy<LastView> state;
    return state.get();
}
struct DrawnHandle {
    int handle;
    float x, y;
};
// NOT ORIGINAL grouping - the handles drawn and the one hovered (were globals). Made on first use, never destroyed
// (trigger_handles()).
struct TriggerHandles {
    std::vector<DrawnHandle> drawn_handles_;
    int hover_handle_ = -1;
};

TriggerHandles& trigger_handles()
{
    static core::NoDestroy<TriggerHandles> state;
    return state.get();
}

struct ResizeDrag {
    bool on = false;
    std::string name;
    uint32_t node = 0;
    int handle = -1;
    V3 pos0 = {}, size0 = {}, start = {}, direction = {};
    float yaw = 0.0f, s0 = 0.0f;
    bool proxim = false, sphere = false; // (a proximity node's shape)
};

ResizeDrag& resize_drag()
{
    static core::NoDestroy<ResizeDrag> state;
    return state.get();
}

void remember_view(V3 camera, const TickCycle& frame, const Lens& lens, std::span<const uint32_t, 4> viewport, float near_z)
{
    last_view().have = true;
    last_view().camera = camera;
    last_view().frame = frame;
    last_view().lens = lens;
    for (int i = 0; i < 4; ++i)
        core::at(last_view().viewport, i) = core::at(viewport, i);
    last_view().near_z_ = near_z;
}

const Added* alive_named(uint32_t node)
{
    for (const Added& a : level_editor_triggers_state().added_) {
        if (a.alive && hash_of_name(a.spec.name) == node)
            return &a;
    }
    return nullptr;
}

} // namespace

void set_selected(uint32_t node)
{
    if (node != stored_selected_node())
        trigger_handles().hover_handle_ = -1;
    stored_selected_node() = node;
}

namespace {

// What the handles resize: an editor trigger's box (not a breakable's: its box is its piece's), or a proximity node's
// sphere / box (its edit's, else the shape the game has).
struct HandleShape {
    bool trigger = false, sphere = false;
    V3 pos = {};
    float yaw = 0.0f;
    V3 size = {};
};
bool shape_for(uint32_t node, HandleShape* s)
{
    *s = HandleShape{};
    if (const Added* a = alive_named(node)) {
        if (a->spec.kind == Kind::kBreak)
            return false;
        s->trigger = true;
        s->pos = a->pos;
        s->yaw = a->yaw;
        s->size = a->spec.size;
        return true;
    }
    const objects::ProximNode* p = is_proxim(node) ? objects::proxim_get_node(node) : nullptr;
    if (!p)
        return false;
    const V3 at = {.x = p->at_point().x, .y = p->at_point().y, .z = p->at_point().z};
    const ProximEdit* e = proxim_of(node);
    if (e && e->shape) {
        s->sphere = !e->box;
        s->pos = e->box ? at + e->offset : at;
        s->size = e->box ? e->size : V3{.x = e->radius * kDiameterFactor, .y = e->radius * kDiameterFactor, .z = e->radius * kDiameterFactor};
        return true;
    }
    if (p->form() == objects::ProximShape::kSphere) {
        s->sphere = true;
        s->pos = at;
        s->size = {.x = p->radius() * kDiameterFactor, .y = p->radius() * kDiameterFactor, .z = p->radius() * kDiameterFactor};
        return p->radius() > 0.0f;
    }
    if (p->form() == objects::ProximShape::kBox) {
        const V3 min = {.x = p->box().min.x, .y = p->box().min.y, .z = p->box().min.z};
        const V3 max = {.x = p->box().max.x, .y = p->box().max.y, .z = p->box().max.z};
        s->pos = (min + max) * kHalf;
        s->size = max - min;
        return s->size.x > 0.0f && s->size.y > 0.0f && s->size.z > 0.0f;
    }
    return false; // (a sector's own shape: not resized here)
}

} // namespace

void add_handles(std::vector<ui::Overlay::Circle>* circles)
{
    trigger_handles().drawn_handles_.clear();
    HandleShape shape;
    if (!last_view().have || !shape_for(stored_selected_node(), &shape))
        return;
    for (int h = 0; h < kHandleCount; ++h) {
        const V3 p = handle_point(shape.pos, shape.yaw, shape.size, h);
        float nx = 0.0f, ny = 0.0f, x = 0.0f, y = 0.0f;
        if (!cast_onto(last_view().camera, last_view().frame, last_view().lens, p, last_view().near_z_, &nx, &ny))
            continue;
        ndc_to_pixel(last_view().viewport, nx, ny, &x, &y);
        const bool lit = h == trigger_handles().hover_handle_ || (resize_drag().on && h == resize_drag().handle);
        const uint32_t colour = lit ? 0xFFFFFF40u : core::at(kHandleColours, h >> 1);
        circles->push_back({.x = x, .y = y, .radius = lit ? kLitHandleRadius : kHandleRadius, .argb = colour, .filled = true, .thickness = 1.0f});
        circles->push_back({.x = x, .y = y, .radius = lit ? kLitHandleRadius : kHandleRadius, .argb = kHandleOutlineColour, .filled = false, .thickness = 1.0f});
        trigger_handles().drawn_handles_.push_back({.handle = h, .x = x, .y = y});
    }
}

int handle_at(maths::Vec2 at, float radius)
{
    const float x = at.x, y = at.y;
    int best = -1;
    float best_distance = radius;
    for (const DrawnHandle& d : trigger_handles().drawn_handles_) {
        const float distance = std::sqrt((d.x - x) * (d.x - x) + (d.y - y) * (d.y - y));
        if (distance <= best_distance) {
            best_distance = distance;
            best = d.handle;
        }
    }
    return best;
}

void set_hover_handle(int handle) { trigger_handles().hover_handle_ = handle; }

bool begin_resize(int handle, V3 origin, V3 dir)
{
    HandleShape shape;
    if (!shape_for(stored_selected_node(), &shape) || handle < 0 || handle > kLastHandle)
        return false;
    const Added* a = shape.trigger ? alive_named(stored_selected_node()) : nullptr;
    ResizeDrag r;
    r.name = a ? a->spec.name : std::string();
    r.node = stored_selected_node();
    r.handle = handle;
    r.pos0 = shape.pos;
    r.size0 = shape.size;
    r.yaw = shape.yaw;
    r.proxim = !shape.trigger;
    r.sphere = shape.sphere;
    r.start = handle_point(shape.pos, shape.yaw, shape.size, handle);
    r.direction = handle_direction({.yaw = shape.yaw, .handle = handle});
    if (!closest_on_axis({.origin = r.start, .dir = r.direction}, {.origin = origin, .dir = dir}, &r.s0))
        return false; // (looking along the axis: no grip on it)
    r.on = true;
    resize_drag() = r;
    note_undo(); // (the state before: one undo step, taken by the editor next frame)
    return true;
}

void rescale(V3 origin, V3 dir, bool snap)
{
    if (!resize_drag().on)
        return;
    float s = 0.0f;
    if (!closest_on_axis({.origin = resize_drag().start, .dir = resize_drag().direction}, {.origin = origin, .dir = dir}, &s))
        return;
    float along = s - resize_drag().s0;
    if (!std::isfinite(along) || std::fabs(along) > kMaxTriggerSize)
        return; // (the ray nearly along the axis: far off)
    const int axis = resize_drag().handle >> 1;
    const float size0 = axis == 0 ? resize_drag().size0.x : axis == 1 ? resize_drag().size0.y : resize_drag().size0.z;
    if (snap)
        along = snap_to_grid(size0 + along, kResizeSnapGrid) - size0;
    V3 pos{}, size{};
    resize_face(resize_drag().pos0, resize_drag().yaw, resize_drag().size0, {.handle = resize_drag().handle, .along = along}, {.new_pos_ = &pos, .new_size_ = &size});
    if (resize_drag().proxim) {
        // (a proximity node: a sphere's radius grows by what the face moved, about its middle; a box as a trigger's -
        // its middle kept as an offset from the node)
        const objects::ProximNode* p = is_proxim(resize_drag().node) ? objects::proxim_get_node(resize_drag().node) : nullptr;
        if (!p) {
            resize_drag().on = false;
            return;
        }
        ProximEdit e = proxim_of(resize_drag().node) ? *proxim_of(resize_drag().node) : ProximEdit{};
        e.node = resize_drag().node;
        e.shape = true;
        e.box = !resize_drag().sphere;
        if (resize_drag().sphere) {
            e.radius = std::max(kProximMin, std::min(kProximMax, size0 * kHalf + along));
        } else {
            e.size = size;
            e.offset = pos - V3{.x = p->at_point().x, .y = p->at_point().y, .z = p->at_point().z};
        }
        set_proxim(e, false);
        return;
    }
    const int i = spec_index(resize_drag().name);
    if (i < 0) {
        resize_drag().on = false;
        return;
    }
    Spec spec = core::at(level_editor_triggers_state().specs_, i);
    if (spec.size.x != size.x || spec.size.y != size.y || spec.size.z != size.z) {
        spec.size = size;
        set_spec(resize_drag().name, spec, false);
    }
    node_edit::Values values;
    if (node_edit::get_take_in(resize_drag().node, &values) &&
        (values.pos.x != pos.x || values.pos.y != pos.y || values.pos.z != pos.z)) {
        values.pos = pos;
        node_edit::assign_value(resize_drag().node, values);
    }
}

void end_resize() { resize_drag().on = false; }

bool resizing() { return resize_drag().on; }

// --- leaving (exit triggers) ---

namespace {

constexpr uint32_t kSkater = 0x5B8AB877u; // Skater (the local skater's alias)
struct Seen {
    uint32_t node;
    bool inside;
};
std::vector<Seen>& stored_seen()
{
    static core::NoDestroy<std::vector<Seen>> seen;
    return seen.get();
}

// The trigger's TriggerScript run on the skater, as the trigger component's get_trigger_node (0x00434A60) runs it: not when
// the skate module skips the node (0x00565BB0), its online_mode_flag / Permanent flags, the node's name.
void fire_on_leaving(objects::Entity* skater, uint32_t node)
{
    script::Struct* s = level_nodes::fetch_vertex(node, false);
    uint32_t script = 0;
    if (!s || !s->read_hash_value(kTriggerScript, &script, 0) || !script)
        return;
    if (modules::Skate::should_be_absent(s))
        return;
    static const uint32_t kOnlineFlag = get_checksums("NetEnabled"), kPermanent = get_checksums("Permanent");
    skater->start_script_task(script, static_cast<int>(node), s->has_bare_flag(kOnlineFlag),
                              s->has_bare_flag(kPermanent), nullptr);
    if (debug_enabled())
        get_log("level editor: trigger %s left - its script run (exit)", get_label_of(node).c_str());
}

} // namespace

void perform(bool editing)
{
    const bool any = std::ranges::any_of(level_editor_triggers_state().added_, [](const Added& a) { return a.alive && a.spec.exit; });
    if (editing || !any || trigger_edit().boxes_.empty() || layout::get_refused()) { // (a network session or a career-type mode)
        stored_seen().clear(); // (seen afresh once play goes on: a box moved while editing doesn't fire)
        return;
    }
    auto* skater = core::down_cast<objects::GameEntity>(objects::find_object(kSkater));
    if (!skater)
        return;
    const V3 at = {.x = skater->at_point().x, .y = skater->at_point().y, .z = skater->at_point().z};
    for (const Added& a : level_editor_triggers_state().added_) {
        if (!a.alive || !a.spec.exit || a.problem)
            continue;
        uint32_t node = hash_of_name(a.spec.name);
        const bool now = get_inside(box_of(a.pos, a.yaw, a.spec.size), at);
        auto it = std::ranges::find_if(stored_seen(), [&](const Seen& s) { return s.node == node; });
        const bool known = it != stored_seen().end();
        if (get_left(known, known && it->inside, now))
            fire_on_leaving(skater, node);
        if (known)
            it->inside = now;
        else
            stored_seen().push_back({.node = node, .inside = now});
    }
}

// --- undo ---

State store_away()
{
    State state;
    state.specs = level_editor_triggers_state().specs_;
    for (const node_create::Spec& n : node_create::at_hand()) {
        if (n.owned && spec_index(n.name) >= 0)
            state.nodes.push_back(n);
    }
    if (!level_editor_triggers_state().switched_.empty())
        state.walls = walls::store_away(level_editor_triggers_state().switched_);
    state.proxims = stored_proxims();
    return state;
}

void get_restore(const State& state)
{
    // The trigger nodes there now that the state hasn't: removed; those it has: made, or put where it had them.
    for (const node_create::Spec& n : node_create::at_hand()) {
        if (!n.owned || spec_index(n.name) < 0)
            continue;
        const bool keep =
            std::ranges::any_of(state.nodes, [&](const node_create::Spec& s) { return same_text(s.name, n.name); });
        if (!keep) {
            node_create::Spec removed;
            int at = 0;
            node_create::drop_out(hash_of_name(n.name), &removed, &at);
        }
    }
    level_editor_triggers_state().specs_ = state.specs;
    std::string message;
    for (const node_create::Spec& n : state.nodes) {
        const uint32_t name = hash_of_name(n.name);
        if (!node_create::is_added(name)) {
            node_create::fabricate(n, -1, &message);
        } else {
            node_edit::Values values;
            if (node_edit::get_take_in(name, &values) && (values.pos.x != n.pos.x || values.pos.y != n.pos.y ||
                                                          values.pos.z != n.pos.z || values.angles.y != n.angles.y)) {
                values.pos = n.pos;
                values.angles = n.angles;
                node_edit::assign_value(name, values);
            }
        }
    }
    if (!state.walls.empty())
        walls::apply_states(state.walls);
    stored_proxims() = state.proxims;
    trigger_edit().has_undo_ = false;
    get_sync();
}

bool take_undo(State* before)
{
    if (!trigger_edit().has_undo_)
        return false;
    trigger_edit().has_undo_ = false;
    *before = std::move(trigger_edit().undo_before_);
    trigger_edit().undo_before_ = State{};
    return true;
}

int take_select()
{
    const int select = trigger_edit().select_;
    trigger_edit().select_ = -1;
    return select;
}

uint32_t take_wall_select()
{
    const uint32_t wall = level_editor_triggers_state().select_wall_;
    level_editor_triggers_state().select_wall_ = 0;
    return wall;
}

// --- the layout ---

std::vector<Placed> capture_added()
{
    std::vector<Placed> out;
    for (const Spec& spec : level_editor_triggers_state().specs_) {
        Placed p;
        p.spec = spec;
        if (!node_alive(spec.name) || !node_place(spec.name, &p.pos, &p.yaw))
            continue;
        V3 pos{}, size{};
        if (spec.kind == Kind::kBreak && break_place(spec, {.pos = &pos, .size = &size})) {
            // (a breakable's box is its piece's where it is now - what the bake makes)
            p.pos = pos;
            p.yaw = 0.0f;
            p.spec.size = size;
        }
        out.push_back(p);
    }
    return out;
}

int get_apply(const std::vector<Placed>& placed, std::string* message)
{
    note_level();
    if (!level_editor_triggers_state().scanned_)
        scan_level();
    int made = 0;
    for (const Placed& p : placed) {
        if (spec_index(p.spec.name) >= 0 || node_create::name_taken(p.spec.name)) {
            get_log("level editor: the layout's trigger %s is there already - skipped", p.spec.name.c_str());
            continue;
        }
        if (!node_create::fabricate(node_spec(p.spec, p.pos, p.yaw), -1, message))
            continue;
        level_editor_triggers_state().specs_.push_back(p.spec);
        ++made;
    }
    get_sync();
    return made;
}

void reset_all()
{
    for (const Spec& spec : std::vector<Spec>(level_editor_triggers_state().specs_)) {
        node_create::Spec removed;
        int at = 0;
        node_create::drop_out(hash_of_name(spec.name), &removed, &at);
    }
    level_editor_triggers_state().specs_.clear();
    level_editor_triggers_state().switched_.clear();
    level_editor_triggers_state().inspected_ = 0;
    trigger_edit().has_undo_ = false;
    stored_proxims().clear(); // (sync_proxims puts their nodes back)
    get_sync();
}

std::vector<ProximEdit> capture_proxims()
{
    std::vector<ProximEdit> out;
    for (const ProximEdit& e : stored_proxims()) {
        if (proxim_edited(e))
            out.push_back(e);
    }
    return out;
}

int apply_proxims(const std::vector<ProximEdit>& edits)
{
    note_level();
    int found = 0;
    for (const ProximEdit& e : edits) {
        const int i = proxim_index(e.node);
        if (i >= 0)
            core::at(stored_proxims(), i) = e;
        else
            stored_proxims().push_back(e);
        found += have_proxim_manager() && objects::proxim_get_node(e.node) ? 1 : 0;
    }
    sync_proxims();
    return found;
}

// --- the feature map's game side ---

Calls script_calls(uint32_t script) { return script ? calls_of(script) : Calls{}; }

void add_boost()
{
    Placed p;
    p.spec.name = node_create::fresh_name("ed_trigger", name_taken);
    p.spec.kind = Kind::kBoost;
    p.spec.size = {.x = kBoostWidth, .y = kBoostHeight, .z = kBoostWidth};
    p.spec.strength = kDefaultBoostStrength;
    p.pos = place_ahead(p.spec.size, 0.0f);
    p.yaw = trigger_edit().yaw_;
    fabricate({p}, "a jump boost");
}

void add_camera()
{
    Placed p;
    p.spec.name = node_create::fresh_name("ed_trigger", name_taken);
    p.spec.kind = Kind::kViewCam;
    p.spec.seconds = kDefaultCameraShotSeconds;
    p.pos = place_ahead(p.spec.size, 0.0f);
    p.yaw = trigger_edit().yaw_;
    // (the nearest of the editor's cameras - another is chosen in the Inspector)
    float best = 0.0f;
    for (const cameras::ViewCam& camera : cameras::get_all()) {
        const V3 d = camera.pos - p.pos;
        const float distance = d.x * d.x + d.y * d.y + d.z * d.z;
        if (p.spec.target.empty() || distance < best) {
            best = distance;
            p.spec.target = camera.name;
        }
    }
    const std::string what =
        p.spec.target.empty() ? std::string("a camera trigger") : "a camera trigger (" + p.spec.target + ")";
    fabricate({p}, what.c_str());
}

const Added* breakable_of(uint32_t sector)
{
    for (const Added& a : level_editor_triggers_state().added_) {
        if (a.alive && a.spec.kind == Kind::kBreak && checksum_of(a.spec.target) == sector)
            return &a;
    }
    return nullptr;
}

void set_breakable(uint32_t sector, bool on)
{
    const Added* there = breakable_of(sector);
    if (on == (there != nullptr))
        return;
    if (on) {
        Placed p;
        p.spec.name = node_create::fresh_name("ed_trigger", name_taken);
        p.spec.kind = Kind::kBreak;
        p.spec.target = name_for(sector, script::kept_qb_name(sector));
        p.spec.sound = break_sounds().front();
        p.spec.replaces = sector; // (its collision off while it's breakable: the skater goes into it, as into glass)
        if (!break_place(p.spec, {.pos = &p.pos, .size = &p.spec.size})) {
            hud::get_message("Piece has no bounds", hud::kShortMessageSeconds);
            return;
        }
        fabricate({p}, "a breakable");
        return;
    }
    // (off: its trigger removed - its collision comes back with it - and the piece shown if a test run broke it)
    note_undo();
    const std::string name = there->spec.name;
    node_create::Spec removed;
    int at = 0;
    node_create::drop_out(hash_of_name(name), &removed, &at);
    const int i = spec_index(name);
    if (i >= 0)
        level_editor_triggers_state().specs_.erase(level_editor_triggers_state().specs_.begin() + i);
    if (render::WorldCell* piece = scenery::sector_of(sector);
        piece && !is_sector_enabled(sector) && !scenery::get_hidden(sector))
        piece->change_live(true);
    get_sync();
    hud::get_message("No longer breakable", hud::kShortMessageSeconds);
}

const ProximEdit* proxim_of(uint32_t node)
{
    const int i = proxim_index(node);
    return i >= 0 ? &core::at(stored_proxims(), i) : nullptr;
}

void set_proxim(const ProximEdit& edit, bool undo)
{
    if (undo)
        note_undo();
    ProximEdit e = edit;
    e.radius = std::max(kProximMin, std::min(kProximMax, e.radius));
    e.size = {.x = std::max(kProximMin, std::min(kProximMax, e.size.x)),
              .y = std::max(kProximMin, std::min(kProximMax, e.size.y)),
              .z = std::max(kProximMin, std::min(kProximMax, e.size.z))};
    const int i = proxim_index(e.node);
    if (!proxim_edited(e)) {
        if (i >= 0)
            stored_proxims().erase(stored_proxims().begin() + i);
    } else if (i >= 0) {
        core::at(stored_proxims(), i) = e;
    } else {
        stored_proxims().push_back(e);
    }
    sync_proxims();
}

bool is_proxim(uint32_t node) { return node && have_proxim_manager() && objects::proxim_get_node(node) != nullptr; }

bool has_handles(uint32_t node) { return is_trigger(node) || is_proxim(node); }

std::string describe()
{
    int alive = 0;
    for (const Added& a : level_editor_triggers_state().added_)
        alive += a.alive ? 1 : 0;
    int off = 0;
    for (const LevelTrigger& l : level_editor_triggers_state().level_)
        off += l.off ? 1 : 0;
    std::array<char, kStatusTextSize> text{};
    text::format_to(text, "%u level trigger volumes (%d off), %d added", static_cast<unsigned>(level_editor_triggers_state().level_.size()), off,
                    alive);
    return text.data();
}

std::string describe_level_trigger(const LevelTrigger& l) { return describe(l.calls, l.kind); }

} // namespace frontend::level_editor::triggers
