// NOT ORIGINAL - the relit mode in the world the game draws: see rt_relit_world.h.
#include "render_pc/rt_relit_world.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <ratio>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "engine/config/settings_model.h"
#include "engine/config/settings_state.h"
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "engine/gfx/engine_parts.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/facade_relief.h"
#include "engine/gfx/mesh_records.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_cel.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_relit.h"
#include "engine/gfx/relit_data.h"
#include "engine/gfx/graphics_options.h"
#include "engine/gfx/day_clock.h"
#include "engine/gfx/rt_light_field.h"
#include "engine/gfx/rt_display.h"
#include "engine/gfx/rt_lights.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sky.h"
#include "engine/gfx/rt_sun.h"
#include "engine/memory/arena_alloc.h"
#include "platform/frame_profile.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"
#include "render_pc/mesh_smoothing.h"
#include "render_pc/rt_record.h"
#include "render_pc/sub_mesh.h"
#include "render_pc/vertex_buffers.h"

namespace render::rt_relit_world {

namespace {

constexpr uint64_t kFnvBasis = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;
constexpr float kSameSun = 0.99999F;          // two suns this close (their directions' dot product): the same
// (one relight at a time) The bounce - the light the surfaces round a vertex bring it - changes slowly as the sun moves:
// traced again, as a pass of its own beside the relights, once the sun is this far (the cosine of 5 degrees) from the
// sun it was traced for. A running clock's sun moves that far in 10 s of a 12-minute day.
constexpr float kBounceSameSun = 0.9961947F;
using rt::kRelitBouncePasses;
constexpr std::size_t kKilobyte = 1024;
constexpr double kDegrees = 180.0 / 3.14159265358979323846; // (the log's heading and elevation)
constexpr int kMaxSceneNodes = 256; // a scene list longer than this is taken as broken
constexpr float kChangedLuma = 2.0F; // (the log: a vertex changed by more than this, of 255)
constexpr float kLumaRed = 0.299F;
constexpr float kLumaGreen = 0.587F;
constexpr float kLumaBlue = 0.114F;
constexpr int kRedShift = 16;
constexpr int kGreenShift = 8;
constexpr uint32_t kByteMask = 0xFF; // the sky model's colour ratio kept within this

// (the offline relit data) What the level is lit by: the original's lighting (no data, data for other meshes, a bake
// with no sunlight, the level's entry) or the relit data.
enum class Status : uint8_t {
    kOff,
    kWaiting,  // for a still world
    kNoData,   // no relit data built for the level: the original's lighting
    kStale,    // its data was made from other meshes: the original's lighting
    kOptedOut, // the level's entry says so: the bake kept
    kNoModel,  // the bake couldn't be taken apart (the data says): the original's lighting
    kRelit,
};

const char* status_name(Status status)
{
    switch (status) {
    case Status::kOff:
        return "off";
    case Status::kWaiting:
        return "waiting for a still world";
    case Status::kNoData:
        return "no relit data for this level - the original's lighting (build it: node tools/game/relit_bake.ts)";
    case Status::kStale:
        return "its relit data is out of date (an older relit bake's, or made from other meshes) - the original's "
               "lighting (build it again: node tools/game/relit_bake.ts)";
    case Status::kOptedOut:
        return "the level's entry turns it off - the bake kept";
    case Status::kNoModel:
        return "the bake couldn't be taken apart - the original's lighting";
    case Status::kRelit:
        return "relit";
    }
    return "?";
}

struct Entry {
    const MeshRef* mesh = nullptr;
    VertexBuffer* buffer = nullptr;
    uint32_t stride = 0;
    uint32_t colour_offset = 0;
    uint64_t content = 0;            // its content's hash (the relit data's key: relit_data.h relit_mesh_content)
    std::vector<uint32_t> originals; // the game's colours (the bake's: relit never writes them)
    std::vector<uint8_t> animated;   // a vertex whose colour the game animates: 1
    // (lighting round A) the bake's shadow drawn as geometry - a blended mesh whose vertex colours are all black (it adds
    // no colour, only takes light away): hidden in the relit level (its store's colour word: transparent)
    bool baked_shadow = false;
    // (lighting round A3) the original's light card - an additive halo, cone, pool or starburst round a lamp
    // (WorldMesh::glow): the lamp's evidence for the bake, no light - hidden in the relit level as the shadows are
    bool glow_card = false;
    // (round light cards) the lamps pass lights it per pixel (WorldMesh::lit_per_pixel); else its store carries its lamps
    bool lit_per_pixel = true;
    // (round P10 - objects relit) an object's mesh (WorldMesh::object - its instance) and the transform it was read with:
    // when the instance stands elsewhere the entry is let go of (its light was for where it stood)
    const void* object = nullptr;
    std::array<float, 16> to_world{};
    uint64_t model_content = 0; // (its content in its own space - BakeMesh::model)
    std::size_t data_mesh = SIZE_MAX; // (round A3) its mesh in the level's data (its lamp links and emitting vertices)
    uint64_t lamps_for = 0;           // ... and the lamps' switches its L was made for (State::lamps_hash)
    rt::RelitMesh input;
    rt::RelitVertices light;
    bool analysed = false;   // its light read from the level's data
    bool looked_up = false;  // looked for in the level's data (found: analysed)
    rt::Vec3 sun_for{};    // the sun its vertices' sun was traced toward
    float radius_for = rt::kRelitSunRadiusDegrees; // ... and the light's angular radius then (rt_daylight.h light_radius)
    rt::Vec3 bounce_for{}; // ... and the sun whose light its bounce was traced with
    uint64_t bounce_pass = 0; // (round A2) ... and the pass it was traced in
    uint64_t written = 0;  // the lighting its light store was written with (0: none - the game's colours drawn)
    uint64_t listed = 0;   // the update that last put it in the order (State::listing)
    // the colours its vertices show the bounce's rays (rebounce: the relit ones), and the light they were made for: made
    // once a relight, not every frame of it
    std::vector<rt::Rgbf> hit_light; // (round L: the light each vertex reflects, physical - the bounce's hits)
    uint64_t hits_for = 0;
    // (one relight at a time) how far its sun's trace has got for the relight `sun_done_for` and its bounce's for the
    // bounce pass `bounce_done_for` (vertices: a frame's share may end inside a mesh)
    std::size_t sun_done = 0;
    std::size_t bounce_done = 0;
    uint64_t sun_done_for = 0;
    uint64_t bounce_done_for = 0;
    uint64_t field_for = 0; // (the light field being made: the lighting its vertices were all added for ...)
    uint64_t field_lighting = 0; // (... the lighting its vertices are being added for, the first field_done of them)
    std::size_t field_done = 0;
    // (round P10b) it joined the light's bounce passes after they were made (State::bounce_joining): its store is to be
    // written again with its own bounce, and its vertices added to the light field made for the lighting now
    bool store_stale = false;
    bool field_join = false;
    // (lighting round A) its light store: the stream (engine/gfx/rt_cel.h - the light's parts and the colour the world
    // draws it with, a buffer of its own beside the game's)
    GpuVertexBuffer* stream = nullptr;
};

// (one relight at a time) The relit mode's work a frame - the sun's rays, the bounce's, the writes, the light field -
// is about kRelitFrameMs of the frame in all while the game is shown (each part what is left of it when its turn comes,
// at least a little): the vertices a frame from the time a vertex took over the last frames. A light that keeps moving (a running clock) then costs each frame the same small time whatever the level's
// size, and a light picked by hand rolls in over a second or two instead of a long frame. While the loading screen holds
// a level's first light (hold_first_light) the most a frame (kRelitRetraceVertices...): it is done sooner.
constexpr double kRelitFrameMs = 2.0;
constexpr double kRelitLeastMs = 0.25; // (a part's share of the frame at least: every part goes on)
constexpr std::size_t kRelitFirstPaced = 4096; // vertices a frame before a time is measured
constexpr std::size_t kRelitLeastPaced = 64;   // ... and at least this many (a relight always goes on)
constexpr std::size_t kRelitMeasuredLeast = 32; // a frame of fewer vertices doesn't tell the time a vertex takes
constexpr double kRelitPaceKeep = 0.75;          // the time a vertex takes: this much of the old, the rest the new

struct Pace {
    double ms_per_vertex = 0.0; // (0: not measured yet)
};

double ms_since(std::chrono::steady_clock::time_point start);

// What is left of this frame's kRelitFrameMs (update's start: State::frame_start), at least kRelitLeastMs.
double frame_left();

std::size_t paced_vertices(const Pace& pace, std::size_t most, bool hurried)
{
    if (hurried) {
        return most;
    }
    if (pace.ms_per_vertex <= 0.0) {
        return std::min(most, kRelitFirstPaced);
    }
    const double fits = frame_left() / pace.ms_per_vertex;
    return std::clamp(fits >= static_cast<double>(most) ? most : static_cast<std::size_t>(fits), kRelitLeastPaced, most);
}

void measured(Pace* pace, double ms, std::size_t vertices)
{
    if (vertices < kRelitMeasuredLeast) {
        return;
    }
    const double now = ms / static_cast<double>(vertices);
    pace->ms_per_vertex = pace->ms_per_vertex > 0.0
                              ? (kRelitPaceKeep * pace->ms_per_vertex) + ((1.0 - kRelitPaceKeep) * now)
                              : now;
}

double ms_since(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// (one relight at a time - its rays never waited for) A trace of some entries' sun or bounce asked of the device
// (rt::RayTracing::probe_later): the order its rays' mesh indices are of, the vertices asked and their rays, the light it was
// asked for (State::relights; the bounce's: its pass, State::bounce_begun), and for the pace the vertices and the time
// the asking took.
struct Job {
    enum class Kind : uint8_t { kNone, kSun, kBounce };
    Kind kind = Kind::kNone;
    uint64_t ticket = 0;
    std::vector<const VertexBufferData*> order;
    rt::RelitAsk ask; // (its spans: the vertices asked)
    uint64_t relight = 0;
    std::size_t vertices = 0;
    double ask_ms = 0.0;
};

// The level's relit data (engine/gfx/relit_data.h), read once a level.
struct LevelData {
    uint32_t level = 0;
    bool read = false;  // looked for
    bool found = false; // read, of this version, for this level
    bool stale = false; // (lighting round A) a relit data file an older relit bake made: out of date
    std::string why;    // why not
    rt::RelitLevelData data;
    std::unordered_map<uint64_t, std::size_t> by_content;
};

struct State {
    std::unordered_map<const VertexBufferData*, Entry> entries;
    std::unordered_map<const MeshRef*, const VertexBufferData*> buffer_of;
    std::vector<const VertexBufferData*> order; // the entries in the order the world gave them
    uint64_t listing = 0;                        // each update's order made again: its number (Entry::listed)
    rt::RelitModel model;
    uint32_t level = 0;
    Status status = Status::kOff;
    Status logged = Status::kOff;
    rt::Vec3 sun{};
    float radius = rt::kRelitSunRadiusDegrees; // the light's angular radius (the lighting state's: the moon's softer)
    bool sky_model = false; // (whether the sun's colour was the sky model's)
    uint64_t day_serial = 0; // (added by the time of day) the clock's light the colour was made for (0: none)
    rt::RelitLighting day_scales; // (added by the time of day) the light now (round A2: in the global unit)
    rt::RelitLighting written_light; // the lighting the colours were last written with (`rt probe`'s)
    uint64_t lighting = 0;
    uint64_t wanted_lighting = 0; // (added by the scene's sun) the lighting the colours are written with now
    LevelData data;
    Status verdict = Status::kWaiting; // (what the level's data says, the meshes matched: new_world / update)
    uint64_t fits = 0;                 // (the baked suns published - rt_scene.h BakedSun::scene)
    uint64_t field_lighting = 0; // (the one lighting path) the lighting the light field was made for (0: none)
    std::size_t retraced = 0;
    std::size_t rebounced = 0;
    uint64_t relights = 0; // the lights taken (update: one relight at a time)
    // (one relight at a time) the bounce's passes: the sun the one under way traces for, the passes begun and the last
    // one done (the lighting's number counts it: the colours written again when one is done)
    rt::Vec3 bounce_sun{};
    uint64_t bounce_begun = 0;
    uint64_t bounce_finished = 0;
    uint32_t bounce_passes = 0;   // (round A2) the passes made for the light now (kRelitBouncePasses) ...
    rt::Vec3 bounce_light_sun{};  // ... toward this sun
    // (round P10b) Entries met after the light's passes were made (light_bounced) - an object placed once it stood
    // still, one back in view that missed them - joining: each one's own bounce traced from the hits of the pass done,
    // its store written, its vertices added to the light field. Before, such an entry made the bounce due and began a
    // pass over the whole level (every vertex traced again, every store written, the field made again) for each object
    // placed: in Boston a pass a few seconds of play, paced to a few hundred vertices a frame - the relit work never
    // done (about 1.5 ms of every frame), and each pass one more bounce for the level.
    bool bounce_joining = false;
    // (one relight at a time) the light field being made for the lighting `field_building_for`, over frames
    std::shared_ptr<rt::LightField> field_building;
    uint64_t field_building_for = 0;
    // (one relight at a time) the time a vertex takes to trace toward the sun, to trace the bounce and to write, as
    // measured (Pace), and whether the loading screen holds the first light now (hold_first_light: the relight hurried)
    Pace sun_pace;
    Pace bounce_pace;
    Pace write_pace;
    bool hurried = false;
    // (round S2) a lighting cut's relight (rt_daylight.h cut_settling): hurried as a first light - the light before the
    // cut is another lighting, at another pre-exposure, not a moment to roll in from
    bool cut_hurried = false;
    uint64_t bounce_cut = 0; // (round S2) the lighting's cut the vertices' bounce B was started again for
    std::chrono::steady_clock::time_point frame_start{}; // (update's start this frame: the parts share kRelitFrameMs)
    Job job; // (the trace asked of the device, answered a frame or two later - one at a time)
    // (lighting round A3 - each lamp by its own daylight) how much each of the data's lamps is on now (rt_lights.h
    // lamp_on, in steps of kLampSteps) and their hash: the entries' L made again from their links when it changes
    std::vector<float> lamps_on;
    uint64_t lamps_hash = 0;
    // (round relit-misses) every world mesh of the last listing (`listing`), the mode's entry or not, and why the mode
    // can't relight those it made none for (refusal): the draws ask whether a mesh has a light of its own
    // (unlit_world_colour); the misses logged per level when they change (log_misses)
    std::unordered_map<const MeshRef*, uint64_t> listed;
    std::unordered_map<const MeshRef*, const char*> refused;
    std::string misses_logged;
    uint32_t misses_level = 0;
    // (round P10 - objects relit) the listed objects' meshes' transforms (where each stands: its centre for the light
    // field) and the objects listed (`listing` they were last listed in): their draws take their light from the store
    std::unordered_map<const MeshRef*, std::array<float, 16>> placed_at;
    std::unordered_map<const void*, uint64_t> objects_listed;
};

State& state()
{
    static core::NoDestroy<State> made;
    return made.get();
}

double frame_left() { return std::max(kRelitLeastMs, kRelitFrameMs - ms_since(state().frame_start)); }

// Whether an entry's sun was traced for another light than the one now (its direction or its softness).
bool sun_stale(const Entry& entry)
{
    const State& st = state();
    return rt::dot(entry.sun_for, st.sun) < kSameSun || entry.radius_for != st.radius;
}

// (round P10b) Whether the light's bounce passes are all made for the sun now (kRelitBouncePasses of them toward it):
// an entry whose bounce is due then is one that wasn't in them (State::bounce_joining).
bool light_bounced()
{
    const State& st = state();
    return st.bounce_passes >= kRelitBouncePasses && rt::dot(st.bounce_light_sun, st.sun) >= kBounceSameSun;
}

// Whether the bounce is due again: an entry's traced for a sun kBounceSameSun or further from the one now.
// (round A2) Or the light's passes not all made yet (kRelitBouncePasses).
bool bounce_due(const Entry& entry)
{
    return rt::dot(entry.bounce_for, state().sun) < kBounceSameSun || state().bounce_passes < kRelitBouncePasses;
}

// Whether an entry's bounce is still to trace in the bounce's pass under way - or (round P10b) it is joining the passes
// made for the light now (its bounce due: it wasn't in them).
bool bounce_stale(const Entry& entry)
{
    const State& st = state();
    if (st.bounce_begun == st.bounce_finished) {
        return st.bounce_joining && light_bounced() && bounce_due(entry);
    }
    return rt::dot(entry.bounce_for, st.bounce_sun) < kSameSun || entry.bounce_pass != st.bounce_begun;
}

// A colour's brightness (0 - 255; A8R8G8B8).
float luma_of(uint32_t argb)
{
    return (kLumaRed * static_cast<float>((argb >> kRedShift) & kByteMask)) +
           (kLumaGreen * static_cast<float>((argb >> kGreenShift) & kByteMask)) +
           (kLumaBlue * static_cast<float>(argb & kByteMask));
}

// Light's luminance (the console's probe).
float light_luma(const rt::Rgbf& light)
{
    return (kLumaRed * std::get<0>(light)) + (kLumaGreen * std::get<1>(light)) + (kLumaBlue * std::get<2>(light));
}

// (round L - the log's, the console's) The light a vertex shows as the frame shows it: x the frame's pre-exposure, as a
// display-referred colour (A8R8G8B8, 128 the game's white; held at 255) - beside the bake's own colours. The store holds
// it physical (light_store.h); only the log compares it with bytes.
uint32_t shown_argb(const rt::RelitModel& model, const rt::RelitVertex& vertex, const rt::RelitLighting& lighting)
{
    const rt::Rgbf light = rt::relit_shown(model, vertex, lighting);
    const float exposure = rt::frame_pre_exposure();
    uint32_t out = 0xFF000000U;
    for (std::size_t ch = 0; ch < light.size(); ++ch) {
        const float display = rt::display_from_linear(core::at(light, ch) * exposure) * 128.0F;
        const auto byte = static_cast<uint32_t>(std::lround(std::clamp(display, 0.0F, 255.0F)));
        out |= byte << (ch == 0 ? kRedShift : ch == 1 ? kGreenShift : 0);
    }
    return out;
}

uint64_t fnv(uint64_t hash, std::span<const uint8_t> bytes)
{
    for (const uint8_t byte : bytes) {
        hash = (hash ^ byte) * kFnvPrime;
    }
    return hash;
}

template <typename T>
uint64_t fnv_value(uint64_t hash, const T& value)
{
    std::array<uint8_t, sizeof(T)> bytes{};
    std::memcpy(bytes.data(), &value, sizeof(T));
    return fnv(hash, bytes);
}

// The level's main scene (the first world scene that isn't a sky): its checksum (0: none).
uint32_t main_scene()
{
    const memory::ListNode* node = engine_parts().scene_list().next();
    for (int guard = 0; node != nullptr && !node->is_head() && guard < kMaxSceneNodes; ++guard) {
        const auto* scene = static_cast<const WorldScene*>(node->data());
        if (scene != nullptr && !scene->is_sky()) {
            return scene->checksum();
        }
        node = node->next();
    }
    return 0;
}

// The level's entry (rt_relit.h: the built-in list, then the user's file beside the exe), or null.
const rt::RelitLevel* level_entry(uint32_t level) { return rt::relit_level_entry(level); }

// --- the level's relit data -----------------------------------------------------------------------------------------

std::string data_path(uint32_t level)
{
    return text::format("%s%s\\%s", build_info::executable_directory(), rt::kRelitDataFolder,
                        rt::relit_data_name(level).c_str());
}

// The model's numbers in a hash (a lighting's number moves with them).
uint64_t model_hash(uint64_t hash, const rt::RelitModel& model)
{
    hash = fnv_value(hash, model.valid);
    hash = fnv_value(hash, model.toward);
    hash = fnv_value(hash, model.sun);
    hash = fnv_value(hash, model.sky);
    hash = fnv_value(hash, model.bounce);
    hash = fnv_value(hash, model.lamps);
    hash = fnv_value(hash, model.sunless);
    return fnv_value(hash, model.albedo_scale);
}

// The level's data, read the first time it is asked for (a level's other loads read it again: it may have been built
// since).
const LevelData& level_data(uint32_t level)
{
    State& st = state();
    LevelData& data = st.data;
    if (data.read && data.level == level) {
        return data;
    }
    data = LevelData{};
    data.level = level;
    data.read = true;
    std::string bytes;
    const std::string path = data_path(level);
    if (level == 0) {
        data.why = "no level";
    } else if (!fs::read_text(path, &bytes)) {
        data.why = "none built";
    } else if (rt::relit_data_stale(bytes)) {
        data.stale = true;
        data.why = "made by an older relit bake - out of date";
    } else if (!rt::read_relit_data(bytes, &data.data, &data.why)) {
        data.why = "unreadable: " + data.why;
    } else if (data.data.scene != level) {
        data.why = "made for another scene";
    } else {
        data.found = true;
        for (std::size_t mi = 0; mi < data.data.meshes.size(); ++mi) {
            data.by_content.emplace(core::at(data.data.meshes, mi).content, mi);
        }
    }
    if (data.found) {
        const rt::RelitModel& model = data.data.model;
        get_log("rt_relit: level %08x - its relit data %s: %s, %zu meshes, the model sun %.3f %.3f %.3f sky %.3f %.3f %.3f "
                "bounce %.3f %.3f %.3f lamps %.3f %.3f %.3f%s (%u lamps, %u sky rays, %u sun rays; margins: the "
                "baked sun %.2f, the model %.2f)",
                level, path.c_str(), rt::relit_verdict_name(data.data.verdict), data.data.meshes.size(),
                static_cast<double>(std::get<0>(model.sun)), static_cast<double>(std::get<1>(model.sun)),
                static_cast<double>(std::get<2>(model.sun)), static_cast<double>(std::get<0>(model.sky)),
                static_cast<double>(std::get<1>(model.sky)), static_cast<double>(std::get<2>(model.sky)),
                static_cast<double>(std::get<0>(model.bounce)), static_cast<double>(std::get<1>(model.bounce)),
                static_cast<double>(std::get<2>(model.bounce)), static_cast<double>(std::get<0>(model.lamps)),
                static_cast<double>(std::get<1>(model.lamps)), static_cast<double>(std::get<2>(model.lamps)),
                model.sunless ? " - no sun in the bake" : "", data.data.lamps, data.data.sky_rays, data.data.sun_rays,
                static_cast<double>(data.data.sun_margin), static_cast<double>(data.data.model_margin));
    } else if (level != 0) {
        get_log("rt_relit: level %08x - no relit data (%s: %s) - the level keeps the original's lighting; build it with "
                "node tools/game/relit_bake.ts",
                level, path.c_str(), data.why.c_str());
    }
    return data;
}

// --- the meshes ------------------------------------------------------------------------------------------------------

// The buffer a world mesh's geometry was drawn from (its buffers take turns), or null.
VertexBuffer* drawn_buffer(const WorldMesh& world)
{
    const MeshRef* mesh = world.mesh;
    VertexBuffer* buffer = mesh->vertex_store(mesh->current_vertex_buffer_);
    if (mesh->vertex_buffer_count() > 1) {
        for (uint32_t iv = 0; iv < mesh->vertex_buffer_count(); ++iv) {
            if (mesh->vertex_store(iv) == world.source) {
                buffer = mesh->vertex_store(iv);
            }
        }
    }
    return buffer;
}

// Why the mode can't relight a world mesh (the light energy audit's census names these), or null: it can.
const char* refusal(const WorldMesh& world)
{
    const rt::Geometry& geometry = *world.geometry;
    const VertexBuffer* buffer = drawn_buffer(world);
    if (geometry.dynamic) {
        return "dynamic (rewritten by the processor)";
    }
    if (buffer == nullptr || buffer != world.source || buffer->data() == nullptr) {
        return "no copy of its vertices";
    }
    if (geometry.cpu_indices == nullptr) {
        return "no copy of its indices";
    }
    if (geometry.vertex_stride == 0 || geometry.colour_format != rt::kColourA8R8G8B8 ||
        geometry.colour_offset + sizeof(uint32_t) > geometry.vertex_stride) {
        return "no vertex colours";
    }
    return nullptr;
}

// An entry for a world mesh read from its buffer's copy, or false: nothing the mode can relight (refusal says why).
// (round P10 - objects relit) A mesh read in its object's space put where the object stands: its positions through the
// transform (Direct3D's rows: p' = (x, y, z, 1) x M), its normals through the inverse transpose (renormalised); a
// mirroring transform (negative determinant) winds its triangles the other way, so they are turned back round.
void place_object(const std::array<float, 16>& m, rt::RelitMesh* mesh)
{
    const auto at = [&m](std::size_t row, std::size_t column) { return core::at(m, (row * 4) + column); };
    for (rt::Vec3& p : mesh->positions) {
        const rt::Vec3 was = p;
        for (std::size_t c = 0; c < 3; ++c) {
            core::at(p, c) = (std::get<0>(was) * at(0, c)) + (std::get<1>(was) * at(1, c)) + (std::get<2>(was) * at(2, c)) +
                             at(3, c);
        }
    }
    // the inverse transpose of the 3 x 3 part, as cofactors (its scale doesn't matter: normalised after)
    std::array<std::array<float, 3>, 3> cofactor{};
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            const std::size_t r1 = (r + 1) % 3;
            const std::size_t r2 = (r + 2) % 3;
            const std::size_t c1 = (c + 1) % 3;
            const std::size_t c2 = (c + 2) % 3;
            core::at(core::at(cofactor, r), c) = (at(r1, c1) * at(r2, c2)) - (at(r1, c2) * at(r2, c1));
        }
    }
    const float determinant = (at(0, 0) * cofactor[0][0]) + (at(0, 1) * cofactor[0][1]) + (at(0, 2) * cofactor[0][2]);
    for (rt::Vec3& n : mesh->normals) {
        const rt::Vec3 was = n;
        rt::Vec3 turned{};
        for (std::size_t c = 0; c < 3; ++c) {
            core::at(turned, c) = (std::get<0>(was) * core::at(core::at(cofactor, 0), c)) +
                                  (std::get<1>(was) * core::at(core::at(cofactor, 1), c)) +
                                  (std::get<2>(was) * core::at(core::at(cofactor, 2), c));
        }
        const float length = std::sqrt(rt::dot(turned, turned));
        n = length > 0.0F ? rt::scaled(turned, (determinant < 0.0F ? -1.0F : 1.0F) / length) : was;
    }
    if (determinant < 0.0F) {
        for (std::array<uint32_t, 3>& triangle : mesh->triangles) {
            std::swap(std::get<0>(triangle), std::get<1>(triangle));
        }
        for (std::array<uint32_t, 3>& triangle : mesh->baked_triangles) {
            std::swap(std::get<0>(triangle), std::get<1>(triangle));
        }
    }
}

bool make_entry(const WorldMesh& world, Entry* out)
{
    const rt::Geometry& geometry = *world.geometry;
    const MeshRef* mesh = world.mesh;
    VertexBuffer* buffer = drawn_buffer(world);
    if (refusal(world) != nullptr) {
        return false;
    }
    const uint32_t stride = geometry.vertex_stride;
    const uint32_t count = std::min(geometry.vertex_count, buffer->size() / stride);
    const std::span<const uint8_t> bytes(buffer->data(), std::size_t{count} * stride);
    Entry entry;
    entry.mesh = mesh;
    entry.buffer = buffer;
    entry.stride = stride;
    entry.colour_offset = geometry.colour_offset;
    entry.input.id = world.id;
    entry.input.positions.resize(count);
    entry.originals.resize(count);
    entry.animated.assign(count, 0);
    // (the normals and the colours where the geometry has them - the vertex format's places, rt_vertex_attributes.h -,
    // not the mesh's own offsets)
    const bool normals = geometry.normal_format == rt::kNormalFloat3 &&
                         geometry.normal_offset + sizeof(rt::Vec3) <= stride;
    if (normals) {
        entry.input.normals.resize(count);
    }
    const uint8_t* animation = mesh->per_vertex_bytes();
    const std::span<const uint8_t> per_vertex =
        animation != nullptr ? std::span<const uint8_t>(animation, count) : std::span<const uint8_t>();
    for (uint32_t vi = 0; vi < count; ++vi) {
        const std::span<const uint8_t> vertex = bytes.subspan(std::size_t{vi} * stride, stride);
        std::memcpy(&core::at(entry.input.positions, vi),
                    vertex.subspan(geometry.position_offset, sizeof(rt::Vec3)).data(), sizeof(rt::Vec3));
        std::memcpy(&core::at(entry.originals, vi), vertex.subspan(geometry.colour_offset, sizeof(uint32_t)).data(),
                    sizeof(uint32_t));
        if (normals) {
            std::memcpy(&core::at(entry.input.normals, vi),
                        vertex.subspan(geometry.normal_offset, sizeof(rt::Vec3)).data(), sizeof(rt::Vec3));
        }
        if (!per_vertex.empty() && core::at(per_vertex, vi) != 0) {
            core::at(entry.animated, vi) = 1;
        }
    }
    entry.input.colours = entry.originals;
    entry.glow_card = world.glow;
    entry.lit_per_pixel = world.lit_per_pixel;
    entry.baked_shadow = world.blended && !entry.originals.empty() &&
                         std::ranges::all_of(entry.originals, [](uint32_t colour) { return luma_of(colour) < 1.0F; });
    // (added by building relief) a relieved mesh's made vertices take the flat wall's light (rt_relit.h
    // RelitMesh::sources): their sources, the last of its vertices
    const std::span<const relief::VertexSource> made = mesh_smoothing::sources_of(core::at(mesh->index_buffers(), 0));
    if (!made.empty() && made.size() <= count && mesh->vertex_count() == count) {
        entry.input.sources.resize(count);
        const std::size_t first = count - made.size();
        for (std::size_t k = 0; k < made.size(); ++k) {
            const relief::VertexSource& from = core::at(made, k);
            rt::RelitMesh::Source& to = core::at(entry.input.sources, first + k);
            to.count = from.count;
            for (std::size_t corner = 0; corner < from.from.size(); ++corner) {
                core::at(to.from, corner) = core::at(from.from, corner);
                core::at(to.weight, corner) = core::at(from.weight, corner);
            }
        }
    }
    // the triangles as the RayTracing numbers them (a strip's odd ones turned back round)
    const std::size_t index_bytes = geometry.indices_32bit ? sizeof(uint32_t) : sizeof(uint16_t);
    const std::span<const uint8_t> indices(static_cast<const uint8_t*>(geometry.cpu_indices),
                                           (std::size_t{geometry.index_offset} + geometry.index_count) * index_bytes);
    const auto index = [&](uint32_t at) {
        const std::size_t byte = (std::size_t{geometry.index_offset} + at) * index_bytes;
        if (index_bytes == sizeof(uint32_t)) {
            uint32_t value = 0;
            std::memcpy(&value, indices.subspan(byte, sizeof(value)).data(), sizeof(value));
            return value;
        }
        uint16_t value = 0;
        std::memcpy(&value, indices.subspan(byte, sizeof(value)).data(), sizeof(value));
        return static_cast<uint32_t>(value);
    };
    uint32_t triangles = geometry.index_count / 3;
    if (geometry.strip) {
        triangles = geometry.index_count >= 3 ? geometry.index_count - 2 : 0U;
    }
    entry.input.triangles.reserve(triangles);
    for (uint32_t tt = 0; tt < triangles; ++tt) {
        const uint32_t first = geometry.strip ? tt : tt * 3;
        std::array<uint32_t, 3> corners{index(first), index(first + 1), index(first + 2)};
        if (geometry.strip && (tt % 2) == 1) {
            std::swap(std::get<0>(corners), std::get<1>(corners));
        }
        entry.input.triangles.push_back(corners);
    }
    // (the bake's evidence - rt_relit.h RelitMesh::baked_triangles) a mesh the game's geometry passes changed at load
    // (smoothing, building relief, the water's grid): the file's strip over its original vertices, still first - the
    // triangles the bake's light is on, as the baked sun's samples read them (rt_record.cpp mesh_triangles)
    const std::span<const uint16_t> original = mesh_smoothing::original_strip(core::at(mesh->index_buffers(), 0));
    if (original.size() >= 3) {
        uint32_t highest = 0;
        bool fits = true;
        for (const uint16_t corner : original) {
            highest = std::max<uint32_t>(highest, corner);
            fits = fits && corner < count;
        }
        if (fits) {
            entry.input.baked_triangles.reserve(original.size() - 2);
            for (std::size_t tt = 0; tt + 2 < original.size(); ++tt) {
                std::array<uint32_t, 3> corners{core::at(original, tt), core::at(original, tt + 1),
                                                core::at(original, tt + 2)};
                if ((tt % 2) == 1) {
                    std::swap(std::get<0>(corners), std::get<1>(corners));
                }
                entry.input.baked_triangles.push_back(corners);
            }
            entry.input.baked_vertices = highest + 1;
        }
    }
    // (round P10 - objects relit) an object's mesh read where it stands: its positions and normals through its
    // instance's transform (rays and the data are the world's); a mirroring transform turns its triangles back round
    if (world.object != nullptr) {
        entry.model_content = rt::relit_mesh_content(entry.input);
        place_object(world.to_world, &entry.input);
        entry.object = world.object;
        entry.to_world = world.to_world;
    }
    // (the offline relit data's key: its positions, triangles and made vertices' sources - whatever the vertex format;
    // not its colours, which the game rewrites)
    entry.content = rt::relit_mesh_content(entry.input);
    *out = std::move(entry);
    return true;
}

// (added by the light energy audit) the entry's light stream given to the ray tracing - or taken back (null): the passes
// that light a pixel again read what its colours were written with (rt_scene.h set_light_stream)
void publish_stream(Entry& entry, GpuVertexBuffer* stream)
{
    if (rt::RayTracing* tracer = rt::ray_tracing(); tracer != nullptr && entry.input.id != 0) {
        tracer->set_light_stream(entry.input.id, stream);
    }
}

// (lighting round A) an entry's light store let go of: its draws show the game's colours again
void release_stream(Entry& entry)
{
    publish_stream(entry, nullptr);
    if (entry.stream != nullptr) {
        gpu().release_vertex_buffer(entry.stream);
        entry.stream = nullptr;
    }
    entry.written = 0;
}
void restore_all()
{
    bool any = false;
    for (auto& [buffer, entry] : state().entries) {
        any = any || entry.stream != nullptr;
        release_stream(entry);
    }
    if (any || rt::cel_world().relit) {
        rt::set_cel_world({});
    }
    State& st = state();
    st.wanted_lighting = 0; // (no relight under way: the next light is taken at once)
    if (st.job.ticket != 0 && rt::ray_tracing() != nullptr) {
        rt::ray_tracing()->probe_forget(st.job.ticket); // (its answers not wanted)
    }
    st.job = Job{};
}

// The sun the world is lit by now, and whether there is one (changed by the scene's sun: rt_scene.h relit_target_sun -
// the console's `rt sun`, else [Graphics] rt_sun_source's, the sun every effect lights by; the level's baked sun is the
// model's own, so the bake comes back exactly).
bool target_sun(rt::Vec3* toward)
{
    const rt::SceneSun sun = rt::relit_target_sun();
    *toward = sun.found() && sun.origin != rt::SunOrigin::kLevel ? sun.toward : state().model.toward;
    const float length = std::sqrt(rt::dot(*toward, *toward));
    if (!(length > 0.0F) || !std::isfinite(length)) {
        return false;
    }
    *toward = rt::scaled(*toward, 1.0F / length); // (lighting round A: where it is - under the horizon its light is gone)
    return true;
}

// (lighting round A2 - one light unit) The light the world is lit by now, in the global unit: the clock's (rt_daylight.h
// DayLight::sun_light / sky_light - the physical sun and sky, or the moon and the night's), else the level's own (the
// model's own-time light - relit_own_lighting). The bounce is traced from the relit colours themselves.
// (Round L - a physical light store) Physical: the published state's pre-exposure divided out - the store, the bounce and
// the light field hold the light as it is, and what draws it multiplies by the frame's pre-exposure (light_store.h).
rt::RelitLighting day_scales(const rt::DayLight& day, const rt::RelitModel& model)
{
    if (!day.on) {
        return rt::relit_own_lighting(model, 0.0F);
    }
    const float exposure = day.pre_exposure > 0.0F ? day.pre_exposure : 1.0F;
    return {.toward = rt::Vec3{std::get<0>(day.light), std::get<1>(day.light), std::get<2>(day.light)},
            .sun = rt::scaled(day.sun_light, 1.0F / exposure),
            .sky = rt::scaled(day.sky_light, 1.0F / exposure),
            .lamps = 0.0F};
}

// (lighting round A) How much of the lamps' light relit lights per vertex: what the lamps pass doesn't light per pixel
// itself (lamps_drawn) - one owner. (Round A3) Which lamps are on is each lamp's own (switch_lamps: its L made with it).
// (round L: physical - the store's light isn't pre-exposed)
float vertex_lamps()
{
    return std::max(0.0F, 1.0F - rt::lamps_drawn());
}

// (lighting round A3 - no "dark level") Each of the data's lamps on by its own daylight (rt_lights.h lamp_on: the open
// sky now - the clock's or the level's own time's - against its switch), and every analysed entry's L made again from
// its links with them where they changed; a lamp's own surface (its emitting vertices) shows its radiance (its light /
// pi) as much as it is on - the bulb, the lit panel. Returns the switches' hash (the lighting's).
uint64_t switch_lamps(const rt::DayLight& day)
{
    State& st = state();
    const LevelData& data = level_data(st.level);
    if (!data.found) {
        return 0;
    }
    constexpr float kLampSteps = 32.0F;
    const std::shared_ptr<const rt::LevelEmitters> level = rt::level_emitters();
    const float sky_now = rt::lamp_sky_now(day, level.get());
    const float open = rt::open_sky_lamps(day, level.get());
    const std::vector<rt::RelitLamp>& lamps = data.data.lamp_list;
    std::vector<float> on(lamps.size(), 0.0F);
    uint64_t hash = fnv_value(kFnvBasis, static_cast<uint64_t>(lamps.size()));
    for (std::size_t li = 0; li < lamps.size(); ++li) {
        core::at(on, li) = std::round(rt::lamp_on(core::at(lamps, li).switch_sky, sky_now, open) * kLampSteps) / kLampSteps;
        hash = fnv_value(hash, core::at(on, li));
    }
    hash = hash == 0 ? 1 : hash;
    st.lamps_on = std::move(on);
    st.lamps_hash = hash;
    constexpr float kLambertian = 1.0F / 3.14159265F; // (a Lambertian emitter's radiance: its exitance over pi)
    // (round L) a lamp's surface shows its luminance (relit_data.h RelitLamp::luminance: its kind's or its solved flux
    // over pi x its area) in its colour - physical, in the unit (a white diffuser's radiance under unit_illuminance:
    // unit_illuminance / pi cd/m^2) - not its light / pi (the light right at a point source, no surface's luminance)
    const float unit_luminance = static_cast<float>(rt::unit_illuminance()) * kLambertian;
    std::vector<rt::Rgbf> shown(lamps.size(), rt::Rgbf{});
    for (std::size_t li = 0; li < lamps.size(); ++li) {
        const rt::RelitLamp& lamp = core::at(lamps, li);
        const float y = (0.2126F * std::get<0>(lamp.light)) + (0.7152F * std::get<1>(lamp.light)) +
                        (0.0722F * std::get<2>(lamp.light));
        core::at(shown, li) = lamp.luminance > 0.0F && y > 0.0F && unit_luminance > 0.0F
                                  ? rt::scaled(lamp.light, lamp.luminance / unit_luminance / y)
                                  : rt::scaled(lamp.light, kLambertian);
    }
    // (round L) the brightest emission the level shows now - the frame's pre-exposure keeps it in range
    // (rt_daylight.h set_brightest_emission)
    float brightest = 0.0F;
    for (std::size_t li = 0; li < lamps.size(); ++li) {
        const rt::Rgbf& light = core::at(shown, li);
        // (round indoor lamps: every lamp's head may be drawn - a candidate's own surface, or the fixture geometry a
        // light card's, a point light's or a pool's lamp took - relit_bake.cpp emitting_vertices)
        const float drawn = 1.0F;
        brightest = std::max(brightest, std::max({std::get<0>(light), std::get<1>(light), std::get<2>(light)}) * drawn *
                                            core::at(st.lamps_on, li));
    }
    rt::set_brightest_emission(brightest);
    for (const VertexBufferData* buffer : st.order) {
        Entry& entry = st.entries.at(buffer);
        if (!entry.analysed || entry.lamps_for == hash || entry.data_mesh >= data.data.meshes.size()) {
            continue;
        }
        entry.lamps_for = hash;
        const rt::RelitDataMesh& mesh = core::at(data.data.meshes, entry.data_mesh);
        rt::relit_mesh_lamp_light(entry.input, &entry.light, mesh.links, lamps, st.lamps_on);
        for (const rt::RelitEmitting& emitting : mesh.emitting) {
            if (emitting.vertex < entry.light.size() && emitting.lamp < lamps.size()) {
                rt::RelitVertex& vertex = core::at(entry.light, emitting.vertex);
                vertex.emit = rt::scaled(core::at(shown, emitting.lamp), core::at(st.lamps_on, emitting.lamp));
                vertex.emit_masked = core::at(lamps, emitting.lamp).kind == rt::EmitterKind::kTextured;
            }
        }
    }
    return hash;
}
// The new entries' light taken from the level's data (nothing traced or fitted: the relit bake made it - relit_data.h):
// each by its content; its normals and which vertices are used made again from the mesh as the bake made them.
void take_from_data()
{
    State& st = state();
    const LevelData& data = level_data(st.level);
    for (const VertexBufferData* buffer : st.order) {
        Entry& entry = st.entries.at(buffer);
        if (entry.analysed || entry.looked_up) {
            continue;
        }
        entry.looked_up = true;
        const auto found = data.found ? data.by_content.find(entry.content) : data.by_content.end();
        if (found == data.by_content.end()) {
            continue;
        }
        const rt::RelitDataMesh& mesh = core::at(data.data.meshes, found->second);
        if (mesh.vertices.size() != entry.input.positions.size()) {
            continue;
        }
        rt::relit_normals(entry.input, data.data.facing, &entry.light);
        entry.data_mesh = found->second;
        entry.lamps_for = 0;
        // (the night's light, decomposed) the data's bounce only while the level is lit by its own light - under the
        // clock's a mesh met after the lighting's cut starts with none, as every mesh did at the cut (rt_relit.h
        // relit_vertex_from_data). Before, Boston's 188 night-state meshes met after the cut kept the afternoon's bounce
        // and the bounce passes spread it: a night's walls at 8 x 10^-5 where their lamps and sky give 3 x 10^-5, the
        // frame 3.6 - 29 x the path-traced reference from one load to the next.
        const bool clock_lit = rt::day_light().on;
        for (std::size_t vi = 0; vi < entry.light.size(); ++vi) {
            rt::relit_vertex_from_data(core::at(mesh.vertices, vi), clock_lit, &core::at(entry.light, vi));
        }
        entry.analysed = true;
        entry.sun_for = data.data.model.toward;
        entry.radius_for = rt::kRelitSunRadiusDegrees;
        entry.bounce_for = data.data.model.toward;
    }
}

// What the level is lit by, from its data and its meshes now: no data, a verdict of no sun or no model, data made from
// other meshes (its meshes hold under kRelitDataLeastShare of the world's vertices), or relit.
Status level_verdict()
{
    State& st = state();
    const LevelData& data = level_data(st.level);
    if (!data.found && !data.stale) {
        return Status::kNoData;
    }
    if (data.stale) {
        return Status::kStale; // (an older relit bake's file)
    }
    if (data.data.verdict == rt::RelitVerdict::kNoModel || !data.data.model.valid) {
        return Status::kNoModel;
    }
    std::size_t all = 0;
    std::size_t matched = 0;
    for (const VertexBufferData* buffer : st.order) {
        const Entry& entry = st.entries.at(buffer);
        all += entry.originals.size();
        matched += entry.analysed ? entry.originals.size() : 0;
    }
    const bool stale = all > 0 && static_cast<float>(matched) < rt::kRelitDataLeastShare * static_cast<float>(all);
    if (stale) {
        // (why: the meshes not found - their sizes, the first few; logged once a level)
        static uint32_t logged_level = 0;
        if (logged_level != st.level) {
            logged_level = st.level;
            std::string examples;
            std::size_t missing = 0;
            for (const VertexBufferData* buffer : st.order) {
                const Entry& entry = st.entries.at(buffer);
                if (entry.analysed) {
                    continue;
                }
                ++missing;
                if (missing <= 8) {
                    examples += text::format(" %zu-vertex (content %016llx)", entry.input.positions.size(),
                                             static_cast<unsigned long long>(entry.content));
                }
            }
            get_log("rt_relit: level %08x - %zu of %zu world vertices in its relit data (%zu meshes not found:%s)", st.level,
                    matched, all, missing, examples.c_str());
        }
    }
    return stale ? Status::kStale : Status::kRelit;
}

// The level's facts for the settings model (relit and the time of day out of effect where the level keeps the
// original's lighting, and why) - the renderer's settings read again when they change.
void publish_level_light(Status verdict)
{
    config::LevelLight light = config::LevelLight::kBuilt;
    switch (verdict) {
    case Status::kNoData:
        light = config::LevelLight::kMissing;
        break;
    case Status::kStale:
        light = config::LevelLight::kStale;
        break;
    case Status::kNoModel:
        light = config::LevelLight::kNoModel;
        break;
    default:
        break;
    }
    if (config::set_level_light(light)) {
        graphics_options::reload_live("rt_relit"); // (what moved with it - relit, the time of day, the lighting passes)
    }
}

// The world's meshes listed (entries made for those not known yet) in the order the world gives them.
void list_world(std::span<const WorldMesh> world)
{
    State& st = state();
    st.order.clear();
    ++st.listing;
    for (const WorldMesh& mesh : world) {
        if (mesh.mesh == nullptr || mesh.geometry == nullptr || mesh.source == nullptr) {
            continue;
        }
        st.listed.insert_or_assign(mesh.mesh, st.listing);
        if (mesh.object != nullptr) {
            st.placed_at.insert_or_assign(mesh.mesh, mesh.to_world);
            st.objects_listed.insert_or_assign(mesh.object, st.listing);
            // (round P10) an object that stands elsewhere now: the entry made where it stood let go of (its light, its
            // place in the data were there) - made again where it stands
            if (const auto kept = st.entries.find(mesh.source);
                kept != st.entries.end() && (kept->second.object != mesh.object || kept->second.to_world != mesh.to_world)) {
                release_stream(kept->second);
                std::erase(st.order, mesh.source);
                st.entries.erase(kept);
            }
        }
        if (!st.entries.contains(mesh.source)) {
            Entry entry;
            if (!make_entry(mesh, &entry)) {
                st.refused.insert_or_assign(mesh.mesh, refusal(mesh));
                continue;
            }
            st.refused.erase(mesh.mesh);
            st.buffer_of.insert_or_assign(mesh.mesh, mesh.source);
            st.entries.emplace(mesh.source, std::move(entry));
        }
        Entry& entry = st.entries.at(mesh.source);
        entry.input.id = mesh.id; // (ids are the RayTracing's: the hits are looked up by them)
        // (each entry once, where the world first gave it - marked, not searched for: a level has thousands)
        if (entry.listed != st.listing) {
            entry.listed = st.listing;
            st.order.push_back(mesh.source);
        }
    }
}

// A new level: what the level before left forgotten.
void level_changed(uint32_t level)
{
    State& st = state();
    if (level == st.level) {
        return;
    }
    rt::set_sun_relight({}); // (added by the scene's sun: a new level is lit by its bake until it is relit)
    rt::set_brightest_emission(0.0F); // (round L: the last level's lamps no longer shown)
    st.field_lighting = 0;
    rt::set_light_field(nullptr); // (the one lighting path: the level before's field gone)
    restore_all();
    st.level = level;
    st.model = {};
    st.verdict = Status::kWaiting;
    st.lighting = 0;
    st.logged = Status::kOff;
    st.bounce_finished = st.bounce_begun; // (a bounce pass of the level before not taken on)
    st.bounce_passes = 0;
    st.bounce_joining = false;
    st.listed.clear();
    st.refused.clear();
    for (auto& [buffer, entry] : st.entries) {
        entry.analysed = false;
        entry.looked_up = false;
    }
    const LevelData& data = level_data(level);
    if (data.found) {
        st.model = data.data.model;
    }
}

// (one relight at a time - its rays never waited for) The entries' meshes and light taken out for a trace, in an order's
// order, and put back after; for the bounce's rays with their hit colours and own light swapped in (the bake's colours
// kept in hit_light meanwhile - round L: the light they reflect, physical).
struct Gathered {
    std::vector<rt::RelitMesh> meshes;
    std::vector<rt::RelitVertices> light;
};

void gather(std::span<const VertexBufferData* const> order, bool bounce, Gathered* out)
{
    State& st = state();
    for (const VertexBufferData* buffer : order) {
        Entry& entry = st.entries.at(buffer);
        out->meshes.push_back(std::move(entry.input));
        out->light.push_back(std::move(entry.light));
        if (bounce) {
            std::swap(out->meshes.back().light, entry.hit_light);
        }
    }
}

void put_back(std::span<const VertexBufferData* const> order, bool bounce, Gathered* in)
{
    State& st = state();
    for (std::size_t iv = 0; iv < order.size(); ++iv) {
        Entry& entry = st.entries.at(core::at(order, iv));
        entry.input = std::move(core::at(in->meshes, iv));
        entry.light = std::move(core::at(in->light, iv));
        if (bounce) {
            std::swap(entry.input.light, entry.hit_light); // (the hits' kept for the next frame)
        }
    }
}

// The trace's rays toward the sun (its sun's share) or over the sky (the sky's share and the bounce) for the wanted:
// traced now (`ask` null), asked, or answered (rt_relit.h RelitAsk).
void trace_part(Job::Kind kind, Gathered& parts, std::span<const uint8_t> wanted, rt::RayTracing& tracer,
                const rt::RelitSettings& settings, rt::RelitStats* stats, rt::RelitAsk* ask)
{
    const State& st = state();
    if (kind == Job::Kind::kBounce) {
        // (the bounce alone: the sky's share as the level's data has it)
        rt::trace_relit_sky(parts.meshes, wanted,
                            rt::RelitParams{.sky_rays = settings.sky_rays,
                                            .sun_rays = settings.sun_rays,
                                            .sun_radius_degrees = rt::kRelitSunRadiusDegrees,
                                            .detail = 1.0F,
                                            .keep_sky = true},
                            tracer, &parts.light, stats, ask);
    } else {
        rt::trace_relit_sun(parts.meshes, wanted, st.sun,
                            {.sky_rays = settings.sky_rays,
                             .sun_rays = settings.sun_rays,
                             .sun_radius_degrees = st.radius,
                             .detail = 1.0F,
                             .keep_sky = false},
                            tracer, &parts.light, stats, ask);
    }
}

// Whether an entry's sun (or bounce) is traced for another light than the one now.
bool stale_for(Job::Kind kind, const Entry& entry)
{
    return entry.analysed && (kind == Job::Kind::kBounce ? bounce_stale(entry) : sun_stale(entry));
}

// An entry's sun traced for the light now, or its bounce for the bounce's pass: marked so.
void mark_traced(Job::Kind kind, Entry& entry)
{
    const State& st = state();
    if (kind == Job::Kind::kBounce) {
        entry.bounce_done = entry.input.positions.size();
        entry.bounce_for = st.bounce_sun; // (written again when the pass is done: the lighting's number moves)
        entry.bounce_pass = st.bounce_begun;
        // (round P10b) one joining the passes made: no pass ends to move the lighting's number - its store written
        // again and its vertices added to the light field on their own
        if (st.bounce_begun == st.bounce_finished) {
            entry.store_stale = true;
            entry.field_join = true;
        }
    } else {
        entry.sun_done = entry.input.positions.size();
        entry.sun_for = st.sun;
        entry.radius_for = st.radius;
    }
}

// The asked spans traced: each entry's trace taken on to the span's end, marked traced at its last vertex.
void mark_spans(Job::Kind kind, std::span<const VertexBufferData* const> order, std::span<const rt::RelitSpan> spans)
{
    State& st = state();
    for (const rt::RelitSpan& span : spans) {
        Entry& entry = st.entries.at(core::at(order, span.mesh));
        std::size_t& done = kind == Job::Kind::kBounce ? entry.bounce_done : entry.sun_done;
        done = span.end;
        if (done >= entry.input.positions.size()) {
            mark_traced(kind, entry);
        }
    }
}

// This frame's share of the vertices still to trace for the light now (its sun's or its bounce's), at most `budget`:
// spans of the entries in the order, each from where its trace got to - the last one may end inside a mesh -, and the
// meshes they are in (`wanted`: traced whole by a trace at once).
std::size_t choose_share(Job::Kind kind, std::size_t budget, std::vector<uint8_t>* wanted,
                         std::vector<rt::RelitSpan>* spans)
{
    State& st = state();
    std::size_t chosen = 0;
    for (std::size_t iv = 0; iv < st.order.size(); ++iv) {
        Entry& entry = st.entries.at(core::at(st.order, iv));
        const bool take = stale_for(kind, entry) && chosen < budget;
        if (take) {
            const uint64_t pass = kind == Job::Kind::kBounce ? st.bounce_begun : st.relights;
            uint64_t& done_for = kind == Job::Kind::kBounce ? entry.bounce_done_for : entry.sun_done_for;
            std::size_t& done = kind == Job::Kind::kBounce ? entry.bounce_done : entry.sun_done;
            if (done_for != pass) {
                done_for = pass;
                done = 0;
            }
            const std::size_t end = std::min(entry.input.positions.size(), done + (budget - chosen));
            spans->push_back({.mesh = iv, .first = done, .end = end});
            chosen += end > done ? end - done : 0;
        }
        wanted->push_back(take ? 1 : 0);
    }
    return chosen;
}

// A trace of this frame's share for the light now: while the level is shown, its rays asked of the device in this
// frame's work and their answers taken a frame or two later (finish_job) - the frame never waits for the device, which
// cost a whole frame's work each time (the frame's commands finished first); while the loading screen holds the first
// light (hurried), or where the tracer can't ask (probe_later 0), the share's meshes traced whole at once.
void start_trace(Job::Kind kind, std::size_t budget, rt::RayTracing& tracer, const rt::RelitSettings& settings)
{
    State& st = state();
    const bool bounce = kind == Job::Kind::kBounce;
    const auto start = std::chrono::steady_clock::now();
    std::vector<uint8_t> wanted;
    rt::RelitAsk ask;
    const std::size_t vertices = choose_share(kind, budget, &wanted, &ask.spans);
    Gathered parts;
    gather(st.order, bounce, &parts);
    rt::RelitStats stats;
    if (!(st.hurried || st.cut_hurried)) {
        trace_part(kind, parts, wanted, tracer, settings, &stats, &ask);
        if (ask.requests.empty()) { // (no ray to ask - the share all turned from the sun: answered as it is)
            ask.answered = true;
            trace_part(kind, parts, {}, tracer, settings, &stats, &ask);
            put_back(st.order, bounce, &parts);
            mark_spans(kind, st.order, ask.spans);
            return;
        }
        const uint64_t ticket = tracer.probe_later(ask.requests);
        if (ticket != 0) {
            put_back(st.order, bounce, &parts);
            st.job = Job{.kind = kind,
                         .ticket = ticket,
                         .order = st.order,
                         .ask = std::move(ask),
                         .relight = bounce ? st.bounce_begun : st.relights,
                         .vertices = vertices,
                         .ask_ms = ms_since(start)};
            return;
        }
    }
    trace_part(kind, parts, wanted, tracer, settings, &stats, nullptr);
    put_back(st.order, bounce, &parts);
    for (std::size_t iv = 0; iv < st.order.size(); ++iv) {
        if (core::at(wanted, iv) != 0) {
            mark_traced(kind, st.entries.at(core::at(st.order, iv)));
        }
    }
    (bounce ? st.rebounced : st.retraced) += stats.vertices;
    measured(bounce ? &st.bounce_pace : &st.sun_pace, ms_since(start), vertices);
}

// The trace asked of the device, its answers taken once they are there (false: still waited for). Answers for a light
// no longer the one now, or for an order a mesh has left since, are dropped (asked again).
bool finish_job(rt::RayTracing& tracer, const rt::RelitSettings& settings)
{
    State& st = state();
    std::vector<rt::ProbeResult> answers;
    if (!tracer.probe_ready(st.job.ticket, &answers)) {
        return false;
    }
    Job job = std::move(st.job);
    st.job = Job{};
    const bool whole = std::ranges::all_of(job.order, [&st](const VertexBufferData* buffer) {
        const auto found = st.entries.find(buffer);
        return found != st.entries.end() && found->second.analysed;
    });
    const uint64_t pass = job.kind == Job::Kind::kBounce ? st.bounce_begun : st.relights;
    if (job.relight != pass || !whole || answers.size() != job.ask.requests.size()) {
        return true;
    }
    const auto start = std::chrono::steady_clock::now();
    const bool bounce = job.kind == Job::Kind::kBounce;
    job.ask.answers = std::move(answers);
    job.ask.answered = true;
    Gathered parts;
    gather(job.order, bounce, &parts);
    rt::RelitStats stats;
    trace_part(job.kind, parts, {}, tracer, settings, &stats, &job.ask);
    put_back(job.order, bounce, &parts);
    mark_spans(job.kind, job.order, job.ask.spans);
    (bounce ? st.rebounced : st.retraced) += stats.vertices;
    measured(bounce ? &st.bounce_pace : &st.sun_pace, job.ask_ms + ms_since(start), job.vertices);
    return true;
}

// The sun's rays traced again for entries lit toward another sun - this frame's paced share (at most
// kRelitRetraceVertices vertices).
void retrace(rt::RayTracing& tracer, const rt::RelitSettings& settings)
{
    State& st = state();
    if (st.job.kind != Job::Kind::kNone) { // (one trace in flight at a time)
        if (st.job.kind == Job::Kind::kSun) {
            finish_job(tracer, settings);
        }
        return;
    }
    // (nothing lit toward another sun - every frame once the sun is still: nothing to trace, the entries left in place)
    const bool any_stale = std::ranges::any_of(st.order, [&st](const VertexBufferData* buffer) {
        return stale_for(Job::Kind::kSun, st.entries.at(buffer));
    });
    if (!any_stale) {
        return;
    }
    start_trace(Job::Kind::kSun, paced_vertices(st.sun_pace, kRelitRetraceVertices, (st.hurried || st.cut_hurried)), tracer, settings);
}

// The bounce traced again for entries whose bounce is another sun's - once every sun is traced, from the colours as
// relit now (one bounce of the new light) - this frame's paced share (at most kRelitBounceVertices vertices).
void rebounce(rt::RayTracing& tracer, const rt::RelitSettings& settings, const rt::RelitLighting& lit)
{
    // (lighting round A) the hits' colours the relit ones at the light now: one bounce of the light as it is
    const rt::RelitLighting& lighting = lit;
    State& st = state();
    if (st.job.kind != Job::Kind::kNone) { // (one trace in flight at a time)
        if (st.job.kind == Job::Kind::kBounce) {
            finish_job(tracer, settings);
        }
        return;
    }
    // the pass under way done, or a new one begun - toward the sun now, once every mesh's sun is traced for it and the
    // bounce is due (kBounceSameSun)
    // (round P10b) or entries met since the light's passes were made joining them (State::bounce_joining) - until none
    // is left, or the light moves (its own passes then)
    if (st.bounce_begun != st.bounce_finished || st.bounce_joining) {
        const bool left = std::ranges::any_of(st.order, [&st](const VertexBufferData* buffer) {
            return stale_for(Job::Kind::kBounce, st.entries.at(buffer));
        });
        if (!left) {
            if (st.bounce_joining) {
                st.bounce_joining = false;
            } else {
                st.bounce_finished = st.bounce_begun;
            }
            return;
        }
    } else {
        bool suns_ready = true;
        bool due = false;
        for (const VertexBufferData* buffer : st.order) {
            const Entry& entry = st.entries.at(buffer);
            suns_ready = suns_ready && (!entry.analysed || !sun_stale(entry));
            due = due || (entry.analysed && bounce_due(entry));
        }
        if (!suns_ready || !due) {
            return;
        }
        if (light_bounced()) {
            st.bounce_joining = true; // (round P10b: the due ones weren't in the passes made for this light - they join them)
        } else {
            ++st.bounce_begun;
            st.bounce_sun = st.sun;
            st.bounce_passes = rt::dot(st.bounce_light_sun, st.sun) < kBounceSameSun ? 1 : st.bounce_passes + 1;
            st.bounce_light_sun = st.sun;
        }
    }
    // (the hits' colours for the pass - the same for every frame of it: made once a pass, over frames as the writes
    // are - kRelitFrameMs of them a frame while the level is shown -, before its first rays; before, every entry's were
    // made again each frame the bounce was traced: all the level's vertices a frame)
    uint64_t hits_key = fnv_value(kFnvBasis, lighting.toward);
    hits_key = fnv_value(hits_key, lighting.sun);
    hits_key = fnv_value(hits_key, lighting.sky);
    hits_key = fnv_value(hits_key, lighting.lamps);
    hits_key = model_hash(hits_key, st.model);
    hits_key = fnv_value(hits_key, st.bounce_begun); // (and made again for each pass: the bounce they show moved)
    hits_key = hits_key == 0 ? 1 : hits_key;
    const auto start = std::chrono::steady_clock::now();
    for (const VertexBufferData* buffer : st.order) {
        Entry& entry = st.entries.at(buffer);
        if (entry.hits_for == hits_key) {
            continue;
        }
        if (!(st.hurried || st.cut_hurried) && ms_since(start) >= frame_left()) {
            return; // (the rest next frame)
        }
        // (the colours the rays bring back: as relit now)
        // (round L: the light each vertex reflects - physical, not 8 bits; an animated vertex its game colour as light)
        entry.hit_light.resize(entry.originals.size());
        for (std::size_t vi = 0; vi < entry.hit_light.size(); ++vi) {
            core::at(entry.hit_light, vi) =
                vi < entry.light.size() && entry.analysed && core::at(entry.animated, vi) == 0
                    ? rt::relit_reflected(st.model, core::at(entry.light, vi), lighting)
                    : rt::colour_light(core::at(entry.originals, vi));
        }
        entry.hits_for = hits_key;
    }
    start_trace(Job::Kind::kBounce, paced_vertices(st.bounce_pace, kRelitBounceVertices, (st.hurried || st.cut_hurried)), tracer,
                settings);
}

void log_status(Status status)
{
    State& st = state();
    st.status = status;
    if (status != Status::kRelit) {
        rt::note_sun_used(rt::SunUser::kRelit, false, {}); // (added by the scene's sun: the bake kept)
        if (st.field_lighting != 0) {
            st.field_lighting = 0;
            rt::set_light_field(nullptr); // (the one lighting path: nothing lit by the field - the original's look)
        }
    }
    if (st.logged != status) {
        st.logged = status;
        get_log("rt_relit: level %08x: %s", st.level, status_name(status));
        rt::set_relit_status(status_text());
    }
}

// (the one lighting path - engine/gfx/rt_light_field.h) The light field made for the lighting now, once every entry's
// sun and bounce are traced for it (all at once: it reads the model, not the written colours), and published; an
// earlier one stays until then.
// A vertex of an entry added to a light field (the light now's parts and the bake's at its own lighting `own`).
void add_to_field(const Entry& entry, std::size_t vi, const rt::RelitLighting& light, const rt::RelitLighting& own,
                  rt::LightField* field)
{
    const State& st = state();
    const rt::RelitVertex& vertex = core::at(entry.light, vi);
    if (!vertex.used || (vi < entry.animated.size() && core::at(entry.animated, vi) != 0)) {
        return;
    }
    rt::RelitVertex at_bake = vertex;
    at_bake.sun = vertex.sun_baked;
    const rt::RelitParts now = rt::relit_parts(st.model, vertex, light);
    const rt::RelitParts baked = rt::relit_parts(st.model, at_bake, own);
    const rt::LightParts parts{.direct_now = now.direct,
                               .indirect_now = now.indirect,
                               .direct_own = baked.direct,
                               .indirect_own = baked.indirect,
                               .local = now.lamps,
                               .sky_now = {std::get<0>(light.sky) * vertex.sky, std::get<1>(light.sky) * vertex.sky,
                                           std::get<2>(light.sky) * vertex.sky}, // (round O: the sky's probe)
                               .normal = vertex.normal};
    field->add(core::at(entry.input.positions, vi), parts);
}

// (round P10b) Whether an entry is still to join the light's passes made (its bounce not traced yet).
bool joining(const Entry& entry)
{
    const State& st = state();
    return st.bounce_begun == st.bounce_finished && entry.analysed && bounce_stale(entry);
}

// (round P10b) The entries that joined the light's passes since the field for the lighting now was made (Entry::
// field_join) added to it: a copy of it with their vertices (the field is their sum) published - the field not made
// again for every vertex of the level.
void join_light_field(uint64_t lighting, const rt::RelitLighting& light)
{
    State& st = state();
    if (st.bounce_joining) {
        return; // (once the ones joining now are all traced: one copy for them all)
    }
    std::shared_ptr<rt::LightField> joined;
    const rt::RelitLighting own = rt::relit_own_lighting(st.model, 0.0F);
    for (const VertexBufferData* buffer : st.order) {
        Entry& entry = st.entries.at(buffer);
        if (!entry.field_join || !entry.analysed || sun_stale(entry) || joining(entry)) {
            continue;
        }
        entry.field_join = false;
        if (entry.field_for == lighting) {
            continue; // (the field's making reached it after it joined)
        }
        if (joined == nullptr) {
            const std::shared_ptr<const rt::LightField> now = rt::light_field();
            if (now == nullptr) {
                return;
            }
            joined = std::make_shared<rt::LightField>(*now);
        }
        const std::size_t vertices = std::min(entry.input.positions.size(), entry.light.size());
        for (std::size_t vi = 0; vi < vertices; ++vi) {
            add_to_field(entry, vi, light, own, joined.get());
        }
        entry.field_for = lighting;
    }
    if (joined != nullptr) {
        rt::set_light_field(std::move(joined));
    }
}

void make_light_field(uint64_t lighting, const rt::RelitLighting& light)
{
    State& st = state();
    if (st.field_lighting == lighting) {
        join_light_field(lighting, light);
        return;
    }
    for (const VertexBufferData* buffer : st.order) {
        const Entry& entry = st.entries.at(buffer);
        if (entry.analysed && sun_stale(entry)) {
            return;
        }
    }
    const rt::RelitLighting own = rt::relit_own_lighting(st.model, 0.0F);
    // (one relight at a time) made over frames - kRelitFrameMs of it a frame while the level is shown, every entry once:
    // made at once it was all the level's vertices in one frame, each relight (a running clock's: a 40 ms frame a second)
    if (st.field_building == nullptr || st.field_building_for != lighting) {
        st.field_building = std::make_shared<rt::LightField>();
        st.field_building_for = lighting;
    }
    const auto start = std::chrono::steady_clock::now();
    const double share = frame_left();
    constexpr std::size_t kFieldTimed = 256; // (the time looked at every this many vertices)
    for (const VertexBufferData* buffer : st.order) {
        Entry& entry = st.entries.at(buffer);
        // (round P10b: one joining the light's passes added once its bounce is traced - join_light_field)
        if (!entry.analysed || entry.field_for == lighting || joining(entry)) {
            continue;
        }
        if (entry.field_lighting != lighting) { // (its vertices added from the first, for this field)
            entry.field_lighting = lighting;
            entry.field_done = 0;
        }
        const std::size_t vertices = std::min(entry.input.positions.size(), entry.light.size());
        for (std::size_t vi = entry.field_done; vi < vertices; ++vi) {
            if (!(st.hurried || st.cut_hurried) && vi % kFieldTimed == 0 && ms_since(start) >= share) {
                entry.field_done = vi;
                return; // (the rest next frame - inside a mesh too)
            }
            add_to_field(entry, vi, light, own, st.field_building.get());
        }
        entry.field_for = lighting;
    }
    std::shared_ptr<rt::LightField> field = std::move(st.field_building);
    field->set_sun({rt::x_of(light.toward), rt::y_of(light.toward), rt::z_of(light.toward)}, light.sun); // (round O)
    st.field_building_for = 0;
    st.field_lighting = lighting;
    const rt::LightRatio whole = field->whole();
    static double logged_hours = -1.0;
    const rt::DayLight day = rt::day_light();
    if (!day.on || std::abs(day.hours - logged_hours) >= 1.0) {
        logged_hours = day.on ? day.hours : -1.0;
        get_log("rt_relit: the light field - %zu vertices in %zu cells; the level's light now over the bake's %.3f %.3f "
                "%.3f (indirect %.3f %.3f %.3f)",
                field->vertices(), field->cells(), static_cast<double>(std::get<0>(whole.whole)),
                static_cast<double>(std::get<1>(whole.whole)), static_cast<double>(std::get<2>(whole.whole)),
                static_cast<double>(std::get<0>(whole.indirect)), static_cast<double>(std::get<1>(whole.indirect)),
                static_cast<double>(std::get<2>(whole.indirect)));
    }
    rt::set_light_field(std::move(field));
}

// Whether the relight toward the light the world is lit by now is still under way (update's one relight at a time): an
// entry's sun traced toward another light, its colours not written for the lighting yet, or the light field
// not made for it yet. (The bounce is
// a pass of its own: neither is a relight.)
bool relight_running()
{
    const State& st = state();
    if (st.wanted_lighting == 0) {
        return false;
    }
    for (const VertexBufferData* buffer : st.order) {
        const Entry& entry = st.entries.at(buffer);
        if (entry.analysed && (sun_stale(entry) || entry.written != st.wanted_lighting || entry.store_stale)) {
            return true;
        }
    }
    return st.field_lighting != st.wanted_lighting;
}

// The entries' light stores written where they are stale (the lighting changed, the bounce traced again) and their
// sun is traced - the stream's words a vertex (rt_cel.h: the light's parts and the colour the world is drawn with), the
// animated vertices "none" (their draws keep the game's colour) - handed to the draws (cel_stream) and to the ray-traced
// passes (rt_scene.h set_light_stream); the change logged once per lighting. The game's vertex buffers are never
// written (lighting round A: one writer to each colour - the game's are the game's).
void write_streams(uint64_t lighting, const rt::RelitLighting& light)
{
    State& st = state();
    std::size_t written = 0;
    // (added by the time of day) while the clock lights the level, a share of the vertices a frame: a new time's
    // light spread over frames, as the original's time of day colours a part of the level a frame (paced: at most
    // kRelitWriteVertices)
    const bool budgeted = rt::day_light().on;
    const std::size_t paced = paced_vertices(st.write_pace, kRelitWriteVertices, (st.hurried || st.cut_hurried));
    std::size_t budget = paced;
    const auto start = std::chrono::steady_clock::now();
    struct Change {
        std::size_t vertices = 0;
        double baked = 0.0;
        double relit = 0.0;
        std::size_t moved = 0;
    } change;
    const uint64_t wanted = lighting;
    for (const VertexBufferData* buffer : st.order) {
        Entry& entry = st.entries.at(buffer);
        if (!entry.analysed || (entry.written == wanted && !entry.store_stale) || sun_stale(entry)) {
            continue;
        }
        const auto count = static_cast<uint32_t>(entry.originals.size());
        if (count == 0) {
            continue;
        }
        if (budgeted) {
            if (budget == 0) {
                break;
            }
            budget -= std::min<std::size_t>(budget, count);
        }
        if (entry.stream == nullptr &&
            gpu().create_vertex_buffer(count * rt::kCelStreamBytes, gpu_value::kUsageWriteOnly, 0,
                                       gpu_value::kPoolDefault, &entry.stream) < 0) {
            entry.stream = nullptr;
            continue;
        }
        void* locked = nullptr;
        if (gpu().lock_vertex_buffer(entry.stream, 0, 0, &locked, 0) < 0 || locked == nullptr) {
            continue;
        }
        const std::span<uint8_t> bytes(static_cast<uint8_t*>(locked), std::size_t{count} * rt::kCelStreamBytes);
        // (round light cards - one owner for each surface's lamp light) a surface the lamps pass can't find (blended,
        // not cut out) takes its lamps' light here, whole - each lamp by how much it is on (in its L already)
        rt::RelitLighting own = light;
        if (!entry.lit_per_pixel) {
            own.lamps = 1.0F;
        }
        for (std::size_t vi = 0; vi < count; ++vi) {
            const bool hidden = entry.baked_shadow || entry.glow_card;
            const rt::CelWords words = hidden ? rt::kCelHiddenStream // (the bake's shadow, a light card: not drawn)
                                       : vi < entry.light.size() && core::at(entry.animated, vi) == 0
                                           ? rt::cel_stream_words(st.model, core::at(entry.light, vi), own)
                                           : rt::kCelNoStream;
            std::memcpy(bytes.subspan(vi * rt::kCelStreamBytes, rt::kCelStreamBytes).data(), words.data(),
                        rt::kCelStreamBytes);
            if (!hidden && vi < entry.light.size() && core::at(entry.animated, vi) == 0) {
                const float was = luma_of(core::at(entry.originals, vi));
                const float now = luma_of(shown_argb(st.model, core::at(entry.light, vi), own));
                change.vertices += 1;
                change.baked += was;
                change.relit += now;
                change.moved += std::abs(now - was) > kChangedLuma ? 1 : 0;
            }
        }
        gpu().unlock_vertex_buffer(entry.stream);
        entry.written = wanted;
        entry.store_stale = false;
        publish_stream(entry, entry.stream);
        ++written;
    }
    if (budgeted) {
        measured(&st.write_pace, ms_since(start), paced - budget);
    }
    rt::Rgbf sun{};
    for (std::size_t ch = 0; ch < sun.size(); ++ch) {
        core::at(sun, ch) = core::at(light.sun, ch);
    }
    rt::set_cel_world({.relit = true, .sun = sun, .toward = light.toward, .sky = light.sky});
    // (added by the time of day: a running clock writes new light twice a second - logged once a game hour)
    static double logged_hours = -1.0;
    const rt::DayLight day = rt::day_light();
    const bool log_it = !day.on || std::abs(day.hours - logged_hours) >= 1.0;
    if (written > 0 && wanted != st.lighting && !log_it) {
        st.lighting = wanted;
        rt::set_relit_status(status_text());
    } else if (written > 0 && wanted != st.lighting) {
        logged_hours = day.on ? day.hours : -1.0;
        st.lighting = wanted;
        const double vertices = static_cast<double>(std::max<std::size_t>(change.vertices, 1));
        get_log("rt_relit: %zu meshes' light written (the sun's light %.3f %.3f %.3f, the lamps x %.2f): mean "
                "brightness %.1f against the bake's %.1f, %.1f%% of the vertices changed by more than %.0f",
                written, static_cast<double>(std::get<0>(sun)), static_cast<double>(std::get<1>(sun)),
                static_cast<double>(std::get<2>(sun)), static_cast<double>(light.lamps), change.relit / vertices,
                change.baked / vertices, 100.0 * static_cast<double>(change.moved) / vertices,
                static_cast<double>(kChangedLuma));
        rt::set_relit_status(status_text());
    }
}
// (round relit-misses) The world meshes a relit level draws without a light of their own - none in the level's data
// (meshes the bake didn't see), refused by the mode (why), their light not written yet - and the vertices whose colour
// the game animates (they keep the game's colour), logged for the level whenever they change: a miss shows in the log,
// not only on the screen. Those meshes are drawn with the light field's ratio over their bake (unlit_world_colour).
void log_misses()
{
    State& st = state();
    std::size_t lit = 0;
    std::size_t not_in_data = 0;
    std::size_t not_in_data_vertices = 0;
    std::size_t waiting = 0;
    std::size_t animated = 0;
    for (const VertexBufferData* buffer : st.order) {
        const Entry& entry = st.entries.at(buffer);
        if (!entry.analysed) {
            ++not_in_data;
            not_in_data_vertices += entry.originals.size();
            continue;
        }
        (entry.written != 0 ? lit : waiting) += 1;
        animated += static_cast<std::size_t>(std::ranges::count(entry.animated, uint8_t{1}));
    }
    std::map<std::string, std::size_t> refused;
    for (const auto& [mesh, why] : st.refused) {
        const auto seen = st.listed.find(mesh);
        if (seen != st.listed.end() && seen->second == st.listing) {
            refused[why != nullptr ? why : "?"] += 1;
        }
    }
    std::string why_refused;
    std::size_t refused_count = 0;
    for (const auto& [why, count] : refused) {
        why_refused += text::format("%s%zu %s", why_refused.empty() ? "" : ", ", count, why.c_str());
        refused_count += count;
    }
    // (the waiting ones change from frame to frame while a relight runs: logged once the level's misses settle)
    const std::string line =
        text::format("%zu world meshes lit by their own light; without it - %zu not in the level's relit data (%zu "
                     "vertices), %zu the mode can't relight%s%s%s; %zu animated vertices keep the game's colour",
                     lit, not_in_data, not_in_data_vertices, refused_count, refused_count > 0 ? " (" : "",
                     why_refused.c_str(), refused_count > 0 ? ")" : "", animated);
    if (waiting == 0 && (line != st.misses_logged || st.misses_level != st.level)) {
        st.misses_logged = line;
        st.misses_level = st.level;
        get_log("rt_relit: level %08x - %s; the meshes without their own light drawn with the light field's", st.level,
                line.c_str());
    }
}

} // namespace

