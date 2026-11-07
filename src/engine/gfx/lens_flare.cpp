// The sun and its lens flare (see lens_flare.h).

#include "engine/gfx/lens_flare.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <memory>
#include <span>

#include "engine/memory/mem.h"
#include "engine/ui/screen_element.h"
#include "engine/ui/screen_element_cfuncs.h"
#include "engine/ui/screen_element_manager.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/engine_primitive.h"
#include "engine/gfx/engine_texture.h"
#include "engine/gfx/vertex_run.h"
#include "engine/gfx/texture_dict.h"
#include "engine/gfx/viewport.h"
#include "engine/gfx/viewport_list.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/script/context.h"
#include "engine/sys/game_clock.h"
#include "base/core/checked_index.h"
#include "base/core/packed_colour.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/engine_parts.h"
#include "base/core/checksums.h"
#include "base/maths/vec2.h"
#include "engine/gfx/screen.h"
#include "engine/gfx/render_device.h"
#include <bit>
#include <gsl/pointers>

namespace sun {

constexpr std::size_t kQuadCornerFloats = 12;
constexpr std::size_t kQuadUvFloats = 8;
constexpr float kIntOneAsFloat = 1.401298464e-45F;
constexpr float kOcclusionProbeHalfSize = 5.0F;
constexpr int kByte1Shift = 8;
constexpr int kByte2Shift = 16;
constexpr int kByte3Shift = 24;
constexpr float kByteMaxFloat = 255.0F;
constexpr float kHalfAlphaFloat = 128.0F;
constexpr float kMillisecondsPerSecond = 1000.0F;
constexpr double kHalf = 0.5;

using script::Array;
using script::Struct;

namespace {

constexpr uint32_t kPosScale = 0xEAA2389E;           // "pos_scale"
constexpr uint32_t kScale = 0x13B9DA7B;              // "scale"
constexpr uint32_t kRgba = 0x3F6BCDBA;               // "rgba"
constexpr uint32_t kId = 0x40C698AF;                 // "id"
constexpr uint32_t kTime = 0x906B67BA;               // "time"
constexpr uint32_t kPos = 0x7F261953;                // "pos"
constexpr uint32_t kMorphIn = 0x8BDA33DC;            // "LensFlare_Morph_In"
constexpr uint32_t kMorphOut = 0x753D46C1;           // "LensFlare_Morph_Out"
constexpr uint32_t kSunBurstOut = 0x2E56C52D;        // "SunBurst_Out"
constexpr uint32_t kCenter = 0xBF0F14DB;             // "center"
constexpr uint32_t kMorphTime = 0xA99F828E;          // "morph_time"
constexpr uint32_t kSunPos = 0x2E089F23;             // "sun_pos"
constexpr uint32_t kContainer = 0x43AAD2DD;          // "Lens_Flare_Container"
constexpr uint32_t kAlpha = 0x2F1FC695;              // "alpha"
constexpr uint32_t kCreateLensFlare = 0xD355725F;    // "CreateLensFlare"
constexpr uint32_t kDoUpdateLensFlare = 0x204F4CEC;  // "DoUpdateLensFlare"
constexpr uint32_t kMaybeHideLensFlare = 0xD4F107CB; // "MaybeHideLensFlare"
constexpr uint32_t kBlend = 0x54628ED7;              // "blend"
constexpr uint32_t kAdd = 0x02E58C18;                // "Add"

// NOT ORIGINAL grouping - lens_flare.cpp's state (the original's globals). Made on first use, never destroyed
// (lens_flare_state()).
struct LensFlareState {
    float flare_theta_{};
    float flare_phi_{};
    float flare_radius_{};
    float flare_size_{};
    float flare_morph_in_time_{};
    float flare_morph_out_time_{};
    float flare_sun_burst_out_time_{};
    uint8_t flare_query_{};
    bool flare_shown_{};
    bool flare_hidden_{};
    render::BitmapTexture* flare_sun_texture_{}; // (taken from the particle dictionary: deleted as the flare's own, as the original does)
    render::BitmapTexture* flare_corona_texture_{};
    FlareElement* flare_elements_{};
    uint32_t flare_element_count_{};
    uint32_t flare_visible_time_{};
    uint32_t flare_hidden_time_{};
    maths::Vec4 flare_color_{};
    maths::Vec4 flare_position_{};
};

LensFlareState& lens_flare_state()
{
    static core::NoDestroy<LensFlareState> state;
    return state.get();
}

// The sun's state.
float& get_theta() { return lens_flare_state().flare_theta_; }
float& get_phi() { return lens_flare_state().flare_phi_; }
float& get_radius() { return lens_flare_state().flare_radius_; }
float& get_size() { return lens_flare_state().flare_size_; }
float& morph_in_time() { return lens_flare_state().flare_morph_in_time_; } // seconds
float& morph_out_time() { return lens_flare_state().flare_morph_out_time_; }
float& sun_burst_out_time() { return lens_flare_state().flare_sun_burst_out_time_; }
uint8_t& get_query() { return lens_flare_state().flare_query_; } // the occlusion query's result number (0: none)
bool& get_shown() { return lens_flare_state().flare_shown_; }
bool& flare_hidden() { return lens_flare_state().flare_hidden_; }
render::BitmapTexture*& get_corona_texture() { return lens_flare_state().flare_corona_texture_; }
FlareElement*& get_elements() { return lens_flare_state().flare_elements_; }
std::span<FlareElement> element_run() { return {lens_flare_state().flare_elements_, lens_flare_state().flare_element_count_}; }
uint32_t& element_count() { return lens_flare_state().flare_element_count_; }
uint32_t& visible_time() { return lens_flare_state().flare_visible_time_; } // when the sun was last seen
uint32_t& hidden_time() { return lens_flare_state().flare_hidden_time_; }   // when it was last not seen
maths::Vec4& get_color() { return lens_flare_state().flare_color_; }
maths::Vec4& get_position() { return lens_flare_state().flare_position_; } // in the world, this frame

// The texture `checksum` (the texture dictionary 0x00700F20's look-up), and a texture deleted.
render::BitmapTexture* find_texture(uint32_t checksum) { return render::engine_parts().particle_texture_dict()->fetch_skin(checksum); }
void delete_texture(gsl::owner<render::BitmapTexture*> texture) { delete texture; }

// The game's clock with its offset, and the milliseconds since `time`.
uint32_t get_now() { return static_cast<uint32_t>(frame_clock::game_clock_time()); }
uint32_t get_since(uint32_t time)
{
    return static_cast<uint32_t>(frame_clock::game_clock_time_since(static_cast<int>(time)));
}

// The first viewport's camera (Nx, 0x004A06D0): its vtable slot 1 (through 0x004A1090) projects a point to the
// screen (the depth scale returned), slot 3 (through 0x004A10A0) issues an occlusion query over a screen
// rectangle (the query's number, 0 if none).
render::ViewRegion* viewport_camera() { return render::fetch_current_view(0); }
// (the depth is written as an integer into the float, as the original)
void cast_onto(render::ViewRegion* camera, const maths::Vec4* point, maths::Vec2* at, float* z)
{
    int whole = 0;
    camera->project_to_screen(point, at, &whole);
    *z = std::bit_cast<float>(whole);
}
uint8_t issue_query(render::ViewRegion* camera, float left, float top, float right, float bottom, int unknown,
                    uint8_t query)
{
    return camera->query_rect({.x0 = left, .y0 = top, .x1 = right, .y1 = bottom}, {.z = static_cast<uint32_t>(unknown), .query = query});
}

// The sun's and the corona's quads: a PC primitive drawer on the stack (0x200 bytes kept for it), made with 2 (not
// listed: drawn by hand) - its texture (through the jump 0x004A1090), its layout (0x004A10A0, given 3 4 4), the blend
// mode (+0x0C: the folded setter 0x00428270), +0x14 set (the folded setter 0x00407A50), readied and drawn from arrays
// - corners, colours, texture coordinates (draw_if_ready). NOT ORIGINAL: the graphics backend makes the drawer in the
// stack bytes kept for it (render::Device::make_primitive_at); it is destroyed with the scope, as the original's.
using render::kStackPrimitiveBytes;
class StackPrimitive {
public:
    explicit StackPrimitive(int own) : run_(render::device().make_primitive_at(storage_, own)) {}
    ~StackPrimitive() { std::destroy_at(run_); }
    StackPrimitive(const StackPrimitive&) = delete;
    StackPrimitive& operator=(const StackPrimitive&) = delete;
    StackPrimitive(StackPrimitive&&) = delete;
    StackPrimitive& operator=(StackPrimitive&&) = delete;
    [[nodiscard]] render::VertexRun& primitive() const { return *run_; }

private:
    alignas(std::max_align_t) std::array<std::byte, kStackPrimitiveBytes> storage_{};
    gsl::owner<render::VertexRun*> run_;
};
using Prim = render::VertexRun;
void blit_quad(Prim* quad, const std::array<float, kQuadCornerFloats>& corners, const std::array<colour::Bytes, 4>& colors, const std::array<float, kQuadUvFloats>& uvs)
{
    std::array<uint32_t, 4> packed{};
    for (size_t i = 0; i < packed.size(); ++i) {
        core::at(packed, i) = colour::packed_of(core::at(colors, i));
    }
    quad->draw_if_ready(corners, packed, uvs, {});
}
// A render state for the primitives (0x0049C700).
void set_prim_state(int state, int value) { render::set_primitive_state(state, value); }
// A query to issue this frame (0x004A0EB0 takes a copy and fills in its slot number), and a query's result - its
// visible pixels (0x004A0F20).
// (0x004426C0 constructs the request: cleared)
void add_query(render::OcclusionRequest* request) { render::request_occlusion_query(request); }
uint32_t query_result(uint8_t /*unused*/)
{
    return render::device().occlusion_pixels();
} // (the original also passed the query: unused)
// `v` times `m` in place (all four components; 0x00434200).
void transform_in_place(maths::Vec4* v, const maths::Mat4* matrix) { v->multiply_by(*matrix); }

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }
// x87's dot product order (0x00402530), unrounded.
double get_dot(const maths::Vec4& a, const maths::Vec4& b)
{
    return (static_cast<double>(a.z) * b.z) + (static_cast<double>(a.y) * b.y) + (static_cast<double>(a.x) * b.x);
}
script::VectorValue get_value(const maths::Vec4& v) { return {.x = v.x, .y = v.y, .z = v.z, .w = v.w}; }
gsl::owner<script::Struct*> new_params() { return script::new_struct(); }
// The screen centre lens flares are placed from.
maths::Vec4 screen_centre() { return {.x = render::kLayoutCentreX, .y = render::kLayoutCentreY, .z = 0.0F, .w = 1.0F}; }

// The sun's colour scaled into bytes for each of the quad's four corners.
void corner_colors(std::array<std::array<uint8_t, 4>, 4>& colors, double scale)
{
    for (int corner = 0; corner < 4; ++corner) {
        for (int i = 0; i < 4; ++i) {
            core::at(core::at(colors, corner), i) = static_cast<uint8_t>(get_truncated(get_color()[i] * scale));
        }
    }
}

} // namespace

