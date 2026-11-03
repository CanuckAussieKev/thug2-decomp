// The custom skater's face: the face points, the face texture and the face massage (see face_texture.h). From the
// disassembly of 0x00486980-0x00487680.

#include "engine/gfx/face_texture.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>

#include "engine/gfx/engine_texture.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/file/packs.h"
#include "engine/gfx/texture_dict.h"
#include "engine/script/param_struct.h"
#include "engine/core/checked_index.h"
#include "engine/core/span_copy.h"
#include <gsl/pointers>
#include <iterator>
#include <span>
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/engine_parts.h"
#include "engine/core/checksums.h"

namespace gfx {

namespace {

using script::Array;
using script::Struct;

constexpr uint32_t kLeftEye = 0x08BF1D3F;      // left_eye
constexpr uint32_t kRightEye = 0xB0B44396;     // right_eye
constexpr uint32_t kNose = 0x7F03932C;         // nose
constexpr uint32_t kLips = 0x0E7EC187;         // lips
constexpr uint32_t kH = 0x6E94F918;            // h
constexpr uint32_t kS = 0xE4F130F4;            // s
constexpr uint32_t kV = 0x949BC47B;            // v
constexpr uint32_t kWidth = 0x73E5BAD0;        // width
constexpr uint32_t kHeight = 0x0AB21AF0;       // height
constexpr uint32_t kImageData = 0x99790D96;    // (name unknown) the face texture's image, a dword an element
constexpr uint32_t kOverlay = 0xA7E7A264;      // (name unknown) the face texture's overlay file name
constexpr uint32_t kTexture = 0x7D99F28D;      // texture
constexpr uint32_t kFacePoints = 0xAC3CD84C;   // face_points
constexpr uint32_t kNoPaletteGen = 0x5905256B; // no_palette_gen
constexpr uint32_t kNoColour = 0x80808080;

// NOT ORIGINAL grouping - face_texture.cpp's state (the original's globals). Made on first use, never destroyed
// (face_texture_state()).
struct FaceTextureState {
    FaceAnchors stock_face_anchors_{.left_eye_ = {49, 54}, .right_eye_ = {76, 54}, .nose_slot_ = {64, 75}, .mouth_part_ = {64, 89}, .has_hsv_ = false, .pad21 = {0, 0, 0}, .h_ = 0.0f, .s_ = 1.0f, .v_ = 1.0f, .span_ = 128, .level_y_ = 128};
    FaceAnchors active_face_anchors_{.left_eye_ = {49, 54}, .right_eye_ = {76, 54}, .nose_slot_ = {64, 75}, .mouth_part_ = {64, 89}, .has_hsv_ = false, .pad21 = {0, 0, 0}, .h_ = 0.0f, .s_ = 1.0f, .v_ = 1.0f, .span_ = 128, .level_y_ = 128};
    render::BitmapTexture* face_overlay_{};
};

FaceTextureState& face_texture_state()
{
    static core::NoDestroy<FaceTextureState> state;
    return state.get();
}

FaceAnchors& stock_face_anchors() { return face_texture_state().stock_face_anchors_; }
FaceAnchors& active_face_anchors() { return face_texture_state().active_face_anchors_; }
render::BitmapTexture*& face_overlay() { return face_texture_state().face_overlay_; }

// The game's texture dictionary (0x00700F1C) and its texture `name`.
render::BitmapTexture* find_texture(uint32_t name) { return render::engine_parts().game_texture_dict()->fetch_skin(name); }

// A texture's editing calls (the original calls most through the folded jumps 0x0048D060 [8], 0x0048DE30 [9],
// 0x0048DEC0 [18], 0x0049F370 [21], 0x0048DEF0 [22], 0x0049F390 [24], 0x0049F3C0 [33], 0x0049F3D0 [34], 0x0049F420
// [41]).
void begin_edit(render::BitmapTexture* texture) { texture->begin_edit(false, false); }
void end_edit(render::BitmapTexture* texture, uint8_t generate_palette)
{
    texture->write_back(generate_palette, nullptr);
}
void get_offset(render::BitmapTexture* texture, int x, int y, bool use_colour, uint32_t colour)
{
    texture->get_offset(x, y, use_colour, static_cast<int>(colour));
}
// Pixels pulled in towards `point` along `axis` (0 x, 1 y) by `amount` (negative: from the other side)...
void get_stretch(render::BitmapTexture* texture, int point, int axis, int amount)
{
    texture->get_stretch({.position = static_cast<uint16_t>(point), .vertical = axis, .amount = amount});
}
// ... or pushed out from it, the gap filled with `colour`.
void stack_on(render::BitmapTexture* texture, int point, int axis, int amount, bool use_colour, uint32_t colour)
{
    texture->squash({.position = static_cast<uint16_t>(point), .vertical = axis, .amount = amount}, use_colour, colour);
}
void shift_hsv(render::BitmapTexture* texture, float h, float s, float v)
{
    texture->shift_hue_sat_value({.hue = h, .saturation = s, .value = v}, false);
}
uint16_t get_width(render::BitmapTexture* texture) { return texture->fetch_breadth(); }
uint16_t get_height(render::BitmapTexture* texture) { return texture->fetch_tallness(); }
void draw_over(render::BitmapTexture* texture, render::BitmapTexture* overlay, uint8_t generate_palette)
{
    texture->get_overlay(overlay, generate_palette);
}

// The C runtime's _ftol (0x006236FC): truncated to a 64-bit integer, the low half kept.
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// strcpy (unbounded, as the original's inlined copies).
void copy_string(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }

// A point read from a structure's array of two (false if it isn't there).
bool read_point(Struct* params, uint32_t name, std::array<int, 2>& point)
{
    Array* array = nullptr;
    params->fetch_list(name, &array, 0);
    if (!array)
        return false;
    std::get<0>(point) = static_cast<int>(array->fetch_part(0));
    std::get<1>(point) = static_cast<int>(array->fetch_part(1));
    return true;
}

// A new integer array of two holding `point`.
gsl::owner<Array*> new_point_array(const std::array<int, 2>& point)
{
    gsl::owner<Array*> array = script::new_array();
    array->configure_size_kind(2, script::kQbvalInteger);
    array->set_element(0, static_cast<uint32_t>(std::get<0>(point)));
    array->set_element(1, static_cast<uint32_t>(std::get<1>(point)));
    return array;
}

} // namespace

// 0x00486980
void fetch_stock_face_anchors(FaceAnchors* out) { *out = stock_face_anchors(); }

// 0x004869A0
void change_face_anchors(const FaceAnchors* points) { active_face_anchors() = *points; }

// 0x004869C0
void set_face_overlay(render::BitmapTexture* overlay) { face_overlay() = overlay; }

// 0x004869D0
bool read_face_points(FaceAnchors* points, Struct* params)
{
    if (!read_point(params, kLeftEye, points->left_eye_) || !read_point(params, kRightEye, points->right_eye_) ||
        !read_point(params, kNose, points->nose_slot_) || !read_point(params, kLips, points->mouth_part_))
        return false;
    points->has_hsv_ = params->fetch_float(kH, &points->h_, 0);
    points->has_hsv_ = params->fetch_float(kS, &points->s_, 0) && points->has_hsv_;
    points->has_hsv_ = params->fetch_float(kV, &points->v_, 0) && points->has_hsv_;
    points->span_ = 0;
    points->level_y_ = 0;
    params->fetch_int_value(kWidth, &points->span_, 0);
    params->fetch_int_value(kHeight, &points->level_y_, 0);
    return true;
}

// 0x00487000
bool write_face_points(const FaceAnchors* points, Struct* params)
{
    gsl::owner<Array*> left_eye = new_point_array(points->left_eye_);
    gsl::owner<Array*> right_eye = new_point_array(points->right_eye_);
    gsl::owner<Array*> nose = new_point_array(points->nose_slot_);
    gsl::owner<Array*> lips = new_point_array(points->mouth_part_);
    params->append_array_ref(kLeftEye, left_eye);
    params->append_array_ref(kRightEye, right_eye);
    params->append_array_ref(kNose, nose);
    params->append_array_ref(kLips, lips);
    if (points->has_hsv_) {
        params->push_float(checksums::Name{kH}, points->h_);
        params->push_float(checksums::Name{kS}, points->s_);
        params->push_float(checksums::Name{kV}, points->v_);
    }
    params->put_int(checksums::Name{kWidth}, points->span_);
    params->put_int(checksums::Name{kHeight}, points->level_y_);
    return true;
}

// 0x00486C40
bool fit_face_texture(render::BitmapTexture* texture, const FaceAnchors* points, bool use_colour, uint32_t colour)
{
    FaceAnchors face = *points;
    if (core::at(face.left_eye_, 0) > core::at(face.right_eye_, 0)) {
        const int x = core::at(face.left_eye_, 0);
        core::at(face.left_eye_, 0) = core::at(face.right_eye_, 0);
        core::at(face.right_eye_, 0) = x;
    }
    const int nose_y = core::at(face.nose_slot_, 1);
    if (core::at(face.left_eye_, 1) > nose_y)
        core::at(face.left_eye_, 1) = nose_y - 1;
    if (core::at(face.right_eye_, 1) >= nose_y)
        core::at(face.right_eye_, 1) = nose_y - 2;
    if (core::at(face.mouth_part_, 1) <= nose_y)
        core::at(face.mouth_part_, 1) = nose_y + 2;

    const FaceAnchors& model = active_face_anchors();
    const int model_nose_x = core::at(model.nose_slot_, 0);
    const int model_nose_y = core::at(model.nose_slot_, 1);
    const int dx = model_nose_x - core::at(face.nose_slot_, 0);
    const int dy = model_nose_y - nose_y;
    get_offset(texture, dx, dy, false, kNoColour);

    const int model_eyes_y = (core::at(model.left_eye_, 1) + core::at(model.right_eye_, 1)) >> 1;
    const int eyes_y = ((core::at(face.left_eye_, 1) + core::at(face.right_eye_, 1)) >> 1) + dy;
    // The scales of the sides right of, above and below the nose (x87 quotients rounded to floats).
    auto scale_right = static_cast<float>(static_cast<double>(get_width(texture) - core::at(face.nose_slot_, 0) - dx) /
                                          static_cast<double>(core::at(face.right_eye_, 0) - core::at(face.nose_slot_, 0)));
    auto scale_up = static_cast<float>(static_cast<double>(dy + nose_y) / static_cast<double>(dy - eyes_y + nose_y));
    auto scale_down = static_cast<float>(static_cast<double>(get_height(texture) - dy - nose_y) /
                                         static_cast<double>(core::at(face.mouth_part_, 1) - nose_y));

    // Left of the nose (its quotient kept at the x87's precision)
    const double scale_left =
        static_cast<double>(core::at(face.nose_slot_, 0) + dx) / static_cast<double>(core::at(face.nose_slot_, 0) - core::at(face.left_eye_, 0));
    int amount = ftol(scale_left * (core::at(model.left_eye_, 0) - dx - core::at(face.left_eye_, 0)) + 0.5f);
    if (amount < 0)
        get_stretch(texture, model_nose_x, 0, amount);
    else
        stack_on(texture, model_nose_x, 0, amount, use_colour, colour);
    // Right of it
    amount = ftol(static_cast<double>(core::at(model.right_eye_, 0) - dx - core::at(face.right_eye_, 0)) * scale_right + 0.5f);
    if (amount >= 0)
        get_stretch(texture, model_nose_x, 0, amount);
    else
        stack_on(texture, model_nose_x, 0, amount, use_colour, colour);
    // Above it
    amount = ftol(static_cast<double>(model_eyes_y - eyes_y) * scale_up + 0.5f);
    if (amount < 0)
        get_stretch(texture, model_nose_y, 1, amount);
    else
        stack_on(texture, model_nose_y, 1, amount, use_colour, colour);
    // Below it
    amount = ftol(static_cast<double>(core::at(model.mouth_part_, 1) - dy - core::at(face.mouth_part_, 1)) * scale_down + 0.5f);
    if (amount >= 0)
        get_stretch(texture, model_nose_y, 1, amount);
    else
        stack_on(texture, model_nose_y, 1, amount, use_colour, colour);
    return true;
}

// 0x00486F70
bool massage_face_texture(render::BitmapTexture* texture, const FaceAnchors* points, uint8_t generate_palette,
                          bool use_colour, uint32_t colour)
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->bottom_up_heap());
    begin_edit(texture);
    memory::Hub::get_pull()->leave_heap_scope();
    fit_face_texture(texture, points, use_colour, colour);
    if (points->has_hsv_)
        shift_hsv(texture, points->h_, points->s_, points->v_);
    if (face_overlay())
        draw_over(texture, face_overlay(), generate_palette);
    end_edit(texture, generate_palette);
    return true;
}