void update(std::span<const WorldMesh> world, bool stable, rt::RayTracing& tracer)
{
    State& st = state();
    st.frame_start = std::chrono::steady_clock::now();
    // (round S2) the lighting's cut shown once the world's colours hold its light: at once on every way out below but
    // the relit one, which says whether its relight toward the published light is done
    const rt::DayLight published = rt::day_light();
    struct CutNote {
        uint64_t cut = 0;
        bool shown = true;
        CutNote(const CutNote&) = delete;
        CutNote& operator=(const CutNote&) = delete;
        CutNote(CutNote&&) = delete;
        CutNote& operator=(CutNote&&) = delete;
        explicit CutNote(uint64_t of) : cut(of) {}
        ~CutNote()
        {
            if (shown) {
                rt::note_cut_shown(cut);
            }
        }
    } note(published.cut);
    st.cut_hurried = rt::cut_settling();
    const rt::RelitSettings settings = rt::relit_settings();
    if (!settings.on) {
        if (st.status != Status::kOff) {
            restore_all();
            log_status(Status::kOff);
            st.field_lighting = 0;
            rt::set_light_field(nullptr);
            rt::set_sun_relight({}); // (added by the scene's sun: the bake's sun lights the scene again)
        }
        return;
    }
    const uint32_t level = main_scene();
    level_changed(level);
    if (st.status == Status::kOff) {
        st.status = Status::kWaiting;
    }
    if (!stable || world.empty() || level == 0) {
        return;
    }
    // the world's meshes, their light from the level's data (the meshes shown since the level's first frame too)
    list_world(world);
    take_from_data();
    const Status verdict = level_verdict();
    if (verdict != st.verdict) {
        st.verdict = verdict;
        publish_level_light(verdict);
    }
    if (verdict != Status::kRelit) {
        restore_all();
        log_status(verdict);
        rt::set_sun_relight({});
        return;
    }
    const rt::RelitLevel* entry_of_level = level_entry(level);
    // (the mode says it relights the level once the model is found - rt_scene.h scene_sun)
    if (entry_of_level != nullptr && entry_of_level->off) {
        restore_all();
        log_status(Status::kOptedOut);
        rt::set_sun_relight({.relights = false});
        return;
    }
    if (st.order.empty()) {
        return;
    }
    log_status(Status::kRelit);
    rt::set_sun_relight({.relights = true});
    // the lighting now: the sun (traced again where it moved) and its colour
    rt::Vec3 toward{};
    if (!target_sun(&toward)) {
        toward = st.model.toward;
    }
    const bool sky_model = rt::sun_light().on;
    const rt::DayLight day = rt::day_light(); // (added by the time of day)
    const uint64_t day_serial = day.on ? day.serial : 0;
    // (one relight at a time - the rule that keeps a moving sun's cost bounded) A new light is taken only once the
    // relight toward the last one is done: every mesh's sun and bounce traced for it, its colours written, the light
    // field made. A light that moves meanwhile - a running clock steps the sun several times a second, more often than
    // a relight takes - waits, and the newest is taken next. Before, each step made every mesh stale again and the
    // per-frame budgets started over from the first mesh: the same first meshes were traced and written (uploaded)
    // every frame, the rest never, the bounce and the light field never made - and the slower the frames, the more
    // often a new step came, until every frame was spent on it (a 3 fps Boston with tod_cycle on).
    const bool newer = rt::dot(toward, st.sun) < kSameSun ||
                       (day.on ? day.light_radius : rt::kRelitSunRadiusDegrees) != st.radius ||
                       day_serial != st.day_serial;
    if (newer && !relight_running()) {
        // (round S2) a lighting cut: the vertices' bounce B is light of another lighting - the bake's (the level's own
        // time, not pre-exposed) or the light before the cut (at its pre-exposure) - and each bounce pass traces it
        // round again: a night's bounce kept a dusk's light. It starts again from none, its passes from the first (each
        // adds one more bounce of the new light: kRelitBouncePasses of them)
        if (published.on && published.cut != st.bounce_cut) {
            st.bounce_cut = published.cut;
            for (auto& [buffer, entry] : st.entries) {
                (void)buffer;
                for (rt::RelitVertex& vertex : entry.light) {
                    vertex.bounce = {};
                }
            }
            st.bounce_light_sun = {};
        }
        ++st.relights;
        st.sky_model = sky_model;
        st.day_serial = day_serial;
        st.day_scales = day_scales(day, st.model);
        st.bounce_passes = 0; // (round A2: the new light's bounce passes from the first)
        st.sun = toward;
        st.radius = day.on ? day.light_radius : rt::kRelitSunRadiusDegrees;
        // (a running clock moves the sun twice a second: logged once a game hour of it)
        static double logged_hours = -1.0;
        if (!day.on || std::abs(day.hours - logged_hours) >= 1.0) {
            logged_hours = day.on ? day.hours : -1.0;
            get_log("rt_relit: the sun toward %.3f %.3f %.3f (in the global unit: sun %.3f %.3f %.3f per N.L, sky %.3f "
                    "%.3f %.3f)",
                    static_cast<double>(rt::x_of(toward)), static_cast<double>(rt::y_of(toward)),
                    static_cast<double>(rt::z_of(toward)), static_cast<double>(std::get<0>(st.day_scales.sun)),
                    static_cast<double>(std::get<1>(st.day_scales.sun)), static_cast<double>(std::get<2>(st.day_scales.sun)),
                    static_cast<double>(std::get<0>(st.day_scales.sky)), static_cast<double>(std::get<1>(st.day_scales.sky)),
                    static_cast<double>(std::get<2>(st.day_scales.sky)));
            get_log("rt_relit: relight %llu, bounce pass %llu (%llu done); a vertex takes %.2f us toward the sun, %.2f us "
                    "for its bounce, %.2f us to write (about %.1f ms of them all a frame while the level is shown)",
                    static_cast<unsigned long long>(st.relights), static_cast<unsigned long long>(st.bounce_begun),
                    static_cast<unsigned long long>(st.bounce_finished), st.sun_pace.ms_per_vertex * 1000.0,
                    st.bounce_pace.ms_per_vertex * 1000.0, st.write_pace.ms_per_vertex * 1000.0, kRelitFrameMs);
        }
    }
    {
        const frame_profile::Scope phase("relit_retrace");
        retrace(tracer, settings);
    }
    // (lighting round A) the lamps' light relit lights per vertex - what the lamps pass doesn't light per pixel: one
    // owner; in steps of kLampSteps, so a pass's amount moving a little doesn't write the level again
    constexpr float kLampSteps = 32.0F;
    const float lamps = std::round(vertex_lamps() * kLampSteps) / kLampSteps;
    const uint64_t switched = switch_lamps(day); // (round A3: each lamp by its own daylight)
    uint64_t lighting = fnv_value(kFnvBasis, st.sun);
    lighting = fnv_value(lighting, st.radius);
    lighting = fnv_value(lighting, lamps);
    lighting = fnv_value(lighting, switched);
    lighting = model_hash(lighting, st.model);
    lighting = fnv_value(lighting, st.bounce_finished); // (one relight at a time: a bounce pass done - written again)
    lighting = fnv_value(lighting, st.day_scales.sun);
    lighting = fnv_value(lighting, st.day_scales.sky);
    lighting = lighting == 0 ? 1 : lighting;
    const rt::RelitLighting light{.toward = st.sun, .sun = st.day_scales.sun, .sky = st.day_scales.sky, .lamps = lamps};
    {
        const frame_profile::Scope phase("relit_rebounce");
        rebounce(tracer, settings, light);
    }
    st.written_light = light;
    st.wanted_lighting = lighting;
    {
        const frame_profile::Scope phase("relit_write");
        write_streams(lighting, light);
    }
    {
        const frame_profile::Scope phase("relit_field");
        make_light_field(lighting, light); // (the one lighting path)
    }
    log_misses(); // (round relit-misses)
    rt::note_sun_used(rt::SunUser::kRelit, true, st.sun);
    // (the relight's writes and the light field done, and the new light's bounce passes - started again at the cut)
    note.shown = st.day_serial == (published.on ? published.serial : 0) && !relight_running() &&
                 st.bounce_passes >= kRelitBouncePasses && st.bounce_begun == st.bounce_finished;
}

