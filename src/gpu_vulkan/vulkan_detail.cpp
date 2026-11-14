// NOT ORIGINAL - detail materials and de-lighting on the Vulkan Gpu (vulkan_detail.h).
#include "gpu_vulkan/vulkan_detail.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "engine/file/disk.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "formats/detail_material_format.h"
#include "formats/surface_map_format.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "gpu_vulkan/vulkan_surface.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

namespace dmf = detail_material_format;
namespace smf = surface_map_format;

constexpr std::size_t kTexel = 4;
constexpr float kDefaultRepeats = 4.0F; // (a texture the library doesn't list: the detail's tiles per unit of its coordinates)
constexpr uint32_t kLargestFile = 256U * 1024U * 1024U;
constexpr uint32_t kHashShift = 32;

struct HashKey {
    uint32_t checksum = 0;
    uint64_t content_hash = 0;
    bool operator==(const HashKey&) const = default;
};
struct HashKeyHash {
    std::size_t operator()(const HashKey& key) const
    {
        return static_cast<std::size_t>(key.content_hash ^ (static_cast<uint64_t>(key.checksum) << kHashShift) ^ key.checksum);
    }
};

struct KindState {
    dmf::KindEntry entry{};
    Texture* texture = nullptr;
};

struct DetailState {
    std::mutex mutex;
    RtDetailSettings settings;
    bool tried = false;  // (the game folder's library looked for since rt_detail went on)
    bool loaded = false; // (a library is in)
    std::array<KindState, dmf::kKindCount> kinds{};
    std::unordered_map<HashKey, dmf::Assignment, HashKeyHash> assignments;
    std::atomic<uint64_t> generation{1};
    std::atomic<uint32_t> made{0};
};

DetailState& detail_state()
{
    static DetailState state;
    return state;
}

// A surface map's class (surface_map_format::MaterialClass) as a kind, for a texture the library doesn't list: glass,
// water, fabric and painted (default) surfaces get none.
dmf::Kind kind_of_class(uint8_t material_class)
{
    switch (static_cast<smf::MaterialClass>(material_class)) {
    case smf::MaterialClass::kAsphalt:
        return dmf::Kind::kAsphalt;
    case smf::MaterialClass::kConcrete:
        return dmf::Kind::kConcreteSmooth;
    case smf::MaterialClass::kStone:
        return dmf::Kind::kStone;
    case smf::MaterialClass::kMetal:
        return dmf::Kind::kMetal;
    case smf::MaterialClass::kWood:
        return dmf::Kind::kWood;
    case smf::MaterialClass::kFoliage:
        return dmf::Kind::kGrass;
    case smf::MaterialClass::kGround:
        return dmf::Kind::kGround;
    case smf::MaterialClass::kTile:
        return dmf::Kind::kTile;
    default:
        return dmf::Kind::kNone;
    }
}

// The classes the de-lighting leaves alone (see-through or liquid: their colour is the art, not painted light).
bool delit_class(uint8_t material_class)
{
    const auto cls = static_cast<smf::MaterialClass>(material_class);
    return cls != smf::MaterialClass::kGlass && cls != smf::MaterialClass::kWater;
}

// The next mip level of a detail texture (rows of RGBA texels): each channel the mean of the 2 x 2 above it.
std::vector<uint8_t> next_level(std::span<const uint8_t> texels, uint32_t side)
{
    const uint32_t down = std::max(1U, side / 2);
    std::vector<uint8_t> out(static_cast<std::size_t>(down) * down * kTexel);
    for (uint32_t row = 0; row < down; ++row) {
        for (uint32_t column = 0; column < down; ++column) {
            for (std::size_t channel = 0; channel < kTexel; ++channel) {
                uint32_t sum = 0;
                for (uint32_t dy = 0; dy < 2; ++dy) {
                    for (uint32_t dx = 0; dx < 2; ++dx) {
                        const uint32_t sx = std::min((column * 2) + dx, side - 1);
                        const uint32_t sy = std::min((row * 2) + dy, side - 1);
                        sum += core::at(texels, (((static_cast<std::size_t>(sy) * side) + sx) * kTexel) + channel);
                    }
                }
                core::at(out, (((static_cast<std::size_t>(row) * down) + column) * kTexel) + channel) =
                    static_cast<uint8_t>((sum + 2U) / 4U);
            }
        }
    }
    return out;
}

