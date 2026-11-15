#pragma once

// NOT ORIGINAL - the settings model: every setting of thug2-decomp.ini the game documents (README "Settings") declared
// once - its [section] and key, its kind and choices, its default, its Options screen tab, name and help line, whether a
// change is put to use at once or at the next start, and its rules:
//   needs     - what it needs to be in effect: other settings (their effective values: Shadows needs Ray tracing on,
//               Ray tracing needs the Vulkan renderer, a ray tracing GPU and the 64-bit game) and the machine, the build
//               and the data (capabilities: an NVIDIA RTX, surface maps made, a texture set installed...). A setting
//               that isn't in effect takes its `off` value (a switch 0) - so a setting that needs it is out of effect
//               too, and says why at the root ("Needs a ray tracing GPU", not "Needs Shadows").
//   choices   - a choice can have needs of its own and a choice used instead (3x frame generation on an RTX 40: 2x).
//   forces    - a value another setting sets (Frame generation turns the upscaler on at native size; a time of day or a
//               sun other than the level's turns the relit mode on).
//   conflicts - two settings that work against each other, both left in effect (V-sync holding generated frames).
// The resolver (resolve) turns the ini's values, the device's capabilities and the level's facts into what is in effect,
// each setting's effective value and the reason when it isn't what was asked - a pure function: the renderer reads its
// settings through it (engine/config/settings_state.h running()), the Options screen greys a row with its reason, the
// screenshot's settings report prints it and thug2_options_check checks every rule on several machines. docs/design/
// settings.md.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace config {

// A setting's values: a switch (0 / 1, read as a number: anything but 0 is on), one of its choices, a number, or text
// (a colour, a list, a path).
enum class Kind : uint8_t { kSwitch, kChoice, kNumber, kText };

// When a change is seen.
enum class Apply : uint8_t {
    kLive,     // at once (the Options screen writes it and the game reads it again)
    kRelaunch, // the next time the game starts (read once at start-up)
    kPreset,   // not a key of its own: a choice sets several others (graphics_preset - presets())
};

// The Options screen's tabs, in their order (kNone: not offered - the ini's alone).
enum class Group : uint8_t {
    kNone,
    kGraphics,
    kLighting,
    kSurfaces,
    kSky,
    kColour,
    kCamera,
    kStyle,
    kUpscaling,
    kInterface,
    kGameplay,
    kOnline,
    kMods,
    kSound,
};
inline constexpr int kGroupCount = static_cast<int>(Group::kSound) + 1;
struct GroupInfo {
    Group group;
    const char* id;   // "graphics", ... (automation)
    const char* name; // the tab's name
};
const std::vector<GroupInfo>& groups(); // (the tabs, kNone left out)

// What a setting changes in the renderer: the part the game reads again when the setting's effective value changes
// while it runs (app/renderer_choice.cpp - one reader each). In the order they are read: the sun's source before the
// effects lit by it, the time of day before the relit mode, the shadows before the bounce light.
enum class Effect : uint8_t {
    kNone,
    kSunSource,
    kTimeOfDay,
    kShadows,
    kAo,
    kGi,
    kLights,
    kReflections,
    kHdr,
    kFog,
    kSun,
    kSky,
    kRelit,
    kPbr,
    kDetail,
    kStyle,
    kCel,
    kGrass,
    kScatter,
    kWater,
    kCameraFx,
    kSnow,
};
inline constexpr int kEffectCount = static_cast<int>(Effect::kSnow) + 1;

// What the machine, the build and the game's data have (Capabilities), and the level's facts (LevelFacts).
enum class Cap : uint8_t {
    kVulkan,          // a Vulkan 1.3 device the game can draw with (and a build with the Vulkan renderer)
    kRayQueries,      // the device has ray queries (VK_KHR_ray_query, acceleration structures)
    k64Bit,           // the 64-bit game (the upscalers, frame generation and ray tracing are built for it alone)
    kNvidia,          // an NVIDIA GPU
    kRtx,             // an NVIDIA RTX (DLSS)
    kRtx40,           // an RTX 40 or 50 (DLSS frame generation)
    kRtx50,           // an RTX 50 (DLSS frame generation 3x / 4x)
    kSurfaceMaps,     // surface maps made beside the textures (tools/pbr_maps)
    kMaterialLibrary, // the detail-material library (tools/detail_materials)
    kHeightMaps,      // height maps for displacement (none are made yet)
    kLevelWithoutFog, // the level has no fog of its own (true while no level is known)
    kLevelOutdoor,    // (added by the snow) the level is outdoors - it has a sky (true while no level is known)
};

