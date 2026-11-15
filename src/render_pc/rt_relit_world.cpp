// NOT ORIGINAL - the relit mode in the world the game draws: see rt_relit_world.h.
#include "render_pc/rt_relit_world.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
constexpr float kMinElevation = 0.02F;        // a sun below this (sine of its elevation) lights nothing new: kept there
constexpr std::size_t kKilobyte = 1024;
constexpr double kDegrees = 180.0 / 3.14159265358979323846; // (the log's heading and elevation)
constexpr int kMaxSceneNodes = 256; // a scene list longer than this is taken as broken
constexpr float kRgbColourLimit = 4.0F;
constexpr float kWholeStrength = 0.999F; // (the light energy audit) relit at this strength or more: its colours are its parts'
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
    kNoSun,    // the bake shows no sun (the data says): the original's lighting
    kOptedOut, // the level's entry says so: the bake kept
    kNoModel,  // the bake's sunlight couldn't be told apart (the data says): the original's lighting
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
        return "its relit data was made from other meshes - the original's lighting (build it again: node "
               "tools/game/relit_bake.ts)";
    case Status::kNoSun:
        return "the bake shows no sun - the original's lighting";
    case Status::kOptedOut:
        return "the level's entry turns it off - the bake kept";
    case Status::kNoModel:
        return "the bake's sunlight couldn't be told apart - the original's lighting";
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
    std::vector<uint32_t> originals; // the baked colours
    std::vector<uint8_t> animated;   // a vertex whose colour the game animates: 1
    rt::RelitMesh input;
    rt::RelitVertices light;
    bool analysed = false;   // its light read from the level's data
    bool looked_up = false;  // looked for in the level's data (found: analysed)
    rt::Vec3 sun_for{};    // the sun its vertices' sun was traced toward
    float radius_for = rt::kRelitSunRadiusDegrees; // ... and the light's angular radius then (rt_daylight.h light_radius)
    rt::Vec3 bounce_for{}; // ... and the sun whose light its bounce was traced with
    uint64_t written = 0;  // the lighting its colours were written with (0: the originals)
    bool dirty = false;    // the game wrote its colours since: to be written again
    uint64_t listed = 0;   // the update that last put it in the order (State::listing)
    // the colours and own light its vertices show the bounce's rays (rebounce), and the light they were made for: made
    // once a relight, not every frame of it
    std::vector<uint32_t> hit_colours;
    std::vector<rt::Rgbf> hit_local;
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
    // (added by cel shading - engine/gfx/rt_cel.h) its decomposition as the cel stream, and what it was written for
    GpuVertexBuffer* cel = nullptr;
    uint64_t cel_written = 0;
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
    std::vector<rt::RelitLevel> levels;
    bool levels_read = false;
    rt::Vec3 sun{};
    float radius = rt::kRelitSunRadiusDegrees; // the light's angular radius (the lighting state's: the moon's softer)
    rt::Rgbf sun_colour{1.0F, 1.0F, 1.0F};
    bool sky_model = false; // (whether the sun's colour was the sky model's)
    uint64_t day_serial = 0; // (added by the time of day) the clock's light the colour was made for (0: none)
    rt::RelitLighting day_scales; // (added by the time of day) its scales (direct, sky, bounce)
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
    // (one relight at a time) the light field being made for the lighting `field_building_for`, over frames
    std::shared_ptr<rt::LightField> field_building;
    uint64_t field_building_for = 0;
    // (one relight at a time) the time a vertex takes to trace toward the sun, to trace the bounce and to write, as
    // measured (Pace), and whether the loading screen holds the first light now (hold_first_light: the relight hurried)
    Pace sun_pace;
    Pace bounce_pace;
    Pace write_pace;
    bool hurried = false;
    std::chrono::steady_clock::time_point frame_start{}; // (update's start this frame: the parts share kRelitFrameMs)
    Job job; // (the trace asked of the device, answered a frame or two later - one at a time)
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

// Whether an entry's bounce is still to trace in the bounce's pass under way.
bool bounce_stale(const Entry& entry)
{
    const State& st = state();
    return st.bounce_begun != st.bounce_finished && rt::dot(entry.bounce_for, st.bounce_sun) < kSameSun;
}

// Whether the bounce is due again: an entry's traced for a sun kBounceSameSun or further from the one now.
bool bounce_due(const Entry& entry) { return rt::dot(entry.bounce_for, state().sun) < kBounceSameSun; }

