#pragma once

#include <cstddef>
#include <cstdint>

// A scene material (Nx engine, PC; 0x110 bytes, constructor 0x004D1A90): up to four passes, each with flags, a
// texture, a colour, a blend mode, addressing, a mip bias and an optional UV wibble; colour and texture animations;
// a specular colour. Set up for drawing by Submit (all passes, for the pixel shaders) or submit_pass (one pass,
// through the fixed-function stages).
namespace render {

// A pass's UV wibble (0x30 bytes, constructor 0x004D1A60: all 0 but the matrix's first value, 1): velocities,
// frequencies, amplitudes and phases (u, v each), an explicit UV matrix's 2x2 part (+0x20; only two values are
// set) and its offsets (+0x28).
struct UvOscillator {
    float velocity[2]{};
    float frequency[2]{};
    float amplitude[2]{};
    float phase[2]{};
    float matrix[2]{};
    float offset[2]{};

    // 0x004D1A60: construct - all 0 but the matrix's first value, 1.
    UvOscillator();
};
static_assert(sizeof(UvOscillator) == 0x30, "layout must match the original");

// A material's texture animation (0x44 bytes, +0xFC): for each pass a list of keys (a time in milliseconds and the
// texture from then on), played from a phase, looping (`loops` times, then held at the end; 0: for ever).
struct TextureKey {
    int time;
    uint8_t* texture;
};
struct TextureAnim {
    uint32_t start_time_;    // (set by Restart; the animation doesn't read it)
    uint32_t key_counts_[4]; // +0x04
    uint32_t phases[4];      // +0x14
    int loops[4];            // +0x24
    TextureKey* keys[4];     // +0x34

    // 0x004D1A80: started again (the time now, frame_clock::GetTime).
    void relaunch();
};
static_assert(sizeof(TextureAnim) == 0x44, "layout must match the original");

// A colour animation (12 bytes): keys of a time (in frames, 0x00786D00) and an RGBA colour, blended between.
struct ColorKey {
    int time;
    uint8_t rgba[4];
};
struct ColorAnim {
    uint32_t key_count_;
    int phase;
    ColorKey* keys;
};

struct MaterialRef {
    uint32_t checksum{};
    uint32_t name_hash_{}; // passes are named relative to it (see pass_from_checksum)
    uint32_t pass_count_{0};
    uint8_t sort_flag_{};   // +0x0C
    uint8_t two_sided_{};   // +0x0D: no culling
    bool has_uv_wibble_{false};    // +0x0E: set when a pass gets a UV wibble of its own
    bool has_texture_anim_{false}; // +0x0F
    uint8_t alpha_ref_{};   // +0x10 (0: no alpha test)
    uint8_t depth_bias_{0}; // +0x11 (up to 16)
    uint8_t unknown12[2]{};
    uint32_t unknown14{};
    uint32_t unknown18{0};
    float sort_key_{}; // +0x1C
    uint32_t
        pass_flags_[4]{};   // +0x20: 1 UV wibble, 2 (pass 0) colour animations, 8 environment mapped, 0x80 (byte) the
                            // colour isn't to be changed, 0x100 (pass 0) specular, 0x200 a border colour, 0x400 bump
                            // environment, 0x800 texture animation, 0x1000 alpha from the texture alone, 0x4000
                            // explicit UV wibble (not animated)
    uint8_t* textures[4]{}; // +0x30 (+0x10 the Direct3D texture)
    float pass_color_[4][4]{};        // +0x40: r, g, b (0.5: unchanged) and a fourth value
    uint32_t blend_modes_[4]{};       // +0x80 (choose_blend_mode's)
    uint32_t address_modes_[4]{};     // +0x90 (kAddressMode0's)
    float reflection_tiling_[4][2]{}; // +0xA0 (3, 3 when constructed)
    uint32_t unknownc0_[4]{};         // +0xC0 (0x10001 when constructed)
    UvOscillator* uv_wibble_[4]{};    // +0xD0
    uint32_t mip_lod_bias_[4]{};      // +0xE0 (kMipLodBias0's: float bits)
    uint32_t color_anim_count_{0};    // +0xF0
    ColorAnim* color_anims_{nullptr}; // +0xF4
    uint32_t* anim_colors_{nullptr};  // +0xF8: each animation's colour now
    TextureAnim* texture_anim_{nullptr}; // +0xFC
    float specular[3]{};            // +0x100
    float specular_power_{};        // +0x10C

    // 0x004D1A90: construct - no passes, animations or wibbles; the per-pass defaults.
    MaterialRef();
    // 0x004D2BD0: the destructor's body - the animations and UV wibbles freed.
    ~MaterialRef();
    MaterialRef(const MaterialRef&) = delete;
    MaterialRef& operator=(const MaterialRef&) = delete;
    // 0x004D1D90: a bit per pass taking its alpha from the texture alone (flag 0x1000).
    uint32_t texture_alpha_passes() const;
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
};
static_assert(offsetof(MaterialRef, pass_flags_) == 0x20 && offsetof(MaterialRef, pass_color_) == 0x40 &&
                  offsetof(MaterialRef, reflection_tiling_) == 0xA0 && offsetof(MaterialRef, uv_wibble_) == 0xD0 &&
                  offsetof(MaterialRef, color_anim_count_) == 0xF0 && offsetof(MaterialRef, texture_anim_) == 0xFC &&
                  offsetof(MaterialRef, specular_power_) == 0x10C && sizeof(MaterialRef) == 0x110,
              "layout must match the original");

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
inline int pass_from_checksum(const MaterialRef* material, uint32_t checksum, int pass)
{
    const uint32_t name = material->name_hash_;
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
