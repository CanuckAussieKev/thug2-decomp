#pragma once

#include <array>
#include <cstddef>
#include <memory>

namespace script {
class Struct;
}
namespace goals {
class CreateATrick;
}

namespace objects {

constexpr std::size_t kTrickSlotCount = 11; // the create-a-skater tricks
constexpr int kCatTrickCount = 11;          // the same, as an int count (the loops)

// NOT ORIGINAL (the class; its field is the skater's +0x17C-+0x1A7): a local skater's created tricks (create-a-trick),
// one per slot - made when the skater is set up, read from and written to the save's CAT info, deleted with the skater.
// Owned by the skater, in its place there.
class CreatedTricks {
public:
    // Not original - the field's offset checked against the original's at compile time (rider.cpp).
    static void check_layout();

    // (0x00581850's part) a new created trick in every slot.
    void make_all();
    // (0x00583FB0's part) every slot's trick deleted, in slot order.
    void delete_all();
    // 0x00580060: the CAT info (its tricks: each one's other params, animations and rotations), then
    // kill_load_premade_cats.
    void load_cat_info(script::Struct* params) const;
    // 0x005825E0: the CAT info written to `save` (tricks).
    void write_cat_info(script::Struct* save) const;

    [[nodiscard]] const std::array<std::unique_ptr<goals::CreateATrick>, kTrickSlotCount>& slots() const
    {
        return slots_;
    }

private:
    std::array<std::unique_ptr<goals::CreateATrick>, kTrickSlotCount> slots_; // +0x00 (the skater's +0x17C)
};

} // namespace objects
