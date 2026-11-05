#pragma once

#include <cstdint>
#include <vector>

#include "gpu_vulkan/vulkan_formats.h"

// NOT ORIGINAL - the screen shot script command's JPEG for the Vulkan Gpu (Gpu::save_surface_jpeg; Direct3D 9 writes it
// with D3DX): a system-memory surface's pixels made RGB (platform/screenshot_files.h) and encoded by stb_image_write, written through fs::disk().
namespace render::vulkan {

// The JPEG's quality (stb_image_write's 1-100 scale).
inline constexpr int kJpegQuality = 90;

// `pixels` (rows of `extent.width` pixels in `d3d_format`: X8R8G8B8 / A8R8G8B8, R5G6B5, X1R5G5B5) written to `file`
// as a JPEG; false when the format is not one of those or the file couldn't be written.
bool write_jpeg_file(const char* file, uint32_t d3d_format, Extent extent, const std::vector<uint8_t>& pixels);
// The same JPEG's bytes, without the file (the check compares them).
bool encode_jpeg(uint32_t d3d_format, Extent extent, const std::vector<uint8_t>& pixels, std::vector<uint8_t>* jpeg);

} // namespace render::vulkan
