// NOT ORIGINAL - per-pixel lighting and surface maps on the Vulkan Gpu (vulkan_surface.h).
#include "gpu_vulkan/vulkan_surface.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_handles.h"
#include "engine/gfx/rt_cel.h"
#include "engine/gfx/rt_daylight.h"
#include "engine/gfx/rt_material_table.h"
#include "engine/gfx/rt_scene.h"
#include "engine/gfx/surface_maps.h"
#include "formats/detail_material_format.h"
#include "formats/surface_map_format.h"
#include "gpu_vulkan/ff_key.h"
#include "gpu_vulkan/ff_uniforms.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_detail.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "platform/run_log.h"

namespace render::vulkan {

namespace {

constexpr std::size_t kTexel = 4;
constexpr float kByteHalf = 127.5F;
constexpr float kByte = 255.0F;
constexpr std::size_t kViewSlot = 1;
constexpr std::size_t kRows = 4;
constexpr uint32_t kFvfNormalBit = 0x010U;
constexpr float kLeastAlpha = 0.0064F; // (the maps' mips: the least alpha Toksvig's widening starts from)
constexpr float kLeastFade = 0.01F;
constexpr float kMostRelief = 16.0F;   // (relief_depth_of_map's ceiling)
constexpr float kMillimetresPerUnit = 25.4F; // (round M: the game's world unit is an inch)
constexpr float kMmToUnits = 1.0F / kMillimetresPerUnit;
struct MapEntry {
    Texture* map = nullptr;
    float f0 = 0.0F; // (the file's class F0 - kept as read; round M: the material table's is used)
    uint8_t material_class = 0;
    // (added by the detail materials - vulkan_detail.h) the texture's checksum and content hash, and its detail as of
    // detail_generation() `generation` (0: not looked up)
    uint32_t checksum = 0;
    uint64_t content_hash = 0;
    uint64_t generation = 0;
    DetailDraw detail{};
    float relief_depth = 0.0F; // (added by surface relief) the map's k (relief_depth_of_map)
    uint8_t flags = 0;         // (added for the characters) surface_map_format::kFlag*
    bool delit = false;        // (added for the de-lit texture sets) MapInfo::delit: no draw-time de-lighting
    float mean_albedo = 0.0F;  // (round MATALB) MapInfo::mean_albedo (0: not measured)
};

// (added for the characters) Whether a map is used: a character model's only with rt_characters on.
bool map_used(const MapEntry& entry, const RtPbrSettings& settings)
{
    return (entry.flags & surface_map_format::kFlagModel) == 0U || settings.characters;
}

// (added for the characters - vulkan_surface.h "Characters") A character model's map's shading: the key's bits for its
// class (round M: the material decides - no strengths): skin's diffusion, cloth's and denim's sheen, hair's fibres. A
// garment whose atlas holds skin texels (kFlagSkinTexels) is shaded as its cloth throughout: which texels are skin is
// not in the map (round M's record - a per-texel material mask in the maps replaces the hue test that guessed it).
uint8_t character_bits(const MapEntry& entry)
{
    namespace smf = surface_map_format;
    switch (static_cast<smf::MaterialClass>(entry.material_class)) {
    case smf::MaterialClass::kSkin:
        return kDetailSkin;
    case smf::MaterialClass::kCloth:
    case smf::MaterialClass::kDenim:
        return kDetailCloth;
    case smf::MaterialClass::kHair:
        return kDetailHair;
    default:
        return 0;
    }
}

// (added by the detail materials) An entry's detail, looked up again when the settings or the library changed (the
// surface state's lock held; detail_draw takes the detail state's own).
const DetailDraw& detail_of(MapEntry& entry)
{
    const uint64_t generation = detail_generation();
    if (entry.generation != generation) {
        entry.detail = detail_draw(entry.checksum, entry.content_hash, entry.material_class);
        entry.generation = generation;
    }
    return entry.detail;
}

// The relit world's sun as the draws use it (looked up once a frame: rt_cel's getter takes a lock).
struct FrameSun {
    uint64_t serial = 0;
    bool known = false;
    rt::CelWorld world{};
};

struct SurfaceState {
    std::mutex mutex;
    RtPbrSettings settings; // (set_rt_pbr_settings: at start-up and on a live change - the Options screen, game thread)
    RtPbrSettings draw_settings; // (the draw thread's: set with settings, read without the lock)
    FrameSun sun;
    std::unordered_map<const Texture*, MapEntry> maps;
    std::atomic<uint64_t> attached{0};
};

SurfaceState& surface_state()
{
    static SurfaceState state;
    return state;
}

float decode(uint8_t value) { return (static_cast<float>(value) - kByteHalf) / kByteHalf; }
uint8_t encode_signed(float value)
{
    return static_cast<uint8_t>(std::lround(std::clamp((value * kByteHalf) + kByteHalf, 0.0F, kByte)));
}
uint8_t encode_unit(float value) { return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * kByte)); }

