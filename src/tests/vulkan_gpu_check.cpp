// NOT ORIGINAL - thug2_vulkan_gpu_check.exe: the Vulkan Gpu's resources (gpu_vulkan/gpu_vulkan.h) tried on this machine
// through render::Gpu and render::GpuContext, as the PC layer calls them. It opens a small SDL window with a Vulkan
// surface, makes the device through the GpuContext (Direct3D 9's present parameters as settings), then for each kind of
// resource makes it, fills it, reads it back from the device's memory and compares the bytes, and releases it:
// - textures in every format the game makes (A8R8G8B8, X8R8G8B8, R5G6B5, X1R5G5B5, A1R5G5B5, P8, DXT1, DXT3, DXT5),
//   every level of their chains, whole and partial locks, read-only locks, a level's surface; the 16-bit formats once as
//   the device has them and once widened to 8 bits (as on a device without them); P8 through its palette;
// - vertex and index buffers, static and dynamic (discard and no-overwrite locks, a discard while the device may still
//   read the old memory), references;
// - render targets (lockable, multisampled and resolved), render-target textures, depth surfaces (D24S8 / D24X8, else
//   D32S8; D16), depth textures, system-memory surfaces, copies between them, render targets and the front buffer read
//   back, the screen shot's JPEG;
// - occlusion queries, samplers from the stage states, pixel shaders, the ring the draws from memory copy into, the
//   state block's reads; two threads using the Gpu at once (the loading screen's timer thread does);
// - everything released: the allocator's count back where it began, then the device shut down.
//   thug2_vulkan_gpu_check [--device N] [--display N] [--no-validation]
// --device: the physical device, counted from 0 in the loader's order (default: the context's choice, discrete first).
// --display: the monitor the window opens on (1 = the first; default: the second when there are two).
// Exit code: 0 when every check passed, 1 when one failed, 2 when this machine has no Vulkan for this process.
#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <system_error>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <volk.h>

#include "tests/no_error_dialogs.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/parse_number.h"
#include "engine/file/disk.h"
#include "engine/gfx/gpu.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/gpu_handles.h"
#include "gpu_vulkan/gpu_vulkan.h"
#include "gpu_vulkan/vulkan_backend.h"
#include "gpu_vulkan/vulkan_context.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "gpu_vulkan/vulkan_jpeg.h"
#include "gpu_vulkan/vulkan_loader.h"
#include "gpu_vulkan/vulkan_objects.h"
#include "fs_win32/win32_disk.h"

namespace {

using render::GpuLockedRect;
using render::GpuRect;
using render::GpuSurface;
using render::GpuTexture;
namespace value = render::gpu_value;

constexpr int kWindowWidth = 640;
constexpr int kWindowHeight = 480;
constexpr int kBitsPerByte = 8;
constexpr uint32_t kTextureWidth = 64;
constexpr uint32_t kTextureHeight = 32;
constexpr uint32_t kOddWidth = 12; // a block-compressed chain whose small levels are partial blocks
constexpr uint32_t kOddHeight = 20;
constexpr uint32_t kBufferBytes = 4096;
constexpr uint32_t kPartOffset = 1000;
constexpr uint32_t kPartBytes = 200;
constexpr uint32_t kTargetWidth = 256;
constexpr uint32_t kTargetHeight = 128;
constexpr uint32_t kShotWidth = 160;
constexpr uint32_t kShotHeight = 120;
constexpr uint32_t kQueryCount = 100; // two pools' worth
constexpr int kRingDraws = 400;       // 400 draws of 4 KB: past the ring's first megabyte
constexpr uint32_t kRingVertices = 256;
constexpr uint32_t kRingStride = 16;
constexpr int kThreadRounds = 200;
constexpr uint32_t kPixelBytes = 4;
constexpr uint32_t kUsageDynamicWriteOnly = value::kUsageDynamic | value::kUsageWriteOnly;
constexpr uint32_t kFvf = 0x142; // XYZ | DIFFUSE | TEX1
constexpr uint32_t kAnisotropy = 16;
constexpr uint32_t kD3dUsageDepthStencil = 2;
constexpr uint8_t kOpaque = 0xFF;
constexpr uint32_t kByteMask = 0xFF;
constexpr uint32_t kKilobyte = 1024;
// a 16-bit texel's channels: their masks and where they start
constexpr uint32_t kMask5 = 0x1F;
constexpr uint32_t kMask6 = 0x3F;
constexpr uint32_t kGreenAt = 5;
constexpr uint32_t kRedAt565 = 11;
constexpr uint32_t kRedAt1555 = 10;
constexpr uint32_t kAlphaBit1555 = 0x8000;

struct Options {
    int display = 0;
    int device = -1;
    bool validation = true;
};

Options read_options(std::span<char*> arguments)
{
    Options options;
    for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string_view argument = core::at(arguments, i);
        const bool has_value = i + 1 < arguments.size();
        if (argument == "--no-validation") {
            options.validation = false;
        } else if (argument == "--device" && has_value) {
            options.device = text::to_int(core::at(arguments, ++i));
        } else if (argument == "--display" && has_value) {
            options.display = text::to_int(core::at(arguments, ++i));
        }
    }
    return options;
}

void print_line(const std::string& text) { text::print("  %s\n", text.c_str()); }