rt::BakedSun new_world(std::span<const WorldMesh> world)
{
    State& st = state();
    const uint32_t level = main_scene();
    level_changed(level);
    if (level == 0) {
        return {};
    }
    list_world(world);
    take_from_data();
    st.verdict = level_verdict();
    publish_level_light(st.verdict);
    const LevelData& data = level_data(level);
    rt::BakedSun sun = data.found ? data.data.sun : rt::BakedSun{};
    sun.scene = ++st.fits;
    const auto [heading, elevation] = std::array<double, 2>{
        std::atan2(static_cast<double>(rt::x_of(sun.toward)), static_cast<double>(rt::z_of(sun.toward))) * kDegrees,
        std::asin(std::clamp(static_cast<double>(rt::y_of(sun.toward)), -1.0, 1.0)) * kDegrees};
    get_log("rt: the baked sun (the level's relit data): %s toward %.3f %.3f %.3f (heading %.0f, elevation %.0f), "
            "confidence %.2f; the level: %s",
            sun.found ? "found" : "none (no sun shadows here)", static_cast<double>(rt::x_of(sun.toward)),
            static_cast<double>(rt::y_of(sun.toward)), static_cast<double>(rt::z_of(sun.toward)), heading, elevation,
            static_cast<double>(sun.confidence), status_name(st.verdict));
    return sun;
}

