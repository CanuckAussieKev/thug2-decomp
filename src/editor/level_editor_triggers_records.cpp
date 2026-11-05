// NOT ORIGINAL - the level editor's trigger records: the kinds, their checks and colours, the volumes' boxes, and the
// scripts and text lines a trigger is saved as (ours). See level_editor_triggers.h.

#include "editor/level_editor_triggers.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <new>
#include <span>
#include <sstream>
#include <string>
#include <vector>
#include <utility>
#include "editor/level_editor.h"
#include "base/core/checksums.h"
#include "editor/level_editor_node_create.h"
#include "engine/script/qb.h"
#include <gsl/pointers>
#include <array>
#include "base/core/ints.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "editor/level_editor_triggers_internal.h"

namespace frontend::level_editor::triggers {
namespace {

constexpr int kBitsPerByte = 8;
constexpr std::size_t kHexNameLength = 10; // "0x" and 8 hex digits
constexpr std::size_t kHexNameDigits = 8;
constexpr std::size_t kTripleTextSize = 96; // "x,y,z" with room
constexpr std::size_t kNumberTextSize = 16;
constexpr std::size_t kHexTextSize = 16;
constexpr std::size_t kRadiusTextSize = 48;
constexpr std::size_t kScriptNameSize = 32;

constexpr std::size_t kMaxCleanTextLength = 60; // the longest gap name written into a script
constexpr int kMaxGapScore = 1000000;
constexpr float kMinTriggerSize = 10.0F; // a trigger box side limits

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

// A breakable Shatter call.
constexpr int kShatterArea = 5000;
constexpr float kShatterSpread = 1.5F;
constexpr float kShatterScale = 0.4F;
constexpr int kShatterLiftSpeed = 60;

} // namespace
namespace internal {

bool same_text(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        char x = core::at(a, i);
        char y = core::at(b, i);
        if (x >= 'A' && x <= 'Z') {
            x = static_cast<char>(x - 'A' + 'a');
        }
        if (y >= 'A' && y <= 'Z') {
            y = static_cast<char>(y - 'A' + 'a');
        }
        if (x != y) {
            return false;
        }
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

} // namespace internal
namespace {

uint32_t current_flags()
{
    static const uint32_t kC = get_checksums("flags");
    return kC;
}

} // namespace
namespace internal {

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

} // namespace internal
namespace {

void get_put4(std::vector<uint8_t>* out, uint32_t value)
{
    for (int i = 0; i < 4; ++i) {
        out->push_back(static_cast<uint8_t>(value >> (kBitsPerByte * i)));
    }
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
    if (name.size() != kHexNameLength || core::at(name, 0) != '0' ||
        (core::at(name, 1) != 'x' && core::at(name, 1) != 'X')) {
        return false;
    }
    uint32_t v = 0;
    for (size_t i = 2; i < kHexNameLength; ++i) {
        const char character = core::at(name, i);
        const int digit = hex_digit(character);
        if (digit < 0) {
            return false;
        }
        v = v << 4 | static_cast<uint32_t>(digit);
    }
    *value = v;
    return true;
}

bool valid_name(const std::string& name) { return node_create::valid_name(name); }

bool read_number(const std::string& text, float* out)
{
    char* end = nullptr;
    *out = std::strtof(text.c_str(), &end);
    return !text.empty() && (end != nullptr) && *end == '\0' && std::isfinite(*out);
}

bool read_triple(const std::string& text, V3* out)
{
    std::array<float, 3> v{};
    size_t at = 0;
    for (int i = 0; i < 3; ++i) {
        const size_t comma = i < 2 ? text.find(',', at) : text.size();
        if (comma == std::string::npos) {
            return false;
        }
        if (!read_number(text.substr(at, comma - at), &core::at(v, i))) {
            return false;
        }
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
    if (calls.kill) {
        return Kind::kKill;
    }
    if (calls.teleport) {
        return Kind::kJumpTo;
    }
    if (calls.end_gap_) {
        return Kind::kGapEnd;
    }
    if (calls.start_gap_) {
        return Kind::kGapStart;
    }
    if (calls.boost) {
        return Kind::kBoost;
    }
    if (calls.shatter) {
        return Kind::kBreak;
    }
    if (calls.camera) {
        return Kind::kViewCam;
    }
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
    if (is_hex_form(name, &value)) {
        return value;
    }
    return name.empty() ? 0U : get_checksums(name.c_str());
}

std::string name_for(uint32_t checksum, const char* known)
{
    if ((known != nullptr) && (*known != '\0') && valid_name(known) && get_checksums(known) == checksum) {
        return known;
    }
    std::array<char, kNumberTextSize> text{};
    text::format_to(text, "0x%08X", checksum);
    return text.data();
}

std::string get_script_name(const Spec& spec) { return spec.name + "Script"; }

float clamp_seconds(float seconds)
{
    if (!std::isfinite(seconds)) {
        return kDefaultCameraShotSeconds;
    }
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
        if (spec.gap.empty()) {
            return "It has no gap";
        }
        if (spec.text.empty() || spec.score <= 0) {
            return "A gap needs a name and a score";
        }
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
    for (char character : text) {
        auto u = static_cast<unsigned char>(character);
        if (u < kFirstPrintableChar || u == kDeleteChar) {
            continue;
        }
        if (character == '"') {
            character = '\'';
        }
        if (character == '#') {
            continue;
        }
        out.push_back(character);
        if (out.size() >= kMaxCleanTextLength) {
            break;
        }
    }
    // (no spaces at either end)
    const size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos) {
        return "";
    }
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
    core::at(box.axes, 0) = turn_about_up({.x = 1.0F, .y = 0.0F, .z = 0.0F}, yaw);
    core::at(box.axes, 1) = {.x = 0.0F, .y = 1.0F, .z = 0.0F};
    core::at(box.axes, 2) = turn_about_up({.x = 0.0F, .y = 0.0F, .z = 1.0F}, yaw);
    box.min = size * -kHalf;
    box.max = size * kHalf;
    return box;
}

bool get_inside(const OrientedBox& box, V3 point)
{
    const V3 delta = point - box.origin;
    const float x = get_dot(delta, core::at(box.axes, 0));
    const float y = get_dot(delta, core::at(box.axes, 1));
    const float z = get_dot(delta, core::at(box.axes, 2));
    return x >= box.min.x && x <= box.max.x && y >= box.min.y && y <= box.max.y && z >= box.min.z && z <= box.max.z;
}

V3 handle_direction(HandleOf handle_of)
{
    const auto yaw = handle_of.yaw;
    const auto handle = handle_of.handle;
    const int axis = (handle >> 1) % 3;
    const float sign = ((handle & 1) != 0) ? 1.0F : -1.0F;
    V3 unit{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    axis_ref(unit, axis) = 1.0F;
    return turn_about_up(unit, yaw) * sign;
}

V3 handle_point(V3 pos, float yaw, V3 size, int handle)
{
    const int axis = (handle >> 1) % 3;
    const float half = axis_value(size, axis) * 0.5F;
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
    float& side = axis_ref(grown, axis);
    const float before = side;
    side = before + (std::isfinite(along) ? along : 0.0F);
    grown = clamp_size(grown);
    const float change = axis_value(grown, axis) - before;
    *new_size = grown;
    *new_pos = pos + handle_direction({.yaw = yaw, .handle = handle}) * (change * kHalf);
}

std::vector<uint8_t> get_body(const Spec& spec)
{
    std::vector<uint8_t> out;
    if (get_problem(spec)) {
        return out;
    }
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
                if (i != 0U) {
                    out.push_back(script::token_byte(script::QbToken::kComma));
                }
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
        real("variance", 1.0F);
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
        if (name.empty() || is_hex_form(name, &value)) {
            return;
        }
        const uint32_t checksum = get_checksums(name.c_str());
        for (const auto& entry : names) {
            if (entry.first == checksum) {
                return;
            }
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
            if (spec.kind == Kind::kKill || spec.kind == Kind::kJumpTo) {
                note(spec.target);
            }
            if (spec.kind == Kind::kScripts) {
                note(spec.script);
            }
            if (spec.kind == Kind::kGapStart || spec.kind == Kind::kGapEnd) {
                note(spec.gap);
            }
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
    for (const auto& name : names) {
        out.push_back(script::token_byte(script::QbToken::kChecksumName));
        get_put4(&out, name.first);
        out.insert(out.end(), name.second.begin(), name.second.end());
        out.push_back(0x00);
    }
    out.push_back(0x00);
    return out;
}

std::string link_record(const Placed& placed)
{
    const Spec& spec = placed.spec;
    std::ostringstream out;
    out << spec.name << ' ' << kind_word(spec.kind) << " pos=" << triple(placed.pos);
    std::array<char, kNumberTextSize> number{};
    text::format_to(number, " yaw=%.9g", placed.yaw);
    out << number.data() << " size=" << triple(spec.size);
    if (!spec.target.empty()) {
        out << " target=" << spec.target;
    }
    if (spec.water) {
        out << " water=1";
    }
    if (!spec.script.empty()) {
        out << " script=" << spec.script;
    }
    if (!spec.gap.empty()) {
        out << " gap=" << spec.gap;
    }
    if (!spec.flags.empty()) {
        out << " flags=";
        for (size_t i = 0; i < spec.flags.size(); ++i) {
            out << ((i != 0U) ? "," : "") << core::at(spec.flags, i);
        }
    }
    if (spec.kind == Kind::kGapStart || spec.kind == Kind::kGapEnd) {
        out << " score=" << spec.score;
    }
    if (spec.replaces != 0U) {
        text::format_to(number, " replaces=%08X", spec.replaces);
        out << number.data();
    }
    if (spec.exit) {
        out << " exit=1";
    }
    if (spec.kind == Kind::kBoost) {
        out << " strength=" << spec.strength;
    }
    if (!spec.sound.empty()) {
        out << " sound=" << spec.sound;
    }
    if (spec.kind == Kind::kViewCam) {
        text::format_to(number, " seconds=%.9g", clamp_seconds(spec.seconds));
        out << number.data();
    }
    const std::string text = clean_text(spec.text);
    if (!text.empty()) {
        out << " text=\"" << text << '"';
    }
    return out.str();
}

namespace {

// A record's flags=a,b,c: each a valid name; false at the first that isn't (the ones before it kept).
bool read_flags(const std::string& value, std::vector<std::string>* flags)
{
    bool ok = true;
    size_t at = 0;
    while (ok && at <= value.size()) {
        size_t comma = value.find(',', at);
        if (comma == std::string::npos) {
            comma = value.size();
        }
        const std::string flag = value.substr(at, comma - at);
        ok = valid_name(flag);
        flags->push_back(flag);
        at = comma + 1;
    }
    return ok;
}

} // namespace

bool read_record(const std::string& line, Placed* placed)
{
    *placed = Placed{};
    Spec& spec = placed->spec;
    std::string text = line;
    // text="..." (last: it may hold spaces)
    const size_t quote = text.find(" text=\"");
    if (quote != std::string::npos) {
        const size_t start = quote + 7;
        const size_t end = text.find('"', start);
        if (end == std::string::npos || text.find_first_not_of(" \t\r", end + 1) != std::string::npos) {
            return false;
        }
        spec.text = clean_text(text.substr(start, end - start));
        text.resize(quote);
    }
    std::istringstream in(text);
    std::string word;
    if (!(in >> spec.name >> word) || !valid_name(spec.name) || !kind_from_word(word, &spec.kind) ||
        spec.kind == Kind::kOther) {
        return false;
    }
    bool have_pos = false;
    bool have_yaw = false;
    bool have_size = false;
    bool have_target = false;
    bool have_water = false;
    bool have_script = false;
    bool have_gap = false;
    bool have_flags = false;
    bool have_score = false;
    bool have_replaces = false;
    bool have_exit = false;
    bool have_strength = false;
    bool have_sound = false;
    bool have_seconds = false;
    spec.score = kDefaultGapScore;
    auto once = [](bool* seen) {
        if (*seen) {
            return false;
        }
        *seen = true;
        return true;
    };
    std::string field;
    while (in >> field) {
        const size_t equals = field.find('=');
        if (equals == std::string::npos || equals == 0) {
            return false;
        }
        const std::string key = field.substr(0, equals);
        const std::string value = field.substr(equals + 1);
        bool ok = true;
        if (key == "pos") {
            ok = once(&have_pos) && read_triple(value, &placed->pos);
        } else if (key == "yaw") {
            ok = once(&have_yaw) && read_number(value, &placed->yaw);
        } else if (key == "size") {
            ok = once(&have_size) && read_triple(value, &spec.size);
            spec.size = clamp_size(spec.size);
        } else if (key == "target") {
            ok = once(&have_target) && valid_name(value);
            spec.target = value;
        } else if (key == "water") {
            ok = once(&have_water) && (value == "0" || value == "1");
            spec.water = value == "1";
        } else if (key == "script") {
            ok = once(&have_script) && valid_name(value);
            spec.script = value;
        } else if (key == "gap") {
            ok = once(&have_gap) && valid_name(value);
            spec.gap = value;
        } else if (key == "flags") {
            ok = once(&have_flags) && read_flags(value, &spec.flags);
        } else if (key == "score") {
            char* end = nullptr;
            const long score = std::strtol(value.c_str(), &end, 10);
            ok = once(&have_score) && !value.empty() && (end != nullptr) && *end == '\0' && score >= 0 &&
                 score <= kMaxGapScore;
            spec.score = static_cast<int>(score);
        } else if (key == "replaces") {
            char* end = nullptr;
            const unsigned long checksum = std::strtoul(value.c_str(), &end, 16);
            ok = once(&have_replaces) && !value.empty() && value.size() <= kHexNameDigits && (end != nullptr) &&
                 *end == '\0';
            spec.replaces = static_cast<uint32_t>(checksum);
        } else if (key == "exit") {
            ok = once(&have_exit) && (value == "0" || value == "1");
            spec.exit = value == "1";
        } else if (key == "strength") {
            char* end = nullptr;
            const long strength = std::strtol(value.c_str(), &end, 10);
            ok = once(&have_strength) && !value.empty() && (end != nullptr) && *end == '\0' && strength >= kBoostMin &&
                 strength <= kBoostMax;
            spec.strength = static_cast<int>(strength);
        } else if (key == "sound") {
            ok = once(&have_sound) && valid_name(value);
            spec.sound = value;
        } else if (key == "seconds") {
            float seconds = 0.0F;
            ok = once(&have_seconds) && read_number(value, &seconds) && seconds >= kCameraMinSeconds &&
                 seconds <= kCameraMaxSeconds;
            spec.seconds = seconds;
        } // (another key: a later version's - skipped)
        if (!ok) {
            return false;
        }
    }
    return have_pos;
}

int partner(const std::vector<Spec>& specs, int index)
{
    if (index < 0 || std::cmp_greater_equal(index, specs.size())) {
        return -1;
    }
    const Spec& spec = core::at(specs, index);
    Kind other{};
    if (spec.kind == Kind::kGapStart) {
        other = Kind::kGapEnd;
    } else if (spec.kind == Kind::kGapEnd) {
        other = Kind::kGapStart;
    } else {
        return -1;
    }
    for (size_t i = 0; i < specs.size(); ++i) {
        if (ints::as_signed(i) != index && core::at(specs, i).kind == other && !spec.gap.empty() &&
            same_text(core::at(specs, i).gap, spec.gap)) {
            return static_cast<int>(i);
        }
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

std::string proxim_record(const ProximEdit& proxim_edit, const char* known)
{
    std::ostringstream out;
    std::array<char, kHexTextSize> hex{};
    text::format_to(hex, "%08X", proxim_edit.node);
    out << hex.data();
    if (!proxim_edit.enter.empty()) {
        out << " enter=" << proxim_edit.enter;
    }
    if (!proxim_edit.exit.empty()) {
        out << " exit=" << proxim_edit.exit;
    }
    if (proxim_edit.shape) {
        out << " shape=" << (proxim_edit.box ? "box" : "sphere");
        if (proxim_edit.box) {
            out << " size=" << triple(proxim_edit.size) << " offset=" << triple(proxim_edit.offset);
        } else {
            std::array<char, kRadiusTextSize> radius{};
            text::format_to(radius, " radius=%.9g", proxim_edit.radius);
            out << radius.data();
        }
    }
    if (known && (*known != '\0')) {
        out << "  # " << known;
    }
    return out.str();
}

bool read_proxim_record(const std::string& line, ProximEdit* edit)
{
    *edit = ProximEdit{};
    const std::string text = line.substr(0, line.find('#'));
    std::istringstream in(text);
    std::string word;
    if (!(in >> word) || word.size() != kHexNameDigits ||
        word.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
        return false;
    }
    edit->node = static_cast<uint32_t>(std::strtoul(word.c_str(), nullptr, kHexBase));
    bool have_enter = false;
    bool have_exit = false;
    bool have_shape = false;
    bool have_radius = false;
    bool have_size = false;
    bool have_offset = false;
    auto once = [](bool* seen) {
        if (*seen) {
            return false;
        }
        *seen = true;
        return true;
    };
    std::string field;
    while (in >> field) {
        const size_t equals = field.find('=');
        if (equals == std::string::npos || equals == 0) {
            return false;
        }
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
        if (!ok) {
            return false;
        }
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
    if ((enter == 0U) && (exit == 0U)) {
        return out;
    }
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
    for (const ProximScript& proxim_script : scripts) {
        out.push_back(script::token_byte(script::QbToken::kScriptStart));
        put_name(&out, get_checksums(proxim_script_name(proxim_script.node).c_str()));
        out.push_back(0x01);
        std::vector<uint8_t> body =
            proxim_body({.enter = proxim_script.enter, .exit = proxim_script.exit, .own = proxim_script.own});
        out.insert(out.end(), body.begin(), body.end());
        out.push_back(script::token_byte(script::QbToken::kScriptEnd));
        out.push_back(0x01);
    }
    for (const ProximScript& proxim_script : scripts) {
        std::string name = proxim_script_name(proxim_script.node);
        out.push_back(script::token_byte(script::QbToken::kChecksumName));
        get_put4(&out, get_checksums(name.c_str()));
        out.insert(out.end(), name.begin(), name.end());
        out.push_back(0x00);
    }
    out.push_back(0x00);
    return out;
}

} // namespace frontend::level_editor::triggers