// The checks' tally.
struct Tally {
    int passed = 0;
    int failed = 0;
    void check(bool ok, const std::string& what)
    {
        if (ok) {
            ++passed;
        } else {
            ++failed;
        }
        text::print("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    }
};

// What makes one pattern differ from another.
struct Seed {
    uint32_t value = 0;
};

// Bytes no two places share (by position and a seed).
uint8_t pattern(Seed seed, std::size_t index)
{
    constexpr std::size_t kMix = 31;
    constexpr uint32_t kSeedMix = 17;
    constexpr std::size_t kHigh = 8;
    return static_cast<uint8_t>(((index * kMix) + (seed.value * kSeedMix) + (index >> kHigh)) & kByteMask);
}
std::vector<uint8_t> pattern_bytes(Seed seed, std::size_t count)
{
    std::vector<uint8_t> bytes(count);
    for (std::size_t i = 0; i < count; ++i) {
        core::at(bytes, i) = pattern(seed, i);
    }
    return bytes;
}

// `bytes` written through a lock's rows (pitch apart), `row_bytes` a row.
void write_rows(const GpuLockedRect& locked, std::span<const uint8_t> bytes, uint32_t row_bytes, uint32_t rows)
{
    const std::span<uint8_t> into(static_cast<uint8_t*>(locked.bits),
                                  (static_cast<std::size_t>(rows - 1) * static_cast<uint32_t>(locked.pitch)) +
                                      row_bytes);
    for (uint32_t row = 0; row < rows; ++row) {
        std::ranges::copy(bytes.subspan(static_cast<std::size_t>(row) * row_bytes, row_bytes),
                          into.subspan(static_cast<std::size_t>(row) * static_cast<uint32_t>(locked.pitch)).begin());
    }
}
std::vector<uint8_t> read_rows(const GpuLockedRect& locked, uint32_t row_bytes, uint32_t rows)
{
    const std::span<const uint8_t> from(static_cast<const uint8_t*>(locked.bits),
                                        (static_cast<std::size_t>(rows - 1) * static_cast<uint32_t>(locked.pitch)) +
                                            row_bytes);
    std::vector<uint8_t> bytes;
    for (uint32_t row = 0; row < rows; ++row) {
        const auto line = from.subspan(static_cast<std::size_t>(row) * static_cast<uint32_t>(locked.pitch), row_bytes);
        bytes.insert(bytes.end(), line.begin(), line.end());
    }
    return bytes;
}

// --- the expected texels: written out here from the formats' definitions (not with the backend's conversion) -----

// A channel of `bits` (its largest value `top`) as the nearest 8-bit value: round(value * 255 / top).
uint8_t nearest_byte(uint32_t value, uint32_t top)
{
    return static_cast<uint8_t>(((value * kByteMask) + (top / 2)) / top);
}

std::array<uint8_t, 4> widened_565(uint32_t word)
{
    const uint32_t red = (word >> kRedAt565) & kMask5;
    const uint32_t green = (word >> kGreenAt) & kMask6;
    const uint32_t blue = word & kMask5;
    return {nearest_byte(blue, kMask5), nearest_byte(green, kMask6), nearest_byte(red, kMask5), kOpaque};
}
std::array<uint8_t, 4> widened_1555(uint32_t word, bool has_alpha)
{
    const uint32_t red = (word >> kRedAt1555) & kMask5;
    const uint32_t green = (word >> kGreenAt) & kMask5;
    const uint32_t blue = word & kMask5;
    const bool opaque = !has_alpha || (word & kAlphaBit1555) != 0U;
    return {nearest_byte(blue, kMask5), nearest_byte(green, kMask5), nearest_byte(red, kMask5),
            static_cast<uint8_t>(opaque ? kOpaque : 0)};
}

// What the image holds for Direct3D 9 bytes `d3d` (tightly packed rows) of a level, for the plan's conversion.
std::vector<uint8_t> expected_texels(const render::vulkan::FormatPlan& plan, std::span<const uint8_t> d3d,
                                     uint32_t pixels)
{
    using render::vulkan::TexelConversion;
    if (plan.conversion == TexelConversion::kCopy) {
        return {d3d.begin(), d3d.end()};
    }
    std::vector<uint8_t> texels;
    texels.reserve(static_cast<std::size_t>(pixels) * kPixelBytes);
    for (uint32_t i = 0; i < pixels; ++i) {
        std::array<uint8_t, 4> texel{};
        if (plan.conversion == TexelConversion::kPalette) {
            const uint8_t index = core::at(d3d, i);
            texel = {index, index, index, kOpaque}; // the grey palette (vulkan_formats.h)
        } else {
            const uint32_t word = static_cast<uint32_t>(core::at(d3d, i * 2)) |
                                  (static_cast<uint32_t>(core::at(d3d, (i * 2) + 1)) << kBitsPerByte);
            texel = plan.conversion == TexelConversion::kR5g6b5
                        ? widened_565(word)
                        : widened_1555(word, plan.conversion == TexelConversion::kA1r5g5b5);
        }
        texels.insert(texels.end(), texel.begin(), texel.end());
    }
    return texels;
}

const char* format_name(uint32_t format)
{
    switch (format) {
    case value::kFormatA8R8G8B8:
        return "A8R8G8B8";
    case value::kFormatX8R8G8B8:
        return "X8R8G8B8";
    case value::kFormatR5G6B5:
        return "R5G6B5";
    case value::kFormatX1R5G5B5:
        return "X1R5G5B5";
    case value::kFormatA1R5G5B5:
        return "A1R5G5B5";
    case value::kFormatP8:
        return "P8";
    case value::kFormatDxt1:
        return "DXT1";
    case value::kFormatDxt3:
        return "DXT3";
    case value::kFormatDxt5:
        return "DXT5";
    case value::kFormatD24S8:
        return "D24S8";
    case value::kFormatD24X8:
        return "D24X8";
    case value::kFormatD16:
        return "D16";
    default:
        return "?";
    }
}

const char* vk_format_name(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_B8G8R8A8_UNORM:
        return "B8G8R8A8_UNORM";
    case VK_FORMAT_R5G6B5_UNORM_PACK16:
        return "R5G6B5_UNORM_PACK16";
    case VK_FORMAT_A1R5G5B5_UNORM_PACK16:
        return "A1R5G5B5_UNORM_PACK16";
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        return "BC1_RGBA_UNORM_BLOCK";
    case VK_FORMAT_BC2_UNORM_BLOCK:
        return "BC2_UNORM_BLOCK";
    case VK_FORMAT_BC3_UNORM_BLOCK:
        return "BC3_UNORM_BLOCK";
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return "D24_UNORM_S8_UINT";
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return "D32_SFLOAT_S8_UINT";
    case VK_FORMAT_D16_UNORM:
        return "D16_UNORM";
    default:
        return "other";
    }
}

// --- textures ----------------------------------------------------------------------------------------------------

// One level filled through a lock (seeded by format and level) and read back from the image; the bytes written.
std::vector<uint8_t> fill_level(render::Gpu& gpu, GpuTexture* texture, uint32_t format, uint32_t level, Tally* tally)
{
    using render::vulkan::Extent;
    const render::GpuSize size = [&] {
        render::GpuSize got;
        gpu.texture_level_size(texture, level, &got);
        return got;
    }();
    const render::vulkan::LevelLayout layout =
        render::vulkan::d3d_level_layout(format, Extent{.width = size.width, .height = size.height});
    const std::vector<uint8_t> bytes = pattern_bytes(Seed{format + level}, layout.bytes());
    GpuLockedRect locked;
    const bool lock_ok = gpu.lock_texture(texture, level, &locked, nullptr, 0) == value::kResultOk &&
                         std::cmp_greater_equal(locked.pitch, layout.pitch);
    if (lock_ok) {
        write_rows(locked, bytes, layout.pitch, layout.rows);
    }
    const bool unlock_ok = gpu.unlock_texture(texture, level) == value::kResultOk;
    if (!lock_ok || !unlock_ok) {
        tally->check(false, text::format("%s level %u locked and unlocked", format_name(format), level));
    }
    return bytes;
}

bool level_matches(GpuTexture* texture, uint32_t level, const std::vector<uint8_t>& expected)
{
    std::vector<uint8_t> texels;
    return render::vulkan::read_texture_level(texture, level, &texels) && texels == expected;
}

// A texture of `format`, its whole chain filled and read back; then a partial lock, a read-only lock and a level's surface.
void check_texture(render::Gpu& gpu, uint32_t format, render::vulkan::Extent top, Tally* tally)
{
    using render::vulkan::Extent;
    GpuTexture* texture = nullptr;
    const long made = gpu.create_texture({.width = top.width,
                                          .height = top.height,
                                          .levels = 0,
                                          .usage = 0,
                                          .format = format,
                                          .pool = value::kPoolManaged},
                                         &texture);
    if (made != value::kResultOk || texture == nullptr) {
        tally->check(false, text::format("%s %ux%u made", format_name(format), top.width, top.height));
        return;
    }
    const render::vulkan::Texture* kept = render::vulkan::texture_of(texture);
    const render::vulkan::FormatPlan plan = kept->plan;
    const uint32_t levels = gpu.texture_level_count(texture);
    bool all_levels = levels == render::vulkan::full_chain_levels(top);
    for (uint32_t level = 0; level < levels; ++level) {
        const std::vector<uint8_t> written = fill_level(gpu, texture, format, level, tally);
        const Extent extent = render::vulkan::level_extent(top, level);
        all_levels =
            all_levels && level_matches(texture, level, expected_texels(plan, written, extent.width * extent.height));
    }
    tally->check(all_levels, text::format("%-8s %2ux%-2u as %-21s %s%u levels written and read back",
                                          format_name(format), top.width, top.height, vk_format_name(plan.format),
                                          plan.alpha_one ? "(alpha 1) " : "", levels));

    // a partial lock of level 0 (blocks for BC: 8..24 x 4..12), the rest of the level kept
    const Extent extent = render::vulkan::level_extent(top, 0);
    const render::vulkan::LevelLayout layout = render::vulkan::d3d_level_layout(format, extent);
    std::vector<uint8_t> level0 = pattern_bytes(Seed{format}, layout.bytes()); // (fill_level's seed for level 0)
    constexpr uint32_t kLeft = 8;
    constexpr uint32_t kTop = 4;
    constexpr uint32_t kRight = 24;
    constexpr uint32_t kBottom = 12;
    constexpr GpuRect kArea{.left = kLeft, .top = kTop, .right = kRight, .bottom = kBottom};
    GpuLockedRect locked;
    const bool block = render::vulkan::d3d_block_bytes(format) != 0;
    const uint32_t unit = block ? render::vulkan::d3d_block_bytes(format) : render::vulkan::d3d_pixel_bytes(format);
    constexpr uint32_t kBlockSide = 4;
    const uint32_t scale = block ? kBlockSide : 1;
    const uint32_t across = (kRight - kLeft) / scale;
    const uint32_t down = (kBottom - kTop) / scale;
    const uint32_t first_row = kTop / scale;
    const uint32_t first_column = kLeft / scale;
    const std::vector<uint8_t> part = pattern_bytes(Seed{format * 3}, static_cast<std::size_t>(across) * unit * down);
    bool part_ok = extent.width >= kRight && gpu.lock_texture(texture, 0, &locked, &kArea, 0) == value::kResultOk;
    if (part_ok) {
        write_rows(locked, part, across * unit, down);
        part_ok = gpu.unlock_texture(texture, 0) == value::kResultOk;
        for (uint32_t row = 0; row < down; ++row) {
            std::ranges::copy(std::span(part).subspan(static_cast<std::size_t>(row) * across * unit, across * unit),
                              std::span(level0)
                                  .subspan((static_cast<std::size_t>(first_row + row) * layout.pitch) +
                                           (static_cast<std::size_t>(first_column) * unit))
                                  .begin());
        }
        part_ok = part_ok && level_matches(texture, 0, expected_texels(plan, level0, extent.width * extent.height));
    }
    if (extent.width >= kRight) {
        tally->check(part_ok,
                     text::format("%-8s a locked area written, the rest of the level kept", format_name(format)));
    }

    // a read-only lock gives the bytes as they are, and its unlock uploads nothing
    bool read_ok = gpu.lock_texture(texture, 0, &locked, nullptr, value::kLockReadOnly) == value::kResultOk &&
                   read_rows(locked, layout.pitch, layout.rows) == level0;
    read_ok = gpu.unlock_texture(texture, 0) == value::kResultOk && read_ok;
    tally->check(read_ok, text::format("%-8s a read-only lock reads what was written", format_name(format)));

    // level 1's surface: its description, a lock through it, the texture's reference it holds
    GpuSurface* surface = nullptr;
    render::GpuSurfaceDesc desc;
    const Extent second = render::vulkan::level_extent(top, 1);
    bool surface_ok = levels > 1 && gpu.texture_surface(texture, 1, &surface) == value::kResultOk &&
                      gpu.surface_desc(surface, &desc) == value::kResultOk && desc.width == second.width &&
                      desc.height == second.height && desc.format == format &&
                      gpu.lock_surface(surface, &locked, nullptr, value::kLockReadOnly) == value::kResultOk &&
                      gpu.unlock_surface(surface) == value::kResultOk;
    surface_ok = surface_ok && gpu.release_surface(surface) == 1;
    tally->check(surface_ok, text::format("%-8s level 1's surface (its size, a lock, the texture's reference)",
                                          format_name(format)));
    tally->check(gpu.release_texture(texture) == 0, text::format("%-8s released", format_name(format)));
}

void check_textures(render::Gpu& gpu, Tally* tally, bool widened)
{
    render::vulkan::Backend& state = render::vulkan::backend();
    const render::vulkan::FormatSupport offered = state.formats;
    if (widened) {
        // as on a device without the 16-bit formats: widened to 8 bits a channel
        state.formats.r5g6b5 = false;
        state.formats.a1r5g5b5 = false;
    }
    text::print("textures%s:\n", widened ? " (the 16-bit formats widened)" : "");
    const std::vector<uint32_t> formats =
        widened ? std::vector<uint32_t>{value::kFormatR5G6B5, value::kFormatX1R5G5B5, value::kFormatA1R5G5B5}
                : std::vector<uint32_t>{value::kFormatA8R8G8B8, value::kFormatX8R8G8B8, value::kFormatR5G6B5,
                                        value::kFormatX1R5G5B5, value::kFormatA1R5G5B5, value::kFormatP8,
                                        value::kFormatDxt1,     value::kFormatDxt3,     value::kFormatDxt5};
    for (const uint32_t format : formats) {
        check_texture(gpu, format, {.width = kTextureWidth, .height = kTextureHeight}, tally);
    }
    if (!widened) {
        for (const uint32_t format : {value::kFormatDxt1, value::kFormatDxt5, value::kFormatA8R8G8B8}) {
            check_texture(gpu, format, {.width = kOddWidth, .height = kOddHeight}, tally);
        }
    }
    state.formats = offered;
}

// --- surfaces ----------------------------------------------------------------------------------------------------

void check_surfaces(render::Gpu& gpu, render::GpuContext& context, Tally* tally)
{
    text::print("surfaces:\n");
    const render::vulkan::Backend& state = render::vulkan::backend();
    // a lockable render target: written through a lock, read back from the image
    GpuSurface* target = nullptr;
    bool ok = gpu.create_render_target({.width = kTargetWidth,
                                        .height = kTargetHeight,
                                        .format = value::kFormatA8R8G8B8,
                                        .samples = 0,
                                        .quality = 0,
                                        .lockable_or_discard = true},
                                       &target) == value::kResultOk;
    const std::vector<uint8_t> pixels =
        pattern_bytes(Seed{1}, static_cast<std::size_t>(kTargetWidth) * kTargetHeight * kPixelBytes);
    GpuLockedRect locked;
    if (ok && gpu.lock_surface(target, &locked, nullptr, 0) == value::kResultOk) {
        write_rows(locked, pixels, kTargetWidth * kPixelBytes, kTargetHeight);
        ok = gpu.unlock_surface(target) == value::kResultOk;
        std::vector<uint8_t> back;
        ok = ok && render::vulkan::read_surface(target, &back) && back == pixels;
    } else {
        ok = false;
    }
    tally->check(ok, "a lockable render target written through a lock and read back");

    // read_render_target into a system-memory surface
    GpuSurface* system = nullptr;
    ok = gpu.create_offscreen_surface(kTargetWidth, kTargetHeight, value::kFormatA8R8G8B8, value::kPoolSystemMemory,
                                      &system) == value::kResultOk &&
         gpu.read_render_target(target, system) == value::kResultOk &&
         gpu.lock_surface(system, &locked, nullptr, value::kLockReadOnly) == value::kResultOk &&
         read_rows(locked, kTargetWidth * kPixelBytes, kTargetHeight) == pixels;
    ok = gpu.unlock_surface(system) == value::kResultOk && ok;
    tally->check(ok, "a render target read into a system-memory surface");

    // a texture's level copied into a render target of its size (point filter): the same pixels; then scaled
    GpuTexture* texture = nullptr;
    GpuSurface* level0 = nullptr;
    GpuSurface* same_size = nullptr;
    ok = gpu.create_texture({.width = kTextureWidth,
                             .height = kTextureHeight,
                             .levels = 1,
                             .usage = 0,
                             .format = value::kFormatA8R8G8B8,
                             .pool = value::kPoolManaged},
                            &texture) == value::kResultOk;
    const std::vector<uint8_t> texels =
        ok ? fill_level(gpu, texture, value::kFormatA8R8G8B8, 0, tally) : std::vector<uint8_t>{};
    ok = ok && gpu.texture_surface(texture, 0, &level0) == value::kResultOk &&
         gpu.create_render_target({.width = kTextureWidth,
                                   .height = kTextureHeight,
                                   .format = value::kFormatA8R8G8B8,
                                   .samples = 0,
                                   .quality = 0,
                                   .lockable_or_discard = false},
                                  &same_size) == value::kResultOk &&
         gpu.copy_surface(level0, nullptr, same_size, nullptr, value::kFilterPoint) == value::kResultOk;
    std::vector<uint8_t> copied;
    tally->check(ok && render::vulkan::read_surface(same_size, &copied) && copied == texels,
                 "a texture level copied into a render target of its size: the same pixels");
    constexpr GpuRect kHalf{.left = 0, .top = 0, .right = kTextureWidth / 2, .bottom = kTextureHeight / 2};
    tally->check(gpu.copy_surface(level0, nullptr, target, &kHalf, value::kFilterLinear) == value::kResultOk,
                 "a texture level copied, scaled down with the linear filter");

    // a render-target texture: set as the target, asked back, the back buffer put back
    GpuTexture* drawn = nullptr;
    GpuSurface* drawn_level = nullptr;
    GpuSurface* asked = nullptr;
    GpuSurface* back = nullptr;
    ok = gpu.create_texture({.width = kTargetWidth,
                             .height = kTargetHeight,
                             .levels = 1,
                             .usage = value::kUsageRenderTarget,
                             .format = value::kFormatX8R8G8B8,
                             .pool = value::kPoolDefault},
                            &drawn) == value::kResultOk &&
         gpu.texture_surface(drawn, 0, &drawn_level) == value::kResultOk &&
         gpu.back_buffer(&back) == value::kResultOk && gpu.set_render_target(0, drawn_level) == value::kResultOk &&
         gpu.render_target(0, &asked) == value::kResultOk && asked == drawn_level &&
         gpu.copy_surface(back, nullptr, drawn_level, nullptr, value::kFilterPoint) == value::kResultOk &&
         gpu.set_render_target(0, back) == value::kResultOk;
    if (asked) {
        gpu.release_surface(asked);
    }
    tally->check(ok, "a render-target texture set as the target and asked back; the back buffer copied into it");

    // multisampled: made and resolved into a single-sampled target of its size
    const uint32_t samples = context.sample_quality_levels({.samples = 4,
                                                            .colour_format = value::kFormatX8R8G8B8,
                                                            .depth_format = value::kFormatD24X8,
                                                            .windowed = true}) != 0
                                 ? 4U
                                 : 2U;
    GpuSurface* multi = nullptr;
    GpuSurface* resolved = nullptr;
    ok = gpu.create_render_target({.width = kTargetWidth,
                                   .height = kTargetHeight,
                                   .format = value::kFormatX8R8G8B8,
                                   .samples = samples,
                                   .quality = 0,
                                   .lockable_or_discard = false},
                                  &multi) == value::kResultOk &&
         gpu.create_render_target({.width = kTargetWidth,
                                   .height = kTargetHeight,
                                   .format = value::kFormatX8R8G8B8,
                                   .samples = 0,
                                   .quality = 0,
                                   .lockable_or_discard = false},
                                  &resolved) == value::kResultOk &&
         gpu.copy_surface(multi, nullptr, resolved, nullptr, value::kFilterNone) == value::kResultOk;
    tally->check(ok, text::format("a %u-sample render target resolved into a single-sampled one", samples));

    // depth surfaces: D24S8 / D24X8 (D24_UNORM_S8_UINT, else D32_SFLOAT_S8_UINT), D16
    for (const uint32_t format : {value::kFormatD24S8, value::kFormatD24X8, value::kFormatD16}) {
        GpuSurface* depth = nullptr;
        ok = gpu.create_depth_surface({.width = kTargetWidth,
                                       .height = kTargetHeight,
                                       .format = format,
                                       .samples = 0,
                                       .quality = 0,
                                       .lockable_or_discard = true},
                                      &depth) == value::kResultOk &&
             gpu.set_depth_surface(depth) == value::kResultOk;
        const VkFormat as = depth ? render::vulkan::object(depth)->image.format : VK_FORMAT_UNDEFINED;
        tally->check(ok && gpu.release_surface(depth) == 1, text::format("depth %-5s as %s (set as the depth surface)",
                                                                         format_name(format), vk_format_name(as)));
        GpuSurface* own = state.auto_depth != nullptr ? render::vulkan::surface_handle(state.auto_depth) : nullptr;
        gpu.set_depth_surface(own);
    }
    if (context.depth_textures_supported()) {
        GpuTexture* depth_texture = nullptr;
        ok = gpu.create_texture({.width = kTargetWidth,
                                 .height = kTargetHeight,
                                 .levels = 1,
                                 .usage = kD3dUsageDepthStencil,
                                 .format = value::kFormatD24S8,
                                 .pool = value::kPoolDefault},
                                &depth_texture) == value::kResultOk;
        tally->check(ok && gpu.release_texture(depth_texture) == 0, "a depth texture (D24S8) made and released");
    } else {
        text::print("  (depth textures: not offered by this device)\n");
    }

    // the front buffer read into a display-sized surface; a JPEG of a system-memory surface
    render::GpuSize display;
    GpuSurface* front = nullptr;
    ok = gpu.display_size(&display) == value::kResultOk &&
         gpu.create_offscreen_surface(display.width, display.height, value::kFormatA8R8G8B8, value::kPoolScratch,
                                      &front) == value::kResultOk &&
         gpu.read_front_buffer(front) == value::kResultOk;
    tally->check(ok, text::format("the front buffer read into a %ux%u surface", display.width, display.height));
    GpuSurface* shot = nullptr;
    const std::vector<uint8_t> shot_pixels =
        pattern_bytes(Seed{7}, static_cast<std::size_t>(kShotWidth) * kShotHeight * kPixelBytes);
    ok = gpu.create_offscreen_surface(kShotWidth, kShotHeight, value::kFormatX8R8G8B8, value::kPoolScratch, &shot) ==
             value::kResultOk &&
         gpu.lock_surface(shot, &locked, nullptr, 0) == value::kResultOk;
    if (ok) {
        write_rows(locked, shot_pixels, kShotWidth * kPixelBytes, kShotHeight);
        ok = gpu.unlock_surface(shot) == value::kResultOk;
    }
    const char* const jpeg_file = "thug2_vulkan_gpu_check.jpg";
    std::vector<uint8_t> encoded;
    ok = ok && gpu.save_surface_jpeg(jpeg_file, shot) == value::kResultOk &&
         render::vulkan::encode_jpeg(value::kFormatX8R8G8B8, {.width = kShotWidth, .height = kShotHeight}, shot_pixels,
                                     &encoded);
    std::vector<uint8_t> saved;
    {
        std::ifstream file(jpeg_file, std::ios::binary);
        saved.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    std::error_code removed;
    std::filesystem::remove(jpeg_file, removed);
    constexpr uint8_t kMarker = 0xFF;
    constexpr uint8_t kStart = 0xD8;
    ok = ok && saved == encoded && saved.size() > 2 && saved.front() == kMarker && core::at(saved, 1) == kStart;
    tally->check(ok, text::format("the screen shot's JPEG of a %ux%u surface (%u bytes, a JPEG's start marker)",
                                  kShotWidth, kShotHeight, static_cast<uint32_t>(saved.size())));

    for (GpuSurface* surface : {target, system, level0, same_size, drawn_level, back, multi, resolved, front, shot}) {
        if (surface) {
            gpu.release_surface(surface);
        }
    }
    const bool released = gpu.release_texture(texture) == 0 && gpu.release_texture(drawn) == 0;
    tally->check(released, "the surfaces' textures released with them");
}

// --- buffers -----------------------------------------------------------------------------------------------------

bool lock_write(render::Gpu& gpu, render::GpuVertexBuffer* buffer, const std::array<uint32_t, 3>& lock,
                std::span<const uint8_t> bytes)
{
    // (lock: offset, bytes, flags)
    void* data = nullptr;
    if (gpu.lock_vertex_buffer(buffer, std::get<0>(lock), std::get<1>(lock), &data, std::get<2>(lock)) !=
        value::kResultOk) {
        return false;
    }
    std::ranges::copy(bytes, std::span(static_cast<uint8_t*>(data), bytes.size()).begin());
    return gpu.unlock_vertex_buffer(buffer) == value::kResultOk;
}

void check_buffers(render::Gpu& gpu, Tally* tally)
{
    text::print("buffers:\n");
    render::vulkan::Backend& state = render::vulkan::backend();
    // static: written whole, then a part; read back from the device; a read-only lock
    render::GpuVertexBuffer* fixed = nullptr;
    std::vector<uint8_t> expected = pattern_bytes(Seed{2}, kBufferBytes);
    bool ok = gpu.create_vertex_buffer(kBufferBytes, value::kUsageWriteOnly, kFvf, value::kPoolManaged, &fixed) ==
                  value::kResultOk &&
              lock_write(gpu, fixed, {0, 0, 0}, expected);
    std::vector<uint8_t> back;
    tally->check(ok && render::vulkan::read_vertex_buffer(fixed, &back) && back == expected,
                 "a static vertex buffer written whole, read back from the device");
    const std::vector<uint8_t> part = pattern_bytes(Seed{3}, kPartBytes);
    std::ranges::copy(part, std::next(expected.begin(), kPartOffset));
    ok = lock_write(gpu, fixed, {kPartOffset, kPartBytes, 0}, part);
    tally->check(ok && render::vulkan::read_vertex_buffer(fixed, &back) && back == expected,
                 "a part of it locked and written, the rest kept");
    void* data = nullptr;
    ok = gpu.lock_vertex_buffer(fixed, 0, 0, &data, value::kLockReadOnly) == value::kResultOk &&
         std::ranges::equal(std::span(static_cast<const uint8_t*>(data), kBufferBytes), expected);
    ok = gpu.unlock_vertex_buffer(fixed) == value::kResultOk && ok;
    tally->check(ok, "a read-only lock reads what was written");
    tally->check(gpu.retain_vertex_buffer(fixed) == 2 && gpu.release_vertex_buffer(fixed) == 1,
                 "a reference added and given back");

    // dynamic: a discard lock, a no-overwrite lock beside it, then a discard while the old memory may be in use
    render::GpuVertexBuffer* dynamic = nullptr;
    std::vector<uint8_t> first = pattern_bytes(Seed{4}, kBufferBytes);
    ok = gpu.create_vertex_buffer(kBufferBytes, kUsageDynamicWriteOnly, kFvf, value::kPoolDefault, &dynamic) ==
             value::kResultOk &&
         lock_write(gpu, dynamic, {0, 0, value::kLockDiscard}, first);
    tally->check(ok && render::vulkan::read_vertex_buffer(dynamic, &back) && back == first,
                 "a dynamic vertex buffer written after a discard, read back");
    std::ranges::copy(part, std::next(first.begin(), kPartOffset));
    ok = lock_write(gpu, dynamic, {kPartOffset, kPartBytes, value::kLockNoOverwrite}, part);
    tally->check(ok && render::vulkan::read_vertex_buffer(dynamic, &back) && back == first,
                 "a no-overwrite lock writes beside what is there");
    render::vulkan::Buffer* kept = render::vulkan::object(dynamic);
    core::at(kept->versions, kept->current).last_use =
        state.frames.completed() + 1; // (as if a frame not finished drew it)
    const std::vector<uint8_t> second = pattern_bytes(Seed{5}, kBufferBytes);
    ok = lock_write(gpu, dynamic, {0, 0, value::kLockDiscard}, second);
    const bool renamed = kept->versions.size() == 2 && kept->current == 1;
    std::vector<uint8_t> old_memory;
    const bool old_kept = state.memory.read_buffer(core::at(kept->versions, 0).memory, 0, kBufferBytes, &old_memory) &&
                          old_memory == first;
    tally->check(ok && renamed && old_kept && render::vulkan::read_vertex_buffer(dynamic, &back) && back == second,
                 "a discard while the device may read the memory: new memory, the old left as it was");
    core::at(kept->versions, 0).last_use = 0;

    // an index buffer (16-bit), static and dynamic
    for (const uint32_t usage : {value::kUsageWriteOnly, kUsageDynamicWriteOnly}) {
        render::GpuIndexBuffer* indices = nullptr;
        const std::vector<uint8_t> written = pattern_bytes(Seed{6 + usage}, kBufferBytes);
        ok = gpu.create_index_buffer(kBufferBytes, usage, value::kFormatIndex16,
                                     usage == value::kUsageWriteOnly ? value::kPoolManaged : value::kPoolDefault,
                                     &indices) == value::kResultOk &&
             gpu.lock_index_buffer(indices, 0, 0, &data, usage == value::kUsageWriteOnly ? 0 : value::kLockDiscard) ==
                 value::kResultOk;
        if (ok) {
            std::ranges::copy(written, std::span(static_cast<uint8_t*>(data), kBufferBytes).begin());
            ok = gpu.unlock_index_buffer(indices) == value::kResultOk;
        }
        render::GpuIndexBuffer* asked = nullptr;
        ok = ok && render::vulkan::read_index_buffer(indices, &back) && back == written &&
             gpu.set_indices(indices) == value::kResultOk && gpu.get_indices(&asked) == value::kResultOk &&
             asked == indices && gpu.release_index_buffer(asked) == 2 && gpu.set_indices(nullptr) == value::kResultOk;
        tally->check(ok && gpu.release_index_buffer(indices) == 0,
                     text::format("a %s index buffer written, read back, set and asked back, released",
                                  usage == value::kUsageWriteOnly ? "static" : "dynamic"));
    }

    // streams hold a reference
    uint32_t offset = 0;
    uint32_t stride = 0;
    render::GpuVertexBuffer* asked = nullptr;
    ok = gpu.set_stream_source(0, fixed, 0, kRingStride) == value::kResultOk &&
         gpu.get_stream_source(0, &asked, &offset, &stride) == value::kResultOk && asked == fixed &&
         stride == kRingStride && gpu.release_vertex_buffer(asked) == 2 && gpu.release_vertex_buffer(fixed) == 1 &&
         gpu.set_stream_source(0, nullptr, 0, 0) == value::kResultOk;
    tally->check(ok, "a stream holds its buffer until it is set to another");
    tally->check(gpu.release_vertex_buffer(dynamic) == 0, "the buffers released");
}

// --- queries, samplers, shaders, the ring, the state block -------------------------------------------------------

void check_others(render::Gpu& gpu, Tally* tally)
{
    text::print("queries, samplers, shaders, the ring, the state block:\n");
    const render::vulkan::Backend& state = render::vulkan::backend();
    std::vector<render::GpuQuery*> queries(kQueryCount, nullptr);
    bool ok = true;
    for (render::GpuQuery*& query : queries) {
        ok = gpu.create_occlusion_query(&query) == value::kResultOk && ok;
    }
    uint32_t pixels = 1;
    render::GpuQuery* const first = queries.front();
    ok = ok && state.query_pools.size() == 2 &&
         gpu.query_result(first, &pixels, sizeof(pixels), 0) == value::kResultNotFound &&
         gpu.begin_query(first) == value::kResultOk && gpu.end_query(first) == value::kResultOk &&
         gpu.query_result(first, &pixels, sizeof(pixels), 0) == value::kResultNotReady;
    for (render::GpuQuery* query : queries) {
        ok = gpu.release_query(query) == 0 && ok;
    }
    tally->check(ok, text::format("%u occlusion queries in %u pools; one begun and ended: not ready before its frame "
                                  "is submitted",
                                  kQueryCount,
                                  static_cast<uint32_t>(state.query_pools.size())));

    const VkSampler point = render::vulkan::stage_sampler(0);
    gpu.set_sampler_state(0, render::GpuSamplerState::kMinFilter, value::kFilterAnisotropic);
    gpu.set_sampler_state(0, render::GpuSamplerState::kMagFilter, value::kFilterLinear);
    gpu.set_sampler_state(0, render::GpuSamplerState::kMipFilter, value::kFilterLinear);
    gpu.set_sampler_state(0, render::GpuSamplerState::kMaxAnisotropy, kAnisotropy);
    gpu.set_sampler_state(0, render::GpuSamplerState::kAddressU, value::kAddressBorder);
    gpu.set_sampler_state(0, render::GpuSamplerState::kBorderColour, 0);
    const VkSampler anisotropic = render::vulkan::stage_sampler(0);
    gpu.set_sampler_state(1, render::GpuSamplerState::kAddressV, value::kAddressClamp);
    const VkSampler clamped = render::vulkan::stage_sampler(1);
    ok = point != VK_NULL_HANDLE && anisotropic != VK_NULL_HANDLE && clamped != VK_NULL_HANDLE &&
         point != anisotropic && clamped != point && render::vulkan::stage_sampler(0) == anisotropic &&
         state.samplers.size() == 3;
    tally->check(ok,
                 text::format("samplers from the stage states: point, anisotropic x%u with a border, clamped - made "
                              "once each (anisotropy %s, at most %.0f)",
                              kAnisotropy, state.sampler_limits.anisotropy ? "on" : "off",
                              static_cast<double>(state.sampler_limits.max_anisotropy)));
    gpu.reset_state_block();

    constexpr std::string_view kShader = "ps.1.1\ntex t0\nmul r0, v0, t0\n";
    render::GpuPixelShader* shader = nullptr;
    render::GpuPixelShader* asked = nullptr;
    ok = gpu.create_pixel_shader(kShader.data(), static_cast<uint32_t>(kShader.size()), &shader) == value::kResultOk &&
         render::vulkan::object(shader)->source == kShader && gpu.set_pixel_shader(shader) == value::kResultOk &&
         gpu.get_pixel_shader(&asked) == value::kResultOk && asked == shader && gpu.release_pixel_shader(asked) == 1 &&
         gpu.set_pixel_shader(nullptr) == value::kResultOk && gpu.release_pixel_shader(shader) == 0;
    tally->check(ok, "a pixel shader kept as its text, set and asked back, released");

    const std::vector<uint8_t> vertices = pattern_bytes(Seed{8}, static_cast<std::size_t>(kRingVertices) * kRingStride);
    ok = true;
    for (int draw = 0; draw < kRingDraws; ++draw) {
        ok = gpu.draw_from_memory(render::GpuPrimitive::kPointList, kRingVertices, vertices.data(), kRingStride) ==
                 value::kResultOk &&
             ok;
    }
    const VkDeviceSize grown = state.ring.capacity();
    ok = ok && grown > render::vulkan::UploadRing::kFirstBytes && state.ring.used() > 0;
    gpu.present();
    tally->check(ok && state.ring.used() == 0,
                 text::format("%d draws from memory copied into the ring (grown to %u KB); "
                              "the next frame starts it again",
                              kRingDraws, static_cast<uint32_t>(grown / kKilobyte)));

    constexpr uint32_t kFogColour = 0xFF336699U;
    constexpr render::GpuMatrix kMatrix{2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 5, 6, 7, 1};
    uint32_t colour = 0;
    render::GpuMatrix matrix{};
    gpu.set_render_state(render::GpuRenderState::kFogColour, kFogColour);
    gpu.set_transform(render::GpuTransform::kView, kMatrix.data());
    ok = gpu.get_render_state(render::GpuRenderState::kFogColour, &colour) == value::kResultOk &&
         colour == kFogColour && gpu.get_transform(render::GpuTransform::kView, matrix.data()) == value::kResultOk &&
         matrix == kMatrix;
    tally->check(ok, "the state block answers the reads (a render state, a transform)");
    gpu.reset_state_block();
}

// Two threads at once: textures made, filled and released on one, states set and draws from memory on the other (the
// loading screen's timer thread draws while the main thread loads).
// --- draws -------------------------------------------------------------------------------------------------------

// A pre-transformed vertex with a colour (D3DFVF_XYZRHW | D3DFVF_DIFFUSE).
struct ScreenVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float rhw = 1.0F;
    uint32_t colour = 0;
};
constexpr uint32_t kScreenFvf = 0x004U | 0x040U;
constexpr uint32_t kClearBlue = 0xFF000080U;  // (A8R8G8B8)
constexpr uint32_t kDrawnGreen = 0xFF00C000U;
constexpr uint32_t kDrawnRed = 0xFFC00000U;
constexpr int kPresentsForResult = 8;

// The back buffer's pixel at (x, y) as A8R8G8B8 (its bytes are B, G, R, A).
uint32_t pixel_at(const std::vector<uint8_t>& pixels, uint32_t x, uint32_t y)
{
    const std::size_t at = ((static_cast<std::size_t>(y) * kWindowWidth) + x) * kPixelBytes;
    uint32_t value = 0;
    for (std::size_t byte = 0; byte < kPixelBytes; ++byte) {
        value |= static_cast<uint32_t>(core::at(pixels, at + byte)) << (byte * kBitsPerByte);
    }
    return value;
}

// A rectangle of the screen as two triangles (a strip).
std::array<ScreenVertex, 4> screen_rect(float left, float top, float right, float bottom, uint32_t colour)
{
    return {{{.x = left, .y = top, .colour = colour},
             {.x = right, .y = top, .colour = colour},
             {.x = left, .y = bottom, .colour = colour},
             {.x = right, .y = bottom, .colour = colour}}};
}

// The draws end to end: a clear opening the back buffer's pass, a pre-transformed strip from memory inside an occlusion
// query (the vertex colour through stage 0), an indexed draw from memory, the frame presented; the back buffer read
// back and the query's pixels counted.
void check_draws(render::Gpu& gpu, Tally* tally)
{
    text::print("draws:\n");
    using render::GpuRenderState;
    using render::GpuStageState;
    const auto width = static_cast<float>(kWindowWidth);
    const auto height = static_cast<float>(kWindowHeight);
    gpu.set_viewport({.x = 0, .y = 0, .width = kWindowWidth, .height = kWindowHeight, .min_z = 0.0F, .max_z = 1.0F});
    gpu.set_render_state(GpuRenderState::kZEnable, 0);
    gpu.set_render_state(GpuRenderState::kLighting, 0);
    gpu.set_render_state(GpuRenderState::kCullMode, value::kCullNone);
    gpu.set_render_state(GpuRenderState::kAlphaBlendEnable, 0);
    gpu.set_vertex_format(kScreenFvf);
    gpu.set_pixel_shader(nullptr);
    gpu.set_stage_state(0, GpuStageState::kColourOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kColourArg1, value::kArgDiffuse);
    gpu.set_stage_state(0, GpuStageState::kAlphaOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kAlphaArg1, value::kArgDiffuse);
    gpu.set_stage_state(1, GpuStageState::kColourOp, value::kOpDisable);
    gpu.set_stage_state(1, GpuStageState::kAlphaOp, value::kOpDisable);
    bool ok = gpu.clear(0, nullptr, value::kClearTarget | value::kClearDepth, kClearBlue, 1.0F, 0) == value::kResultOk;
    render::GpuQuery* query = nullptr;
    ok = ok && gpu.create_occlusion_query(&query) == value::kResultOk && gpu.begin_query(query) == value::kResultOk;
    // the left half green
    const std::array<ScreenVertex, 4> left = screen_rect(0.0F, 0.0F, width / 2, height, kDrawnGreen);
    ok = ok && gpu.draw_from_memory(render::GpuPrimitive::kTriangleStrip, 2, left.data(), sizeof(ScreenVertex)) ==
                   value::kResultOk;
    ok = ok && gpu.end_query(query) == value::kResultOk;
    // the right half's top quarter red: indexed, two triangles
    const std::array<ScreenVertex, 4> corner = screen_rect(width / 2, 0.0F, width, height / 4, kDrawnRed);
    constexpr std::array<uint16_t, 6> kIndices{0, 1, 2, 2, 1, 3};
    ok = ok && gpu.draw_indexed_from_memory(render::GpuPrimitive::kTriangleList, 0, 4, 2, kIndices.data(),
                                            value::kFormatIndex16, corner.data(),
                                            sizeof(ScreenVertex)) == value::kResultOk;
    ok = ok && gpu.present() == value::kResultOk;
    uint32_t pixels = 0;
    long result = value::kResultNotReady;
    for (int frame = 0; ok && frame < kPresentsForResult && result != value::kResultOk; ++frame) {
        result = gpu.query_result(query, &pixels, sizeof(pixels), 0);
        if (result != value::kResultOk) {
            ok = gpu.present() == value::kResultOk;
        }
    }
    const uint32_t half = (kWindowWidth / 2) * kWindowHeight;
    tally->check(ok && result == value::kResultOk && pixels == half,
                 text::format("an occlusion query around a draw: %u pixels (%u expected)", pixels, half));
    if (query != nullptr) {
        gpu.release_query(query);
    }
    render::GpuSurface* back = nullptr;
    std::vector<uint8_t> shown;
    ok = gpu.back_buffer(&back) == value::kResultOk && render::vulkan::read_surface(back, &shown) &&
         shown.size() == static_cast<std::size_t>(kWindowWidth) * kWindowHeight * kPixelBytes;
    if (back != nullptr) {
        gpu.release_surface(back);
    }
    const auto at = [&shown](uint32_t x, uint32_t y) { return pixel_at(shown, x, y); };
    constexpr uint32_t kRgb = 0x00FFFFFFU;
    constexpr uint32_t kInside = 10; // (a pixel well inside the left half)
    ok = ok && (at(kInside, kInside) & kRgb) == (kDrawnGreen & kRgb) &&
         (at((kWindowWidth / 2) - 1, kWindowHeight - 1) & kRgb) == (kDrawnGreen & kRgb);
    tally->check(ok, text::format("a pre-transformed strip from memory: the left half 0x%08X",
                                  ok ? at(kInside, kInside) : 0U));
    const bool corner_ok = ok && (at(kWindowWidth - 1, 0) & kRgb) == (kDrawnRed & kRgb) &&
                           (at(kWindowWidth / 2, (kWindowHeight / 4) - 1) & kRgb) == (kDrawnRed & kRgb) &&
                           (at(kWindowWidth / 2, kWindowHeight / 4) & kRgb) == (kClearBlue & kRgb);
    tally->check(corner_ok, text::format("an indexed draw from memory: the top right quarter 0x%08X",
                                         ok ? at(kWindowWidth - 1, 0) : 0U));
    const bool clear_ok = ok && (at(kWindowWidth - 1, kWindowHeight - 1) & kRgb) == (kClearBlue & kRgb);
    tally->check(clear_ok, text::format("the clear that opened the pass: the rest 0x%08X",
                                        ok ? at(kWindowWidth - 1, kWindowHeight - 1) : 0U));
    const render::vulkan::Backend& state = render::vulkan::backend();
    tally->check(state.pipelines.stats().pipelines >= 1 && state.pipelines.stats().refused == 0,
                 text::format("pipelines made: %u, refused: %u", static_cast<unsigned>(state.pipelines.stats().pipelines),
                              static_cast<unsigned>(state.pipelines.stats().refused)));
}

// A pre-transformed vertex with a colour and a texture coordinate (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1).
struct TexturedScreenVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float rhw = 1.0F;
    uint32_t colour = 0;
    float u = 0.0F;
    float v = 0.0F;
};
constexpr uint32_t kTexturedScreenFvf = kScreenFvf | 0x100U;