// A colour's brightness (0 - 255; A8R8G8B8).
float luma_of(uint32_t argb)
{
    return (kLumaRed * static_cast<float>((argb >> kRedShift) & kByteMask)) +
           (kLumaGreen * static_cast<float>((argb >> kGreenShift) & kByteMask)) +
           (kLumaBlue * static_cast<float>(argb & kByteMask));
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

// The per-level list: built in, then the user's file beside the exe.
const std::vector<rt::RelitLevel>& levels()
{
    State& st = state();
    if (!st.levels_read) {
        st.levels_read = true;
        if (!rt::parse_relit_levels(rt::built_in_relit_levels(), &st.levels)) {
            get_log("rt_relit: the built-in level list isn't valid JSON of its layout");
        }
        std::string text;
        const std::string path = std::string(build_info::executable_directory()) + "thug2-decomp-relit.json";
        if (fs::read_text(path, &text)) {
            const bool read = rt::parse_relit_levels(text, &st.levels);
            get_log("rt_relit: %s %s", read ? "read" : "couldn't read (not JSON of the layout)", path.c_str());
        }
    }
    return st.levels;
}

// The level's entry (the last one naming it wins), or null.
const rt::RelitLevel* level_entry(uint32_t level)
{
    const rt::RelitLevel* found = nullptr;
    for (const rt::RelitLevel& entry : levels()) {
        if (checksums::hash_from_text(entry.name.c_str()) == level) {
            found = &entry;
        }
    }
    return found;
}

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
    return fnv_value(hash, model.scale);
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
                "bounce %.3f %.3f %.3f lamps %.3f %.3f %.3f scale %.3f (%u lamps, %u sky rays, %u sun rays; margins: the "
                "baked sun %.2f, the model %.2f)",
                level, path.c_str(), rt::relit_verdict_name(data.data.verdict), data.data.meshes.size(),
                static_cast<double>(std::get<0>(model.sun)), static_cast<double>(std::get<1>(model.sun)),
                static_cast<double>(std::get<2>(model.sun)), static_cast<double>(std::get<0>(model.sky)),
                static_cast<double>(std::get<1>(model.sky)), static_cast<double>(std::get<2>(model.sky)),
                static_cast<double>(std::get<0>(model.bounce)), static_cast<double>(std::get<1>(model.bounce)),
                static_cast<double>(std::get<2>(model.bounce)), static_cast<double>(std::get<0>(model.lamps)),
                static_cast<double>(std::get<1>(model.lamps)), static_cast<double>(std::get<2>(model.lamps)),
                static_cast<double>(model.scale), data.data.lamps, data.data.sky_rays, data.data.sun_rays,
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
    // (the offline relit data's key: its positions, triangles and made vertices' sources - whatever the vertex format;
    // not its colours, which the game rewrites)
    entry.content = rt::relit_mesh_content(entry.input);
    *out = std::move(entry);
    return true;
}

// The entry's colours written into its buffer (and to the card): `colours` per vertex, the animated ones left alone.
void write_colours(Entry& entry, std::span<const uint32_t> colours)
{
    uint8_t* data = nullptr;
    entry.buffer->get_lock(0, 0, &data, 0);
    if (data == nullptr) {
        return;
    }
    const std::span<uint8_t> bytes(data, entry.buffer->size());
    for (std::size_t vi = 0; vi < colours.size() && vi < entry.originals.size(); ++vi) {
        if (core::at(entry.animated, vi) != 0) {
            continue;
        }
        const uint32_t colour = core::at(colours, vi);
        std::memcpy(bytes.subspan((vi * entry.stride) + entry.colour_offset, sizeof(colour)).data(), &colour,
                    sizeof(colour));
    }
    const rt_record::ColourWrite colours_only; // (the positions are as they were: no BLAS rebuilt)
    entry.buffer->get_unlock();
}

// (added by the light energy audit) the entry's light stream given to the ray tracing - or taken back (null): the passes
// that light a pixel again read what its colours were written with (rt_scene.h set_light_stream)
void publish_stream(Entry& entry, GpuVertexBuffer* stream)
{
    if (rt::RayTracing* tracer = rt::ray_tracing(); tracer != nullptr && entry.input.id != 0) {
        tracer->set_light_stream(entry.input.id, stream);
    }
}

void restore(Entry& entry)
{
    if (entry.written != 0) {
        publish_stream(entry, nullptr); // (the bake's colours: no parts of relit's)
        write_colours(entry, entry.originals);
        entry.written = 0;
        entry.cel_written = 0;
    }
}

// (added by cel shading) an entry's cel stream let go of
void release_cel(Entry& entry)
{
    publish_stream(entry, nullptr);
    if (entry.cel != nullptr) {
        gpu().release_vertex_buffer(entry.cel);
        entry.cel = nullptr;
    }
    entry.cel_written = 0;
}

void release_all_cel()
{
    bool any = false;
    for (auto& [buffer, entry] : state().entries) {
        any = any || entry.cel != nullptr;
        release_cel(entry);
    }
    if (any || rt::cel_world().relit) {
        rt::set_cel_world({});
    }
}

void restore_all()
{
    for (auto& [buffer, entry] : state().entries) {
        restore(entry);
    }
    release_all_cel(); // (added by cel shading: the stream goes with the relit colours)
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
    *toward = rt::scaled(*toward, 1.0F / length);
    if (rt::y_of(*toward) < kMinElevation) { // (at or under the horizon: held just above it)
        const float across =
            std::sqrt((rt::x_of(*toward) * rt::x_of(*toward)) + (rt::z_of(*toward) * rt::z_of(*toward)));
        const float keep = std::sqrt(1.0F - (kMinElevation * kMinElevation));
        *toward = across > 0.0F
                      ? rt::Vec3{rt::x_of(*toward) * keep / across, kMinElevation, rt::z_of(*toward) * keep / across}
                      : rt::Vec3{0.0F, 1.0F, 0.0F};
    }
    return true;
}

