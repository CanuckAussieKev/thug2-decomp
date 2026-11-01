#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include "frontend/screen_element.h"

namespace script {
class Array;
}

// The text element (TextElement) and its blur effect. The text itself is drawn by the engine's text objects
// (0x0048D1D0 makes one, 0x0048D5F0 frees it; their setters 0x0048D0C0-0x0048D530 are still game code) in a
// font (0x0048D760 finds one by checksum).
namespace frontend {

// The blur effect (0x47C bytes, made by blur_effect): 16 copies of the text behind it, each further along a
// displacement, more scaled and fainter, morphing between settings over a time.
struct SmearEffect {
    struct DupFrom {
        float alpha; // +0x00 the copy's alpha factor (1/16 of the alpha pair's value at its place)
        std::array<float, 4> unknown04;
        float dx, dy; // +0x14 its displacement
        std::array<float, 3> unknown1c_;
        float scale_x_, scale_y_; // +0x28
        std::array<float, 5> unknown30;
    };
    // The settings, in order: blur_alpha_pair (x, y), blur_bottom_scales (x, y), blur_max_displacement (x, y).
    struct Settings {
        std::array<float, 2> alpha;
        std::array<float, 2> bottom_scales_;
        std::array<float, 2> max_displacement_;
    };

    // 0x004A5770 (a function Ghidra hadn't found): copy `i`.
    DupFrom* get_copy(int i);
    // 0x004A5780: a morph to the target over `ms` (0: the target at once), from now.
    void start_morph(int ms);
    // 0x004A57D0 (each frame): the settings morphed on, then the copies placed from them (copy n at n/16).
    void tick_frame();
    // 0x004A5CF0 (not found by Ghidra either): new target settings, morphed to over `ms`.
    void set_target(const Settings* target, int ms);
    // 0x004A5D30: the target settings from the parameters (each pair if given), morphed to over `time` seconds
    // (its milliseconds into `out_ms` when given). Returns the target.
    Settings* set_from_params(script::Struct* params, int* out_ms);

    std::array<DupFrom, 16> copies_;  // +0x000
    Settings aim_point_;  // +0x440
    Settings ongoing_;    // +0x458
    int last_time_;       // +0x470 (ms)
    int remaining_;       // +0x474 the morph's time left (ms)
    uint32_t rgba_value_; // +0x478 blur_rgba (the copies' colour)
};

// TextElement (vtable 0x0064A290, 0x1B0 bytes): a line of text in a font, optionally with a shadow (a second
// set of text objects behind it) and the blur effect (16 more text objects).
class LabelWidget final : public UiWidget {
public:
    LabelWidget(const LabelWidget&) = delete;
    LabelWidget& operator=(const LabelWidget&) = delete;
    // 0x004B0920: no text / font / blur / shadow, shadow colour black at half
    // alpha, one text object; type TextElement.
    LabelWidget();
    // 0x004B1830 (0x004B2030 the deleting one): the blur, the text objects and the string freed, then UiWidget's.
    ~LabelWidget() override;
    // 0x004B18E0 (slot 1): UiWidget's, then font, rot_angle (at once), internal_just, text, blur_effect /
    // no_blur_effect, blur_rgba, shadow_offs, Shadow / no_shadow, shadow_rgba, the encoded colour overrides,
    // the per-character properties; the text objects go in the window's viewport.
    void change_traits(script::Struct* params) override;
    // 0x004B09D0 (slot 9): the blur's settings, UiWidget's, rot_angle (morphed to).
    void blend_towards(script::Struct* params) override;
    // 0x004AFE70 (slot 10): the text objects (the text, the shadow, the blur copies) set from the element.
    void tick_frame() override;

    // 0x004AF650: the text's length.
    [[nodiscard]] int fetch_magnitude() const;
    // 0x004AF670: the first text object (and shadow) in the window's viewport.
    void set_viewports();
    // 0x004AF8C0: a copy of `text` (\\ kept, \_ a space, \m<c> a button glyph), the size from the font (unless
    // locked); kVisibleNow.
    void change_caption(const char* text);
    // 0x004AFA00: the rotation (degrees, plus a constant) morphed to and with `now` shown, the text objects
    // turned about their internal_just point.
    void set_text_rotation(float degrees, int now);
    // 0x004AFBB0: the per-character properties (an array of { index, rot_angle, offset, scale }) on the first
    // text object.
    void set_char_properties(script::Array* chars);
    // 0x004AFD00 / 0x004AFDD0: text added at the end (under 0x60 characters in all) / the last character taken
    // off (an escaped backslash both). False when nothing changed.
    bool join_onto(const char* text);
    bool backspace();
    // 0x004B0620: `count` new text objects (and as many shadow objects with a shadow; `only_shadow`: just those).
    void make_text_objects(int count, bool only_shadow);
    // 0x004B0760: the shadow objects freed (and the text objects unless `only_shadow`).
    void free_text_objects(bool only_shadow);
    // 0x004B0A20 / 0x004B0AE0: the blur effect on (17 text objects) / off (one).
    void enable_blur();
    void disable_blur();

    char* chars_{nullptr};                // +0x178
    uint32_t font_name_{0};               // +0x17C
    void* typeface_{nullptr};             // +0x180
    void** texts_{};                      // +0x184 the text objects (the first the text, the others the blur copies)
    void** shadows_{};                    // +0x188
    int num_{};                           // +0x18C
    SmearEffect* blur_{nullptr};          // +0x190
    bool shadow_{false};                  // +0x194
    float shadow_x_{}, shadow_y_{};       // +0x198 shadow_offs
    std::array<uint8_t, 4> shadow_rgba_{};            // +0x1A0
    bool override_rgba_;                  // +0x1A4 override_encoded_rgba
    bool remembered_override_;            // +0x1A5
    bool override_alpha_{};               // +0x1A6 override_encoded_alpha
    float inner_align_h_, inner_align_v_; // +0x1A8
};

} // namespace frontend