// The screen effects' pattern: a render-target texture set as the target (the viewport follows it, as Direct3D 9's
// SetRenderTarget resets it), cleared - its pass opened - then the back buffer set and the texture drawn from at once:
// what the texture's pass drew is what is sampled (the pass on the other target ends first).
void check_target_chain(render::Gpu& gpu, Tally* tally)
{
    text::print("render-target chain:\n");
    using render::GpuStageState;
    constexpr uint32_t kSide = 64;
    GpuTexture* texture = nullptr;
    GpuSurface* level = nullptr;
    GpuSurface* back = nullptr;
    bool ok = gpu.create_texture({.width = kSide,
                                  .height = kSide,
                                  .levels = 1,
                                  .usage = value::kUsageRenderTarget,
                                  .format = value::kFormatX8R8G8B8,
                                  .pool = value::kPoolDefault},
                                 &texture) == value::kResultOk &&
              gpu.texture_surface(texture, 0, &level) == value::kResultOk && gpu.back_buffer(&back) == value::kResultOk;
    render::GpuViewport viewport{};
    ok = ok && gpu.set_render_target(0, level) == value::kResultOk && gpu.get_viewport(&viewport) == value::kResultOk;
    tally->check(ok && viewport.x == 0 && viewport.y == 0 && viewport.width == kSide && viewport.height == kSide,
                 text::format("set_render_target resets the viewport to the target: %ux%u", viewport.width,
                              viewport.height));
    ok = ok && gpu.clear(0, nullptr, value::kClearTarget, kDrawnGreen, 1.0F, 0) == value::kResultOk;
    ok = ok && gpu.set_render_target(0, back) == value::kResultOk && gpu.get_viewport(&viewport) == value::kResultOk &&
         viewport.width == static_cast<uint32_t>(kWindowWidth) && viewport.height == static_cast<uint32_t>(kWindowHeight);
    gpu.set_depth_surface(nullptr);
    gpu.set_vertex_format(kTexturedScreenFvf);
    gpu.set_texture(0, texture);
    gpu.set_stage_state(0, GpuStageState::kColourOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kColourArg1, value::kArgTexture);
    gpu.set_stage_state(0, GpuStageState::kAlphaOp, value::kOpSelectArg1);
    gpu.set_stage_state(0, GpuStageState::kAlphaArg1, value::kArgTexture);
    const auto width = static_cast<float>(kWindowWidth);
    const auto height = static_cast<float>(kWindowHeight);
    const std::array<TexturedScreenVertex, 4> quad{{{.x = 0.0F, .y = 0.0F, .colour = kDrawnRed, .u = 0.0F, .v = 0.0F},
                                                     {.x = width, .y = 0.0F, .colour = kDrawnRed, .u = 1.0F, .v = 0.0F},
                                                     {.x = 0.0F, .y = height, .colour = kDrawnRed, .u = 0.0F, .v = 1.0F},
                                                     {.x = width, .y = height, .colour = kDrawnRed, .u = 1.0F, .v = 1.0F}}};
    ok = ok && gpu.draw_from_memory(render::GpuPrimitive::kTriangleStrip, 2, quad.data(), sizeof(TexturedScreenVertex)) ==
                   value::kResultOk;
    gpu.set_texture(0, nullptr);
    std::vector<uint8_t> shown;
    ok = ok && render::vulkan::read_surface(back, &shown) &&
         shown.size() == static_cast<std::size_t>(kWindowWidth) * kWindowHeight * kPixelBytes;
    constexpr uint32_t kRgb = 0x00FFFFFFU;
    const uint32_t middle = ok ? pixel_at(shown, kWindowWidth / 2, kWindowHeight / 2) : 0U;
    tally->check(ok && (middle & kRgb) == (kDrawnGreen & kRgb),
                 text::format("a target drawn from straight after its pass: 0x%08X (0x%08X expected)", middle,
                              kDrawnGreen));
    for (GpuSurface* surface : {level, back}) {
        if (surface != nullptr) {
            gpu.release_surface(surface);
        }
    }
    if (texture != nullptr) {
        gpu.release_texture(texture);
    }
}

