#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

namespace script {
class Struct;
class Script;
} // namespace script

namespace render {
class BitmapTexture;
}

// The custom skater's face: where its features are on the face image (the face points), the face texture a profile
// keeps (gfx::FaceSkin - the image, its points and the overlay drawn over it), and "massaging" a face image onto
// the head model's texture: stretched so its features land where the model's are, coloured, and the overlay drawn
// over it. From the disassembly of 0x00486980-0x00487680.
namespace gfx {

constexpr std::size_t kOverlayNameSize = 0x40;

// Where a face's features are on its image (0x38 bytes; script structures hold them as left_eye / right_eye / nose /
// lips = [x y], h / s / v (all three or none), width / height).
struct FaceAnchors {
    std::array<int, 2> left_eye_;   // +0x00
    std::array<int, 2> right_eye_;  // +0x08
    std::array<int, 2> nose_slot_;  // +0x10
    std::array<int, 2> mouth_part_; // +0x18
    bool has_hsv_;      // +0x20: h, s and v were all given
    std::array<uint8_t, 3> pad21;
    float h_;     // +0x24
    float s_;     // +0x28
    float v_;     // +0x2C
    int span_;    // +0x30 (0x80 by default)
    int level_y_; // +0x34
};
constexpr std::size_t kFaceAnchorsHOffset = 0x24;
constexpr std::size_t kFaceAnchorsBytes = 0x38;

static_assert(offsetof(FaceAnchors, h_) == kFaceAnchorsHOffset && sizeof(FaceAnchors) == kFaceAnchorsBytes, "layout must match the original");

// The face points of the head model's texture (0x0067DC14: the default ones - CS_NSN_Head's) and the current ones
// massaged images are fitted to (0x0067DC4C), and the overlay drawn over them (0x006FA2EC).
// 0x00486980: a copy of the default points.
void fetch_stock_face_anchors(FaceAnchors* out);
// 0x004869A0: the current points set.
void change_face_anchors(const FaceAnchors* points);
// 0x004869C0: the overlay set (null: none).
void set_face_overlay(render::BitmapTexture* overlay);
// 0x004869D0: the points read from a structure: left_eye, right_eye, nose and lips (in that order - false at the first
// missing one, the ones before it set), then h, s and v (has_hsv_ only when all three are there; each read either
// way), width and height (0 when missing). True when all four points were there.
bool read_face_points(FaceAnchors* points, script::Struct* params);
// 0x00487000: the points written to a structure: the four as integer arrays of two, h / s / v when has_hsv_,
// width and height. True.
bool write_face_points(const FaceAnchors* points, script::Struct* params);
// 0x00486C40: `texture` (a face image) stretched so that `points` land on the current model points: moved by the
// noses' difference (Offset - texture slot 18 - with no colour), then pushed out or in along x on both sides of the
// nose and along y above and below it (slot 22 with `use_colour` / `colour` when pushing out, slot 21 when pulling
// in). The eyes' x are put in order and the eyes / lips kept off the nose's y first. True.
bool fit_face_texture(render::BitmapTexture* texture, const FaceAnchors* points, bool use_colour, uint32_t colour);
// 0x00486F70: the whole massage - the texture's image opened for editing (slot 8, on the bottom-up heap), fitted,
// recoloured with the points' h / s / v when there are some (slot 24), the overlay drawn over it (slot 41 with
// `generate_palette`) and closed again (slot 9). True. (`generate_palette` is a byte: replace_face_texture passes a
// parameter byte as it is, not made 0 / 1; the PC texture's slots ignore it.)
bool massage_face_texture(render::BitmapTexture* texture, const FaceAnchors* points, uint8_t generate_palette,
                          bool use_colour, uint32_t colour);

constexpr std::size_t kFaceSkinImageOffset = 0x3C;
constexpr std::size_t kFaceSkinOverlayNameOffset = 0x445C;
constexpr std::size_t kFaceSkinBytes = 0x449C;

// The face texture a skater profile keeps (0x449C bytes; made by memory::alloc_zeroed + Construct): a 128 x 128 8-bit image
// in the image file's layout (a 0x20-byte header, the 256-colour palette, the pixels), where its features are, and
// the overlay texture's file name.
class FaceSkin {
public:
    static constexpr int kTexelEdge = 0x4420;

