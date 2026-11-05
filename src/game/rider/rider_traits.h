#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "engine/util/text_store.h"

namespace script {
class Struct;
}

namespace objects {

constexpr std::size_t kGivenNameBytes = 0x40;

struct RiderIdentity;

// NOT ORIGINAL (the class; its fields are the skater's +0x108-+0x16B): what the skater keeps of its profile - its push
// and skate style, stance, pro or not, male or not, first name, name (a checksum) and the name shown - and whether its
// special meter is always full (ToggleAlwaysSpecial). Read from the profile by Rider::update_from_profile, matched
// against a description by the career's commands. Owned by the skater, in its place there.
class RiderTraits {
public:
    // Not original - the fields' offsets checked against the original's at compile time (rider.cpp).
    static void check_layout();

    // (0x00580EB0's middle) push style, stance, pro, display name, male, skate style, first name, name from `profile`.
    void read_from(RiderIdentity* profile);
    // 0x0057FED0: whether it matches `params`' description (is_pro, the custom flag, is_male / is_female, is_named,
    // stance, style).
    bool matches_params(script::Struct* params) const;

    [[nodiscard]] uint32_t stroke_kind() const { return stroke_kind_; }
    [[nodiscard]] bool goofy() const { return goofy_; }
    [[nodiscard]] bool pro() const { return pro_; }
    std::array<char, kGivenNameBytes>& given_name() { return given_name_; }
    [[nodiscard]] const std::array<char, kGivenNameBytes>& given_name() const { return given_name_; }
    [[nodiscard]] uint32_t label() const { return label_; }
    [[nodiscard]] const char* shown_name() const { return shown_name_.c_str(); }
    bool& always_special() { return always_special_; }
    [[nodiscard]] bool always_special() const { return always_special_; }

private:
    uint32_t stroke_kind_{}; // +0x00 (the skater's +0x108: the profile's pushstyle)
    uint32_t style_{};       // +0x04 (skatestyle)
    bool goofy_{};           // +0x08
    bool pro_{};             // +0x09
    std::array<uint8_t, 2> pad112{};
    int male_{};                                     // +0x0C
    std::array<char, kGivenNameBytes> given_name_{}; // +0x10
    uint32_t label_{};                               // +0x50
    text::Chars shown_name_;                         // +0x54
    bool always_special_{};                          // +0x60 (the special meter always full)
    std::array<uint8_t, 3> pad169_{};
};

} // namespace objects