// The gamma ramp: the identity changes nothing (no pass); a ramp that halves every value, put through the pass, halves
// a render target's pixels (each entry's top 8 bits); the ramp put back.
void check_gamma(render::Gpu& gpu, Tally* tally)
{
    text::print("gamma ramp:\n");
    constexpr uint32_t kEntryShift = 8; // (an entry's value in its top 8 bits)
    render::GpuGammaRamp identity{};
    render::GpuGammaRamp halving{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        for (std::size_t index = 0; index < render::kGpuGammaSteps; ++index) {
            core::at(core::at(identity, channel), index) = static_cast<uint16_t>(index << kEntryShift);
            core::at(core::at(halving, channel), index) = static_cast<uint16_t>((index / 2) << kEntryShift);
        }
    }
    GpuSurface* target = nullptr;
    bool ok = gpu.create_render_target({.width = kTargetWidth,
                                        .height = kTargetHeight,
                                        .format = value::kFormatX8R8G8B8,
                                        .samples = 0,
                                        .quality = 0,
                                        .lockable_or_discard = false},
                                       &target) == value::kResultOk;
    GpuSurface* back = nullptr;
    ok = ok && gpu.back_buffer(&back) == value::kResultOk && gpu.set_render_target(0, target) == value::kResultOk;
    gpu.set_depth_surface(nullptr);
    constexpr uint32_t kColour = 0xFF8040C2U; // (A8R8G8B8)
    ok = ok && gpu.clear(0, nullptr, value::kClearTarget, kColour, 1.0F, 0) == value::kResultOk;
    gpu.set_gamma_ramp(identity);
    std::vector<uint8_t> shown;
    tally->check(ok && !render::vulkan::read_gamma_pass(target, &shown), "the identity ramp: no gamma pass");
    gpu.set_gamma_ramp(halving);
    ok = ok && render::vulkan::read_gamma_pass(target, &shown) &&
         shown.size() == static_cast<std::size_t>(kTargetWidth) * kTargetHeight * kPixelBytes;
    // (the middle pixel of a kTargetWidth-wide image, its bytes B G R A)
    const std::size_t middle = ((static_cast<std::size_t>(kTargetHeight / 2) * kTargetWidth) + (kTargetWidth / 2)) * kPixelBytes;
    uint32_t pixel = 0;
    for (std::size_t byte = 0; ok && byte < kPixelBytes; ++byte) {
        pixel |= static_cast<uint32_t>(core::at(shown, middle + byte)) << (byte * kBitsPerByte);
    }
    constexpr uint32_t kHalved = 0xFF402061U;
    tally->check(ok && pixel == kHalved,
                 text::format("a ramp halving the values: 0x%08X through the pass (0x%08X expected)", pixel, kHalved));
    gpu.set_gamma_ramp(identity);
    if (back != nullptr) {
        gpu.set_render_target(0, back);
        gpu.release_surface(back);
    }
    if (target != nullptr) {
        gpu.release_surface(target);
    }
}