// The next mip level of a map (rows of RGBA texels): each texel from the 2 x 2 above it (one row or column at an odd
// edge) - the normals averaged and renormalised, the roughness widened by how far the average falls short of a unit
// normal (Toksvig: alpha^2 + (1 - |n|) / |n|), the height averaged.
struct Extent {
    uint32_t width = 0;
    uint32_t height = 0;
};

std::vector<uint8_t> next_level(std::span<const uint8_t> texels, Extent* size)
{
    const uint32_t width = size->width;
    const uint32_t height = size->height;
    const uint32_t down_width = std::max(1U, width / 2);
    const uint32_t down_height = std::max(1U, height / 2);
    std::vector<uint8_t> out(static_cast<std::size_t>(down_width) * down_height * kTexel);
    for (uint32_t row = 0; row < down_height; ++row) {
        for (uint32_t column = 0; column < down_width; ++column) {
            std::array<float, 3> normal{};
            float alpha_squared = 0.0F;
            float height_sum = 0.0F;
            float count = 0.0F;
            for (uint32_t dy = 0; dy < 2; ++dy) {
                for (uint32_t dx = 0; dx < 2; ++dx) {
                    const uint32_t sx = std::min((column * 2) + dx, width - 1);
                    const uint32_t sy = std::min((row * 2) + dy, height - 1);
                    const std::span<const uint8_t> texel =
                        texels.subspan(((static_cast<std::size_t>(sy) * width) + sx) * kTexel, kTexel);
                    const float nx = decode(core::at(texel, 0));
                    const float ny = decode(core::at(texel, 1));
                    const float nz = std::sqrt(std::max(0.0F, 1.0F - (nx * nx) - (ny * ny)));
                    const float length = std::sqrt((nx * nx) + (ny * ny) + (nz * nz));
                    const float scale = length > 0.0F ? 1.0F / length : 0.0F;
                    normal = {core::at(normal, 0) + (nx * scale), core::at(normal, 1) + (ny * scale), core::at(normal, 2) + (nz * scale)};
                    const float rough = static_cast<float>(core::at(texel, 2)) / kByte;
                    alpha_squared += rough * rough * rough * rough;
                    height_sum += static_cast<float>(core::at(texel, 3)) / kByte;
                    count += 1.0F;
                }
            }
            const std::array<float, 3> mean{core::at(normal, 0) / count, core::at(normal, 1) / count, core::at(normal, 2) / count};
            const float length = std::sqrt((core::at(mean, 0) * core::at(mean, 0)) + (core::at(mean, 1) * core::at(mean, 1)) + (core::at(mean, 2) * core::at(mean, 2)));
            const float spread = length > 0.0F ? (1.0F - std::min(length, 1.0F)) / length : 1.0F;
            const float alpha = std::sqrt(std::max((alpha_squared / count) + spread, kLeastAlpha * kLeastAlpha));
            const std::span<uint8_t> target =
                std::span<uint8_t>(out).subspan(((static_cast<std::size_t>(row) * down_width) + column) * kTexel, kTexel);
            const float inverse = length > 0.0F ? 1.0F / length : 0.0F;
            core::at(target, 0) = encode_signed(core::at(mean, 0) * inverse);
            core::at(target, 1) = encode_signed(core::at(mean, 1) * inverse);
            core::at(target, 2) = encode_unit(std::sqrt(alpha));
            core::at(target, 3) = encode_unit(height_sum / count);
        }
    }
    *size = {.width = down_width, .height = down_height};
    return out;
}