// --- the face texture -------------------------------------------------------------------------------------------

// 0x004875C0
FaceSkin::FaceSkin()
{
    fetch_stock_face_anchors(&path_vertices_);
    copy_string(overlay_name_.data(), "faces\\CS_NSN_Head_wht_alpha");
}

// 0x00487380
void FaceSkin::load_image_file(const char* name, bool full_path)
{
    std::array<char, 512> path{};
    text::format_to(path, "%s%s.img.xbx", full_path ? "" : "images\\", name); // "%s.img.xbx" / "images\%s.img.xbx"
    int size = 0; // (uninitialised in the original: a file that can't be loaded copies from null)
    auto* data = static_cast<uint8_t*>(packs::load_file_into(path.data(), &size, nullptr));
    std::memcpy(image_.data(), data, static_cast<size_t>(size));
    memory::free_malloc(data);
    is_usable_ = true;
}

// 0x00487420
std::size_t FaceSkin::set_image(std::span<const uint8_t> data)
{
    // (NOT ORIGINAL: a shorter run copies what it has - the original read on past it)
    const std::size_t copied = std::min<std::size_t>(data.size(), kTexelEdge);
    core::copy_into(data.first(copied), std::span<uint8_t>(image_).first(copied));
    return copied;
}

// 0x00487440
int FaceSkin::write_to(std::span<uint8_t> buffer)
{
    const std::size_t length = std::strlen(overlay_name_.data());
    const std::size_t total = kTexelEdge + sizeof(path_vertices_) + length + 1;
    const std::span<uint8_t> out = buffer.first(total); // (the caller's room is checked)
    core::copy_into(std::span<const uint8_t>(image_), out.first(kTexelEdge));
    std::memcpy(out.subspan(kTexelEdge, sizeof(path_vertices_)).data(), &path_vertices_, sizeof(path_vertices_));
    std::transform(overlay_name_.begin(), overlay_name_.begin() + static_cast<std::ptrdiff_t>(length + 1),
                   out.subspan(kTexelEdge + sizeof(path_vertices_)).begin(),
                   [](char c) { return static_cast<uint8_t>(c); });
    return static_cast<int>(total);
}