void check_threads(render::Gpu& gpu, Tally* tally)
{
    text::print("threads:\n");
    std::atomic<int> failures{0};
    std::thread loader([&gpu, &failures] {
        for (int round = 0; round < kThreadRounds; ++round) {
            GpuTexture* texture = nullptr;
            GpuLockedRect locked;
            if (gpu.create_texture({.width = kTextureWidth,
                                    .height = kTextureHeight,
                                    .levels = 1,
                                    .usage = 0,
                                    .format = value::kFormatA8R8G8B8,
                                    .pool = value::kPoolManaged},
                                   &texture) != value::kResultOk ||
                gpu.lock_texture(texture, 0, &locked, nullptr, 0) != value::kResultOk ||
                gpu.unlock_texture(texture, 0) != value::kResultOk || gpu.release_texture(texture) != 0) {
                ++failures;
            }
        }
    });
    const std::vector<uint8_t> vertices = pattern_bytes(Seed{9}, static_cast<std::size_t>(kRingVertices) * kRingStride);
    for (int round = 0; round < kThreadRounds; ++round) {
        if (gpu.set_render_state(render::GpuRenderState::kZEnable, 1) != value::kResultOk ||
            gpu.draw_from_memory(render::GpuPrimitive::kTriangleStrip, kRingVertices - 2, vertices.data(),
                                 kRingStride) != value::kResultOk ||
            gpu.present() != value::kResultOk) {
            ++failures;
        }
    }
    loader.join();
    tally->check(failures.load() == 0,
                 text::format("%d rounds on each of two threads, 0 failed (%d)", kThreadRounds, failures.load()));
}