// One level into a locked A8R8G8B8 texture: the file's R, G, B, A bytes as Direct3D's B, G, R, A in memory.
void copy_level(const GpuLockedRect& locked, std::span<const uint8_t> texels, uint32_t width, uint32_t height)
{
    const auto pitch = static_cast<std::size_t>(locked.pitch);
    const std::span<uint8_t> level(static_cast<uint8_t*>(locked.bits), (pitch * (height - 1)) + (std::size_t{width} * kTexel));
    for (uint32_t line = 0; line < height; ++line) {
        const std::span<uint8_t> row = level.subspan(line * pitch, static_cast<std::size_t>(width) * kTexel);
        const std::span<const uint8_t> from = texels.subspan(static_cast<std::size_t>(line) * width * kTexel, row.size());
        for (std::size_t at = 0; at < row.size(); at += kTexel) {
            core::at(row, at) = core::at(from, at + 2);
            core::at(row, at + 1) = core::at(from, at + 1);
            core::at(row, at + 2) = core::at(from, at);
            core::at(row, at + 3) = core::at(from, at + 3);
        }
    }
}

uint32_t level_count(uint32_t width, uint32_t height)
{
    uint32_t levels = 1;
    while (width > 1 || height > 1) {
        width = std::max(1U, width / 2);
        height = std::max(1U, height / 2);
        ++levels;
    }
    return levels;
}

class Registry final : public surface::SurfaceMaps {
public:
    bool attach(GpuTexture* colour, const surface::MapInfo& info, std::span<const uint8_t> texels) override
    {
        if (colour == nullptr || info.width == 0 || info.height == 0 ||
            texels.size() != static_cast<std::size_t>(info.width) * info.height * kTexel) {
            return false;
        }
        Gpu& device = gpu();
        const uint32_t levels = level_count(info.width, info.height);
        GpuTexture* made = nullptr;
        if (device.create_texture({.width = info.width,
                                   .height = info.height,
                                   .levels = levels,
                                   .usage = 0,
                                   .format = gpu_value::kFormatA8R8G8B8,
                                   .pool = gpu_value::kPoolManaged},
                                  &made) != gpu_value::kResultOk ||
            made == nullptr) {
            return false;
        }
        const float relief_depth = relief_depth_of_map(texels, info.width, info.height); // (added by surface relief)
        std::vector<uint8_t> level(texels.begin(), texels.end());
        Extent size{.width = info.width, .height = info.height};
        for (uint32_t index = 0; index < levels; ++index) {
            GpuLockedRect locked{};
            if (device.lock_texture(made, index, &locked, nullptr, 0) == gpu_value::kResultOk && locked.bits != nullptr) {
                copy_level(locked, level, size.width, size.height);
                device.unlock_texture(made, index);
            }
            if (index + 1 < levels) {
                level = next_level(level, &size);
            }
        }
        Texture* replaced = nullptr;
        {
            SurfaceState& state = surface_state();
            const std::scoped_lock lock(state.mutex);
            MapEntry& entry = state.maps[object(colour)];
            replaced = entry.map;
            entry = {.map = object(made),
                     .f0 = std::clamp(info.f0, 0.0F, 1.0F),
                     .material_class = info.material_class,
                     .checksum = info.checksum,
                     .content_hash = info.content_hash,
                     .relief_depth = relief_depth,
                     .flags = info.flags,
                     .delit = info.delit,
                     .mean_albedo = info.mean_albedo};
        }
        ensure_detail_library(); // (added by the detail materials: the library read the first time it is wanted)
        if (replaced != nullptr) {
            device.release_texture(texture_handle(replaced));
        }
        surface_state().attached.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
};

Registry& registry()
{
    static Registry maps;
    return maps;
}

// A direction (row vector) through the view's 3 x 3: world into camera space.
Float4 to_camera(const std::array<float, kMatrixFloats>& view, const std::array<float, 3>& direction, float w)
{
    Float4 out{};
    for (std::size_t column = 0; column < 3; ++column) {
        float sum = 0.0F;
        for (std::size_t row = 0; row < 3; ++row) {
            sum += core::at(direction, row) * core::at(view, (row * kRows) + column);
        }
        core::at(out, column) = sum;
    }
    const float length = std::sqrt((core::at(out, 0) * core::at(out, 0)) + (core::at(out, 1) * core::at(out, 1)) + (core::at(out, 2) * core::at(out, 2)));
    if (length > 0.0F) {
        out = {core::at(out, 0) / length, core::at(out, 1) / length, core::at(out, 2) / length, w};
    } else {
        out = {};
    }
    return out;
}

} // namespace