bool read_world_mesh(const WorldMesh& world, BakeMesh* out)
{
    Entry entry;
    if (!make_entry(world, &entry)) {
        return false;
    }
    out->mesh = std::move(entry.input);
    out->mesh.id = world.id;
    out->animated = std::move(entry.animated);
    out->content = entry.content;
    out->card = world.glow;
    out->blended = world.blended && !entry.baked_shadow;
    out->object = world.object != nullptr;
    out->model = entry.model_content;
    return true;
}

GpuVertexBuffer* cel_stream(const VertexBufferData* buffer)
{
    const State& st = state();
    const auto found = st.entries.find(buffer);
    return found != st.entries.end() && found->second.written != 0 ? found->second.stream : nullptr;
}

void buffer_released(const VertexBufferData* buffer)
{
    State& st = state();
    const auto entry = st.entries.find(buffer);
    if (entry == st.entries.end()) {
        return;
    }
    release_stream(entry->second);
    std::erase_if(st.buffer_of, [buffer](const auto& kept) { return kept.second == buffer; });
    st.entries.erase(entry);
    std::erase(st.order, buffer);
}

void buffer_written(const VertexBufferData* buffer)
{
    State& st = state();
    // (lighting round A) nothing to do: relit never writes the game's colours, and its draws read the light store in
    // their place - the game's write stays the game's (its animated vertices keep it)
    (void)buffer;
}

