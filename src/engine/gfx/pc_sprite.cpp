// The PC sprite: see pc_sprite.h.

#include "engine/gfx/pc_sprite.h"

#include <memory>
#include <cstring>

#include "engine/gfx/billboard_quad.h"
#include "engine/gfx/engine_sprite.h"
#include "engine/gfx/pc_texture.h"

namespace render {

// 0x004C70E0
PcSprite::PcSprite() : BillboardQuad(nullptr) { sprite = std::make_unique<QuadSprite>(0.0f); }

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
void PcSprite::update_hidden() { sprite->change_concealed(invisible()); }

// 0x004C71B0
void PcSprite::update_settings()
{
    if (bitmap()) {
        sprite->texture = image_of(bitmap()); // (its image, or its edit's)
    }
    std::memcpy(&sprite->flip_v_, &flip_v(), 2); // (the bytes as they are)
    sprite->x = pos_x();
    sprite->y = y();
    sprite->width = span();
    sprite->height = level_y();
    sprite->xscale = scale_h();
    sprite->yscale = scale_v();
    sprite->hot_x_ = static_cast<float>((static_cast<double>(hot_x()) + 1.0f) * 0.5f * static_cast<int>(span()));
    sprite->hot_y_ = static_cast<float>((static_cast<double>(hot_y()) + 1.0f) * 0.5f * static_cast<int>(level_y()));
    sprite->rotation = orientation_turn();
    std::memcpy(&sprite->color, rgba_value().data(), 4);
}

// 0x004C70C0
void PcSprite::update_priority()
{
    if (sprite)
        sprite->change_rank(rank());
}

} // namespace render