int window_position(int display)
{
    const int displays = SDL_GetNumVideoDisplays();
    int chosen = display;
    if (chosen <= 0) {
        chosen = displays > 1 ? 2 : 1;
    }
    if (chosen > displays) {
        return static_cast<int>(SDL_WINDOWPOS_CENTERED);
    }
    return static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(chosen - 1));
}

int run(const Options& options, SDL_Window* window)
{
    render::vulkan::set_context_options(
        {.validation = options.validation, .device_index = options.device, .on_message = print_line}, window);
    render::GpuContext& context = render::vulkan::gpu_context();
    render::Gpu& gpu = render::vulkan::gpu();
    if (!context.create_instance()) {
        text::print("the device could not be opened\n");
        return render::vulkan::loader_version() == 0 ? 2 : 1;
    }
    const render::vulkan::Backend& state = render::vulkan::backend();
    text::print("validation layer: %s\n",
                state.context.validation_on() ? "on" : "off (not installed, or not asked for)");
    context.configure({.width = kWindowWidth,
                       .height = kWindowHeight,
                       .windowed = true,
                       .vsync = true,
                       .samples = 0,
                       .sample_quality = 0,
                       .colour_format = value::kFormatX8R8G8B8,
                       .depth_format = value::kFormatD24X8});
    const long made = context.create_device(
        {.width = kWindowWidth, .height = kWindowHeight, .hardware_vertices = true, .samples = 0, .sample_quality = 0});
    if (made != value::kResultOk) {
        text::print("create_device failed (%ld)\n", made);
        context.shutdown();
        return 1;
    }
    context.mark_ready();
    const render::GpuAdapterInfo& adapter = context.adapter();
    const render::GpuCaps caps = context.caps();
    text::print("device: %s (vendor 0x%04X, device 0x%04X)\n", adapter.description.data(), adapter.vendor_id,
                adapter.device_id);
    text::print("caps: ps.1.1 %d, stream offsets %d, anisotropy %u (minification %d), border %d, blend factor %d\n",
                static_cast<int>(caps.pixel_shaders_1_1), static_cast<int>(caps.stream_offset), caps.max_anisotropy,
                static_cast<int>(caps.min_filter_anisotropic), static_cast<int>(caps.border_addressing),
                static_cast<int>(caps.blend_factor));
    text::print("formats: R5G6B5 %s, A1R5G5B5 %s, BC %s, depth / stencil %s; depth textures %s\n",
                state.formats.r5g6b5 ? "native" : "widened", state.formats.a1r5g5b5 ? "native" : "widened",
                state.formats.block_compressed ? "yes" : "NO", vk_format_name(state.formats.depth_stencil),
                context.depth_textures_supported() ? "yes" : "no");
    const uint32_t baseline = state.memory.statistics().total.statistics.allocationCount;
    text::print("memory: %u allocations for the device's own objects (back buffer, depth, ring)\n", baseline);

    Tally tally;
    check_textures(gpu, &tally, false);
    check_textures(gpu, &tally, true);
    check_surfaces(gpu, context, &tally);
    check_buffers(gpu, &tally);
    check_others(gpu, &tally);
    check_draws(gpu, &tally);
    check_target_chain(gpu, &tally);
    check_gamma(gpu, &tally);
    check_threads(gpu, &tally);

    const VmaTotalStatistics after = state.memory.statistics();
    const bool all_released = after.total.statistics.allocationCount == baseline && state.textures.size() == 0 &&
                              state.buffers.size() == 0 && state.shaders.size() == 0 && state.queries.size() == 0 &&
                              state.surfaces.size() == 2;
    tally.check(all_released, text::format("everything released: %u allocations (%u KB), the device's 2 surfaces left",
                                           after.total.statistics.allocationCount,
                                           static_cast<uint32_t>(after.total.statistics.allocationBytes / kKilobyte)));
    context.shutdown();
    tally.check(!context.device_made() && state.memory.statistics().total.statistics.allocationCount == 0,
                "the device shut down");
    text::print("%d checks passed, %d failed\n", tally.passed, tally.failed);
    return tally.failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    tests::no_error_dialogs();
    const Options options = read_options(std::span(argv, static_cast<size_t>(argc)));
    text::print("thug2_vulkan_gpu_check: a %d-bit process\n", static_cast<int>(sizeof(void*)) * kBitsPerByte);
    if (!render::vulkan::open_loader()) {
        text::print("no Vulkan loader (vulkan-1.dll) for this process\n");
        return 2;
    }
    fs::install_disk(fs::win32::disk()); // (the screen shot's JPEG is written through the game's file seam)
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        text::print("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const int position = window_position(options.display);
    SDL_Window* window =
        SDL_CreateWindow("thug2 Vulkan Gpu check", position, position, kWindowWidth, kWindowHeight, SDL_WINDOW_VULKAN);
    if (window == nullptr) {
        text::print("SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    // (a thread the check starts can throw - std::thread's system_error: reported as a failure)
    int result = 1;
    try {
        result = run(options, window);
    } catch (const std::exception& error) {
        text::print("the check stopped: %s\n", error.what());
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
