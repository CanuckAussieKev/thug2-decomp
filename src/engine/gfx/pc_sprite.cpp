// The PC sprite: see pc_sprite.h.

#include "engine/gfx/pc_sprite.h"

#include <memory>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/billboard_quad.h"
#include "engine/gfx/draw2d.h"
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
void PcSprite::update_hidden() { sprite->change_concealed(invisible_); }

// 0x004C71B0
void PcSprite::update_settings()
{
    if (bitmap_) {
        sprite->texture = image_of(bitmap_); // (its image, or its edit's)
    }
    std::memcpy(&sprite->flip_v_, &flip_v_, 2); // (the bytes as they are)
    sprite->x = pos_x_;
    sprite->y = y_;
    sprite->width = span_;
    sprite->height = level_y_;
    sprite->xscale = scale_h_;
    sprite->yscale = scale_v_;
    sprite->hot_x_ = static_cast<float>((static_cast<double>(hot_x_) + 1.0f) * 0.5f * static_cast<int>(span_));
    sprite->hot_y_ = static_cast<float>((static_cast<double>(hot_y_) + 1.0f) * 0.5f * static_cast<int>(level_y_));
    sprite->rotation = orientation_turn_;
    std::memcpy(&sprite->color, rgba_value_.data(), 4);
}

// 0x004C70C0
void PcSprite::update_priority()
{
    if (sprite)
        sprite->change_rank(rank_);
}

} // namespace render