class Reader;

// A rule's condition over other settings' effective values (`is`), their values as asked (`asked_is` - before any force
// or need: a force's condition on the setting it forces), capabilities, texture sets installed, and computed tests.
struct Cond {
    enum class Op : uint8_t { kAlways, kIs, kAskedIs, kAll, kAny, kNot, kHas, kTextureSet, kTest };
    Op op = Op::kAlways;
    const char* key = nullptr;       // kIs, kAskedIs; kTextureSet: the set's name
    std::vector<const char*> values; // kIs, kAskedIs: any of them; kTest: the keys it reads
    std::vector<Cond> parts;         // kAll, kAny, kNot (one)
    Cap cap = Cap::kVulkan;          // kHas
    bool (*test)(const Reader& reader) = nullptr; // kTest
};
Cond always();
Cond is(const char* key, std::vector<const char*> values);
Cond is_not(const char* key, std::vector<const char*> values);
Cond on(const char* key); // is(key, {"1"})
Cond asked_is(const char* key, std::vector<const char*> values);
Cond all(std::vector<Cond> parts);
Cond any(std::vector<Cond> parts);
Cond negate(Cond part);
Cond has(Cap cap);
Cond installed(const char* texture_set);
Cond computed(bool (*test)(const Reader& reader), std::vector<const char*> keys);

// One rule a setting must meet to be in effect; `reason` the player sees when it doesn't (null: made from the condition
// - "Needs " and the other setting's name, or the capability's words).
struct Need {
    Cond cond;
    const char* reason = nullptr;
};

struct Choice {
    const char* value; // as written to the ini ("" written as an empty value: the game's own default)
    const char* label; // the Options screen's text (null: not offered there)
    std::vector<Need> needs;       // (empty: always possible)
    const char* instead = nullptr; // what is used when a need fails (null: the setting's default)
};

struct Alias {
    const char* from; // an older value still read ("1" for rt_style: toon)
    const char* to;
};

struct Setting {
    const char* section = nullptr;
    const char* key = nullptr;
    Kind kind = Kind::kSwitch;
    const char* fallback = "0"; // the default (README's), as written
    Group group = Group::kNone;
    const char* label = nullptr; // the Options screen's name (null: not offered)
    const char* help = "";
    std::vector<Choice> choices;  // the Options screen's (a number's or a text's: the ones offered; others still read)
    Apply apply = Apply::kRelaunch;
    Effect effect = Effect::kNone;
    bool open = false; // a choice setting that takes other values as written (tod's HH:MM, rt_sun_source's x,y,z)
    std::vector<Alias> aliases;
    std::vector<Need> needs;
    const char* off = nullptr; // its value while it isn't in effect (null: the value asked, out of effect)
    const char* retired = nullptr; // read no more: why (the row says so; never in effect)
};

// A value one setting sets on another while `when` holds (the target's needs still apply: forced on, a relit mode with
// no ray tracing still isn't in effect).
struct Force {
    const char* key = nullptr;
    const char* value = nullptr;
    Cond when;
    const char* reason = nullptr;
};

// Two or more settings that work against each other while `when` holds - each stays in effect; each row says so.
struct Conflict {
    std::vector<const char*> keys;
    Cond when;
    const char* reason = nullptr;
};

// A graphics preset's values (the graphics_preset row's choices after Custom): [Graphics] key and value.
struct PresetValue {
    const char* key = nullptr;
    const char* value = nullptr;
};

const std::vector<Setting>& settings();
const std::vector<Force>& forces();
const std::vector<Conflict>& conflicts();
// What choosing graphics_preset's choice `choice` writes (none for Custom, 0).
const std::vector<PresetValue>& preset_values(int choice);

// The setting `key` (any case; the first of the name - a key in two sections is named by `section` too): its index into
// settings(), -1 if none.
int find(const char* key);
int find(const char* section, const char* key);
// The choice `value` is (numbers compared as numbers: "1.50" is "1.5"; names in any case), -1 if none.
int match_choice(const Setting& setting, const std::string& value);

