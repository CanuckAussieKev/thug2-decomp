#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "base/maths/vec4.h"

namespace objects {

constexpr std::size_t kProjectileSlots = 5;
constexpr int kProjectileCount = 5; // the same, as an int count (the loops)

// NOT ORIGINAL (the class; its field is the skater's +0x1C0-+0x237): the five projectiles a local skater can have
// launched (projectile_0 ... projectile_4): each one's object name, whether it is out, and where it is (its projectile
// component sets those; the peds' proximity check reads them). Owned by the skater, in its place there.
class ProjectileSlots {
public:
    // A launched projectile.
    struct Projectile {
        uint32_t name; // projectile_0 ... projectile_4
        bool active;
        std::array<uint8_t, 3> pad5_;
        maths::Vec4 pos; // +0x08 (the projectile component's position; w unused)
    };

    // Not original - the field's offset checked against the original's at compile time (rider.cpp).
    static void check_layout();

    // (0x00581850's part) each slot named projectile_<n>, none out.
    void name_all();

    std::array<Projectile, kProjectileSlots>& slots() { return slots_; }
    [[nodiscard]] const std::array<Projectile, kProjectileSlots>& slots() const { return slots_; }

private:
    std::array<Projectile, kProjectileSlots> slots_{}; // +0x00 (the skater's +0x1C0)
};

} // namespace objects