void set_rt_pbr_settings(const RtPbrSettings& settings)
{
    SurfaceState& state = surface_state();
    {
        const std::scoped_lock lock(state.mutex);
        state.settings = settings;
        state.draw_settings = settings;
        state.sun = {};
    }
    surface::set_surface_maps(settings.on ? &registry() : nullptr);
    if (settings.on) {
        get_log("rt_pbr: per-pixel lighting on - the light model and the material table%s%s%s",
                settings.objects ? ", objects too" : "", settings.relief ? ", relief" : "",
                settings.characters ? ", characters" : "");
    }
}

RtPbrSettings rt_pbr_settings()
{
    SurfaceState& state = surface_state();
    const std::scoped_lock lock(state.mutex);
    return state.settings;
}

SurfaceDraw surface_draw(const GpuStateBlock& block, const ShaderKey& key)
{
    SurfaceDraw draw;
    SurfaceState& state = surface_state();
    if (!state.draw_settings.on) {
        return draw;
    }
    const FragmentKey& fragment = key.fragment;
    const bool program = fragment.program == PixelProgram::kMaterial || fragment.program == PixelProgram::kTextureVertexX4 ||
                         fragment.program == PixelProgram::kTextureConstantAlpha;
    if (!program || fragment.unlit != 0 || fvf_pre_transformed(key.vertex.fvf) || key.vertex.points ||
        (key.vertex.coord_mask & 1U) == 0U) {
        return draw;
    }
    const bool lit = key.vertex.lighting && key.vertex.light_count > 0 && (key.vertex.fvf & kFvfNormalBit) != 0U;
    GpuTexture* const first = block.textures.front();
    const std::scoped_lock lock(state.mutex);
    if (first != nullptr) {
        const auto found = state.maps.find(object(first));
        if (found != state.maps.end() && map_used(found->second, state.draw_settings)) { // (a model's: rt_characters)
            draw.map = found->second.map;
            draw.kind = rt::kind_of_map_class(found->second.material_class);
            draw.strands_u = (found->second.flags & surface_map_format::kFlagStrandsU) != 0U;
            draw.albedo_scale = rt::material_albedo_scale(draw.kind, found->second.mean_albedo); // (round MATALB)
            draw.flags = static_cast<uint8_t>(kSurfaceLighting | kSurfaceMap | (lit && state.settings.objects ? kSurfaceLit : 0));
            if ((found->second.flags & surface_map_format::kFlagModel) != 0U) {
                // (added for the characters) no detail layer or relief - skin, cloth or hair shading where lit by lights
                if ((draw.flags & kSurfaceLit) != 0U) {
                    draw.detail = character_bits(found->second);
                }
                return draw;
            }
            // (round M: a world or object map - the reflections pass may trace its environment; not a blended draw: the
            // pass sees only what the depth holds - opaque and cut-out surfaces - so a blended one keeps its own estimate)
            draw.traced = block.render_state(GpuRenderState::kAlphaBlendEnable) == 0;
            const DetailDraw& detail = detail_of(found->second); // (added by the detail materials)
            if (detail.kind != 0) {
                draw.kind = rt::kind_of_detail(detail.kind); // (the library's kind: the terrain's evidence beside the class's)
                draw.albedo_scale = rt::material_albedo_scale(draw.kind, found->second.mean_albedo);
            }
            if (detail.flags != 0) {
                draw.detail = detail.flags;
                draw.detail_map = detail.texture;
                draw.detail_uniforms = detail.uniforms;
            }
            // (added by surface relief) parallax occlusion mapping where the material has relief and the map a height
            // (round M: its depth the material's relief in world units - rt_material_table.h)
            const RtPbrSettings& settings = state.draw_settings;
            const float depth = rt::material_values(draw.kind).relief_mm * kMmToUnits;
            if (settings.relief && depth > 0.0F && found->second.relief_depth > 0.0F) {
                draw.detail = static_cast<uint8_t>(draw.detail | kDetailRelief);
                draw.detail_uniforms.relief = {depth, settings.relief_fade_start,
                                               std::max(settings.relief_fade_end, settings.relief_fade_start + kLeastFade), 0.0F};
            }
            return draw;
        }
    }
    if (lit && state.settings.objects) {
        draw.flags = kSurfaceLighting | kSurfaceLit;
    }
    return draw;
}