// One level into a locked A8R8G8B8 texture: the file's R, G, B, A bytes as Direct3D's B, G, R, A in memory.
void copy_level(const GpuLockedRect& locked, std::span<const uint8_t> texels, uint32_t side)
{
    const auto pitch = static_cast<std::size_t>(locked.pitch);
    const std::span<uint8_t> level(static_cast<uint8_t*>(locked.bits), (pitch * (side - 1)) + (std::size_t{side} * kTexel));
    for (uint32_t line = 0; line < side; ++line) {
        const std::span<uint8_t> row = level.subspan(line * pitch, static_cast<std::size_t>(side) * kTexel);
        const std::span<const uint8_t> from = texels.subspan(static_cast<std::size_t>(line) * side * kTexel, row.size());
        for (std::size_t at = 0; at < row.size(); at += kTexel) {
            core::at(row, at) = core::at(from, at + 2);
            core::at(row, at + 1) = core::at(from, at + 1);
            core::at(row, at + 2) = core::at(from, at);
            core::at(row, at + 3) = core::at(from, at + 3);
        }
    }
}

// A kind's texture with its mips (null: the device refused it).
Texture* make_texture(std::span<const uint8_t> texels, uint32_t side)
{
    uint32_t levels = 1;
    for (uint32_t size = side; size > 1; size /= 2) {
        ++levels;
    }
    Gpu& device = gpu();
    GpuTexture* made = nullptr;
    if (device.create_texture({.width = side,
                               .height = side,
                               .levels = levels,
                               .usage = 0,
                               .format = gpu_value::kFormatA8R8G8B8,
                               .pool = gpu_value::kPoolManaged},
                              &made) != gpu_value::kResultOk ||
        made == nullptr) {
        return nullptr;
    }
    std::vector<uint8_t> level(texels.begin(), texels.end());
    uint32_t size = side;
    for (uint32_t index = 0; index < levels; ++index) {
        GpuLockedRect locked{};
        if (device.lock_texture(made, index, &locked, nullptr, 0) == gpu_value::kResultOk && locked.bits != nullptr) {
            copy_level(locked, level, size);
            device.unlock_texture(made, index);
        }
        if (index + 1 < levels) {
            level = next_level(level, size);
            size = std::max(1U, size / 2);
        }
    }
    return object(made);
}

// Every kind's texture let go of and the tables emptied (the lock held; the textures returned for release outside it).
std::vector<Texture*> clear_locked(DetailState& state)
{
    std::vector<Texture*> released;
    for (KindState& kind : state.kinds) {
        if (kind.texture != nullptr) {
            released.push_back(kind.texture);
        }
        kind = {};
    }
    state.assignments.clear();
    state.loaded = false;
    state.generation.fetch_add(1, std::memory_order_acq_rel);
    return released;
}

void release(const std::vector<Texture*>& textures)
{
    for (Texture* texture : textures) {
        gpu().release_texture(texture_handle(texture));
    }
}

std::vector<uint8_t> read_library_file()
{
    std::vector<uint8_t> bytes;
    fs::DiskStream* const in = fs::disk().open_stream(kDetailLibraryFile, "rb", fs::StreamSharing::kAll);
    if (in == nullptr) {
        return bytes;
    }
    if (fs::disk().stream_seek(in, 0, SEEK_END) == 0) {
        const long size = fs::disk().stream_tell(in);
        if (size > 0 && static_cast<unsigned long>(size) <= kLargestFile && fs::disk().stream_seek(in, 0, SEEK_SET) == 0) {
            bytes.resize(static_cast<std::size_t>(size));
            bytes.resize(fs::disk().stream_read(in, bytes.data(), 1, bytes.size()));
        }
    }
    fs::disk().stream_close(in);
    return bytes;
}

} // namespace

void set_rt_detail_settings(const RtDetailSettings& settings)
{
    DetailState& state = detail_state();
    std::vector<Texture*> released;
    {
        const std::scoped_lock lock(state.mutex);
        const bool was_on = state.settings.on;
        state.settings = settings;
        state.settings.strength = std::clamp(settings.strength, 0.0F, 2.0F);
        state.settings.delight_strength = std::clamp(settings.delight_strength, 0.0F, 1.0F);
        if (!settings.on) {
            state.tried = false;
            if (was_on && state.loaded) {
                released = clear_locked(state); // (the library's textures given back while it is off)
            }
        }
        state.generation.fetch_add(1, std::memory_order_acq_rel);
    }
    release(released);
    if (settings.on || settings.delight) {
        get_log("rt_detail: detail materials %s (strength %.2f), de-lighting %s (strength %.2f) - on the draws rt_pbr "
                "lights with a surface map",
                settings.on ? "on" : "off", static_cast<double>(settings.strength), settings.delight ? "on" : "off",
                static_cast<double>(settings.delight_strength));
    }
    if (settings.on && surface_maps_attached() > 0) {
        ensure_detail_library(); // (a live change: the device is there and maps are attached - read it now)
    }
}

