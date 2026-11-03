#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>
#include "engine/core/checksums.h"

// A scene material (Nx engine, PC; 0x110 bytes, constructor 0x004D1A90): up to four passes, each with flags, a
// texture, a colour, a blend mode, addressing, a mip bias and an optional UV wibble; colour and texture animations;
// a specular colour. Set up for drawing by Submit (all passes, for the pixel shaders) or submit_pass (one pass,
// through the fixed-function stages).
namespace render {

struct TextureImage;

// A pass's UV wibble (0x30 bytes, constructor 0x004D1A60: all 0 but the matrix's first value, 1): velocities,
// frequencies, amplitudes and phases (u, v each), an explicit UV matrix's 2x2 part (+0x20; only two values are
// set) and its offsets (+0x28).
struct UvOscillator {
    // The fields other code reads and sets.
    std::array<float, 2>& velocity() { return velocity_; }
    [[nodiscard]] const std::array<float, 2>& velocity() const { return velocity_; }
    std::array<float, 2>& frequency() { return frequency_; }
    [[nodiscard]] const std::array<float, 2>& frequency() const { return frequency_; }
    std::array<float, 2>& amplitude() { return amplitude_; }
    [[nodiscard]] const std::array<float, 2>& amplitude() const { return amplitude_; }
    std::array<float, 2>& phase() { return phase_; }
    [[nodiscard]] const std::array<float, 2>& phase() const { return phase_; }
    std::array<float, 2>& matrix() { return matrix_; }
    [[nodiscard]] const std::array<float, 2>& matrix() const { return matrix_; }
    std::array<float, 2>& offset() { return offset_; }
    [[nodiscard]] const std::array<float, 2>& offset() const { return offset_; }

private:
    std::array<float, 2> velocity_{};
    std::array<float, 2> frequency_{};
    std::array<float, 2> amplitude_{};
    std::array<float, 2> phase_{};
    std::array<float, 2> matrix_{};
    std::array<float, 2> offset_{};

public:

