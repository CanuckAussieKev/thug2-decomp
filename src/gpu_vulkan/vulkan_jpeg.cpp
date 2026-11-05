// NOT ORIGINAL - the Vulkan Gpu's JPEG writer (vulkan_jpeg.h).
#include "gpu_vulkan/vulkan_jpeg.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "engine/file/disk.h"
#include "engine/gfx/gpu.h"
#include "gpu_vulkan/vulkan_formats.h"
#include "platform/screenshot_files.h"
#include "stb_image_write.h"

namespace render::vulkan {

namespace {

constexpr int kRgbChannels = 3;

sys::screenshot::PixelFormat pixel_format_of(uint32_t d3d_format)
{
    using sys::screenshot::PixelFormat;
    switch (d3d_format) {
    case gpu_value::kFormatA8R8G8B8:
        return PixelFormat::kA8r8g8b8;
    case gpu_value::kFormatX8R8G8B8:
        return PixelFormat::kX8r8g8b8;
    case gpu_value::kFormatR5G6B5:
        return PixelFormat::kR5g6b5;
    case gpu_value::kFormatX1R5G5B5:
        return PixelFormat::kX1r5g5b5;
    default:
        return PixelFormat::kUnknown;
    }
}

void append_bytes(std::vector<uint8_t>* out, std::span<const uint8_t> bytes)
{
    out->insert(out->end(), bytes.begin(), bytes.end());
}

// stb_image_write's output, appended to the vector it was given.
void append(void* context, void* data, int size)
{
    append_bytes(static_cast<std::vector<uint8_t>*>(context),
                 std::span(static_cast<const uint8_t*>(data), static_cast<std::size_t>(size)));
}

} // namespace

bool encode_jpeg(uint32_t d3d_format, Extent extent, const std::vector<uint8_t>& pixels, std::vector<uint8_t>* jpeg)
{
    jpeg->clear();
    sys::screenshot::TickCycle frame;
    frame.width = static_cast<int>(extent.width);
    frame.height = static_cast<int>(extent.height);
    frame.format = pixel_format_of(d3d_format);
    frame.pitch = frame.width * sys::screenshot::get_bytes_per_pixel(frame.format);
    frame.bytes = pixels;
    std::vector<uint8_t> rgb;
    if (!sys::screenshot::to_rgb(frame, &rgb)) {
        return false;
    }
    return stbi_write_jpg_to_func(append, jpeg, frame.width, frame.height, kRgbChannels, rgb.data(), kJpegQuality) != 0;
}

bool write_jpeg_file(const char* file, uint32_t d3d_format, Extent extent, const std::vector<uint8_t>& pixels)
{
    std::vector<uint8_t> jpeg;
    if (!encode_jpeg(d3d_format, extent, pixels, &jpeg)) {
        return false;
    }
    // (through the game's file seam, as every file it writes)
    fs::DiskStream* const out = fs::disk().open_stream(file, "wb", fs::StreamSharing::kAll);
    if (!out) {
        return false;
    }
    const std::size_t written = fs::disk().stream_write(out, jpeg.data(), 1, jpeg.size());
    return fs::disk().stream_close(out) == 0 && written == jpeg.size();
}

} // namespace render::vulkan