// 0x004874B0
std::size_t FaceSkin::read_from(std::span<const uint8_t> buffer)
{
    // (NOT ORIGINAL: a run too short for the image and the points reads nothing; a name too long for the field is
    // cut to fit - the original read on past the run and wrote past the field)
    constexpr std::size_t kHead = kTexelEdge + sizeof(path_vertices_);
    if (buffer.size() < kHead)
        return 0;
    core::copy_into(buffer.first(kTexelEdge), std::span<uint8_t>(image_));
    std::memcpy(&path_vertices_, buffer.subspan(kTexelEdge, sizeof(path_vertices_)).data(), sizeof(path_vertices_));
    const std::span<const uint8_t> rest = buffer.subspan(kHead);
    const auto terminator = std::ranges::find(rest, uint8_t{0}); // (the overlay's name: up to its NUL)
    const auto name_length = static_cast<std::size_t>(std::distance(rest.begin(), terminator));
    const std::size_t length = std::min(name_length, overlay_name_.size() - 1);
    std::transform(rest.begin(), rest.begin() + static_cast<std::ptrdiff_t>(length), overlay_name_.begin(),
                   [](uint8_t c) { return static_cast<char>(c); });
    core::at(overlay_name_, length) = '\0';
    return kHead + name_length + 1;
}

