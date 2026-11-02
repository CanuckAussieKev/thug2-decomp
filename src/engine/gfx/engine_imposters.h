#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

namespace lists {
class KeyMap;
}

// The engine's imposter manager (Nx; the object at 0x006FA7BC, 8 bytes): its imposter groups in a hash table, each
// (by its vtable) asked once a frame whether it's near enough to be drawn as itself - else its imposter made, at
// most `max_per_frame_` made or updated a frame - and the imposters drawn between the PC's primitive states.
namespace render {

// An imposter group (the original's table order in the comments). Nothing in this build makes one (the PC never
// adds a group to the manager's table): the interface is what the manager calls.
class ImposterGroup {
public:
    virtual ~ImposterGroup() = default;   // [0]
    virtual void create_imposter() = 0;   // [1]
    virtual void drop_imposter() = 0;     // [2]
    virtual bool update_imposter() = 0;   // [3]: whether it did
    virtual void draw_imposter() = 0;     // [4]
    virtual float distance_to_view() = 0; // [5]
    virtual void tick_frame() = 0;        // [6]

    // The fields other code reads and sets.
    [[nodiscard]] float near_range() const { return near_range_; }
    int& frame_count() { return frame_count_; }
    [[nodiscard]] int frame_count() const { return frame_count_; }
    [[nodiscard]] bool has_imposter() const { return has_imposter_; }

private:
    float near_range_{}; // +0x04: below this distance the group is drawn as itself
    std::array<uint8_t, 0x50> pad08_{};
    int frame_count_{};   // +0x58
    bool has_imposter_{}; // +0x5C
};

struct ImposterManager {
private:
    lists::KeyMap* groups_ptr_;
    int max_per_frame_{1};

public:

    // 0x0048E3E0: construct - a table of 2^8 buckets, 1 imposter a frame.
    ImposterManager();
    // 0x0048E260: every group deleted (virtual destructor, flag 1), the table emptied.
    void delete_all();
    // 0x0048E2D0: each group updated, its imposter dropped when it's within its distance, else made or brought up to
    // date while the frame's count allows (made: counted; brought up to date: its frame count +1, counted when it
    // was).
    void tick_frame();
    // 0x0048E370: the imposters drawn (the groups that have one), between set_primitive_states and
    // reset_primitive_states.
    void render();
};
static_assert(sizeof(ImposterManager) == 8, "layout must match the original");

} // namespace render