// What is wrong with the model, one line each ("" when nothing): a key declared twice, a rule naming a setting that
// isn't one or a value that isn't one of its choices, a default that isn't a choice, a setting another's rule reads
// without an `off` value though it has needs of its own, a rule that needs an explicit reason, a cycle between rules.
std::string check_model();

// --- the resolver's inputs ---

// The ini's values: lower-case "section/key" -> the value as written (trimmed; the first of a key in the first section
// of the name, as Windows reads the file).
using IniValues = std::map<std::string, std::string>;
std::string ini_slot(const char* section, const char* key);

struct Capabilities {
    bool vulkan = false;
    bool ray_queries = false;
    bool x64 = false;
    uint32_t vendor = 0; // the GPU's PCI vendor (0x10DE NVIDIA, 0x1002 AMD, 0x8086 Intel; 0 unknown)
    int rtx = 0;         // an NVIDIA RTX's series (20, 30, 40, 50 ...; 0: none)
    bool surface_maps = false;
    bool material_library = false;
    bool height_maps = false;
    std::vector<std::string> texture_sets; // the sets installed ("original" always is)
    // (added by the de-lit texture sets) the installed sets made de-lit (base.ts --delight): rt_delight is out with one
    std::vector<std::string> delit_texture_sets;
    int refresh_hz = 0;                    // the display's (0 unknown)
    std::string gpu;                       // the device's name (the report's)
};
inline constexpr uint32_t kVendorNvidia = 0x10DE;
inline constexpr uint32_t kVendorAmd = 0x1002;
// An NVIDIA device name's RTX series ("NVIDIA GeForce RTX 5070 Ti" -> 50, "RTX 4090" -> 40, "RTX 3060" -> 30, "RTX A4000"
// -> 30); 0 when it isn't an RTX.
int rtx_series(const std::string& device_name);

struct LevelFacts {
    bool known = false;
    bool fog = false; // the level has fog of its own
    bool outdoor = true; // (added by the snow) the level has a sky (render_pc/snow_world.cpp says, each level)
};

struct Inputs {
    IniValues ini;
    Capabilities caps;
    LevelFacts level;
};

// --- the resolver's answer ---

struct Resolved {
    std::string asked;   // the value as asked: the ini's, or the default (normalised: a choice's own spelling)
    bool set = false;    // the ini has the key
    std::string value;   // the effective value
    bool in_effect = true;
    bool forced = false;    // another setting set it (Force)
    bool changed = false;   // the effective value isn't the one asked (forced, a choice not possible here, out of effect)
    std::string reason;     // why, for the player ("" when it is in effect as asked)
    std::vector<std::string> conflicts; // what works against it (Conflict reasons)
    // One per choice: "" when it is possible here, else why not (a texture set not installed, 3x without an RTX 50) -
    // the Options screen offers only the possible ones.
    std::vector<std::string> blocked;
};

class Resolution {
public:
    std::vector<Resolved> items; // one per settings() entry
    std::vector<int> active;     // the conflicts() in force

    // The setting `key`'s answer (an undeclared key: an empty one, not in effect).
    [[nodiscard]] const Resolved& at(const char* key) const;
    [[nodiscard]] const Resolved& at(const char* section, const char* key) const;
    // Its effective value.
    [[nodiscard]] const std::string& value(const char* key) const { return at(key).value; }
    // In effect and on: not 0, off or empty.
    [[nodiscard]] bool on(const char* key) const;
    // Its effective value as a number (`fallback` when it isn't one).
    [[nodiscard]] float number(const char* key, float fallback) const;
    [[nodiscard]] int integer(const char* key, int fallback) const;
};

Resolution resolve(const Inputs& inputs);

// What a rule's computed test reads: the effective values (resolved as they are asked for), the values as asked and the
// capabilities.
class Reader {
public:
    virtual ~Reader() = default;
    Reader() = default;
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    Reader(Reader&&) = delete;
    Reader& operator=(Reader&&) = delete;
    [[nodiscard]] virtual std::string value(const char* key) const = 0;
    [[nodiscard]] virtual bool in_effect(const char* key) const = 0;
    [[nodiscard]] virtual const Capabilities& caps() const = 0;
};

// A value as a switch's: on unless it is 0, off or empty (a number read as GetPrivateProfileInt reads it).
bool truthy(const std::string& value);

} // namespace config
