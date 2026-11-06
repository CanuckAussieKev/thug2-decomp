// The PC sprite: see pc_sprite.h.

#include "render_pc/pc_sprite.h"

#include <cstdint>
#include <cstring>
#include <memory>

#include "render_pc/billboard_quad.h"
#include "engine/gfx/engine_sprite.h"
#include "render_pc/pc_texture.h"

namespace render {

constexpr float kHalf = 0.5F;

// 0x004C70E0
PcSprite::PcSprite() : BillboardQuad(nullptr) { sprite = std::make_unique<QuadSprite>(0.0F); }

// 0x004C7050
PcSprite::~PcSprite() { sprite.reset(); }

// (0x004B3D80, the PC text's, folded)
void PcSprite::refresh_all()
{
    update_settings();
    update_priority();
    update_hidden();
}

// 0x004C70B0
void PcSprite::update_hidden() { sprite->change_concealed(static_cast<uint8_t>(invisible())); }

// 0x004C71B0
void PcSprite::update_settings()
{
    if (bitmap()) {
        sprite->set_texture(image_of(bitmap())); // (its image, or its edit's)
    }
    std::memcpy(&sprite->flip_v(), &flip_v(), 2); // (the bytes as they are)
    sprite->set_x(pos_x());
    sprite->set_y(y());
    sprite->set_width(span());
    sprite->set_height(level_y());
    sprite->set_xscale(scale_h());
    sprite->set_yscale(scale_v());
    sprite->set_hot_x(static_cast<float>((static_cast<double>(hot_x()) + 1.0F) * kHalf * static_cast<int>(span())));
    sprite->set_hot_y(static_cast<float>((static_cast<double>(hot_y()) + 1.0F) * kHalf * static_cast<int>(level_y())));
    sprite->set_rotation(orientation_turn());
    std::memcpy(&sprite->color(), rgba_value().data(), 4);
}

// 0x004C70C0
void PcSprite::update_priority()
{
    if (sprite) {
        sprite->change_rank(rank());
    }
}

} // namespace render
