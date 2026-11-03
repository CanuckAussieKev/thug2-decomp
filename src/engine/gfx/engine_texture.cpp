// The engine's texture and the folded virtual-slot dispatchers (see engine_texture.h).

#include "engine/gfx/engine_texture.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <string_view>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"

namespace render {

// 0x0049F220 (the original sprintfs into its 512 bytes unbounded)
bool BitmapTexture::load_by_name(const char* name, int in_images, int argument)
{
    std::array<char, 0x200> path{};
    text::format_to(path, "%s", name);
    if (static_cast<uint8_t>(in_images)) {
        if (std::string_view(name).starts_with("../"))
            text::format_to(path, "%s", std::next(name, 3));
        else
            text::format_to(path, "images/%s", name);
    }
    int start = static_cast<int>(std::strlen(path.data()));
    while (start > 0 && core::at(path, start - 1) != '\\' && core::at(path, start - 1) != '/')
        --start;
    checksum_ = checksums::hash_from_text(std::next(path.data(), start));
    return ingest_file(path.data(), in_images, argument);
}

// 0x0049F300
bool BitmapTexture::read_from_ram_block(const uint8_t* data, uint32_t size, checksums::Name texture_name, uint32_t a3, uint32_t a4,
                                        uint32_t a5)
{
    const uint32_t checksum = texture_name.value();
    checksum_ = checksum;
    return load_memory(data, size, a3, a4, a5);
}

// 0x0049F330
bool BitmapTexture::fabricate(uint32_t checksum, uint32_t width, uint32_t height, uint32_t bits_per_pixel, uint32_t a4,
                              uint32_t a5)
{
    checksum_ = checksum;
    return create_image(checksums::Name{checksum}, width, height, static_cast<int>(bits_per_pixel), static_cast<int>(a4),
                        static_cast<int>(a5));
}

// 0x0049F3F0
void BitmapTexture::pin_resident(bool permanent)
{
    if (permanent)
        flag_bits_ |= kTexturePermanent;
    else
        flag_bits_ &= ~kTexturePermanent;
}

} // namespace render
