#pragma once

namespace objects {

// NOT ORIGINAL (the class; its fields are the skater's +0x1A8-+0x1BF): the create-a-trick editor's state for the
// skater (its scripts' CAT_Set... / CAT_Get... commands, game/goals/create_a_trick.cpp): how many animations are on,
// which steps are done, whether the skater is shown turned round, the hold time and the rotations' totals. Owned by
// the skater, in its place there.
class TrickEditorState {
public:
    // Not original - the fields' offsets checked against the original's at compile time (rider.cpp).
    static void check_layout();

    int& num_anims() { return num_anims_; }
    [[nodiscard]] int num_anims() const { return num_anims_; }
    bool& bail_done() { return bail_done_; }
    [[nodiscard]] bool bail_done() const { return bail_done_; }
    bool& flip_180() { return flip_180_; }
    [[nodiscard]] bool flip_180() const { return flip_180_; }
    bool& anims_done() { return anims_done_; }
    [[nodiscard]] bool anims_done() const { return anims_done_; }
    bool& rots_done() { return rots_done_; }
    [[nodiscard]] bool rots_done() const { return rots_done_; }
    [[nodiscard]] float hold_time() const { return hold_time_; }
    void set_hold_time(float value) { hold_time_ = value; }
    int& total_x() { return total_x_; }
    [[nodiscard]] int total_x() const { return total_x_; }
    int& total_y() { return total_y_; }
    [[nodiscard]] int total_y() const { return total_y_; }
    int& total_z() { return total_z_; }
    [[nodiscard]] int total_z() const { return total_z_; }

private:
    int num_anims_{};   // +0x00 (the skater's +0x1A8) num_animations_on
    bool bail_done_{};  // +0x04 bailtimescriptdone
    bool flip_180_{};   // +0x05 cat_flip_skater_180
    bool anims_done_{}; // +0x06 cat_animations_done
    bool rots_done_{};  // +0x07 cat_rotations_done
    float hold_time_{}; // +0x08 cat_hold_time
    int total_x_{};     // +0x0C
    int total_y_{};     // +0x10
    int total_z_{};     // +0x14
};

} // namespace objects