// The sun's colour over the bake's: the sky model's colour at the sun's elevation over its colour at the bake's sun's,
// when the sky model is on (rt_sky_model publishes a SunLight); else white.
rt::Rgbf sun_colour_for(const rt::Vec3& toward)
{
    // (added by the time of day) the clock's lighting state carries the light's colour with its strength (light_colour:
    // RelitLighting::direct_scale) - none here
    const rt::DayLight day = rt::day_light();
    if (day.on) {
        return {1.0F, 1.0F, 1.0F};
    }
    if (!rt::sun_light().on) {
        return {1.0F, 1.0F, 1.0F};
    }
    const std::array<float, 3> now =
        rt::sun_colour_at(std::asin(std::clamp(static_cast<double>(rt::y_of(toward)), 0.0, 1.0)));
    const std::array<float, 3> baked =
        rt::sun_colour_at(std::asin(std::clamp(static_cast<double>(rt::y_of(state().model.toward)), 0.0, 1.0)));
    rt::Rgbf out{};
    for (std::size_t ch = 0; ch < out.size(); ++ch) {
        core::at(out, ch) = core::at(baked, ch) > 0.0F
                                ? std::clamp(core::at(now, ch) / core::at(baked, ch), 0.0F, kRgbColourLimit)
                                : 1.0F;
    }
    return out;
}