void mesh_released(const MeshRef* mesh)
{
    State& st = state();
    st.listed.erase(mesh);
    st.refused.erase(mesh);
    const auto found = st.buffer_of.find(mesh);
    if (found == st.buffer_of.end()) {
        return;
    }
    const VertexBufferData* buffer = found->second;
    st.buffer_of.erase(found);
    const auto entry = st.entries.find(buffer);
    if (entry != st.entries.end() && entry->second.mesh == mesh) {
        release_stream(entry->second);
        st.entries.erase(entry);
        std::erase(st.order, buffer);
    }
}

bool data_emitters(std::vector<rt::Emitter>* out, std::vector<uint8_t>* surfaced)
{
    const State& st = state();
    if (st.level == 0) {
        return false;
    }
    const LevelData& data = level_data(st.level);
    if (!data.found || !rt::relit_verdict_relights(data.data.verdict)) {
        return false;
    }
    out->clear();
    // (path tracing, stage 1 - one owner per lamp) the lamps whose own surface emits: their emitting vertices
    std::vector<uint8_t> has_surface(data.data.lamp_list.size(), 0);
    for (const rt::RelitDataMesh& mesh : data.data.meshes) {
        for (const rt::RelitEmitting& emitting : mesh.emitting) {
            if (emitting.lamp < has_surface.size()) {
                core::at(has_surface, emitting.lamp) = 1;
            }
        }
    }
    if (surfaced != nullptr) {
        surfaced->clear();
    }
    std::size_t lamp_index = 0;
    // (round indoor lamps) every lamp of the data, the level's own point lights too: with the data the frame record's
    // point lights aren't shone (vulkan_rt_lights.cpp: only the solved lamps - round O), so leaving them out here left
    // the art's own lights dark in every relit level - Training's 13, its hall's lamps - while their light was in the
    // data (a solve or their fixture's model)
    for (const rt::RelitLamp& lamp : data.data.lamp_list) {
        const uint8_t own_surface = core::at(has_surface, lamp_index);
        ++lamp_index;
        const float brightest = std::max({std::get<0>(lamp.light), std::get<1>(lamp.light), std::get<2>(lamp.light)});
        if (!(brightest > 0.0F)) {
            continue;
        }
        if (surfaced != nullptr) {
            surfaced->push_back(own_surface);
        }
        out->push_back({.position = lamp.position,
                        .size = lamp.size,
                        .colour = rt::scaled(lamp.light, 1.0F / brightest),
                        .intensity = brightest,
                        .reach = lamp.reach,
                        .material = 0, // (the solved light is its whole colour: no texture's tint on it)
                        .kind = lamp.kind,
                        .core = lamp.core,
                        .shape = lamp.shape,
                        .axis = lamp.axis,
                        .half_length = lamp.half_length,
                        .switch_sky = lamp.switch_sky}); // (round A3: on by its own daylight - rt_lights.h lamp_on)
    }
    return true;
}