    // 0x004D1A60: construct - all 0 but the matrix's first value, 1.
    UvOscillator();
};
static_assert(sizeof(UvOscillator) == 0x30, "layout must match the original");

// A material's texture animation (0x44 bytes, +0xFC): for each pass a list of keys (a time in milliseconds and the
// texture from then on), played from a phase, looping (`loops` times, then held at the end; 0: for ever).
struct TextureKey {
    int time;
    TextureImage* texture;
};
struct TextureAnim {
    uint32_t start_time_;    // (set by Restart; the animation doesn't read it)
    std::array<uint32_t, 4> key_counts_; // +0x04
    std::array<uint32_t, 4> phases;      // +0x14
    std::array<int, 4> loops;            // +0x24
    std::array<TextureKey*, 4> keys;     // +0x34


};

// NOT ORIGINAL - a pass's keys as a run of its key count.
[[nodiscard]] inline std::span<TextureKey> key_run(const TextureAnim& anim, size_t pass)
{
    return {anim.keys.at(pass), anim.key_counts_.at(pass)};
}

// 0x004D1A80: started again (the time now, frame_clock::GetTime).
void relaunch(TextureAnim& anim);
static_assert(sizeof(TextureAnim) == 0x44, "layout must match the original");

// A colour animation (12 bytes): keys of a time (in frames, 0x00786D00) and an RGBA colour, blended between.
struct ColorKey {
    int time;
    std::array<uint8_t, 4> rgba;
};
struct ColorAnim {
    uint32_t key_count_;
    int phase_;
    ColorKey* keys;

};

// NOT ORIGINAL - the keys as a run of key_count_.
[[nodiscard]] inline std::span<ColorKey> key_run(const ColorAnim& anim) { return {anim.keys, anim.key_count_}; }

struct MaterialRef {
    // The fields other code reads and sets.
    uint32_t& checksum() { return checksum_; }
    [[nodiscard]] uint32_t checksum() const { return checksum_; }
    void set_checksum(uint32_t value) { checksum_ = value; }
    [[nodiscard]] uint32_t name_hash() const { return name_hash_; }
    void set_name_hash(uint32_t value) { name_hash_ = value; }
    [[nodiscard]] uint32_t pass_count() const { return pass_count_; }
    void set_pass_count(uint32_t value) { pass_count_ = value; }
    [[nodiscard]] uint8_t sort_flag() const { return sort_flag_; }
    void set_sort_flag(uint8_t value) { sort_flag_ = value; }
    [[nodiscard]] uint8_t two_sided() const { return two_sided_; }
    void set_two_sided(uint8_t value) { two_sided_ = value; }
    void set_has_uv_wibble(bool value) { has_uv_wibble_ = value; }
    void set_has_texture_anim(bool value) { has_texture_anim_ = value; }
    [[nodiscard]] uint8_t alpha_ref() const { return alpha_ref_; }
    void set_alpha_ref(uint8_t value) { alpha_ref_ = value; }
    [[nodiscard]] uint8_t depth_bias() const { return depth_bias_; }
    void set_depth_bias(uint8_t value) { depth_bias_ = value; }
    [[nodiscard]] uint32_t unknown14() const { return unknown14_; }
    void set_unknown14(uint32_t value) { unknown14_ = value; }
    [[nodiscard]] uint32_t unknown18() const { return unknown18_; }
    void set_unknown18(uint32_t value) { unknown18_ = value; }
    [[nodiscard]] float sort_key() const { return sort_key_; }
    void set_sort_key(float value) { sort_key_ = value; }
    std::array<uint32_t, 4>& pass_flags() { return pass_flags_; }
    [[nodiscard]] const std::array<uint32_t, 4>& pass_flags() const { return pass_flags_; }
    std::array<TextureImage*, 4>& textures() { return textures_; }
    [[nodiscard]] const std::array<TextureImage*, 4>& textures() const { return textures_; }
    std::array<std::array<float, 4>, 4>& pass_color() { return pass_color_; }
    [[nodiscard]] const std::array<std::array<float, 4>, 4>& pass_color() const { return pass_color_; }
    std::array<uint32_t, 4>& blend_modes() { return blend_modes_; }
    [[nodiscard]] const std::array<uint32_t, 4>& blend_modes() const { return blend_modes_; }
    std::array<uint32_t, 4>& address_modes() { return address_modes_; }
    [[nodiscard]] const std::array<uint32_t, 4>& address_modes() const { return address_modes_; }
    std::array<std::array<float, 2>, 4>& reflection_tiling() { return reflection_tiling_; }
    [[nodiscard]] const std::array<std::array<float, 2>, 4>& reflection_tiling() const { return reflection_tiling_; }
    std::array<uint32_t, 4>& unknownc0() { return unknownc0_; }
    [[nodiscard]] const std::array<uint32_t, 4>& unknownc0() const { return unknownc0_; }
    std::array<UvOscillator*, 4>& uv_wibble() { return uv_wibble_; }
    [[nodiscard]] const std::array<UvOscillator*, 4>& uv_wibble() const { return uv_wibble_; }
    std::array<uint32_t, 4>& mip_lod_bias() { return mip_lod_bias_; }
    [[nodiscard]] const std::array<uint32_t, 4>& mip_lod_bias() const { return mip_lod_bias_; }
    void set_color_anim_count(uint32_t value) { color_anim_count_ = value; }
    void set_color_anims(ColorAnim* value) { color_anims_ = value; }
    void set_anim_colors(uint32_t* value) { anim_colors_ = value; }
    [[nodiscard]] TextureAnim* texture_anim() const { return texture_anim_; }
    void set_texture_anim(TextureAnim* value) { texture_anim_ = value; }
    std::array<float, 3>& specular() { return specular_; }
    [[nodiscard]] const std::array<float, 3>& specular() const { return specular_; }
    [[nodiscard]] float specular_power() const { return specular_power_; }
    void set_specular_power(float value) { specular_power_ = value; }

private:
    uint32_t checksum_{};
    uint32_t name_hash_{}; // passes are named relative to it (see pass_from_checksum)
    uint32_t pass_count_{0};
    uint8_t sort_flag_{};   // +0x0C
    uint8_t two_sided_{};   // +0x0D: no culling
    bool has_uv_wibble_{false};    // +0x0E: set when a pass gets a UV wibble of its own
    bool has_texture_anim_{false}; // +0x0F
    uint8_t alpha_ref_{};   // +0x10 (0: no alpha test)
    uint8_t depth_bias_{0}; // +0x11 (up to 16)
    std::array<uint8_t, 2> unknown12{};
    uint32_t unknown14_{};
    uint32_t unknown18_{0};
    float sort_key_{}; // +0x1C
    std::array<uint32_t, 4> pass_flags_{};   // +0x20: 1 UV wibble, 2 (pass 0) colour animations, 8 environment mapped, 0x80 (byte) the
                            // colour isn't to be changed, 0x100 (pass 0) specular, 0x200 a border colour, 0x400 bump
                            // environment, 0x800 texture animation, 0x1000 alpha from the texture alone, 0x4000
                            // explicit UV wibble (not animated)
    std::array<TextureImage*, 4> textures_{}; // +0x30
    std::array<std::array<float, 4>, 4> pass_color_{};        // +0x40: r, g, b (0.5: unchanged) and a fourth value
    std::array<uint32_t, 4> blend_modes_{};       // +0x80 (choose_blend_mode's)
    std::array<uint32_t, 4> address_modes_{};     // +0x90 (kAddressMode0's)
    std::array<std::array<float, 2>, 4> reflection_tiling_{}; // +0xA0 (3, 3 when constructed)
    std::array<uint32_t, 4> unknownc0_{};         // +0xC0 (0x10001 when constructed)
    std::array<UvOscillator*, 4> uv_wibble_{};    // +0xD0
    std::array<uint32_t, 4> mip_lod_bias_{};      // +0xE0 (kMipLodBias0's: float bits)
    uint32_t color_anim_count_{0};    // +0xF0
    ColorAnim* color_anims_{nullptr}; // +0xF4
    uint32_t* anim_colors_{nullptr};  // +0xF8: each animation's colour now

public:

