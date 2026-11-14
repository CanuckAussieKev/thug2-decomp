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
constexpr float kLeastAlpha = 0.0064F; // (the shader's floor on GGX's alpha)
constexpr float kLeastRelief = 0.05F;  // (added by surface relief: a depth in texels below which no relief is drawn)
constexpr float kLeastFade = 0.01F;
constexpr float kMostRelief = 16.0F;   // (relief_depth_of_map's ceiling)
// (added for the surface reflections) the glossy kinds' share of a mirror's reflection (surface_map_gloss)
constexpr float kGlossStone = 0.5F; // polished stone (cobbles and rock rough by their maps)
constexpr float kGlossWood = 0.35F; // varnished planks
constexpr float kGlossPaint = 0.3F; // painted or unclassified

struct MapEntry {
    Texture* map = nullptr;
    float f0 = kRtPbrF0;
    uint8_t material_class = 0;
    // (added by the detail materials - vulkan_detail.h) the texture's checksum and content hash, and its detail as of
    // detail_generation() `generation` (0: not looked up)
    uint32_t checksum = 0;
    uint64_t content_hash = 0;
    uint64_t generation = 0;
    DetailDraw detail{};
    float relief_depth = 0.0F; // (added by surface relief) the map's k (relief_depth_of_map)
    uint8_t flags = 0;         // (added for the characters) surface_map_format::kFlag*
};

// (added for the characters) Whether a map is used: a character model's only with rt_characters on.
bool map_used(const MapEntry& entry, const RtPbrSettings& settings)
{
    return (entry.flags & surface_map_format::kFlagModel) == 0U || settings.characters;
}

// (added for the characters - vulkan_surface.h "Characters") A character model's map's shading: the key's bits for its
// class and their vector (DetailUniforms::character) - none for the classes shaded as anything else (leather, rubber,
// metal, a board's wood: their roughness and F0 alone) or at strength 0.
constexpr float kSkinFollowsColour = 0.6F;   // (a skin texture: its eyes, brows and lips less skin)
constexpr float kGarmentFollowsColour = 1.0F; // (a garment's skin texels: wholly by their colour)
constexpr float kDenimSheen = 0.6F;          // (denim's sheen against cloth's: a harder, flatter weave)
uint8_t character_bits(const MapEntry& entry, const RtPbrSettings& settings, Float4* character)
{
    namespace smf = surface_map_format;
    const auto cls = static_cast<smf::MaterialClass>(entry.material_class);
    const bool skin_texels = (entry.flags & smf::kFlagSkinTexels) != 0U && settings.skin > 0.0F;
    uint8_t bits = 0;
    switch (cls) {
    case smf::MaterialClass::kSkin:
        if (settings.skin > 0.0F) {
            bits = kDetailSkin;
            *character = {settings.skin, kSkinFollowsColour, 0.0F, 0.0F};
        }
        break;
    case smf::MaterialClass::kCloth:
    case smf::MaterialClass::kDenim: {
        const float sheen = settings.cloth * (cls == smf::MaterialClass::kDenim ? kDenimSheen : 1.0F);
        bits = static_cast<uint8_t>((sheen > 0.0F ? kDetailCloth : 0) | (skin_texels ? kDetailSkin : 0));
        *character = {skin_texels ? settings.skin : 0.0F, kGarmentFollowsColour, sheen, 0.0F};
        break;
    }
    case smf::MaterialClass::kHair:
        if (settings.hair > 0.0F) {
            bits = kDetailHair;
            *character = {0.0F, 0.0F, settings.hair, (entry.flags & smf::kFlagStrandsU) != 0U ? 1.0F : 0.0F};
        }
        break;
    default:
        break;
    }
    return bits;
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

// The frame's sun as the draws use it (looked up once a frame: rt_scene's getters take locks).
struct FrameSun {
    uint64_t serial = 0;
    bool known = false;
    std::array<float, 3> toward{};
    float weight = 0.0F;
    std::array<float, 3> colour{1.0F, 1.0F, 1.0F};
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
                     .flags = info.flags};
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
    if (settings.on && !rt::baked_sun_wanted()) {
        rt::set_baked_sun_wanted(true); // (the bake's sun: fitted when ray tracing is on)
    }
    if (settings.on) {
        get_log("rt_pbr: per-pixel lighting on (specular %.2f, normals %.2f%s%s%s)", static_cast<double>(settings.specular),
                static_cast<double>(settings.normal_strength), settings.objects ? ", objects too" : "",
                settings.relief ? text::format(", relief %.2f", static_cast<double>(settings.relief_strength)).c_str() : "",
                settings.characters ? text::format(", characters (skin %.2f, cloth %.2f, hair %.2f)",
                                                   static_cast<double>(settings.skin), static_cast<double>(settings.cloth),
                                                   static_cast<double>(settings.hair))
                                          .c_str()
                                    : "");
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
            draw.f0 = found->second.f0;
            draw.flags = static_cast<uint8_t>(kSurfaceLighting | kSurfaceMap | (lit && state.settings.objects ? kSurfaceLit : 0));
            if ((found->second.flags & surface_map_format::kFlagModel) != 0U) {
                // (added for the characters) no detail layer or relief - skin, cloth or hair shading where lit by lights
                if ((draw.flags & kSurfaceLit) != 0U) {
                    draw.detail = character_bits(found->second, state.draw_settings, &draw.detail_uniforms.character);
                }
                return draw;
            }
            const DetailDraw& detail = detail_of(found->second); // (added by the detail materials)
            if (detail.flags != 0) {
                draw.detail = detail.flags;
                draw.detail_map = detail.texture;
                draw.detail_uniforms = detail.uniforms;
                draw.f0 = detail.f0 >= 0.0F ? detail.f0 : draw.f0;
            }
            // (added by surface relief) parallax occlusion mapping where the kind has relief and the map has depth
            const RtPbrSettings& settings = state.draw_settings;
            const float depth = found->second.relief_depth * relief_weight(detail.kind) * settings.relief_strength;
            if (settings.relief && depth > kLeastRelief) {
                draw.detail = static_cast<uint8_t>(draw.detail | kDetailRelief);
                draw.detail_uniforms.relief = {depth, settings.relief_fade_start,
                                               std::max(settings.relief_fade_end, settings.relief_fade_start + kLeastFade),
                                               std::clamp(settings.relief_shadow, 0.0F, 1.0F)};
            }
            return draw;
        }
    }
    if (lit && state.settings.objects) {
        draw.flags = kSurfaceLighting | kSurfaceLit;
        draw.f0 = state.settings.f0;
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
    // The sun: the bake's (fitted with ray tracing on; the console's `rt sun` before it), else the game's shown one
    // at its own weight, else none (the sky's detail alone) - looked up once a frame.
    FrameSun& sun = state.sun;
    if (!sun.known || sun.serial != frame) {
        sun = {.serial = frame, .known = true};
        const rt::BakedSun& baked = rt::baked_sun();
        if (rt::sun_override(&sun.toward)) {
            sun.weight = 1.0F;
        } else if (baked.found) {
            sun.toward = baked.toward;
            sun.weight = 1.0F;
        } else if (settings.game_sun > 0.0F && rt::game_sun(&sun.toward)) {
            sun.weight = settings.game_sun;
        }
        const rt::SunLight light = rt::sun_light();
        if (light.on) {
            sun.colour = light.colour;
        }
    }
    uniforms->sun = sun.weight > 0.0F ? to_camera(view, sun.toward, sun.weight) : Float4{};
    uniforms->sun_colour = {core::at(sun.colour, 0), core::at(sun.colour, 1), core::at(sun.colour, 2), settings.ambient_share};
    uniforms->sky = to_camera(view, {0.0F, 1.0F, 0.0F}, settings.sky_detail);
    uniforms->params = {settings.specular, settings.normal_strength, settings.roughness, draw.f0};
    uniforms->shade = {settings.shade_low, settings.shade_high, settings.detail_low, settings.detail_high};
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
    const DetailDraw& detail = detail_of(found->second); // (added by the detail materials: the surface kind's F0)
    return detail.f0 >= 0.0F ? detail.f0 : found->second.f0;
}