// 0x00487510
bool FaceSkin::is_sound() { return is_usable_; }

// 0x00487520
void FaceSkin::mark_usable(bool valid) { is_usable_ = valid; }

// 0x00487530
uint8_t* FaceSkin::get_image() { return image_.data(); }

// 0x00487540
int FaceSkin::fetch_skin_resolution() { return kTexelEdge; }

// 0x00487550
FaceAnchors* FaceSkin::get_points(FaceAnchors* out)
{
    *out = path_vertices_;
    return out;
}

// 0x00487570
void FaceSkin::set_points(const FaceAnchors* points) { path_vertices_ = *points; }

// 0x00487590
void FaceSkin::set_overlay_name(const char* name) { copy_string(overlay_name_.data(), name); }

// 0x004875B0
char* FaceSkin::get_overlay_name() { return overlay_name_.data(); }

// 0x004875F0
void FaceSkin::load_from_structure(Struct* params)
{
    Array* image = nullptr; // (uninitialised in the original when missing)
    params->fetch_list(kImageData, &image, 1);
    // (NOT ORIGINAL: no more words than the image holds - the original wrote as many as the array had)
    constexpr uint32_t kWords = kTexelEdge / 4;
    for (uint32_t i = 0; i < image->extent() && i < kWords; ++i) {
        const uint32_t word = image->fetch_part(i);
        std::memcpy(&core::at(image_, 4 * i), &word, sizeof(word));
    }
    const char* overlay = nullptr; // (uninitialised in the original when missing)
    params->fetch_text(overlay, &overlay, 1);
    copy_string(overlay_name_.data(), overlay);
    read_face_points(&path_vertices_, params);
}