bool object_lighting(ObjectLighting* out)
{
    const State& st = state();
    if (st.status != Status::kRelit || !st.model.valid) {
        return false;
    }
    const rt::Vec3 from = st.model.toward;
    const rt::Vec3 to = st.sun;
    const bool moved = rt::dot(from, to) < kSameSun;
    const bool coloured = st.day_serial != 0;
    if (!moved && !coloured) {
        return false;
    }
    // (Rodrigues: the rotation about from x to by the angle between them)
    const rt::Vec3 axis = rt::cross(from, to);
    const float sine = std::sqrt(rt::dot(axis, axis));
    const float cosine = rt::dot(from, to);
    std::array<std::array<float, 3>, 3> rotation{{{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}}};
    if (sine > 0.0F) {
        const rt::Vec3 idx = rt::scaled(axis, 1.0F / sine);
        const std::array<std::array<float, 3>, 3> cross_matrix{{{0.0F, -rt::z_of(idx), rt::y_of(idx)},
                                                                {rt::z_of(idx), 0.0F, -rt::x_of(idx)},
                                                                {-rt::y_of(idx), rt::x_of(idx), 0.0F}}};
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t ch = 0; ch < 3; ++ch) {
                float squared = 0.0F;
                for (std::size_t mi = 0; mi < 3; ++mi) {
                    squared += core::at(core::at(cross_matrix, row), mi) * core::at(core::at(cross_matrix, mi), ch);
                }
                core::at(core::at(rotation, row), ch) +=
                    (sine * core::at(core::at(cross_matrix, row), ch)) + ((1.0F - cosine) * squared);
            }
        }
    }
    out->rotation = rotation;
    // (the one lighting path: the key light is the direct light - the sun's colour as relit, with the clock the
    // lighting state's light_colour, the moon's at night; the game's lights sent are the level's own state's)
    // (round A2: the game's own lights on objects are its art - the clock's ratio to the level's own light moves them;
    // the objects in the global unit are still to come - docs/design/vulkan.md)
    const rt::DayLight day = rt::day_light();
    out->key_colour = day.on ? day.light_colour : rt::Rgbf{1.0F, 1.0F, 1.0F};
    return true;
}