float kind_gloss(uint8_t kind)
{
    namespace dmf = detail_material_format;
    switch (static_cast<dmf::Kind>(kind)) {
    case dmf::Kind::kMetal:
    case dmf::Kind::kTile:
        return 1.0F;
    case dmf::Kind::kStone:
        return kGlossStone;
    case dmf::Kind::kWood:
        return kGlossWood;
    default:
        return 0.0F; // (asphalt, concrete, brick, ground, grass, plaster, gravel: too rough to mirror anything)
    }
}

float class_gloss(uint8_t material_class)
{
    namespace smf = surface_map_format;
    switch (static_cast<smf::MaterialClass>(material_class)) {
    case smf::MaterialClass::kMetal:
    case smf::MaterialClass::kTile:
    case smf::MaterialClass::kGlass:
    case smf::MaterialClass::kWater:
        return 1.0F;
    case smf::MaterialClass::kStone:
        return kGlossStone;
    case smf::MaterialClass::kWood:
        return kGlossWood;
    case smf::MaterialClass::kDefault:
        return kGlossPaint;
    default:
        return 0.0F; // (asphalt, concrete, plants, ground, cloth and the characters' classes)
    }
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
    const DetailDraw& detail = detail_of(found->second);
    out->f0 = detail.f0 >= 0.0F ? detail.f0 : found->second.f0;
    // (the kind decides where there is one - brick is stone by its class, glass and water have no kind)
    const float by_class = class_gloss(found->second.material_class);
    out->gloss = detail.kind != 0 && by_class < 1.0F ? kind_gloss(detail.kind) : by_class;
    if (detail.flags != 0) {
        out->kind_roughness = std::get<0>(detail.uniforms.material);
        out->kind_roughness_weight = std::get<3>(detail.uniforms.material);
    }
    return true;
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

// The weights by kind: relief that is really there - brick and stone (cobbles, rock, blocks) whole, gravel, tiles and
// wood planks less, rough concrete and dirt a little; smooth asphalt and concrete, metal, grass, plaster and the kinds
// with none (glass, water, fabric, painted) flat - their maps' grain is paint and stains, not depth.
float relief_weight(uint8_t kind)
{
    namespace dmf = detail_material_format;
    switch (static_cast<dmf::Kind>(kind)) {
    case dmf::Kind::kBrick:
    case dmf::Kind::kStone:
        return 1.0F;
    case dmf::Kind::kGravel:
        return 0.8F;
    case dmf::Kind::kWood:
        return 0.7F;
    case dmf::Kind::kTile:
        return 0.6F;
    case dmf::Kind::kConcreteRough:
    case dmf::Kind::kGround:
        return 0.5F;
    default:
        return 0.0F;
    }
}

} // namespace render::vulkan