    // NOT ORIGINAL - the colour animations and their current colours as runs of color_anim_count_.
    [[nodiscard]] std::span<ColorAnim> color_anim_run() const { return {color_anims_, color_anim_count_}; }
    [[nodiscard]] std::span<uint32_t> anim_colour_run() const { return {anim_colors_, color_anim_count_}; }
private:
    TextureAnim* texture_anim_{nullptr}; // +0xFC
    std::array<float, 3> specular_{};            // +0x100
    float specular_power_{};        // +0x10C

public:

    // 0x004D1A90: construct - no passes, animations or wibbles; the per-pass defaults.
    MaterialRef();
    // 0x004D2BD0: the destructor's body - the animations and UV wibbles freed.
    ~MaterialRef();
    MaterialRef(const MaterialRef&) = delete;
    MaterialRef& operator=(const MaterialRef&) = delete;
    // 0x004D1D90: a bit per pass taking its alpha from the texture alone (flag 0x1000).
    [[nodiscard]] uint32_t texture_alpha_passes() const;
    // 0x004D1B40 / 0x004D1CC0 / 0x004D1DC0: the colour animations (with pass 0's flag 2), the texture animations
    // and the UV wibbles' offsets brought up to date.
    void update_color_anims();
    void update_texture_anims();
    void update_uv_wibbles();
    // 0x004D1F00: set up for drawing with the pixel shaders: the blend mode, the states, the animations, the specular
    // material, each pass's texture, addressing, mip bias and texture matrix (environment map, UV wibble or none)
    // on its stage; the pass colours into the shader constants when they changed. Nothing while 0x00786F30 is set.
    void hand_in();
    // 0x004D23E0: the same without the blend mode and the passes.
    void submit_states();
    // 0x004D24D0: one pass set up on the fixed-function stages (0, and 1 for blending with the pass before), its
    // colour - or `color` (three floats), unless the pass's colour isn't to be changed - as the texture factor.
    void submit_pass(uint32_t pass, const float* color);
private:
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(MaterialRef, pass_flags_) == 0x20 && offsetof(MaterialRef, pass_color_) == 0x40 &&
                          offsetof(MaterialRef, reflection_tiling_) == 0xA0 && offsetof(MaterialRef, uv_wibble_) == 0xD0 &&
                          offsetof(MaterialRef, color_anim_count_) == 0xF0 && offsetof(MaterialRef, texture_anim_) == 0xFC &&
                          offsetof(MaterialRef, specular_power_) == 0x10C && sizeof(MaterialRef) == 0x110,
                      "layout must match the original");
    }
};

} // namespace render

namespace lists {
class KeyMap;
}
namespace scene_format {
class MemoryStream;
class FileStream;
} // namespace scene_format

namespace render {

// 0x004D2CB0 / 0x004D3390: a scene's materials (a count, then each one's record: its passes, wibbles, animations -
// scene_format.h) from memory or from a file (the stream moved past them), into a new table by checksum (the first of
// a checksum kept); the textures by checksum from `textures`. A damaged record ends the list (the stream failed).
lists::KeyMap* read_materials_from_buffer(scene_format::MemoryStream& in, const lists::KeyMap* textures);
lists::KeyMap* load_materials_from_file(scene_format::FileStream& in, const lists::KeyMap* textures);

// Which of a material's passes a checksum names, given the pass asked for: the material's own name checksum - that
// pass; the name plus d (1-0x7F) - with d's low three bits set, they must be the pass + 1 and name pass 0; else
// d >> 3 is a mask of passes that must include the pass, and the pass is counted among the mask's set bits below it.
// -1 when it names none (an index that may still be past the material's passes otherwise).
inline int pass_from_checksum(const MaterialRef* material, checksums::Name texture, int pass)
{
    const uint32_t checksum = texture.value();
    const uint32_t name = material->name_hash();
    if (name == checksum)
        return pass;
    if (name < checksum || name - checksum > 0x7F)
        return -1;
    const uint32_t difference = name - checksum;
    const uint32_t low = difference & 7;
    const uint32_t mask = difference >> 3;
    if (low)
        return low == static_cast<uint32_t>(pass) + 1 ? 0 : -1;
    if (!mask || !(mask & (1u << (pass & 31)))) // (shifts as x86 does: by the low five bits)
        return -1;
    int index = pass;
    for (int i = 0; i < pass; ++i) {
        if (!(mask & (1u << (i & 31))))
            --index;
    }
    return index;
}

} // namespace render