// (added by the time of day) The lighting state's terms for the model's (rt_daylight.h DayLight - the clock's, read as
// they are): the direct light (its strength and colour), the sky's light and the bounce's. All 1 without the clock.
rt::RelitLighting day_scales(const rt::DayLight& day)
{
    rt::RelitLighting scales;
    if (!day.on) {
        return scales;
    }
    scales.direct_scale = day.light_colour;
    scales.sky_scale = day.sky;
    scales.bounce_scale = day.bounce;
    return scales;
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
        for (std::size_t vi = 0; vi < entry.light.size(); ++vi) {
            rt::RelitVertex& vertex = core::at(entry.light, vi);
            const rt::RelitVertex& stored = core::at(mesh.vertices, vi);
            vertex.sun = stored.sun_baked;
            vertex.sun_baked = stored.sun_baked;
            vertex.sky = stored.sky;
            vertex.bounce = stored.bounce;
            vertex.bounce_local = stored.bounce_local;
            vertex.tint = stored.tint;
            vertex.residual = stored.residual;
            vertex.lamp = stored.lamp;
            vertex.sun_missed = stored.sun_missed;
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
    if (!data.found) {
        return Status::kNoData;
    }
    if (data.data.verdict == rt::RelitVerdict::kNoSun) {
        return Status::kNoSun;
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
    return all > 0 && static_cast<float>(matched) < rt::kRelitDataLeastShare * static_cast<float>(all) ? Status::kStale
                                                                                                     : Status::kRelit;
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
    case Status::kNoSun:
    case Status::kNoModel:
        light = config::LevelLight::kNoSun;
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
        if (!st.entries.contains(mesh.source)) {
            Entry entry;
            if (!make_entry(mesh, &entry)) {
                continue;
            }
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
    st.field_lighting = 0;
    rt::set_light_field(nullptr); // (the one lighting path: the level before's field gone)
    restore_all();
    st.level = level;
    st.model = {};
    st.verdict = Status::kWaiting;
    st.lighting = 0;
    st.logged = Status::kOff;
    st.bounce_finished = st.bounce_begun; // (a bounce pass of the level before not taken on)
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
// kept in hit_colours meanwhile).
struct Gathered {
    std::vector<rt::RelitMesh> meshes;
    std::vector<rt::RelitVertices> light;
    std::vector<std::vector<rt::Rgbf>> local;
};

void gather(std::span<const VertexBufferData* const> order, bool bounce, Gathered* out)
{
    State& st = state();
    for (const VertexBufferData* buffer : order) {
        Entry& entry = st.entries.at(buffer);
        out->meshes.push_back(std::move(entry.input));
        out->light.push_back(std::move(entry.light));
        if (bounce) {
            std::swap(out->meshes.back().colours, entry.hit_colours);
            out->local.push_back(std::move(entry.hit_local));
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
            std::swap(entry.input.colours, entry.hit_colours); // (the bake's back; the hits' kept for the next frame)
            entry.hit_local = std::move(core::at(in->local, iv));
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
                            tracer, &parts.light, stats, parts.local, ask);
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
    if (!st.hurried) {
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
    start_trace(Job::Kind::kSun, paced_vertices(st.sun_pace, kRelitRetraceVertices, st.hurried), tracer, settings);
}

// The bounce traced again for entries whose bounce is another sun's - once every sun is traced, from the colours as
// relit now (one bounce of the new light) - this frame's paced share (at most kRelitBounceVertices vertices).
void rebounce(rt::RayTracing& tracer, const rt::RelitSettings& settings, const rt::RelitLighting& lit)
{
    // (added by the time of day) the hits' colours at the level's own strength: the bounce's scale is the clock's
    rt::RelitLighting lighting = lit;
    lighting.direct_scale = {1.0F, 1.0F, 1.0F};
    lighting.sky_scale = {1.0F, 1.0F, 1.0F};
    lighting.bounce_scale = {1.0F, 1.0F, 1.0F};
    State& st = state();
    if (st.job.kind != Job::Kind::kNone) { // (one trace in flight at a time)
        if (st.job.kind == Job::Kind::kBounce) {
            finish_job(tracer, settings);
        }
        return;
    }
    // the pass under way done, or a new one begun - toward the sun now, once every mesh's sun is traced for it and the
    // bounce is due (kBounceSameSun)
    if (st.bounce_begun != st.bounce_finished) {
        const bool left = std::ranges::any_of(st.order, [&st](const VertexBufferData* buffer) {
            return stale_for(Job::Kind::kBounce, st.entries.at(buffer));
        });
        if (!left) {
            st.bounce_finished = st.bounce_begun;
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
        ++st.bounce_begun;
        st.bounce_sun = st.sun;
    }
    // (the hits' colours for the pass - the same for every frame of it: made once a pass, over frames as the writes
    // are - kRelitFrameMs of them a frame while the level is shown -, before its first rays; before, every entry's were
    // made again each frame the bounce was traced: all the level's vertices a frame)
    uint64_t hits_key = fnv_value(kFnvBasis, lighting.toward);
    hits_key = fnv_value(hits_key, lighting.sun_colour);
    hits_key = fnv_value(hits_key, lighting.strength);
    hits_key = fnv_value(hits_key, lighting.residual);
    hits_key = model_hash(hits_key, st.model);
    hits_key = fnv_value(hits_key, st.bounce_begun); // (and made again for each pass: the bounce they show moved)
    hits_key = hits_key == 0 ? 1 : hits_key;
    const auto start = std::chrono::steady_clock::now();
    for (const VertexBufferData* buffer : st.order) {
        Entry& entry = st.entries.at(buffer);
        if (entry.hits_for == hits_key) {
            continue;
        }
        if (!st.hurried && ms_since(start) >= frame_left()) {
            return; // (the rest next frame)
        }
        // (the colours the rays bring back: as relit now)
        entry.hit_colours.resize(entry.originals.size());
        for (std::size_t vi = 0; vi < entry.hit_colours.size(); ++vi) {
            core::at(entry.hit_colours, vi) =
                vi < entry.light.size() && entry.analysed
                    ? rt::relit_colour(st.model, core::at(entry.light, vi), lighting, core::at(entry.originals, vi))
                    : core::at(entry.originals, vi);
        }
        entry.hit_local.assign(entry.input.positions.size(), rt::Rgbf{});
        for (std::size_t vi = 0; vi < entry.hit_local.size() && vi < entry.light.size(); ++vi) {
            core::at(entry.hit_local, vi) = lighting.residual && entry.analysed
                                                ? rt::relit_own_light(st.model, core::at(entry.light, vi))
                                                : rt::Rgbf{};
        }
        entry.hits_for = hits_key;
    }
    start_trace(Job::Kind::kBounce, paced_vertices(st.bounce_pace, kRelitBounceVertices, st.hurried), tracer,
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
void make_light_field(uint64_t lighting, const rt::RelitLighting& light)
{
    State& st = state();
    if (st.field_lighting == lighting) {
        return;
    }
    for (const VertexBufferData* buffer : st.order) {
        const Entry& entry = st.entries.at(buffer);
        if (entry.analysed && sun_stale(entry)) {
            return;
        }
    }
    const rt::RelitLighting own{.toward = st.model.toward};
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
        if (!entry.analysed || entry.field_for == lighting) {
            continue;
        }
        if (entry.field_lighting != lighting) { // (its vertices added from the first, for this field)
            entry.field_lighting = lighting;
            entry.field_done = 0;
        }
        const std::size_t vertices = std::min(entry.input.positions.size(), entry.light.size());
        for (std::size_t vi = entry.field_done; vi < vertices; ++vi) {
            if (!st.hurried && vi % kFieldTimed == 0 && ms_since(start) >= share) {
                entry.field_done = vi;
                return; // (the rest next frame - inside a mesh too)
            }
            const rt::RelitVertex& vertex = core::at(entry.light, vi);
            if (!vertex.used || (vi < entry.animated.size() && core::at(entry.animated, vi) != 0)) {
                continue;
            }
            rt::RelitVertex at_bake = vertex;
            at_bake.sun = vertex.sun_baked;
            const rt::RelitParts now = rt::relit_parts(st.model, vertex, light);
            const rt::RelitParts baked = rt::relit_parts(st.model, at_bake, own);
            rt::LightParts parts{.direct_now = now.direct,
                                 .indirect_now = now.indirect,
                                 .direct_own = baked.direct,
                                 .indirect_own = baked.indirect,
                                 .local = {},
                                 .normal = vertex.normal};
            if (light.residual) {
                parts.local = rt::relit_local_light(st.model, vertex);
            }
            st.field_building->add(core::at(entry.input.positions, vi), parts);
        }
        entry.field_for = lighting;
    }
    std::shared_ptr<rt::LightField> field = std::move(st.field_building);
    st.field_building_for = 0;
    field->set_local_scale(st.model.scale); // (what the local light shows on a tint of 1)
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
// not made for it yet. (A game write over relit colours - Entry::dirty - is written again on its own, and the bounce is
// a pass of its own: neither is a relight.)
bool relight_running()
{
    const State& st = state();
    if (st.wanted_lighting == 0) {
        return false;
    }
    for (const VertexBufferData* buffer : st.order) {
        const Entry& entry = st.entries.at(buffer);
        if (entry.analysed && (sun_stale(entry) || entry.written != st.wanted_lighting)) {
            return true;
        }
    }
    return st.field_lighting != st.wanted_lighting;
}

// The entries' relit colours written where they are stale (the lighting changed, the bounce traced again) and their
// sun is traced, and the change logged once per lighting.
void write_relit(uint64_t lighting, const rt::RelitLighting& light)
{
    State& st = state();
    std::size_t written = 0;
    // (added by the time of day) while the clock lights the level, a share of the vertices a frame: a new time's
    // colours spread over frames, as the original's time of day colours a part of the level a frame (paced: at most
    // kRelitWriteVertices)
    const bool budgeted = rt::day_light().on;
    const std::size_t paced = paced_vertices(st.write_pace, kRelitWriteVertices, st.hurried);
    std::size_t budget = paced;
    const auto start = std::chrono::steady_clock::now();
    std::vector<uint32_t> colours;
    struct Change {
        std::size_t vertices = 0;
        double baked = 0.0;
        double relit = 0.0;
        std::size_t moved = 0;
    } change;
    for (const VertexBufferData* buffer : st.order) {
        Entry& entry = st.entries.at(buffer);
        if (!entry.analysed || (entry.written == lighting && !entry.dirty) || sun_stale(entry)) {
            continue;
        }
        if (budgeted) {
            if (budget == 0) {
                break;
            }
            budget -= std::min(budget, entry.originals.size());
        }
        colours.resize(entry.originals.size());
        for (std::size_t vi = 0; vi < colours.size(); ++vi) {
            core::at(colours, vi) = vi < entry.light.size() ? rt::relit_colour(st.model, core::at(entry.light, vi),
                                                                               light, core::at(entry.originals, vi))
                                                            : core::at(entry.originals, vi);
        }
        write_colours(entry, colours);
        entry.written = lighting;
        entry.dirty = false;
        entry.cel_written = 0; // (its stream follows the colours - a bounce traced again too)
        ++written;
        for (std::size_t vi = 0; vi < colours.size(); ++vi) {
            const float was = luma_of(core::at(entry.originals, vi));
            const float now = luma_of(core::at(colours, vi));
            change.vertices += 1;
            change.baked += was;
            change.relit += now;
            change.moved += std::abs(now - was) > kChangedLuma ? 1 : 0;
        }
    }
    if (budgeted) {
        measured(&st.write_pace, ms_since(start), paced - budget);
    }
    // (added by the time of day: a running clock writes new colours twice a second - logged once a game hour)
    static double logged_hours = -1.0;
    const rt::DayLight day = rt::day_light();
    const bool log_it = !day.on || std::abs(day.hours - logged_hours) >= 1.0;
    if (written > 0 && lighting != st.lighting && !log_it) {
        st.lighting = lighting;
        rt::set_relit_status(status_text());
    } else if (written > 0 && lighting != st.lighting) {
        logged_hours = day.on ? day.hours : -1.0;
        st.lighting = lighting;
        const double vertices = static_cast<double>(std::max<std::size_t>(change.vertices, 1));
        get_log(
            "rt_relit: %zu meshes written (strength %.2f, residual %s): mean brightness %.1f against the bake's %.1f, "
            "%.1f%% of the vertices changed by more than %.0f",
            written, static_cast<double>(light.strength), light.residual ? "kept" : "left out", change.relit / vertices,
            change.baked / vertices, 100.0 * static_cast<double>(change.moved) / vertices,
            static_cast<double>(kChangedLuma));
        rt::set_relit_status(status_text());
    }
}

// (added by cel shading - engine/gfx/rt_cel.h) Each entry's cel stream written where its relit colours are (the same
// lighting, the cel settings' number with it), and the world's sun light published for the draws; with rt_cel off,
// the streams let go of.
void write_cel(uint64_t lighting, const rt::RelitLighting& light)
{
    State& st = state();
    // (added by the light energy audit) the stream is the relit world's light in its parts: the cel draws read it, and so
    // do the ray-traced passes that light a pixel again (rt_scene.h set_light_stream) - written while either can
    if (!rt::cel_settings().on && rt::ray_tracing() == nullptr) {
        release_all_cel();
        return;
    }
    // (a blend with the bake - strength under 1 - isn't what the parts say: those colours keep the passes' old ways)
    const bool parts_drawn = light.strength >= kWholeStrength;
    uint64_t wanted = fnv_value(lighting, rt::cel_settings_serial());
    wanted = wanted == 0 ? 1 : wanted;
    std::size_t written = 0;
    for (const VertexBufferData* buffer : st.order) {
        Entry& entry = st.entries.at(buffer);
        if (!entry.analysed || entry.written != lighting || entry.cel_written == wanted) {
            continue;
        }
        const auto count = static_cast<uint32_t>(entry.originals.size());
        if (count == 0) {
            continue;
        }
        if (entry.cel == nullptr &&
            gpu().create_vertex_buffer(count * rt::kCelStreamBytes, gpu_value::kUsageWriteOnly, 0, gpu_value::kPoolDefault,
                                       &entry.cel) < 0) {
            entry.cel = nullptr;
            continue;
        }
        void* locked = nullptr;
        if (gpu().lock_vertex_buffer(entry.cel, 0, 0, &locked, 0) < 0 || locked == nullptr) {
            continue;
        }
        const std::span<uint8_t> bytes(static_cast<uint8_t*>(locked), std::size_t{count} * rt::kCelStreamBytes);
        for (std::size_t vi = 0; vi < count; ++vi) {
            const rt::CelWords words = vi < entry.light.size() && core::at(entry.animated, vi) == 0
                                           ? rt::cel_stream_words(st.model, core::at(entry.light, vi), light)
                                           : rt::kCelNoStream;
            std::memcpy(bytes.subspan(vi * rt::kCelStreamBytes, rt::kCelStreamBytes).data(), words.data(),
                        rt::kCelStreamBytes);
        }
        gpu().unlock_vertex_buffer(entry.cel);
        entry.cel_written = wanted;
        publish_stream(entry, parts_drawn ? entry.cel : nullptr);
        ++written;
    }
    rt::Rgbf sun{};
    for (std::size_t ch = 0; ch < sun.size(); ++ch) {
        core::at(sun, ch) = core::at(st.model.sun, ch) * core::at(light.sun_colour, ch) * core::at(light.direct_scale, ch);
    }
    rt::set_cel_world({.relit = true, .sun = sun, .toward = light.toward});
    if (written > 0) {
        get_log("rt_relit: %zu meshes' light streams written (the sun's light %.3f %.3f %.3f)", written,
                static_cast<double>(std::get<0>(sun)), static_cast<double>(std::get<1>(sun)),
                static_cast<double>(std::get<2>(sun)));
    }
}

} // namespace

void update(std::span<const WorldMesh> world, bool stable, rt::RayTracing& tracer)
{
    State& st = state();
    st.frame_start = std::chrono::steady_clock::now();
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
    rt::set_baked_sun_wanted(true);
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
    // (added by the scene's sun) whether rt_sun_source=game may light this level by the game's sun: its entry may keep
    // the baked one; the mode says it relights the level once the model is found (rt_scene.h scene_sun)
    const bool keeps_baked_sun = entry_of_level != nullptr && entry_of_level->keeps_baked_sun;
    if (entry_of_level != nullptr && entry_of_level->off) {
        restore_all();
        log_status(Status::kOptedOut);
        rt::set_sun_relight({.relights = false, .level_keeps_bake = keeps_baked_sun});
        return;
    }
    if (st.order.empty()) {
        return;
    }
    log_status(Status::kRelit);
    rt::set_sun_relight({.relights = true, .level_keeps_bake = keeps_baked_sun});
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
                       sky_model != st.sky_model || day_serial != st.day_serial; // (the sky model publishes late)
    if (newer && !relight_running()) {
        ++st.relights;
        st.sky_model = sky_model;
        st.day_serial = day_serial;
        st.day_scales = day_scales(day);
        st.sun = toward;
        st.radius = day.on ? day.light_radius : rt::kRelitSunRadiusDegrees;
        st.sun_colour = sun_colour_for(toward);
        // (a running clock moves the sun twice a second: logged once a game hour of it)
        static double logged_hours = -1.0;
        if (!day.on || std::abs(day.hours - logged_hours) >= 1.0) {
            logged_hours = day.on ? day.hours : -1.0;
            get_log("rt_relit: the sun toward %.3f %.3f %.3f (colour x %.2f %.2f %.2f; strength x %.2f, sky x %.2f)",
                    static_cast<double>(rt::x_of(toward)), static_cast<double>(rt::y_of(toward)),
                    static_cast<double>(rt::z_of(toward)), static_cast<double>(std::get<0>(st.sun_colour)),
                    static_cast<double>(std::get<1>(st.sun_colour)), static_cast<double>(std::get<2>(st.sun_colour)),
                    static_cast<double>(std::get<0>(st.day_scales.direct_scale)),
                    static_cast<double>(std::get<1>(st.day_scales.sky_scale)));
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
    const float strength =
        entry_of_level != nullptr && entry_of_level->strength >= 0.0F ? entry_of_level->strength : settings.strength;
    uint64_t lighting = fnv_value(kFnvBasis, st.sun);
    lighting = fnv_value(lighting, st.sun_colour);
    lighting = fnv_value(lighting, st.radius);
    lighting = fnv_value(lighting, strength);
    lighting = fnv_value(lighting, settings.residual);
    lighting = model_hash(lighting, st.model);
    lighting = fnv_value(lighting, st.bounce_finished); // (one relight at a time: a bounce pass done - written again)
    if (st.day_serial != 0) { // (added by the time of day)
        lighting = fnv_value(lighting, st.day_scales.direct_scale);
        lighting = fnv_value(lighting, st.day_scales.sky_scale);
        lighting = fnv_value(lighting, st.day_scales.bounce_scale);
    }
    lighting = lighting == 0 ? 1 : lighting;
    const rt::RelitLighting light{.toward = st.sun,
                                  .sun_colour = st.sun_colour,
                                  .strength = strength,
                                  .residual = settings.residual,
                                  .direct_scale = st.day_scales.direct_scale,
                                  .sky_scale = st.day_scales.sky_scale,
                                  .bounce_scale = st.day_scales.bounce_scale};
    {
        const frame_profile::Scope phase("relit_rebounce");
        rebounce(tracer, settings, light);
    }
    st.written_light = light;
    st.wanted_lighting = lighting;
    {
        const frame_profile::Scope phase("relit_write");
        write_relit(lighting, light);
    }
    {
        const frame_profile::Scope phase("relit_field");
        make_light_field(lighting, light); // (the one lighting path)
    }
    write_cel(lighting, light); // (added by cel shading)
    rt::note_sun_used(rt::SunUser::kRelit, true, st.sun);
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
    return true;
}

GpuVertexBuffer* cel_stream(const VertexBufferData* buffer)
{
    const State& st = state();
    const auto found = st.entries.find(buffer);
    return found != st.entries.end() && found->second.cel_written != 0 ? found->second.cel : nullptr;
}

void buffer_released(const VertexBufferData* buffer)
{
    State& st = state();
    const auto entry = st.entries.find(buffer);
    if (entry == st.entries.end()) {
        return;
    }
    release_cel(entry->second); // (added by cel shading)
    std::erase_if(st.buffer_of, [buffer](const auto& kept) { return kept.second == buffer; });
    st.entries.erase(entry);
    std::erase(st.order, buffer);
}

void buffer_written(const VertexBufferData* buffer)
{
    State& st = state();
    const auto entry = st.entries.find(buffer);
    if (entry != st.entries.end() && entry->second.written != 0) {
        entry->second.dirty = true;
    }
}

void mesh_released(const MeshRef* mesh)
{
    State& st = state();
    const auto found = st.buffer_of.find(mesh);
    if (found == st.buffer_of.end()) {
        return;
    }
    const VertexBufferData* buffer = found->second;
    st.buffer_of.erase(found);
    const auto entry = st.entries.find(buffer);
    if (entry != st.entries.end() && entry->second.mesh == mesh) {
        release_cel(entry->second); // (added by cel shading)
        st.entries.erase(entry);
        std::erase(st.order, buffer);
    }
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
    const bool coloured = st.sun_colour != rt::Rgbf{1.0F, 1.0F, 1.0F} || st.day_serial != 0;
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
    out->key_colour = st.sun_colour;
    for (std::size_t ch = 0; ch < 3; ++ch) {
        core::at(out->key_colour, ch) *= core::at(st.day_scales.direct_scale, ch);
    }
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
        const long byte = std::lround(std::clamp(core::at(ratio.whole, ch) * kOne, 0.0F, kMostByte));
        core::at(*out, ch) = static_cast<uint8_t>(byte);
        same = same && byte == static_cast<long>(kOne);
    }
    core::at(*out, 3) = static_cast<uint8_t>(kOne);
    return !same; // (the light there the bake's: drawn as the original draws it)
}

namespace {

bool first_light_held(bool loading)
{
    struct Hold {
        bool armed = false;
        bool holding = false;
        std::chrono::steady_clock::time_point since{};
        uint32_t frames = 0;
        uint32_t steady = 0; // frames in a row the lighting has been ready
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
        hold.steady = 0;
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
    case Status::kNoSun:
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
    // (ready for kFirstLightSteadyFrames frames in a row: the night state's scripts show their meshes over the frames
    // after the clock sets it, each relit as it comes)
    hold.steady = relit_ready && day_clock::light_ready() ? hold.steady + 1 : 0;
    const bool ready = hold.steady >= kFirstLightSteadyFrames;
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
        if (entry.analysed && (entry.written != st.wanted_lighting || entry.dirty || sun_stale(entry) ||
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
    const auto found = st.entries.find(buffer);
    if (found == st.entries.end() || found->second.written == 0) {
        return {};
    }
    return found->second.originals;
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
    return text::format("relit: %s (setting %s, strength %.2f, residual %s); level %08x, %zu meshes (%zu written), %zu "
                        "vertices; sun %.3f %.3f %.3f; model sun %.3f sky %.3f bounce %.3f scale %.3f error %.3f; "
                        "%zu vertices' sun and %zu vertices' bounce traced again",
                        status_name(st.status), settings.on ? "on" : "off", static_cast<double>(settings.strength),
                        settings.residual ? "kept" : "left out", st.level, st.entries.size(), written, vertices,
                        static_cast<double>(rt::x_of(st.sun)), static_cast<double>(rt::y_of(st.sun)),
                        static_cast<double>(rt::z_of(st.sun)), static_cast<double>(std::get<1>(st.model.sun)),
                        static_cast<double>(std::get<1>(st.model.sky)),
                        static_cast<double>(std::get<1>(st.model.bounce)), static_cast<double>(st.model.scale),
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
        return st.status == Status::kRelit ? "not relit: not met yet" : std::string("kept bake: ") + status_name(st.status);
    }
    const Entry& entry = found->second;
    if (st.status != Status::kRelit) {
        return std::string("kept bake: ") + status_name(st.status);
    }
    if (!entry.analysed) {
        return entry.looked_up ? "not relit: not in the level's relit data" : "relit: not looked up yet";
    }
    if (entry.written == 0) {
        return "relit: not written yet";
    }
    const bool animated = std::ranges::any_of(entry.animated, [](uint8_t a) { return a != 0; });
    if (entry.cel_written == 0) {
        return animated ? "relit: no light stream (some vertices animated)" : "relit: no light stream";
    }
    return animated ? "relit: light stream (some vertices animated - those keep their colour)" : "relit: light stream";
}

std::string census_status()
{
    const State& st = state();
    return text::format("relit %s; model sun %.2f %.2f %.2f, sky %.2f %.2f %.2f, bounce %.2f %.2f %.2f, lamps %.2f %.2f "
                        "%.2f, scale %.2f, %zu entries",
                        status_name(st.status), static_cast<double>(std::get<0>(st.model.sun)),
                        static_cast<double>(std::get<1>(st.model.sun)), static_cast<double>(std::get<2>(st.model.sun)),
                        static_cast<double>(std::get<0>(st.model.sky)), static_cast<double>(std::get<1>(st.model.sky)),
                        static_cast<double>(std::get<2>(st.model.sky)),
                        static_cast<double>(std::get<0>(st.model.bounce)),
                        static_cast<double>(std::get<1>(st.model.bounce)),
                        static_cast<double>(std::get<2>(st.model.bounce)),
                        static_cast<double>(std::get<0>(st.model.lamps)), static_cast<double>(std::get<1>(st.model.lamps)),
                        static_cast<double>(std::get<2>(st.model.lamps)), static_cast<double>(st.model.scale),
                        st.entries.size());
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
            relit += luma_of(rt::relit_colour(st.model, core::at(entry.light, vi), st.written_light,
                                              core::at(entry.originals, vi)));
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
                                "sky %.2f, tint %.2f %.2f %.2f, residual %.3f %.3f %.3f, missed %.2f",
                                static_cast<double>(rt::x_of(at)), static_cast<double>(rt::y_of(at)),
                                static_cast<double>(rt::z_of(at)), static_cast<double>(rt::x_of(vertex.normal)),
                                static_cast<double>(rt::y_of(vertex.normal)), static_cast<double>(rt::z_of(vertex.normal)),
                                core::at(entry.originals, vi) & 0xFFFFFFU,
                                rt::relit_colour(st.model, vertex, st.written_light, core::at(entry.originals, vi)) & 0xFFFFFFU,
                                colour_held(entry, vi), static_cast<double>(vertex.sun), static_cast<double>(vertex.sun_baked),
                                static_cast<double>(vertex.sky), static_cast<double>(std::get<0>(vertex.tint)),
                                static_cast<double>(std::get<1>(vertex.tint)), static_cast<double>(std::get<2>(vertex.tint)),
                                static_cast<double>(std::get<0>(vertex.residual)),
                                static_cast<double>(std::get<1>(vertex.residual)),
                                static_cast<double>(std::get<2>(vertex.residual)), static_cast<double>(vertex.sun_missed));
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
    double residual = 0.0;
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
            relit += luma_of(rt::relit_colour(st.model, vertex, st.written_light, original));
            residual += (kLumaRed * std::get<0>(vertex.residual)) + (kLumaGreen * std::get<1>(vertex.residual)) +
                        (kLumaBlue * std::get<2>(vertex.residual));
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
                                   "residual %.3f, sun share %.2f; scales direct %.3f sky %.3f bounce %.3f",
                                   static_cast<double>(rt::x_of(point)), static_cast<double>(rt::y_of(point)),
                                   static_cast<double>(rt::z_of(point)), static_cast<double>(radius), count,
                                   baked / each, relit / each, residual / each, sunlit / each,
                                   static_cast<double>(std::get<1>(st.written_light.direct_scale)),
                                   static_cast<double>(std::get<1>(st.written_light.sky_scale)),
                                   static_cast<double>(std::get<1>(st.written_light.bounce_scale)));
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
                            "sky %.2f bounce %.3f %.3f %.3f, tint %.2f %.2f %.2f, residual %.3f %.3f %.3f, missed sun %.2f",
                            static_cast<double>(rt::x_of(at)), static_cast<double>(rt::y_of(at)),
                            static_cast<double>(rt::z_of(at)), static_cast<double>(near.distance),
                            static_cast<double>(rt::x_of(vertex.normal)), static_cast<double>(rt::y_of(vertex.normal)),
                            static_cast<double>(rt::z_of(vertex.normal)), original & 0xFFFFFFU,
                            rt::relit_colour(st.model, vertex, st.written_light, original) & 0xFFFFFFU,
                            colour_held(*near.entry, near.vertex), static_cast<double>(vertex.sun),
                            static_cast<double>(vertex.sky),
                            static_cast<double>(std::get<0>(vertex.bounce)), static_cast<double>(std::get<1>(vertex.bounce)),
                            static_cast<double>(std::get<2>(vertex.bounce)), static_cast<double>(std::get<0>(vertex.tint)),
                            static_cast<double>(std::get<1>(vertex.tint)), static_cast<double>(std::get<2>(vertex.tint)),
                            static_cast<double>(std::get<0>(vertex.residual)),
                            static_cast<double>(std::get<1>(vertex.residual)),
                            static_cast<double>(std::get<2>(vertex.residual)),
                            static_cast<double>(vertex.sun_missed));
    }
    return out;
}

} // namespace render::rt_relit_world