    // 0x004875C0: not valid, the default points, the overlay "faces\CS_NSN_Head_wht_alpha".
    FaceSkin();
    // 0x00487380: the image loaded from "images\<name>.img.xbx" (with `full_path`: "<name>.img.xbx") - all the file's
    // bytes copied in, however many - and valid.
    void load_image_file(const char* name, bool full_path);
    // 0x00487420: the image copied from `data` (kTexelEdge bytes). Returns how many bytes it took.
    std::size_t set_image(std::span<const uint8_t> data);
    // 0x00487440: the image, the points and the overlay's name (with its terminator) written to `buffer`. Returns how
    // many bytes.
    int write_to(std::span<uint8_t> buffer);
    // 0x004874B0: the same read back. Returns how many bytes it took.
    std::size_t read_from(std::span<const uint8_t> buffer);
    // 0x00487510: whether there is an image.
    [[nodiscard]] bool is_sound() const;
    // 0x00487520
    void mark_usable(bool valid);
    // 0x00487530
    uint8_t* get_image();
    // 0x00487540: kTexelEdge.
    static int fetch_skin_resolution();
    // 0x00487550: a copy of the points in `out`. Returns out.
    FaceAnchors* get_points(FaceAnchors* out);
    // 0x00487570
    void set_points(const FaceAnchors* points);
    // 0x00487590: the overlay's file name (copied, unbounded).
    void set_overlay_name(const char* name);
    // 0x004875B0
    char* get_overlay_name();
    // 0x004875F0: the image from the structure's 0x99790D96 array (each element a dword of it - as many as there are),
    // the overlay's name from its 0xA7E7A264 string, the points from it (read_face_points).
    void load_from_structure(script::Struct* params);
    // 0x00487680: the same written to a structure (the image as an array of 0x1108 checksums).
    void save_to_structure(script::Struct* params);

private:
    bool is_usable_{false}; // +0x00
    std::array<uint8_t, 3> pad1{};
    FaceAnchors path_vertices_{}; // +0x04
    std::array<uint8_t, kTexelEdge> image_{}; // +0x3C
    std::array<char, kOverlayNameSize> overlay_name_{};   // +0x445C
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(FaceSkin, image_) == kFaceSkinImageOffset && offsetof(FaceSkin, overlay_name_) == kFaceSkinOverlayNameOffset &&
                          sizeof(FaceSkin) == kFaceSkinBytes,
                      "layout must match the original");
    }
};

} // namespace gfx

// Script commands (CFunctions) of the face massage. cdecl (params, script); all return true.
namespace commands {
// 0x00486B70: SetModelFaceTexturePoints face_points={...}: the current model points (read even when some are missing).
bool cfunc_set_model_face_texture_points(script::Struct* params, script::Script* script);
// 0x00486BC0: SetFaceMassageTextureOverlay <texture>: the overlay (unchanged if there's no such texture).
bool cfunc_set_face_massage_texture_overlay(script::Struct* params, script::Script* script);
// 0x00486BF0: CombineFaceTextureWithOverlay texture=<name>: the overlay drawn over the texture (slot 41).
bool cfunc_combine_face_texture_with_overlay(script::Struct* params, script::Script* script);
// 0x00486EE0: AdjustFaceTextureToModel texture=<name> face_points={...}: the texture fitted (fit_face_texture).
bool cfunc_adjust_face_texture_to_model(script::Struct* params, script::Script* script);
// 0x004872D0: MassageFaceTexture texture=<name> face_points={...} [no_palette_gen]: the whole massage.
bool cfunc_massage_face_texture(script::Struct* params, script::Script* script);
} // namespace commands
