#pragma once

#include <memory>
#include <cstddef>
#include <cstdint>

#include "engine/gfx/billboard_quad.h"
#include "engine/gfx/engine_sprite.h"

// The PC sprite (Nx engine: the engine's BillboardQuad - constructor 0x0049ED60, destructor body 0x0049EBA0 - over it): the
// engine's settings copied into the 2D sprite that draws it (QuadSprite, billboard_quad.h).
namespace render {

// The PC sprite (0x48 bytes, vtable 0x0064A744; made by build_sprite_quad 0x004B2A80).
class PcSprite final : public BillboardQuad {
public:
    PcSprite(const PcSprite&) = delete;
    PcSprite& operator=(const PcSprite&) = delete;
    // 0x004C70E0: the engine's (no window), and the 2D sprite made with priority 0 (none without memory).
    PcSprite();
    // 0x004C7050 (0x004C7180 the scalar deleting destructor): the 2D sprite destroyed, then the engine's.
    ~PcSprite() override;
    // (Vtable slot 1, folded with the PC text's: the settings, the priority, the hidden flag.)
    void refresh_all() override;
    // 0x004C70B0 (slot 2): the 2D sprite hidden or shown as the engine's flag says.
    void update_hidden() override;
    // 0x004C71B0 (slot 3): the engine's settings copied into the 2D sprite (the texture only when there is one; the
    // hot spot into pixels).
    void update_settings() override;
    // 0x004C70C0 (slot 4): the 2D sprite's priority from the engine's (when there is a 2D sprite).
    void update_priority() override;

    std::unique_ptr<QuadSprite> sprite; // +0x44
};
static_assert(offsetof(PcSprite, sprite) == 0x44 && sizeof(PcSprite) == 0x48, "layout must match the original");

} // namespace render