void fill_surface_uniforms(const GpuStateBlock& block, const VertexUniforms& vertex, const SurfaceDraw& draw, uint64_t frame,
                           SurfaceUniforms* uniforms)
{
    SurfaceState& state = surface_state();
    const RtPbrSettings& settings = state.draw_settings;
    *uniforms = {};
    for (std::size_t index = 0; index < kMaxLights; ++index) {
        const Float4& from = core::at(vertex.light_direction, index);
        const float length = std::sqrt((core::at(from, 0) * core::at(from, 0)) + (core::at(from, 1) * core::at(from, 1)) + (core::at(from, 2) * core::at(from, 2)));
        if (length > 0.0F) {
            core::at(uniforms->light_toward, index) = {-core::at(from, 0) / length, -core::at(from, 1) / length, -core::at(from, 2) / length, 0.0F};
            core::at(uniforms->light_colour, index) = core::at(vertex.light_diffuse, index);
        }
    }
    const std::array<float, kMatrixFloats>& view = core::at(block.transforms, kViewSlot);
    // (round M) the relit world's sun - the light store's - once a frame; none without the store (the bake keeps its
    // light whole: nothing separates a sun from it)
    FrameSun& sun = state.sun;
    if (!sun.known || sun.serial != frame) {
        sun = {.serial = frame, .known = true, .world = rt::cel_world()};
    }
    const rt::CelWorld& world = sun.world;
    uniforms->sun = world.relit ? to_camera(view, world.toward, 1.0F) : Float4{};
    // (round L: the relit world's sun is physical - x the frame's pre-exposure here, where it is drawn)
    const float exposure = rt::frame_pre_exposure();
    uniforms->sun_light = {std::get<0>(world.sun) * exposure, std::get<1>(world.sun) * exposure,
                           std::get<2>(world.sun) * exposure, kMillimetresPerUnit};
    // the material: the table's values for the draw's kind (rt_material_table.h)
    const rt::MaterialValues& material = rt::material_values(draw.kind);
    const rt::brdf::Rgb f0 = rt::material_f0(draw.kind);
    uniforms->scatter = {std::get<0>(material.scatter_mm), std::get<1>(material.scatter_mm), std::get<2>(material.scatter_mm),
                         draw.strands_u ? 1.0F : 0.0F};
    uniforms->params = {rt::material_is_metal(draw.kind) ? 1.0F : 0.0F, 0.0F, material.roughness,
                        std::get<0>(f0)};
    uniforms->sheen = {std::get<0>(material.sheen), std::get<1>(material.sheen), std::get<2>(material.sheen),
                       material.sheen_roughness};
    // (round MATALB) the texture's colour to its kind's albedo (rt_material_table.h material_albedo_scale; 1: as painted)
    uniforms->albedo = {draw.albedo_scale, 0.0F, 0.0F, 0.0F};
    (void)settings;
}

Texture* surface_map_for(const Texture* colour)
{
    SurfaceState& state = surface_state();
    const std::scoped_lock lock(state.mutex);
    const auto found = state.maps.find(colour);
    return found != state.maps.end() && map_used(found->second, state.settings) ? found->second.map : nullptr;
}

float surface_map_f0(const Texture* colour)
{
    SurfaceState& state = surface_state();
    const std::scoped_lock lock(state.mutex);
    const auto found = state.maps.find(colour);
    if (found == state.maps.end() || !map_used(found->second, state.settings)) {
        return 0.0F;
    }
    if ((found->second.flags & surface_map_format::kFlagModel) != 0U) {
        return found->second.f0; // (added for the characters: no surface kind)
    }
    // (round M) the material table's F0 for the map's kind (the detail library's kind where it has one)
    const DetailDraw& detail = detail_of(found->second);
    const rt::MaterialKind kind =
        detail.kind != 0 ? rt::kind_of_detail(detail.kind) : rt::kind_of_map_class(found->second.material_class);
    return std::get<0>(rt::material_f0(kind));
}