RtDetailSettings rt_detail_settings()
{
    DetailState& state = detail_state();
    const std::scoped_lock lock(state.mutex);
    return state.settings;
}

bool load_detail_library(std::span<const uint8_t> file)
{
    dmf::Library library;
    if (!dmf::read_library(file, &library)) {
        return false;
    }
    std::array<KindState, dmf::kKindCount> kinds{};
    uint32_t made = 0;
    for (const dmf::KindEntry& entry : library.kinds) {
        KindState& kind = core::at(kinds, static_cast<std::size_t>(entry.kind));
        if (kind.texture != nullptr) {
            continue; // (a kind listed twice: the first kept)
        }
        kind.entry = entry;
        kind.texture = make_texture(file.subspan(entry.offset, entry.bytes), entry.side);
        made += kind.texture != nullptr ? 1U : 0U;
    }
    DetailState& state = detail_state();
    std::vector<Texture*> released;
    {
        const std::scoped_lock lock(state.mutex);
        released = clear_locked(state);
        state.kinds = kinds;
        for (const dmf::Assignment& assignment : library.assignments) {
            state.assignments[{.checksum = assignment.checksum, .content_hash = assignment.content_hash}] = assignment;
        }
        state.loaded = true;
        state.tried = true;
        state.generation.fetch_add(1, std::memory_order_acq_rel);
    }
    release(released);
    state.made.fetch_add(made, std::memory_order_relaxed);
    get_log("rt_detail: the detail-material library - %u kinds (%u textures made), %zu textures assigned",
            static_cast<unsigned>(library.kinds.size()), made, library.assignments.size());
    return true;
}

void ensure_detail_library()
{
    DetailState& state = detail_state();
    {
        const std::scoped_lock lock(state.mutex);
        if (!state.settings.on || state.tried) {
            return;
        }
        state.tried = true;
    }
    const std::vector<uint8_t> file = read_library_file();
    if (file.empty()) {
        get_log("rt_detail: no %s in the game folder (node tools/detail_materials/detail_materials.ts fetch + pack) - "
                "no detail layer; the de-lighting and the kinds' materials from the surface maps' classes only",
                kDetailLibraryFile);
        return;
    }
    if (!load_detail_library(file)) {
        get_log("rt_detail: %s isn't a detail-material library (or is damaged) - not used", kDetailLibraryFile);
    }
}

uint64_t detail_generation() { return detail_state().generation.load(std::memory_order_acquire); }

uint32_t detail_textures_made() { return detail_state().made.load(std::memory_order_relaxed); }

DetailDraw detail_draw(uint32_t checksum, uint64_t content_hash, uint8_t material_class)
{
    DetailDraw draw;
    DetailState& state = detail_state();
    const std::scoped_lock lock(state.mutex);
    const RtDetailSettings& settings = state.settings;
    if (!settings.on && !settings.delight) {
        return draw;
    }
    const auto found = state.assignments.find({.checksum = checksum, .content_hash = content_hash});
    const dmf::Assignment* assignment = found != state.assignments.end() ? &found->second : nullptr;
    const dmf::Kind kind = assignment != nullptr ? assignment->kind : kind_of_class(material_class);
    DetailUniforms& uniforms = draw.uniforms;
    if (settings.on && kind != dmf::Kind::kNone) {
        const KindState& known = core::at(state.kinds, static_cast<std::size_t>(kind));
        if (known.texture != nullptr) {
            draw.flags = static_cast<uint8_t>(draw.flags | kDetailMaterial);
            draw.texture = known.texture;
            draw.f0 = std::clamp(known.entry.f0, 0.0F, 1.0F);
            const float repeats = assignment != nullptr && assignment->repeats > 0.0F ? assignment->repeats : kDefaultRepeats;
            uniforms.scale = {repeats, settings.strength, settings.fade_start, std::max(settings.fade_end, settings.fade_start + 0.01F)};
            uniforms.material = {std::clamp(known.entry.roughness, 0.0F, 1.0F), std::max(known.entry.roughness_swing, 0.0F),
                                 std::clamp(known.entry.metalness, 0.0F, 1.0F),
                                 std::clamp(known.entry.roughness_weight, 0.0F, 1.0F)};
        }
    }
    if (settings.delight && settings.delight_strength > 0.0F && delit_class(material_class)) {
        draw.flags = static_cast<uint8_t>(draw.flags | kDetailDelight);
        const float mean = assignment != nullptr ? assignment->mean_luma : -1.0F;
        uniforms.delight = {settings.delight_strength, mean, std::max(settings.delight_limit, 1.0F), 0.0F};
    }
    return draw;
}

} // namespace render::vulkan
