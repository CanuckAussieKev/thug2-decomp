// NOT ORIGINAL - the relit mode in the world the game draws: see rt_relit_world.h.
#include "render_pc/rt_relit_world.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
#include "engine/file/disk.h"
#include "engine/file/disk_files.h"
#include "engine/gfx/engine_parts.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/mesh_records.h"
#include "engine/gfx/rt_relit.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/rt_sky.h"
#include "engine/gfx/rt_sun.h"
#include "engine/memory/arena_alloc.h"
#include "platform/platform_config.h"
#include "platform/run_log.h"
#include "render_pc/rt_record.h"
#include "render_pc/sub_mesh.h"
#include "render_pc/vertex_buffers.h"

namespace render::rt_relit_world {

namespace {

constexpr uint64_t kFnvBasis = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;
constexpr float kSameSun = 0.99999F;          // two suns this close (their directions' dot product): the same
constexpr float kMinElevation = 0.02F;        // a sun below this (sine of its elevation) lights nothing new: kept there
constexpr uint32_t kCacheMagic = 0x4C523254U; // "T2RL"
constexpr uint32_t kCacheVersion = 3;
constexpr std::size_t kKilobyte = 1024;
constexpr int kMaxSceneNodes = 256; // a scene list longer than this is taken as broken
constexpr std::size_t kVertexFloats =
    15; // a vertex in the cache: normal 3, sun, sky, bounce 3, tint 3, residual 3, used
constexpr std::size_t kModelFloats = 17; // the model: valid, toward 3, sun 3, sky 3, bounce 3, scale, error, vertices,
                                         // the sun's need
constexpr float kRgbColourLimit = 4.0F;
constexpr float kChangedLuma = 2.0F; // (the log: a vertex changed by more than this, of 255)
constexpr float kLumaRed = 0.299F;
constexpr float kLumaGreen = 0.587F;
constexpr float kLumaBlue = 0.114F;
constexpr int kRedShift = 16;
constexpr int kGreenShift = 8;
constexpr uint32_t kByteMask = 0xFF; // the sky model's colour ratio kept within this

enum class Status : uint8_t {
    kOff,
    kWaiting,  // for a still world and the baked sun
    kNoSun,    // the bake shows no sun: kept
    kOptedOut, // the level's entry says so: kept
    kNoModel,  // the fit found no sun term: kept
    kRelit,
};

const char* status_name(Status status)
{
    switch (status) {
    case Status::kOff:
        return "off";
    case Status::kWaiting:
        return "waiting for a still world and its baked sun";
    case Status::kNoSun:
        return "the bake shows no sun - the bake kept";
    case Status::kOptedOut:
        return "the level's entry turns it off - the bake kept";
    case Status::kNoModel:
        return "the fit found no sunlight in the bake - the bake kept";
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
    uint64_t content = 0;            // a hash of its vertices and indices (the cache's key)
    std::vector<uint32_t> originals; // the baked colours
    std::vector<uint8_t> animated;   // a vertex whose colour the game animates: 1
    rt::RelitMesh input;
    rt::RelitVertices light;
    bool analysed = false;
    rt::Vec3 sun_for{};    // the sun its vertices' sun was traced toward
    rt::Vec3 bounce_for{}; // ... and the sun whose light its bounce was traced with
    uint64_t written = 0;  // the lighting its colours were written with (0: the originals)
    bool dirty = false;    // its bounce traced again since: to be written
    uint64_t listed = 0;   // the update that last put it in the order (State::listing)
};

struct Cached {
    std::unordered_map<uint64_t, rt::RelitVertices> meshes;
    rt::RelitModel model;
    bool loaded = false;
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
    rt::Rgbf sun_colour{1.0F, 1.0F, 1.0F};
    bool sky_model = false; // (whether the sun's colour was the sky model's)
    uint64_t lighting = 0;
    uint64_t wanted_lighting = 0; // (added by the scene's sun) the lighting the colours are written with now
    Cached cache;
    std::size_t retraced = 0;
    std::size_t rebounced = 0;
};

State& state()
{
    static core::NoDestroy<State> made;
    return made.get();
}

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

std::string cache_path(uint32_t level)
{
    return text::format("%sthug2-decomp-relit\\%08x.bin", build_info::executable_directory(), level);
}

// What decides the analysis's results besides the meshes themselves.
uint64_t cache_key(const rt::RelitSettings& settings)
{
    uint64_t key = fnv_value(kFnvBasis, kCacheVersion);
    key = fnv_value(key, settings.sky_rays);
    key = fnv_value(key, settings.sun_rays);
    key = fnv_value(key, settings.detail);
    key = fnv_value(key, rt::kRelitSmoothPasses);
    key = fnv_value(key, rt::kRelitMaxTint);
    return key;
}

// --- the cache file: the magic, the version, the key, the model, then each mesh (its hash, its count, its vertices) ---
class Writer {
public:
    template <typename T>
    void put(const T& value)
    {
        std::array<char, sizeof(T)> bytes{};
        std::memcpy(bytes.data(), &value, sizeof(T));
        text_.append(bytes.data(), bytes.size());
    }
    [[nodiscard]] const std::string& text() const { return text_; }

private:
    std::string text_;
};

class Reader {
public:
    explicit Reader(std::string_view text) : text_(text) {}
    template <typename T>
    bool get(T* value)
    {
        if (at_ + sizeof(T) > text_.size()) {
            return false;
        }
        const std::string_view bytes = text_.substr(at_, sizeof(T));
        std::memcpy(value, bytes.data(), bytes.size());
        at_ += sizeof(T);
        return true;
    }

private:
    std::string_view text_;
    std::size_t at_ = 0;
};

// A run of floats written and read in order (the cache's model and vertices).
class Floats {
public:
    void put(float value) { values_.push_back(value); }
    void put(const rt::Vec3& value)
    {
        for (const float part : value) {
            put(part);
        }
    }
    [[nodiscard]] const std::vector<float>& values() const { return values_; }

private:
    std::vector<float> values_;
};

class FloatCursor {
public:
    explicit FloatCursor(std::span<const float> values) : values_(values) {}
    float next() { return at_ < values_.size() ? core::at(values_, at_++) : 0.0F; }
    rt::Vec3 next3()
    {
        const float first = next();
        const float second = next();
        return {first, second, next()};
    }

private:
    std::span<const float> values_;
    std::size_t at_ = 0;
};

std::array<float, kModelFloats> model_floats(const rt::RelitModel& model)
{
    Floats floats;
    floats.put(model.valid ? 1.0F : 0.0F);
    floats.put(model.toward);
    floats.put(model.sun);
    floats.put(model.sky);
    floats.put(model.bounce);
    floats.put(model.scale);
    floats.put(model.error);
    floats.put(static_cast<float>(model.vertices));
    floats.put(model.sun_needed);
    std::array<float, kModelFloats> out{};
    std::ranges::copy(floats.values(), out.begin());
    return out;
}

rt::RelitModel model_of(const std::array<float, kModelFloats>& values)
{
    FloatCursor cursor(values);
    rt::RelitModel model;
    model.valid = cursor.next() != 0.0F;
    model.toward = cursor.next3();
    model.sun = cursor.next3();
    model.sky = cursor.next3();
    model.bounce = cursor.next3();
    model.scale = cursor.next();
    model.error = cursor.next();
    model.vertices = static_cast<std::size_t>(cursor.next());
    model.sun_needed = cursor.next();
    return model;
}

std::array<float, kVertexFloats> vertex_floats(const rt::RelitVertex& vertex)
{
    Floats floats;
    floats.put(vertex.normal);
    floats.put(vertex.sun);
    floats.put(vertex.sky);
    floats.put(vertex.bounce);
    floats.put(vertex.tint);
    floats.put(vertex.residual);
    floats.put(vertex.used ? 1.0F : 0.0F);
    std::array<float, kVertexFloats> out{};
    std::ranges::copy(floats.values(), out.begin());
    return out;
}

rt::RelitVertex vertex_of(const std::array<float, kVertexFloats>& values)
{
    FloatCursor cursor(values);
    rt::RelitVertex vertex;
    vertex.normal = cursor.next3();
    vertex.sun = cursor.next();
    vertex.sky = cursor.next();
    vertex.bounce = cursor.next3();
    vertex.tint = cursor.next3();
    vertex.residual = cursor.next3();
    vertex.used = cursor.next() != 0.0F;
    return vertex;
}

struct CacheId {
    uint32_t level = 0;
    uint64_t key = 0;
};

Cached read_cache(const CacheId& id)
{
    Cached cached;
    std::string text;
    if (!fs::read_text(cache_path(id.level), &text)) {
        return cached;
    }
    Reader reader(text);
    uint32_t magic = 0;
    uint32_t version = 0;
    uint64_t stored_key = 0;
    std::array<float, kModelFloats> model{};
    uint32_t meshes = 0;
    if (!reader.get(&magic) || !reader.get(&version) || !reader.get(&stored_key) || magic != kCacheMagic ||
        version != kCacheVersion || stored_key != id.key || !reader.get(&model) || !reader.get(&meshes)) {
        return cached;
    }
    for (uint32_t mi = 0; mi < meshes; ++mi) {
        uint64_t content = 0;
        uint32_t count = 0;
        if (!reader.get(&content) || !reader.get(&count)) {
            return {};
        }
        rt::RelitVertices vertices(count);
        for (rt::RelitVertex& vertex : vertices) {
            std::array<float, kVertexFloats> floats{};
            if (!reader.get(&floats)) {
                return {};
            }
            vertex = vertex_of(floats);
        }
        cached.meshes.emplace(content, std::move(vertices));
    }
    cached.model = model_of(model);
    cached.loaded = true;
    return cached;
}

void write_cache(const CacheId& id)
{
    const State& st = state();
    Writer writer;
    writer.put(kCacheMagic);
    writer.put(kCacheVersion);
    writer.put(id.key);
    writer.put(model_floats(st.model));
    std::vector<const Entry*> analysed;
    for (const auto& [buffer, entry] : st.entries) {
        if (entry.analysed) {
            analysed.push_back(&entry);
        }
    }
    writer.put(static_cast<uint32_t>(analysed.size()));
    for (const Entry* entry : analysed) {
        writer.put(entry->content);
        writer.put(static_cast<uint32_t>(entry->light.size()));
        for (const rt::RelitVertex& vertex : entry->light) {
            writer.put(vertex_floats(vertex));
        }
    }
    const std::string folder = std::string(build_info::executable_directory()) + "thug2-decomp-relit";
    fs::disk().make_folders(folder.c_str());
    const fs::Written written = fs::write_text(cache_path(id.level), writer.text());
    get_log("rt_relit: %s %s (%zu meshes, %zu KB)", written.written && written.closed ? "wrote" : "couldn't write",
            cache_path(id.level).c_str(), analysed.size(), writer.text().size() / kKilobyte);
}

// --- the meshes ------------------------------------------------------------------------------------------------------

// An entry for a world mesh read from its buffer's copy, or false: nothing the mode can relight (no colours, no copy
// of its vertices or indices, skinned).
bool make_entry(const WorldMesh& world, Entry* out)
{
    const rt::Geometry& geometry = *world.geometry;
    const MeshRef* mesh = world.mesh;
    VertexBuffer* buffer = mesh->vertex_store(mesh->current_vertex_buffer_);
    if (mesh->vertex_buffer_count() > 1) {
        // (the buffers take turns: the one the geometry was drawn from)
        for (uint32_t iv = 0; iv < mesh->vertex_buffer_count(); ++iv) {
            if (mesh->vertex_store(iv) == world.source) {
                buffer = mesh->vertex_store(iv);
            }
        }
    }
    if (buffer == nullptr || buffer != world.source || buffer->data() == nullptr || geometry.dynamic ||
        geometry.cpu_indices == nullptr || geometry.vertex_stride == 0 ||
        geometry.colour_format != rt::kColourA8R8G8B8 ||
        geometry.colour_offset + sizeof(uint32_t) > geometry.vertex_stride) {
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
    // the hash: the vertices and the indices as the buffers hold them
    entry.content = fnv(fnv_value(kFnvBasis, stride), bytes);
    entry.content = fnv(entry.content, indices.subspan(std::size_t{geometry.index_offset} * index_bytes));
    entry.content = fnv_value(entry.content, geometry.strip);
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

void restore(Entry& entry)
{
    if (entry.written != 0) {
        write_colours(entry, entry.originals);
        entry.written = 0;
    }
}

void restore_all()
{
    for (auto& [buffer, entry] : state().entries) {
        restore(entry);
    }
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

// The new entries analysed: from the cache where it has them, the rest traced (the model fitted first when the level
// has none yet), then the cache written.
void analyse(rt::RayTracing& tracer, const rt::RelitSettings& settings)
{
    State& st = state();
    const auto start = std::chrono::steady_clock::now();
    const uint64_t key = cache_key(settings);
    if (settings.cache && !st.cache.loaded) {
        st.cache = read_cache({.level = st.level, .key = key});
        st.cache.loaded = true;
    }
    std::size_t from_cache = 0;
    for (const VertexBufferData* buffer : st.order) {
        Entry& entry = st.entries.at(buffer);
        if (entry.analysed) {
            continue;
        }
        const auto found = st.cache.meshes.find(entry.content);
        if (found != st.cache.meshes.end() && found->second.size() == entry.input.positions.size()) {
            entry.light = found->second;
            entry.analysed = true;
            entry.sun_for = st.cache.model.toward;
            entry.bounce_for = st.cache.model.toward;
            ++from_cache;
        }
    }
    const bool cached_model = from_cache > 0 && st.cache.model.valid && !st.model.valid;
    if (cached_model) {
        st.model = st.cache.model;
    }
    // the meshes, in the world's order (moved out of the entries for the call, back after)
    std::vector<rt::RelitMesh> meshes;
    std::vector<rt::RelitVertices> light;
    std::vector<uint8_t> wanted;
    for (const VertexBufferData* buffer : st.order) {
        Entry& entry = st.entries.at(buffer);
        meshes.push_back(std::move(entry.input));
        light.push_back(std::move(entry.light));
        wanted.push_back(entry.analysed ? 0 : 1);
    }
    const auto to_trace = static_cast<std::size_t>(std::ranges::count(wanted, uint8_t{1}));
    const rt::RelitParams params{.sky_rays = settings.sky_rays,
                                 .sun_rays = settings.sun_rays,
                                 .sun_radius_degrees = rt::kRelitSunRadiusDegrees,
                                 .detail = settings.detail};
    rt::RelitStats stats;
    if (to_trace > 0) {
        const rt::Vec3 toward = st.model.valid ? st.model.toward : rt::baked_sun().toward;
        rt::trace_relit_light(meshes, wanted, toward, params, tracer, &light, &stats);
        const bool first = !st.model.valid;
        if (first) {
            st.model = rt::fit_relit_model(meshes, light, toward);
        }
        rt::decompose_relit(meshes, wanted, params, &st.model, &light, first);
    }
    for (std::size_t iv = 0; iv < st.order.size(); ++iv) {
        Entry& entry = st.entries.at(core::at(st.order, iv));
        entry.input = std::move(core::at(meshes, iv));
        entry.light = std::move(core::at(light, iv));
        if (core::at(wanted, iv) != 0) {
            entry.analysed = true;
            entry.sun_for = st.model.toward;
            entry.bounce_for = st.model.toward;
        }
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const rt::RelitModel& fitted = st.model;
    get_log("rt_relit: level %08x - %zu meshes (%zu from the cache, %zu traced: %zu vertices, %zu rays), %.0f ms; the "
            "model %s: sun %.3f %.3f %.3f, sky %.3f %.3f %.3f, bounce %.3f %.3f %.3f, scale %.3f, median residual %.4f "
            "over %zu vertices, without the sun %.2f times worse",
            st.level, st.order.size(), from_cache, to_trace, stats.vertices, stats.rays, ms,
            fitted.valid ? "valid" : "not valid", static_cast<double>(std::get<0>(fitted.sun)),
            static_cast<double>(std::get<1>(fitted.sun)), static_cast<double>(std::get<2>(fitted.sun)),
            static_cast<double>(std::get<0>(fitted.sky)), static_cast<double>(std::get<1>(fitted.sky)),
            static_cast<double>(std::get<2>(fitted.sky)), static_cast<double>(std::get<0>(fitted.bounce)),
            static_cast<double>(std::get<1>(fitted.bounce)), static_cast<double>(std::get<2>(fitted.bounce)),
            static_cast<double>(fitted.scale), static_cast<double>(fitted.error), fitted.vertices,
            static_cast<double>(fitted.sun_needed));
    if (settings.cache && to_trace > 0 && st.model.valid) {
        write_cache({.level = st.level, .key = key});
        st.cache = {};
        st.cache.loaded = true; // (what it held is in the entries now)
    }
}

// The sun's rays traced again for entries lit toward another sun - at most kRelitRetraceVertices vertices this frame.
void retrace(rt::RayTracing& tracer, const rt::RelitSettings& settings)
{
    State& st = state();
    // (nothing lit toward another sun - every frame once the sun is still: nothing to trace, the entries left in place)
    const bool any_stale = std::ranges::any_of(st.order, [&st](const VertexBufferData* buffer) {
        const Entry& entry = st.entries.at(buffer);
        return entry.analysed && rt::dot(entry.sun_for, st.sun) < kSameSun;
    });
    if (!any_stale) {
        return;
    }
    std::vector<rt::RelitMesh> meshes;
    std::vector<rt::RelitVertices> light;
    std::vector<uint8_t> wanted;
    std::size_t budget = kRelitRetraceVertices;
    for (const VertexBufferData* buffer : st.order) {
        Entry& entry = st.entries.at(buffer);
        const bool stale = entry.analysed && rt::dot(entry.sun_for, st.sun) < kSameSun;
        const bool take = stale && budget > 0;
        if (take) {
            budget -= std::min(budget, entry.input.positions.size());
        }
        meshes.push_back(std::move(entry.input));
        light.push_back(std::move(entry.light));
        wanted.push_back(take ? 1 : 0);
    }
    if (std::ranges::count(wanted, uint8_t{1}) > 0) {
        rt::RelitStats stats;
        rt::trace_relit_sun(meshes, wanted, st.sun,
                            {.sky_rays = settings.sky_rays,
                             .sun_rays = settings.sun_rays,
                             .sun_radius_degrees = rt::kRelitSunRadiusDegrees,
                             .detail = settings.detail},
                            tracer, &light, &stats);
        st.retraced += stats.vertices;
    }
    for (std::size_t iv = 0; iv < st.order.size(); ++iv) {
        Entry& entry = st.entries.at(core::at(st.order, iv));
        entry.input = std::move(core::at(meshes, iv));
        entry.light = std::move(core::at(light, iv));
        if (core::at(wanted, iv) != 0) {
            entry.sun_for = st.sun;
        }
    }
}

// The bounce traced again for entries whose bounce is another sun's - once every sun is traced, from the colours as
// relit now (one bounce of the new light) - at most kRelitBounceVertices vertices this frame.
void rebounce(rt::RayTracing& tracer, const rt::RelitSettings& settings, const rt::RelitLighting& lighting)
{
    State& st = state();
    bool suns_ready = true;
    bool stale = false;
    for (const VertexBufferData* buffer : st.order) {
        const Entry& entry = st.entries.at(buffer);
        suns_ready = suns_ready && (!entry.analysed || rt::dot(entry.sun_for, st.sun) >= kSameSun);
        stale = stale || (entry.analysed && rt::dot(entry.bounce_for, st.sun) < kSameSun);
    }
    if (!suns_ready || !stale) {
        return;
    }
    std::vector<rt::RelitMesh> meshes;
    std::vector<rt::RelitVertices> light;
    std::vector<uint8_t> wanted;
    std::size_t budget = kRelitBounceVertices;
    for (const VertexBufferData* buffer : st.order) {
        Entry& entry = st.entries.at(buffer);
        const bool take = entry.analysed && rt::dot(entry.bounce_for, st.sun) < kSameSun && budget > 0;
        if (take) {
            budget -= std::min(budget, entry.input.positions.size());
        }
        // (the colours the rays bring back: as relit now - the bake's go back after)
        std::vector<uint32_t> relit(entry.originals.size());
        for (std::size_t vi = 0; vi < relit.size(); ++vi) {
            core::at(relit, vi) =
                vi < entry.light.size() && entry.analysed
                    ? rt::relit_colour(st.model, core::at(entry.light, vi), lighting, core::at(entry.originals, vi))
                    : core::at(entry.originals, vi);
        }
        meshes.push_back(std::move(entry.input));
        meshes.back().colours = std::move(relit);
        light.push_back(std::move(entry.light));
        wanted.push_back(take ? 1 : 0);
    }
    rt::RelitStats stats;
    rt::trace_relit_sky(meshes, wanted,
                        rt::RelitParams{.sky_rays = settings.sky_rays,
                                        .sun_rays = settings.sun_rays,
                                        .sun_radius_degrees = rt::kRelitSunRadiusDegrees,
                                        .detail = settings.detail},
                        tracer, &light, &stats);
    st.rebounced += stats.vertices;
    for (std::size_t iv = 0; iv < st.order.size(); ++iv) {
        Entry& entry = st.entries.at(core::at(st.order, iv));
        entry.input = std::move(core::at(meshes, iv));
        entry.input.colours = entry.originals;
        entry.light = std::move(core::at(light, iv));
        if (core::at(wanted, iv) != 0) {
            entry.bounce_for = st.sun;
            entry.dirty = true;
        }
    }
}

void log_status(Status status)
{
    State& st = state();
    st.status = status;
    if (status != Status::kRelit) {
        rt::note_sun_used(rt::SunUser::kRelit, false, {}); // (added by the scene's sun: the bake kept)
    }
    if (st.logged != status) {
        st.logged = status;
        get_log("rt_relit: level %08x: %s", st.level, status_name(status));
        rt::set_relit_status(status_text());
    }
}

// The entries' relit colours written where they are stale (the lighting changed, the bounce traced again) and their
// sun is traced, and the change logged once per lighting.
void write_relit(uint64_t lighting, const rt::RelitLighting& light)
{
    State& st = state();
    std::size_t written = 0;
    std::vector<uint32_t> colours;
    struct Change {
        std::size_t vertices = 0;
        double baked = 0.0;
        double relit = 0.0;
        std::size_t moved = 0;
    } change;
    for (const VertexBufferData* buffer : st.order) {
        Entry& entry = st.entries.at(buffer);
        if (!entry.analysed || (entry.written == lighting && !entry.dirty) ||
            rt::dot(entry.sun_for, st.sun) < kSameSun) {
            continue;
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
    if (written > 0 && lighting != st.lighting) {
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

} // namespace

void update(std::span<const WorldMesh> world, bool stable, rt::RayTracing& tracer)
{
    State& st = state();
    const rt::RelitSettings settings = rt::relit_settings();
    if (!settings.on) {
        if (st.status != Status::kOff) {
            restore_all();
            log_status(Status::kOff);
            rt::set_sun_relight({}); // (added by the scene's sun: the bake's sun lights the scene again)
        }
        return;
    }
    rt::set_baked_sun_wanted(true);
    const uint32_t level = main_scene();
    if (level != st.level) {
        rt::set_sun_relight({}); // (added by the scene's sun: a new level is lit by its bake until it is relit)
        restore_all();
        st.level = level;
        st.model = {};
        st.cache = {};
        st.lighting = 0;
        st.logged = Status::kOff;
        for (auto& [buffer, entry] : st.entries) {
            entry.analysed = false;
        }
    }
    if (st.status == Status::kOff) {
        st.status = Status::kWaiting;
    }
    const rt::BakedSun& baked = rt::baked_sun();
    if (!stable || world.empty() || level == 0 || baked.scene == 0) {
        return;
    }
    if (!baked.found) {
        restore_all();
        log_status(Status::kNoSun);
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
    // the world's meshes the mode doesn't know yet
    st.order.clear();
    ++st.listing;
    bool fresh = false;
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
        fresh = fresh || !entry.analysed;
    }
    if (st.order.empty()) {
        return;
    }
    if (fresh) {
        analyse(tracer, settings);
    }
    if (!st.model.valid) {
        restore_all();
        log_status(Status::kNoModel);
        rt::set_sun_relight({.relights = false, .level_keeps_bake = keeps_baked_sun});
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
    if (rt::dot(toward, st.sun) < kSameSun ||
        sky_model != st.sky_model) { // (the sky model publishes after its first pass)
        st.sky_model = sky_model;
        st.sun = toward;
        st.sun_colour = sun_colour_for(toward);
        get_log("rt_relit: the sun toward %.3f %.3f %.3f (colour x %.2f %.2f %.2f)",
                static_cast<double>(rt::x_of(toward)), static_cast<double>(rt::y_of(toward)),
                static_cast<double>(rt::z_of(toward)), static_cast<double>(std::get<0>(st.sun_colour)),
                static_cast<double>(std::get<1>(st.sun_colour)), static_cast<double>(std::get<2>(st.sun_colour)));
    }
    retrace(tracer, settings);
    const float strength =
        entry_of_level != nullptr && entry_of_level->strength >= 0.0F ? entry_of_level->strength : settings.strength;
    uint64_t lighting = fnv_value(kFnvBasis, st.sun);
    lighting = fnv_value(lighting, st.sun_colour);
    lighting = fnv_value(lighting, strength);
    lighting = fnv_value(lighting, settings.residual);
    lighting = fnv_value(lighting, model_floats(st.model));
    lighting = lighting == 0 ? 1 : lighting;
    const rt::RelitLighting light{
        .toward = st.sun, .sun_colour = st.sun_colour, .strength = strength, .residual = settings.residual};
    rebounce(tracer, settings, light);
    st.wanted_lighting = lighting;
    write_relit(lighting, light);
    rt::note_sun_used(rt::SunUser::kRelit, true, st.sun);
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
    const bool coloured = st.sun_colour != rt::Rgbf{1.0F, 1.0F, 1.0F};
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
    out->key_colour = st.sun_colour;
    return true;
}

bool settled(rt::Vec3* sun, uint64_t* lighting)
{
    const State& st = state();
    if (st.status != Status::kRelit || !st.model.valid || st.wanted_lighting == 0) {
        return false;
    }
    for (const VertexBufferData* buffer : st.order) {
        const Entry& entry = st.entries.at(buffer);
        if (entry.analysed && (entry.written != st.wanted_lighting || entry.dirty ||
                               rt::dot(entry.sun_for, st.sun) < kSameSun || rt::dot(entry.bounce_for, st.sun) < kSameSun)) {
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

} // namespace render::rt_relit_world