bool surface_map_gloss(const Texture* colour, SurfaceGloss* out)
{
    SurfaceState& state = surface_state();
    const std::scoped_lock lock(state.mutex);
    *out = {};
    const auto found = state.maps.find(colour);
    if (!state.settings.on || found == state.maps.end() || (found->second.flags & surface_map_format::kFlagModel) != 0U ||
        !map_used(found->second, state.settings)) {
        return false;
    }
    // (round M) every mapped surface reflects as its material does - its F0 (the table's) and its texels' roughness;
    // no kind is held off (the per-kind gloss table and least roughness are gone)
    const DetailDraw& detail = detail_of(found->second);
    const rt::MaterialKind kind =
        detail.kind != 0 ? rt::kind_of_detail(detail.kind) : rt::kind_of_map_class(found->second.material_class);
    out->gloss = 1.0F;
    out->f0 = std::get<0>(rt::material_f0(kind));
    return true;
}

float surface_map_albedo_scale(const Texture* colour)
{
    SurfaceState& state = surface_state();
    const std::scoped_lock lock(state.mutex);
    const auto found = state.maps.find(colour);
    if (!state.settings.on || found == state.maps.end() || !map_used(found->second, state.settings)) {
        return 1.0F;
    }
    // (the kind as surface_draw takes it: the detail library's where it lists the texture - not a character model's)
    rt::MaterialKind kind = rt::kind_of_map_class(found->second.material_class);
    if ((found->second.flags & surface_map_format::kFlagModel) == 0U) {
        const DetailDraw& detail = detail_of(found->second);
        if (detail.kind != 0) {
            kind = rt::kind_of_detail(detail.kind);
        }
    }
    return rt::material_albedo_scale(kind, found->second.mean_albedo);
}

Texture* take_surface_map(const Texture* colour)
{
    SurfaceState& state = surface_state();
    const std::scoped_lock lock(state.mutex);
    if (state.maps.empty()) {
        return nullptr;
    }
    const auto found = state.maps.find(colour);
    if (found == state.maps.end()) {
        return nullptr;
    }
    Texture* const map = found->second.map;
    state.maps.erase(found);
    return map;
}

uint64_t surface_maps_attached() { return surface_state().attached.load(std::memory_order_relaxed); }

// (added by surface relief) the generator's normals are (-du k, -dv k, 1) normalised, du / dv its height's Sobel
// differences (tools/pbr_maps classical.ts normalsFrom): k = -sum(sx du + sy dv) / sum(du^2 + dv^2), s the normal's
// slope (x / z, y / z) - over every texel, the heights read from the bytes (their rounding only adds noise).
float relief_depth_of_map(std::span<const uint8_t> texels, uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || texels.size() < static_cast<std::size_t>(width) * height * kTexel) {
        return 0.0F;
    }
    constexpr float kSobel = 8.0F;
    constexpr float kLeastZ = 0.05F;
    const auto at = [&](uint32_t column, uint32_t row) {
        return static_cast<float>(core::at(texels, (((static_cast<std::size_t>(row % height) * width) + (column % width)) * kTexel) + 3)) / kByte;
    };
    double slope = 0.0;
    double gradient = 0.0;
    for (uint32_t row = 0; row < height; ++row) {
        const uint32_t up = row + height - 1;
        const uint32_t down = row + 1;
        for (uint32_t column = 0; column < width; ++column) {
            const uint32_t left = column + width - 1;
            const uint32_t right = column + 1;
            const float du = (at(right, up) + (2.0F * at(right, row)) + at(right, down) - at(left, up) - (2.0F * at(left, row)) -
                              at(left, down)) /
                             kSobel;
            const float dv = (at(left, down) + (2.0F * at(column, down)) + at(right, down) - at(left, up) - (2.0F * at(column, up)) -
                              at(right, up)) /
                             kSobel;
            const std::size_t texel = ((static_cast<std::size_t>(row) * width) + column) * kTexel;
            const float nx = decode(core::at(texels, texel));
            const float ny = decode(core::at(texels, texel + 1));
            const float nz = std::sqrt(std::max(1.0F - (nx * nx) - (ny * ny), 0.0F));
            if (nz < kLeastZ) {
                continue;
            }
            slope -= static_cast<double>(((nx / nz) * du) + ((ny / nz) * dv));
            gradient += static_cast<double>((du * du) + (dv * dv));
        }
    }
    if (gradient <= 0.0 || slope <= 0.0) {
        return 0.0F;
    }
    return std::min(static_cast<float>(slope / gradient), kMostRelief);
}

} // namespace render::vulkan