// 0x00487680
void FaceSkin::save_to_structure(Struct* params)
{
    gsl::owner<Array*> image = script::new_array();
    constexpr uint32_t kWords = kTexelEdge / 4;
    image->configure_size_kind(kWords, script::kQbvalName);
    for (uint32_t i = 0; i < kWords; ++i) {
        uint32_t word = 0;
        std::memcpy(&word, &core::at(image_, 4 * i), sizeof(word));
        image->set_element(i, word);
    }
    params->append_array_ref(kImageData, image);
    params->append_word(kOverlay, overlay_name_.data());
    write_face_points(&path_vertices_, params);
}

} // namespace gfx

// --- the script commands ----------------------------------------------------------------------------------------

namespace commands {

using gfx::FaceAnchors;
using script::Struct;

// 0x00486B70
bool cfunc_set_model_face_texture_points(Struct* params, script::Script*)
{
    Struct* points = nullptr;
    params->read_sub_struct("face_points", &points, 0);
    if (points) {
        FaceAnchors read{}; // (the original's isn't cleared: points missing are the stack's)
        gfx::read_face_points(&read, points);
        gfx::change_face_anchors(&read); // (whatever was read)
    }
    return true;
}

// 0x00486BC0
bool cfunc_set_face_massage_texture_overlay(Struct* params, script::Script*)
{
    uint32_t name = 0; // (uninitialised in the original without one)
    params->read_hash_value(0u, &name, 0);
    if (render::BitmapTexture* texture = gfx::find_texture(name))
        gfx::set_face_overlay(texture);
    return true;
}

// 0x00486BF0
bool cfunc_combine_face_texture_with_overlay(Struct* params, script::Script*)
{
    uint32_t name = 0; // (uninitialised in the original without one)
    params->read_hash_value(gfx::kTexture, &name, 0);
    render::BitmapTexture* texture = gfx::find_texture(name);
    if (texture && gfx::face_overlay())
        gfx::draw_over(texture, gfx::face_overlay(), false);
    return true;
}

// 0x00486EE0
bool cfunc_adjust_face_texture_to_model(Struct* params, script::Script*)
{
    uint32_t name = 0; // (uninitialised in the original without one)
    params->read_hash_value(gfx::kTexture, &name, 0);
    render::BitmapTexture* texture = gfx::find_texture(name);
    if (texture) {
        Struct* points_params = nullptr;
        params->read_sub_struct(gfx::kFacePoints, &points_params, 0);
        if (points_params) {
            FaceAnchors points{}; // (the original's isn't cleared)
            gfx::read_face_points(&points, points_params);
            gfx::fit_face_texture(texture, &points, false, gfx::kNoColour);
        }
    }
    return true;
}

// 0x004872D0
bool cfunc_massage_face_texture(Struct* params, script::Script*)
{
    uint32_t name = 0; // (uninitialised in the original without one)
    params->read_hash_value(gfx::kTexture, &name, 0);
    render::BitmapTexture* texture = gfx::find_texture(name);
    if (texture) {
        bool generate_palette = true;
        Struct* points_params = nullptr;
        params->read_sub_struct(gfx::kFacePoints, &points_params, 0);
        if (params->has_bare_flag(gfx::kNoPaletteGen))
            generate_palette = false;
        if (points_params) {
            FaceAnchors points{}; // (the original's isn't cleared)
            gfx::read_face_points(&points, points_params);
            gfx::massage_face_texture(texture, &points, generate_palette, false, gfx::kNoColour);
        }
    }
    return true;
}

} // namespace commands