bool unlit_object_colour(const std::array<float, 3>& position, std::array<uint8_t, 4>* out)
{
    const State& st = state();
    if (st.status != Status::kRelit || !st.model.valid || st.field_lighting == 0) {
        return false;
    }
    const rt::LightRatio ratio = rt::light_ratio_at(position);
    constexpr float kOne = 128.0F; // (the instance colour's 1)
    constexpr float kMostByte = 255.0F;
    bool same = true;
    for (std::size_t ch = 0; ch < 3; ++ch) {
        // (round O: the ratio is linear light; the instance colour multiplies a display-referred colour - rt_display.h)
        const long byte =
            std::lround(std::clamp(rt::display_from_linear(core::at(ratio.whole, ch)) * kOne, 0.0F, kMostByte));
        core::at(*out, ch) = static_cast<uint8_t>(byte);
        same = same && byte == static_cast<long>(kOne);
    }
    core::at(*out, 3) = static_cast<uint8_t>(kOne);
    return !same; // (the light there the bake's: drawn as the original draws it)
}

bool unlit_world_colour(const MeshRef* mesh, std::array<uint8_t, 4>* out)
{
    const State& st = state();
    if (st.status != Status::kRelit || mesh == nullptr) {
        return false;
    }
    const auto seen = st.listed.find(mesh);
    if (seen == st.listed.end() || seen->second != st.listing) {
        return false; // (not a world mesh of the relit level now: the sky's, an object's - lit their own ways)
    }
    if (const auto buffer = st.buffer_of.find(mesh); buffer != st.buffer_of.end()) {
        if (const auto entry = st.entries.find(buffer->second);
            entry != st.entries.end() && entry->second.analysed && entry->second.written != 0) {
            return false; // (its own light: the light store)
        }
    }
    // (round P10: an object's mesh where it stands)
    std::array<float, 3> centre = mesh->centre();
    if (const auto placed = st.placed_at.find(mesh); placed != st.placed_at.end()) {
        const std::array<float, 16>& m = placed->second;
        const std::array<float, 3> was = centre;
        for (std::size_t c = 0; c < 3; ++c) {
            core::at(centre, c) = (std::get<0>(was) * core::at(m, c)) + (std::get<1>(was) * core::at(m, 4 + c)) +
                                  (std::get<2>(was) * core::at(m, 8 + c)) + core::at(m, 12 + c);
        }
    }
    return unlit_object_colour(centre, out);
}

bool object_listed(const void* instance)
{
    const State& st = state();
    const auto found = st.objects_listed.find(instance);
    return st.status == Status::kRelit && found != st.objects_listed.end() && found->second == st.listing;
}

namespace {

bool first_light_held(bool loading)
{
    struct Hold {
        bool armed = false;
        bool holding = false;
        std::chrono::steady_clock::time_point since{};
        uint32_t frames = 0;
    };
    static Hold hold;
    if (loading) {
        hold.armed = true;
        hold.holding = false;
        return false;
    }
    if (hold.armed) {
        hold.armed = false;
        // (only where the light can differ from the bake's at the first frame: the clock may light the level)
        hold.holding = rt::relit_settings().on && rt::ray_tracing() != nullptr && rt::tod_may_run(rt::tod_settings());
        hold.since = std::chrono::steady_clock::now();
        hold.frames = 0;
    }
    if (!hold.holding) {
        return false;
    }
    ++hold.frames;
    const State& st = state();
    bool relit_ready = false;
    switch (st.status) {
    case Status::kOff:
    case Status::kNoData:
    case Status::kStale:
    case Status::kOptedOut:
    case Status::kNoModel:
        relit_ready = true;
        break;
    case Status::kWaiting:
        relit_ready = false;
        break;
    case Status::kRelit: {
        rt::Vec3 sun{};
        uint64_t lighting = 0;
        relit_ready = settled(&sun, &lighting) && st.field_lighting == st.wanted_lighting;
        break;
    }
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - hold.since).count();
    // (ready from the state itself: the clock's light published and the level's scripts for its state ended - the night
    // geometry they show is in the world -, every world mesh's light written for that light and the light field made)
    const bool ready = relit_ready && day_clock::light_ready();
    if (ready || seconds >= kFirstLightMostSeconds) {
        hold.holding = false;
        get_log("rt_relit: the level's first light %s after %u frames (%.0f ms) - %s", ready ? "ready" : "not ready",
                hold.frames, seconds * 1000.0, ready ? "shown" : "shown as it is");
        return false;
    }
    return true;
}

} // namespace

bool hold_first_light(bool loading)
{
    const bool held = first_light_held(loading);
    state().hurried = held || loading; // (the relight's pace: the most a frame while nothing of the level is shown)
    return held;
}

bool settled(rt::Vec3* sun, uint64_t* lighting)
{
    const State& st = state();
    if (st.status != Status::kRelit || !st.model.valid || st.wanted_lighting == 0) {
        return false;
    }
    for (const VertexBufferData* buffer : st.order) {
        const Entry& entry = st.entries.at(buffer);
        if (entry.analysed && (entry.written != st.wanted_lighting || sun_stale(entry) ||
                               bounce_stale(entry) || bounce_due(entry))) {
            return false;
        }
    }
    *sun = st.sun;
    *lighting = st.wanted_lighting;
    return true;
}

std::span<const uint32_t> original_colours(const VertexBufferData* buffer)
{
    const State& st = state();
    (void)buffer; // (lighting round A: the game's buffers hold the game's colours - relit never writes them)
    return {};
}

std::string status_text()
{
    const State& st = state();
    const rt::RelitSettings settings = rt::relit_settings();
    std::size_t vertices = 0;
    std::size_t written = 0;
    for (const auto& [buffer, entry] : st.entries) {
        vertices += entry.originals.size();
        written += entry.written != 0 ? 1 : 0;
    }
    return text::format("relit: %s (setting %s); level %08x, %zu meshes (%zu written), %zu "
                        "vertices; sun %.3f %.3f %.3f; model sun %.3f sky %.3f bounce %.3f%s error %.3f; "
                        "%zu vertices' sun and %zu vertices' bounce traced again",
                        status_name(st.status), settings.on ? "on" : "off", st.level, st.entries.size(), written, vertices,
                        static_cast<double>(rt::x_of(st.sun)), static_cast<double>(rt::y_of(st.sun)),
                        static_cast<double>(rt::z_of(st.sun)), static_cast<double>(std::get<1>(st.model.sun)),
                        static_cast<double>(std::get<1>(st.model.sky)),
                        static_cast<double>(std::get<1>(st.model.bounce)), st.model.sunless ? " (no sun in the bake)" : "",
                        static_cast<double>(st.model.error), st.retraced, st.rebounced);
}

uint32_t level_scene() { return main_scene(); }