// 0x00442680
float clamp_to_range(float value, float min, float max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

// 0x004426D0
void set_sun(const Settings* settings, uint32_t mask)
{
    if ((mask & kTheta) != 0U) {
        get_theta() = settings->theta;
    }
    if ((mask & kPhi) != 0U) {
        get_phi() = settings->phi;
    }
    if ((mask & kRadius) != 0U) {
        get_radius() = settings->radius;
    }
    if ((mask & kSize) != 0U) {
        get_size() = settings->size;
    }
    if ((mask & kColor) != 0U) {
        get_color() = settings->color;
    }
    if ((mask & kShown) != 0U) {
        get_shown() = settings->shown;
    }
    if ((mask & kTexture) != 0U) {
        if (lens_flare_state().flare_sun_texture_) {
            delete_texture(memory::owned_element(lens_flare_state().flare_sun_texture_));
        }
        lens_flare_state().flare_sun_texture_ = nullptr;
        lens_flare_state().flare_sun_texture_ = find_texture(settings->texture);
    }
    if ((mask & kCoronaTexture) != 0U) {
        // (the sun's texture is the one deleted, as the original)
        if (get_corona_texture() && lens_flare_state().flare_sun_texture_) {
            delete_texture(memory::owned_element(lens_flare_state().flare_sun_texture_));
        }
        get_corona_texture() = find_texture(settings->corona_texture_);
    }
}

// 0x00442810
void query_sun_visibility(uint32_t /*data*/, uint32_t slot)
{
    const auto query = static_cast<uint8_t>(slot); // (the original's callback took the slot as a byte)
    maths::Vec4 screen = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    render::ViewRegion* camera = viewport_camera();
    float z = kIntOneAsFloat; // (an int 1 in the original, overwritten)
    maths::Vec2 at{.x = screen[0], .y = screen[1]};
    cast_onto(camera, &get_position(), &at, &z);
    screen[0] = at.x;
    screen[1] = at.y;
    get_query() = issue_query(camera, screen[0] - kOcclusionProbeHalfSize, screen[1] - kOcclusionProbeHalfSize, screen[0] + kOcclusionProbeHalfSize, screen[1] + kOcclusionProbeHalfSize, 1, query);
}

// 0x004428F0
void place_flare_elements(float time, const maths::Vec4* sun, const maths::Vec4* centre)
{
    const maths::Vec4 line = *sun - *centre;
    for (uint32_t i = 0; i < element_count(); ++i) {
        FlareElement& element = core::at(element_run(), i);
        const maths::Vec4 pos = *centre + element.pos_scale_ * line;
        element.morph->push_float(checksums::Name{kTime}, time);
        element.morph->put_vec3(checksums::Name{kPos}, get_value(pos));
        frontend::cfunc_do_screen_element_morph(element.morph, script::Context());
    }
    flare_hidden() = false;
}

// 0x004429D0
void prepare_all(const FlareSetup& setup)
{
    const auto theta = setup.theta;
    const auto phi = setup.phi;
    const auto radius = setup.radius;
    const auto size = setup.size;
    const auto texture = setup.texture;
    const auto corona_texture = setup.corona_texture_;
    const auto lens_flare_data = setup.lens_flare_data_;
    get_theta() = theta;
    get_phi() = phi;
    get_radius() = radius;
    get_size() = size;
    get_shown() = true;
    lens_flare_state().flare_sun_texture_ = nullptr;
    lens_flare_state().flare_sun_texture_ = find_texture(texture);
    get_corona_texture() = find_texture(corona_texture);
    Array* data = script::fetch_list(lens_flare_data);
    element_count() = data->extent();
    get_elements() = static_cast<FlareElement*>(memory::alloc(element_count() * sizeof(FlareElement)));
    for (uint32_t i = 0; i < element_count(); ++i) {
        auto* item = data->struct_at(i);
        Array* rgba = nullptr;
        FlareElement& element = core::at(element_run(), i);
        item->fetch_float(kPosScale, &element.pos_scale_, 1);
        item->fetch_float(kScale, &element.scale, 1);
        item->fetch_list(kRgba, &rgba, 1);
        element.rgba =
            rgba->fetch_part(3) << kByte3Shift | rgba->fetch_part(2) << kByte2Shift | rgba->fetch_part(1) << kByte1Shift | rgba->fetch_part(0);
        item->read_hash_value(kId, &element.id, 1);
        gsl::owner<Struct*> create = new_params(); // (the element's for the flare's life: never freed, as the original)
        element.create = create;
        element.create->put_name_key(checksums::Name{kId}, element.id);
        element.create->push_float(checksums::Name{kScale}, element.scale);
        element.create->append_array_value(kRgba, rgba);
        gsl::owner<Struct*> morph = new_params(); // (the same)
        element.morph = morph;
        element.morph->put_name_key(checksums::Name{kId}, element.id);
        element.morph->put_vec3(checksums::Name{kPos}, script::VectorValue{.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F});
        element.morph->push_float(checksums::Name{kTime}, 0.0F);
    }
    morph_in_time() = script::fetch_float(kMorphIn);
    morph_out_time() = script::fetch_float(kMorphOut);
    sun_burst_out_time() = script::fetch_float(kSunBurstOut);
    get_color() = {.x = kByteMaxFloat, .y = kByteMaxFloat, .z = kByteMaxFloat, .w = kHalfAlphaFloat};
}

// 0x00442D70
void update_flare(Struct* params, script::Context /*context*/)
{
    script::VectorValue centre = get_value(screen_centre());
    float time = 0.0F;
    params->fetch_vec(kCenter, &centre, 0);
    params->fetch_float(kMorphTime, &time, 0);
    script::VectorValue sun{}; // (unset if there's none: the getter asserts)
    params->fetch_vec(kSunPos, &sun, 1);
    const maths::Vec4 sun_pos = {.x = sun.x, .y = sun.y, .z = sun.z, .w = sun.w};
    const maths::Vec4 centre_pos = {.x = centre.x, .y = centre.y, .z = centre.z, .w = centre.w};
    place_flare_elements(time, &sun_pos, &centre_pos);
}

// 0x00442DF0
void hide_flare(Struct* params, script::Context context)
{
    script::VectorValue centre = get_value(screen_centre()); // (read, not used)
    float time = 0.0F;
    frontend::ElementPtr container;
    frontend::UiTree::fetch_part(&container, kContainer, 0);
    if (container.is_null()) {
        container.get_release();
        return;
    }
    if (params) {
        params->fetch_vec(kCenter, &centre, 0);
        params->fetch_float(kMorphTime, &time, 0);
    }
    gsl::owner<Struct*> morph = new_params();
    morph->put_name_key(checksums::Name{kId}, kContainer);
    morph->push_float(checksums::Name{kAlpha}, 0.0F);
    morph->push_float(checksums::Name{kTime}, time);
    frontend::cfunc_do_screen_element_morph(morph, context);
    script::free_struct(morph);
    if (params) {
        script::VectorValue sun{};
        if (params->fetch_vec(kSunPos, &sun, 0)) {
            for (uint32_t i = 0; i < element_count(); ++i) {
                FlareElement& element = core::at(element_run(), i);
                element.morph->put_vec3(checksums::Name{kPos}, sun);
                element.morph->push_float(checksums::Name{kTime}, time);
                frontend::cfunc_do_screen_element_morph(element.morph, context);
            }
        }
    }
    flare_hidden() = true;
    container.get_release();
}

// 0x00443030
void unhide_flare(Struct* /*params*/, script::Context context)
{
    frontend::ElementPtr container;
    frontend::UiTree::fetch_part(&container, kContainer, 0);
    if (container.is_null()) {
        container.get_release();
        return;
    }
    const float time = 0.2F;
    gsl::owner<Struct*> morph = new_params();
    morph->put_name_key(checksums::Name{kId}, kContainer);
    morph->push_float(checksums::Name{kAlpha}, 1.0F);
    morph->push_float(checksums::Name{kTime}, time);
    frontend::cfunc_do_screen_element_morph(morph, context);
    script::free_struct(morph);
    flare_hidden() = false;
    container.get_release();
}

// 0x00443180
void build_sun_quad(gfx::ViewCam* camera, render::VertexRun* quad, const maths::Vec4* position, std::array<float, kQuadCornerFloats>& corners)
{
    // facing the camera: its right axis reversed, then up and back from cross products
    maths::Mat4 facing{};
    facing.reset_to_identity();
    facing[0] = -(*camera->fetch_mat())[0];
    facing[2] = maths::cross_vec((*camera->fetch_mat())[1], facing[0]);
    facing[1] = maths::cross_vec(facing[2], facing[0]);
    if (lens_flare_state().flare_sun_texture_) {
        quad->set_texture(lens_flare_state().flare_sun_texture_, 0, 0);
    }
    const float size = camera->unknownb4() * get_size(); // (0x004426B0 reads the factor)
    const float half = size / 2.0F;
    std::array<maths::Vec4, 4> quad_corners = {{
        {.x = -half, .y = -half, .z = 0.0F, .w = 1.0F},
        {.x = half, .y = -half, .z = 0.0F, .w = 1.0F},
        {.x = half, .y = half, .z = 0.0F, .w = 1.0F},
        {.x = -half, .y = half, .z = 0.0F, .w = 1.0F},
    }};
    for (maths::Vec4& corner : quad_corners) {
        transform_in_place(&corner, &facing);
    }
    for (maths::Vec4& corner : quad_corners) {
        corner.x += position->x;
        corner.y += position->y;
        corner.z += position->z;
    }
    for (int i = 0; i < 4; ++i) {
        core::at(corners, (i * 3) + 0) = core::at(quad_corners, i).x;
        core::at(corners, (i * 3) + 1) = core::at(quad_corners, i).y;
        core::at(corners, (i * 3) + 2) = core::at(quad_corners, i).z;
    }
    quad->set_blend_mode(kBlend);
    quad->lay_out({.layout_flags_ = 3, .primitive_type_ = 4, .count = 4});
    quad->set_blend_mode(kAdd);
    quad->set_unknown14(1U);
    quad->get_ready();
}

// 0x00443780
void show_flare(const maths::Vec4* sun, float time)
{
    const maths::Vec4 centre = screen_centre();
    frontend::ElementPtr container;
    frontend::UiTree::fetch_part(&container, kContainer, 0);
    if (container.is_null()) {
        script::launch_script(kCreateLensFlare, nullptr, nullptr, false, nullptr);
    }
    if (script::fetch_int_value(kDoUpdateLensFlare) == 0) {
        container.get_release();
        return;
    }
    if (flare_hidden()) {
        unhide_flare(nullptr, script::Context());
        flare_hidden() = false;
    }
    place_flare_elements(time, sun, &centre);
    container.get_release();
}

// 0x00443870
void render(gfx::ViewCam* camera)
{
    if (!get_shown()) {
        script::launch_script(kMaybeHideLensFlare, nullptr, nullptr, false, nullptr);
        return;
    }
    // where the sun is: theta round the vertical, phi up, `radius` from the camera
    maths::Vec4 sun_pos = *camera->fetch_location();
    maths::Mat4 rotation{};
    rotation.reset_to_identity();
    rotation.rotate_y(get_theta());
    maths::Vec4 direction = {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 1.0F};
    direction = direction * rotation;
    rotation.reset_to_identity();
    maths::Vec4 axis = maths::cross_vec(direction, maths::Vec4{.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 1.0F});
    axis.make_unit();
    rotation.spin_by(axis, get_phi());
    direction = direction * rotation;
    sun_pos = sun_pos + get_radius() * direction;
    sun_pos[3] = 1.0F;
    get_position() = sun_pos;
    std::array<std::array<uint8_t, 4>, 4> colors{};
    corner_colors(colors, 1.0);

    // behind the camera, or off the projection: the lens flare hidden
    maths::Vec4 to_sun = sun_pos - *camera->fetch_location();
    to_sun.make_unit();
    if (get_dot(to_sun, (*camera->fetch_mat())[2]) >= 0.0) {
        hide_flare(nullptr, script::Context());
        return;
    }
    maths::Vec4 screen = {.x = 0.0F, .y = 0.0F, .z = 0.0F, .w = 1.0F};
    float z = kIntOneAsFloat; // (an int 1 in the original, overwritten)
    maths::Vec2 at{.x = screen[0], .y = screen[1]};
    cast_onto(viewport_camera(), &sun_pos, &at, &z);
    screen[0] = at.x;
    screen[1] = at.y;
    if (screen[0] == 0.0F && screen[1] == 0.0F) {
        hide_flare(nullptr, script::Context());
        return;
    }

    // the sun, and a query for how much of it shows
    const StackPrimitive sun(2);
    std::array<float, kQuadCornerFloats> corners = {};
    const std::array<float, 8> uvs = {{0.0F, 0.0F, 1.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F}};
    build_sun_quad(camera, &sun.primitive(), &sun_pos, corners);
    blit_quad(&sun.primitive(), corners, colors, uvs);
    render::OcclusionRequest request{};
    request.callback = query_sun_visibility;
    request.data = 0;
    add_query(&request);
    uint32_t visible = 0;
    if (get_query() != 0U) {
        visible = query_result(get_query());
    }
    if (visible == 0) { // (no query, or the sun not seen)
        // not seen: the corona fades out over SunBurst_Out seconds, the lens flare maybe hidden
        const uint32_t unseen = get_since(visible_time());
        auto fade_time = static_cast<uint32_t>(get_truncated(static_cast<double>(sun_burst_out_time()) * kMillisecondsPerSecond));
        auto fade = static_cast<float>(static_cast<double>(unseen) / static_cast<double>(fade_time));
        fade = clamp_to_range(fade, 0.0F, 1.0F);
        hidden_time() = get_now();
        if (fade < 1.0F) {
            const float left = 1.0F - fade;
            corner_colors(colors, left);
            const StackPrimitive corona(2);
            std::array<float, kQuadCornerFloats> corona_corners = {};
            const std::array<float, 8> corona_uvs = {{0.0F, 0.0F, 1.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F}};
            build_sun_quad(camera, &corona.primitive(), &sun_pos, corona_corners);
            if (get_corona_texture()) {
                corona.primitive().set_texture(get_corona_texture(), 0, 0);
            }
            set_prim_state(1, 2);
            set_prim_state(4, 0);
            blit_quad(&corona.primitive(), corona_corners, colors, corona_uvs);
        }
        if (!flare_hidden()) {
            gsl::owner<Struct*> params = new_params();
            params->put_vec3(checksums::Name{kSunPos}, get_value(screen));
            if (!(static_cast<double>(morph_out_time()) * kMillisecondsPerSecond < static_cast<double>(unseen))) {
                params->push_float(checksums::Name{kMorphTime}, morph_out_time());
            }
            script::launch_script(kMaybeHideLensFlare, params, nullptr, false, nullptr);
            script::free_struct(params);
        }
    } else {
        // seen: the corona at half the colour, the lens flare shown (morphed in if it was hidden for a while)
        visible_time() = get_now();
        const uint32_t unseen = get_since(hidden_time());
        const float brightness =
            clamp_to_range(static_cast<float>(static_cast<double>(visible) / 50000.0F), 0.0F, 1.0F);
        (void)brightness; // (worked out, not used)
        corner_colors(colors, kHalf);
        if (get_corona_texture()) {
            sun.primitive().set_texture(get_corona_texture(), 0, 0);
        }
        sun.primitive().set_blend_mode(kAdd);
        blit_quad(&sun.primitive(), corners, colors, uvs);
        if (static_cast<double>(morph_in_time()) * kMillisecondsPerSecond < static_cast<double>(unseen)) {
            show_flare(&screen, 0.0F);
        } else {
            show_flare(&screen, morph_in_time());
        }
    }
}

} // namespace sun
