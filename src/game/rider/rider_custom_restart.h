#pragma once

#include <array>
#include <cstdint>

#include "base/maths/mat4.h"
#include "base/maths/vec4.h"

namespace objects {

// NOT ORIGINAL (the class; its fields are the skater's +0x2C8-+0x31B): the skater's own restart point
// (SetCustomRestart, SkipToCustomRestart; the test view's drop-in uses it too): where, facing which way, walking or
// not, and whether one is set. Owned by the skater, in its place there.
class CustomRestart {
public:
    // Not original - the fields' offsets checked against the original's at compile time (rider.cpp).
    static void check_layout();

    [[nodiscard]] const maths::Vec4& pos() const { return pos_; }
    void set_pos(const maths::Vec4& value) { pos_ = value; }
    maths::Mat4& matrix() { return matrix_; }
    [[nodiscard]] const maths::Mat4& matrix() const { return matrix_; }
    void set_matrix(const maths::Mat4& value) { matrix_ = value; }
    [[nodiscard]] bool walking() const { return walking_; }
    void set_walking(bool value) { walking_ = value; }
    [[nodiscard]] bool is_set() const { return set_; }
    void set_is_set(bool value) { set_ = value; }

private:
    maths::Vec4 pos_{};    // +0x00 (the skater's +0x2C8)
    maths::Mat4 matrix_{}; // +0x10
    bool walking_{};       // +0x50
    bool set_{};           // +0x51
    std::array<uint8_t, 2> pad31a_{};
};

} // namespace objects