// (the console's `rt probe`) The colour word a vertex holds in its buffer's copy now (what the card was last sent).
uint32_t colour_held(const Entry& entry, std::size_t vertex)
{
    uint8_t* data = nullptr;
    entry.buffer->get_lock(0, 0, &data, gpu_value::kLockReadOnly);
    uint32_t colour = 0;
    if (data != nullptr) {
        const std::span<const uint8_t> bytes(data, entry.buffer->size());
        const std::size_t at = (vertex * entry.stride) + entry.colour_offset;
        if (at + sizeof(colour) <= bytes.size()) {
            std::memcpy(&colour, bytes.subspan(at, sizeof(colour)).data(), sizeof(colour));
        }
    }
    entry.buffer->get_unlock();
    return colour & 0xFFFFFFU;
}

namespace {
// (the census) What the level is lit by: the mode's state, or - the mode out of effect (the settings model turns it off
// where the level keeps the original's lighting) - what its data says.
Status shown_status()
{
    const State& st = state();
    return st.status == Status::kOff && st.verdict != Status::kWaiting ? st.verdict : st.status;
}
} // namespace

std::string census_bucket(const WorldMesh& world)
{
    const State& st = state();
    if (world.geometry == nullptr || world.source == nullptr) {
        return "not traced";
    }
    if (const char* why = refusal(world); why != nullptr) {
        return std::string("not relit: ") + why;
    }
    const auto found = st.entries.find(world.source);
    if (found == st.entries.end()) {
        return st.status == Status::kRelit ? "not relit: not met yet" : std::string("kept bake: ") + status_name(shown_status());
    }
    const Entry& entry = found->second;
    if (st.status != Status::kRelit) {
        return std::string("kept bake: ") + status_name(shown_status());
    }
    if (!entry.analysed) {
        return entry.looked_up ? "not relit: not in the level's relit data" : "relit: not looked up yet";
    }
    if (entry.written == 0) {
        return "relit: not written yet";
    }
    if (entry.baked_shadow) {
        return "relit: the bake's shadow drawn as geometry - hidden";
    }
    if (entry.glow_card) {
        return "relit: the original's light card (an additive glow) - hidden: the lamp lights by its own light";
    }
    const bool animated = std::ranges::any_of(entry.animated, [](uint8_t a) { return a != 0; });
    if (entry.stream == nullptr) {
        return animated ? "relit: no light stream (some vertices animated)" : "relit: no light stream";
    }
    return animated ? "relit: light stream (some vertices animated - those keep their colour)" : "relit: light stream";
}

std::string census_status()
{
    const State& st = state();
    return text::format("relit %s; model sun %.2f %.2f %.2f, sky %.2f %.2f %.2f, bounce %.2f %.2f %.2f, lamps %.2f %.2f "
                        "%.2f%s, %zu entries",
                        status_name(shown_status()), static_cast<double>(std::get<0>(st.model.sun)),
                        static_cast<double>(std::get<1>(st.model.sun)), static_cast<double>(std::get<2>(st.model.sun)),
                        static_cast<double>(std::get<0>(st.model.sky)), static_cast<double>(std::get<1>(st.model.sky)),
                        static_cast<double>(std::get<2>(st.model.sky)),
                        static_cast<double>(std::get<0>(st.model.bounce)),
                        static_cast<double>(std::get<1>(st.model.bounce)),
                        static_cast<double>(std::get<2>(st.model.bounce)),
                        static_cast<double>(std::get<0>(st.model.lamps)), static_cast<double>(std::get<1>(st.model.lamps)),
                        static_cast<double>(std::get<2>(st.model.lamps)), st.model.sunless ? ", no sun in the bake" : "",
                        st.entries.size());
}

// (the console's `rt probe`) The lamp a vertex is a surface of - " (lamp N: its kind, how the bake knew it, its fixture,
// where)" - or "".
std::string emitting_lamp_text(const Entry& entry, std::size_t vertex)
{
    const State& st = state();
    const LevelData& data = level_data(st.level);
    if (!data.found || entry.data_mesh >= data.data.meshes.size()) {
        return {};
    }
    for (const rt::RelitEmitting& emitting : core::at(data.data.meshes, entry.data_mesh).emitting) {
        if (emitting.vertex == vertex && emitting.lamp < data.data.lamp_list.size()) {
            const rt::RelitLamp& lamp = core::at(data.data.lamp_list, emitting.lamp);
            return text::format(" (lamp %u: kind %u, %s, %s, at %.0f %.0f %.0f)", emitting.lamp,
                                static_cast<unsigned>(lamp.kind), lamp.solved ? "told" : "not told",
                                rt::relit_lamp_source_name(static_cast<rt::RelitLampSource>(lamp.source)),
                                static_cast<double>(rt::x_of(lamp.position)),
                                static_cast<double>(rt::y_of(lamp.position)), static_cast<double>(rt::z_of(lamp.position)));
        }
    }
    return {};
}

std::string describe_mesh(rt::MeshId id)
{
    const State& st = state();
    for (const auto& [buffer, entry] : st.entries) {
        if (entry.input.id != id) {
            continue;
        }
        double baked = 0.0;
        double relit = 0.0;
        std::size_t count = 0;
        std::size_t animated = 0;
        for (std::size_t vi = 0; vi < entry.originals.size() && vi < entry.light.size(); ++vi) {
            ++count;
            animated += vi < entry.animated.size() && core::at(entry.animated, vi) != 0 ? 1 : 0;
            baked += luma_of(core::at(entry.originals, vi));
            relit += luma_of(shown_argb(st.model, core::at(entry.light, vi), st.written_light));
        }
        const double each = static_cast<double>(std::max<std::size_t>(count, 1));
        std::string out = text::format("relit mesh: %s, %s, %zu vertices (%zu animated), mean baked %.1f relit %.1f",
                                       entry.analysed ? "analysed" : "not analysed",
                                       entry.written == 0 ? "not written"
                                                          : (entry.written == st.wanted_lighting ? "written" : "stale"),
                                       count, animated, baked / each, relit / each);
        // its first vertices' parts (a big quad's corners)
        constexpr std::size_t kShownVertices = 24;
        for (std::size_t vi = 0; vi < std::min(count, kShownVertices); ++vi) {
            const rt::RelitVertex& vertex = core::at(entry.light, vi);
            const rt::Vec3& at = core::at(entry.input.positions, vi);
            out += text::format("\n    corner %.0f %.0f %.0f n %.2f %.2f %.2f: baked %06X relit %06X held %06X, sun now %.2f baked %.2f, "
                                "sky %.2f, tint %.2f %.2f %.2f, lamps' light %.3g %.3g %.3g, emits %.3g %.3g %.3g%s",
                                static_cast<double>(rt::x_of(at)), static_cast<double>(rt::y_of(at)),
                                static_cast<double>(rt::z_of(at)), static_cast<double>(rt::x_of(vertex.normal)),
                                static_cast<double>(rt::y_of(vertex.normal)), static_cast<double>(rt::z_of(vertex.normal)),
                                core::at(entry.originals, vi) & 0xFFFFFFU,
                                shown_argb(st.model, vertex, st.written_light) & 0xFFFFFFU,
                                colour_held(entry, vi), static_cast<double>(vertex.sun), static_cast<double>(vertex.sun_baked),
                                static_cast<double>(vertex.sky), static_cast<double>(std::get<0>(vertex.tint)),
                                static_cast<double>(std::get<1>(vertex.tint)), static_cast<double>(std::get<2>(vertex.tint)),
                                static_cast<double>(std::get<0>(vertex.lamp)),
                                static_cast<double>(std::get<1>(vertex.lamp)), static_cast<double>(std::get<2>(vertex.lamp)),
                                static_cast<double>(std::get<0>(vertex.emit)), static_cast<double>(std::get<1>(vertex.emit)),
                                static_cast<double>(std::get<2>(vertex.emit)), emitting_lamp_text(entry, vi).c_str());
        }
        return out;
    }
    return "relit mesh: none kept for it (not relit - drawn with its own colours)";
}

std::string describe_near(const rt::Vec3& point, float radius)
{
    const State& st = state();
    if (st.status != Status::kRelit || !st.model.valid) {
        return std::string("relit: ") + status_name(st.status);
    }
    struct Near {
        float distance = 0.0F;
        const Entry* entry = nullptr;
        std::size_t vertex = 0;
    };
    constexpr std::size_t kShown = 6;
    std::vector<Near> nearest;
    std::size_t count = 0;
    double baked = 0.0;
    double relit = 0.0;
    double lamps = 0.0;
    double sunlit = 0.0;
    for (const VertexBufferData* buffer : st.order) {
        const Entry& entry = st.entries.at(buffer);
        const std::size_t vertices = std::min(entry.input.positions.size(), entry.light.size());
        for (std::size_t vi = 0; vi < vertices; ++vi) {
            const rt::RelitVertex& vertex = core::at(entry.light, vi);
            if (!vertex.used) {
                continue;
            }
            const rt::Vec3 apart = rt::minus(core::at(entry.input.positions, vi), point);
            const float distance = std::sqrt(rt::dot(apart, apart));
            if (!(distance <= radius)) {
                continue;
            }
            ++count;
            const uint32_t original = core::at(entry.originals, vi);
            baked += luma_of(original);
            relit += luma_of(shown_argb(st.model, vertex, st.written_light));
            lamps += (kLumaRed * std::get<0>(vertex.lamp)) + (kLumaGreen * std::get<1>(vertex.lamp)) +
                     (kLumaBlue * std::get<2>(vertex.lamp));
            sunlit += vertex.sun;
            nearest.push_back({.distance = distance, .entry = &entry, .vertex = vi});
            std::ranges::sort(nearest, {}, &Near::distance);
            if (nearest.size() > kShown) {
                nearest.pop_back();
            }
        }
    }
    const double each = static_cast<double>(std::max<std::size_t>(count, 1));
    std::string out = text::format("relit near %.1f %.1f %.1f (%.0f units): %zu vertices, mean baked %.1f relit %.1f, "
                                   "lamps' light %.3g, sun share %.2f; the light (global unit) sun %.3g sky %.3g, lamps %.2f",
                                   static_cast<double>(rt::x_of(point)), static_cast<double>(rt::y_of(point)),
                                   static_cast<double>(rt::z_of(point)), static_cast<double>(radius), count,
                                   baked / each, relit / each, lamps / each, sunlit / each,
                                   static_cast<double>(std::get<1>(st.written_light.sun)),
                                   static_cast<double>(std::get<1>(st.written_light.sky)),
                                   static_cast<double>(st.written_light.lamps));
    // (the one lighting path) what a thing standing here is lit by
    const rt::LightRatio here = rt::light_ratio_at(point);
    out += text::format("; the light field here: whole %.3f %.3f %.3f, indirect %.3f %.3f %.3f",
                        static_cast<double>(std::get<0>(here.whole)), static_cast<double>(std::get<1>(here.whole)),
                        static_cast<double>(std::get<2>(here.whole)), static_cast<double>(std::get<0>(here.indirect)),
                        static_cast<double>(std::get<1>(here.indirect)), static_cast<double>(std::get<2>(here.indirect)));
    for (const Near& near : nearest) {
        const rt::RelitVertex& vertex = core::at(near.entry->light, near.vertex);
        const uint32_t original = core::at(near.entry->originals, near.vertex);
        const rt::Vec3& at = core::at(near.entry->input.positions, near.vertex);
        out += text::format("\n  vertex %.1f %.1f %.1f (%.1f away) normal %.2f %.2f %.2f: baked %06X relit %06X held %06X, sun %.2f "
                            "sky %.2f bounce %.3g %.3g %.3g, tint %.2f %.2f %.2f, lamps' light %.3g %.3g %.3g, drawn "
                            "with %.3g (luma, the global unit)",
                            static_cast<double>(rt::x_of(at)), static_cast<double>(rt::y_of(at)),
                            static_cast<double>(rt::z_of(at)), static_cast<double>(near.distance),
                            static_cast<double>(rt::x_of(vertex.normal)), static_cast<double>(rt::y_of(vertex.normal)),
                            static_cast<double>(rt::z_of(vertex.normal)), original & 0xFFFFFFU,
                            shown_argb(st.model, vertex, st.written_light) & 0xFFFFFFU,
                            colour_held(*near.entry, near.vertex), static_cast<double>(vertex.sun),
                            static_cast<double>(vertex.sky),
                            static_cast<double>(std::get<0>(vertex.bounce)), static_cast<double>(std::get<1>(vertex.bounce)),
                            static_cast<double>(std::get<2>(vertex.bounce)), static_cast<double>(std::get<0>(vertex.tint)),
                            static_cast<double>(std::get<1>(vertex.tint)), static_cast<double>(std::get<2>(vertex.tint)),
                            static_cast<double>(std::get<0>(vertex.lamp)),
                            static_cast<double>(std::get<1>(vertex.lamp)), static_cast<double>(std::get<2>(vertex.lamp)),
                            static_cast<double>(light_luma(rt::relit_shown(st.model, vertex, st.written_light))));
        // (the night's light, decomposed) the lamps its L is made of: each link's lamp, its weight (lamp_geometry x the
        // bake's visibility, per unit of intensity) and its share of L now
        const LevelData& data = level_data(st.level);
        if (data.found && near.entry->data_mesh < data.data.meshes.size()) {
            const rt::RelitMeshLinks& links = core::at(data.data.meshes, near.entry->data_mesh).links;
            if (near.vertex + 1 < links.first.size()) {
                for (uint32_t k = core::at(links.first, near.vertex); k < core::at(links.first, near.vertex + 1); ++k) {
                    const rt::RelitLampLink& link = core::at(links.links, k);
                    if (link.lamp >= data.data.lamp_list.size()) {
                        continue;
                    }
                    const rt::RelitLamp& lamp = core::at(data.data.lamp_list, link.lamp);
                    const float on = link.lamp < st.lamps_on.size() ? core::at(st.lamps_on, link.lamp) : 1.0F;
                    out += text::format("\n    lamp %u at %.0f %.0f %.0f (kind %u, %s, %.0f lm): weight %.3g, its light %.3g, "
                                        "on %.2f - %.3g of L",
                                        link.lamp, static_cast<double>(rt::x_of(lamp.position)),
                                        static_cast<double>(rt::y_of(lamp.position)),
                                        static_cast<double>(rt::z_of(lamp.position)), static_cast<unsigned>(lamp.kind),
                                        rt::relit_lamp_source_name(static_cast<rt::RelitLampSource>(lamp.source)),
                                        static_cast<double>(lamp.lumens), static_cast<double>(link.weight),
                                        static_cast<double>(light_luma(lamp.light)), static_cast<double>(on),
                                        static_cast<double>(link.weight * on * light_luma(lamp.light)));
                }
            }
        }
    }
    return out;
}

} // namespace render::rt_relit_world
