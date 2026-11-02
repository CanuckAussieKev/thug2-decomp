// The sun and its lens flare (see lens_flare.h).

#include "engine/gfx/lens_flare.h"

#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/mem.h"
#include "frontend/screen_element_cfuncs.h"
#include "frontend/screen_element_manager.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/engine_primitive.h"
#include "engine/gfx/engine_texture.h"
#include "engine/gfx/pc_primitive.h"
#include "engine/gfx/pc_viewport.h"
#include "engine/gfx/texture_dict.h"
#include "engine/gfx/viewport.h"
#include "engine/gfx/viewport_list.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "platform/game_clock.h"
#include "engine/core/checked_index.h"
#include "engine/core/packed_colour.h"
#include <bit>

namespace sun {

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

float g_flare_theta;
float g_flare_phi;
float g_flare_radius;
float g_flare_size;
float g_flare_morph_in_time;
float g_flare_morph_out_time;
float g_flare_sun_burst_out_time;
uint8_t g_flare_query;
bool g_flare_shown;
bool g_flare_hidden;
render::BitmapTexture* g_flare_sun_texture;
render::BitmapTexture* g_flare_corona_texture;
FlareElement* g_flare_elements;
uint32_t g_flare_element_count;
uint32_t g_flare_visible_time;
uint32_t g_flare_hidden_time;
maths::Vec4 g_flare_color;
maths::Vec4 g_flare_position;

// The sun's state.
float& get_theta() { return g_flare_theta; }
float& get_phi() { return g_flare_phi; }
float& get_radius() { return g_flare_radius; }
float& get_size() { return g_flare_size; }
float& morph_in_time() { return g_flare_morph_in_time; } // seconds
float& morph_out_time() { return g_flare_morph_out_time; }
float& sun_burst_out_time() { return g_flare_sun_burst_out_time; }
uint8_t& get_query() { return g_flare_query; } // the occlusion query's result number (0: none)
bool& get_shown() { return g_flare_shown; }
bool& flare_hidden() { return g_flare_hidden; }
render::BitmapTexture*& sun_texture() { return g_flare_sun_texture; }
render::BitmapTexture*& get_corona_texture() { return g_flare_corona_texture; }
FlareElement*& get_elements() { return g_flare_elements; }
uint32_t& element_count() { return g_flare_element_count; }
uint32_t& visible_time() { return g_flare_visible_time; } // when the sun was last seen
uint32_t& hidden_time() { return g_flare_hidden_time; }   // when it was last not seen
maths::Vec4& get_color() { return g_flare_color; }
maths::Vec4& get_position() { return g_flare_position; } // in the world, this frame

// The texture `checksum` (the texture dictionary 0x00700F20's look-up), and a texture deleted.
render::BitmapTexture* find_texture(uint32_t checksum) { return render::g_particle_texture_dict->fetch_skin(checksum); }
void delete_texture(render::BitmapTexture* texture) { delete texture; }

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
void cast_onto(render::ViewRegion* camera, const maths::Vec4* point, float* x, float* y, float* z)
{
    int whole = 0;
    camera->project_to_screen(point, x, y, &whole);
    *z = std::bit_cast<float>(whole);
}
uint8_t issue_query(render::ViewRegion* camera, float left, float top, float right, float bottom, int unknown,
                    uint8_t query)
{
    return camera->query_rect(left, top, right, bottom, static_cast<uint32_t>(unknown), query);
}

// The sun's and the corona's quads: a PC primitive drawer on the stack (0x200 bytes kept for it), made with 2 (not
// listed: drawn by hand) - its texture (through the jump 0x004A1090), its layout (0x004A10A0, given 3 4 4), the blend
// mode (+0x0C: the folded setter 0x00428270), +0x14 set (the folded setter 0x00407A50), readied and drawn from arrays
// - corners, colours, texture coordinates (draw_if_ready).
using Prim = render::PcPrimitive;
void blit_quad(Prim* quad, const float* corners, const std::array<colour::Bytes, 4>& colors, const float* uvs)
{
    std::array<uint32_t, 4> packed{};
    for (size_t i = 0; i < packed.size(); ++i)
        core::at(packed, i) = colour::packed_of(core::at(colors, i));
    quad->draw_if_ready(corners, packed.data(), uvs, nullptr);
}
// A render state for the primitives (0x0049C700).
void set_prim_state(int state, int value) { render::set_primitive_state(state, value); }
// A query to issue this frame (0x004A0EB0 takes a copy and fills in its slot number), and a query's result - its
// visible pixels (0x004A0F20).
// (0x004426C0 constructs the request: cleared)
void add_query(render::OcclusionRequest* request) { render::request_occlusion_query(request); }
uint32_t query_result(uint8_t) { return render::occlusion_pixels(); } // (the original also passed the query: unused)
// `v` times `m` in place (all four components; 0x00434200).
void transform_in_place(maths::Vec4* v, const maths::Mat4* m) { v->multiply_by(*m); }

// The original's _ftol (0x006236FC): truncated towards zero, the low 32 bits.
int get_truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }
// x87's dot product order (0x00402530), unrounded.
double get_dot(const maths::Vec4& a, const maths::Vec4& b)
{
    return static_cast<double>(a.z) * b.z + static_cast<double>(a.y) * b.y + static_cast<double>(a.x) * b.x;
}
script::VectorValue get_value(const maths::Vec4& v) { return {v.x, v.y, v.z, v.w}; }
script::Struct* new_params()
{
    Struct* structure = script::spawn_param_bag();
    if (structure)
        new (structure) script::Struct;
    return structure;
}
// The screen centre lens flares are placed from.
maths::Vec4 screen_centre() { return {320.0f, 240.0f, 0.0f, 1.0f}; }

// The sun's colour scaled into bytes for each of the quad's four corners.
void corner_colors(std::array<std::array<uint8_t, 4>, 4>& colors, double scale)
{
    for (int corner = 0; corner < 4; ++corner) {
        for (int i = 0; i < 4; ++i)
            core::at(core::at(colors, corner), i) = static_cast<uint8_t>(get_truncated(get_color()[i] * scale));
    }
}

} // namespace


// 0x00442680
float clamp_to_range(float value, float min, float max)
{
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}

// 0x004426D0
void set_sun(const Settings* settings, uint32_t mask)
{
    if (mask & kTheta)
        get_theta() = settings->theta;
    if (mask & kPhi)
        get_phi() = settings->phi;
    if (mask & kRadius)
        get_radius() = settings->radius;
    if (mask & kSize)
        get_size() = settings->size;
    if (mask & kColor)
        get_color() = settings->color;
    if (mask & kShown)
        get_shown() = settings->shown;
    if (mask & kTexture) {
        if (sun_texture())
            delete_texture(sun_texture());
        sun_texture() = nullptr;
        sun_texture() = find_texture(settings->texture);
    }
    if (mask & kCoronaTexture) {
        // (the sun's texture is the one deleted, as the original)
        if (get_corona_texture() && sun_texture())
            delete_texture(sun_texture());
        get_corona_texture() = find_texture(settings->corona_texture_);
    }
}

// 0x00442810
void query_sun_visibility(uint32_t /*data*/, uint32_t slot)
{
    const auto query = static_cast<uint8_t>(slot); // (the original's callback took the slot as a byte)
    maths::Vec4 screen = {0.0f, 0.0f, 0.0f, 1.0f};
    render::ViewRegion* camera = viewport_camera();
    float z = 1.401298464e-45f; // (an int 1 in the original, overwritten)
    cast_onto(camera, &get_position(), &screen[0], &screen[1], &z);
    get_query() = issue_query(camera, screen[0] - 5.0f, screen[1] - 5.0f, screen[0] + 5.0f, screen[1] + 5.0f, 1, query);
}

// 0x004428F0
void place_flare_elements(float time, const maths::Vec4* sun, const maths::Vec4* centre)
{
    const maths::Vec4 line = *sun - *centre;
    for (uint32_t i = 0; i < element_count(); ++i) {
        FlareElement& element = get_elements()[i];
        const maths::Vec4 pos = *centre + element.pos_scale_ * line;
        element.morph->push_float(kTime, time);
        element.morph->put_vec3(kPos, get_value(pos));
        frontend::cfunc_do_screen_element_morph(element.morph, nullptr);
    }
    flare_hidden() = false;
}

// 0x004429D0
void prepare_all(float theta, float phi, float radius, float size, uint32_t texture, uint32_t corona_texture,
                 uint32_t lens_flare_data)
{
    get_theta() = theta;
    get_phi() = phi;
    get_radius() = radius;
    get_size() = size;
    get_shown() = true;
    sun_texture() = nullptr;
    sun_texture() = find_texture(texture);
    get_corona_texture() = find_texture(corona_texture);
    Array* data = script::fetch_list(lens_flare_data);
    element_count() = data->extent_;
    get_elements() = static_cast<FlareElement*>(memory::alloc(element_count() * sizeof(FlareElement)));
    for (uint32_t i = 0; i < element_count(); ++i) {
        auto* item = data->struct_at(i);
        Array* rgba = nullptr;
        FlareElement& element = get_elements()[i];
        item->fetch_float(kPosScale, &element.pos_scale_, 1);
        item->fetch_float(kScale, &element.scale, 1);
        item->fetch_list(kRgba, &rgba, 1);
        element.rgba =
            rgba->fetch_part(3) << 24 | rgba->fetch_part(2) << 16 | rgba->fetch_part(1) << 8 | rgba->fetch_part(0);
        item->read_hash_value(kId, &element.id, 1);
        element.create = new_params();
        element.create->put_name_key(kId, element.id);
        element.create->push_float(kScale, element.scale);
        element.create->append_array_value(kRgba, rgba);
        element.morph = new_params();
        element.morph->put_name_key(kId, element.id);
        element.morph->put_vec3(kPos, script::VectorValue{0.0f, 0.0f, 0.0f, 1.0f});
        element.morph->push_float(kTime, 0.0f);
    }
    morph_in_time() = script::fetch_float(kMorphIn);
    morph_out_time() = script::fetch_float(kMorphOut);
    sun_burst_out_time() = script::fetch_float(kSunBurstOut);
    get_color() = {255.0f, 255.0f, 255.0f, 128.0f};
}

// 0x00442D70
void update_flare(Struct* params, script::Script*)
{
    script::VectorValue centre = get_value(screen_centre());
    float time = 0.0f;
    params->fetch_vec(kCenter, &centre, 0);
    params->fetch_float(kMorphTime, &time, 0);
    script::VectorValue sun{}; // (unset if there's none: the getter asserts)
    params->fetch_vec(kSunPos, &sun, 1);
    const maths::Vec4 sun_pos = {sun.x, sun.y, sun.z, sun.w};
    const maths::Vec4 centre_pos = {centre.x, centre.y, centre.z, centre.w};
    place_flare_elements(time, &sun_pos, &centre_pos);
}

// 0x00442DF0
void hide_flare(Struct* params, script::Script* script)
{
    script::VectorValue centre = get_value(screen_centre()); // (read, not used)
    float time = 0.0f;
    frontend::ElementPtr container;
    frontend::UiTree::sole()->fetch_part(&container, kContainer, 0);
    if (container.is_null()) {
        container.get_release();
        return;
    }
    if (params) {
        params->fetch_vec(kCenter, &centre, 0);
        params->fetch_float(kMorphTime, &time, 0);
    }
    Struct* morph = new_params();
    morph->put_name_key(kId, kContainer);
    morph->push_float(kAlpha, 0.0f);
    morph->push_float(kTime, time);
    frontend::cfunc_do_screen_element_morph(morph, script);
    script::free_struct(morph);
    if (params) {
        script::VectorValue sun{};
        if (params->fetch_vec(kSunPos, &sun, 0)) {
            for (uint32_t i = 0; i < element_count(); ++i) {
                FlareElement& element = get_elements()[i];
                element.morph->put_vec3(kPos, sun);
                element.morph->push_float(kTime, time);
                frontend::cfunc_do_screen_element_morph(element.morph, script);
            }
        }
    }
    flare_hidden() = true;
    container.get_release();
}

// 0x00443030
void unhide_flare(Struct*, script::Script* script)
{
    frontend::ElementPtr container;
    frontend::UiTree::sole()->fetch_part(&container, kContainer, 0);
    if (container.is_null()) {
        container.get_release();
        return;
    }
    const float time = 0.2f;
    Struct* morph = new_params();
    morph->put_name_key(kId, kContainer);
    morph->push_float(kAlpha, 1.0f);
    morph->push_float(kTime, time);
    frontend::cfunc_do_screen_element_morph(morph, script);
    script::free_struct(morph);
    flare_hidden() = false;
    container.get_release();
}

// 0x00443180
void build_sun_quad(gfx::ViewCam* camera, render::PcPrimitive* quad, const maths::Vec4* position, float* corners)
{
    // facing the camera: its right axis reversed, then up and back from cross products
    maths::Mat4 facing{};
    facing.reset_to_identity();
    facing[0] = -(*camera->fetch_mat())[0];
    facing[2] = maths::cross_vec((*camera->fetch_mat())[1], facing[0]);
    facing[1] = maths::cross_vec(facing[2], facing[0]);
    if (sun_texture())
        quad->set_texture(sun_texture(), 0, 0);
    const float size = camera->unknownb4_ * get_size(); // (0x004426B0 reads the factor)
    float half = size / 2.0f;
    std::array<maths::Vec4, 4> quad_corners = {{
        {-half, -half, 0.0f, 1.0f},
        {half, -half, 0.0f, 1.0f},
        {half, half, 0.0f, 1.0f},
        {-half, half, 0.0f, 1.0f},
    }};
    for (maths::Vec4& corner : quad_corners)
        transform_in_place(&corner, &facing);
    for (maths::Vec4& corner : quad_corners) {
        corner.x += position->x;
        corner.y += position->y;
        corner.z += position->z;
    }
    for (int i = 0; i < 4; ++i) {
        corners[i * 3 + 0] = core::at(quad_corners, i).x;
        corners[i * 3 + 1] = core::at(quad_corners, i).y;
        corners[i * 3 + 2] = core::at(quad_corners, i).z;
    }
    quad->blend_mode_ = kBlend;
    quad->prepare_all(3, 4, 4);
    quad->blend_mode_ = kAdd;
    quad->unknown14 = true;
    quad->get_ready();
}

// 0x00443780
void show_flare(const maths::Vec4* sun, float time)
{
    const maths::Vec4 centre = screen_centre();
    frontend::ElementPtr container;
    frontend::UiTree::sole()->fetch_part(&container, kContainer, 0);
    if (container.is_null())
        script::launch_script(kCreateLensFlare, nullptr, nullptr, false, nullptr);
    if (!script::fetch_int_value(kDoUpdateLensFlare)) {
        container.get_release();
        return;
    }
    if (flare_hidden()) {
        unhide_flare(nullptr, nullptr);
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
    maths::Vec4 direction = {0.0f, 0.0f, 1.0f, 1.0f};
    direction = direction * rotation;
    rotation.reset_to_identity();
    maths::Vec4 axis = maths::cross_vec(direction, maths::Vec4{0.0f, 1.0f, 0.0f, 1.0f});
    axis.make_unit();
    rotation.spin_by(axis, get_phi());
    direction = direction * rotation;
    sun_pos = sun_pos + get_radius() * direction;
    sun_pos[3] = 1.0f;
    get_position() = sun_pos;
    std::array<std::array<uint8_t, 4>, 4> colors{};
    corner_colors(colors, 1.0);

    // behind the camera, or off the projection: the lens flare hidden
    maths::Vec4 to_sun = sun_pos - *camera->fetch_location();
    to_sun.make_unit();
    if (get_dot(to_sun, (*camera->fetch_mat())[2]) >= 0.0) {
        hide_flare(nullptr, nullptr);
        return;
    }
    maths::Vec4 screen = {0.0f, 0.0f, 0.0f, 1.0f};
    float z = 1.401298464e-45f; // (an int 1 in the original, overwritten)
    cast_onto(viewport_camera(), &sun_pos, &screen[0], &screen[1], &z);
    if (screen[0] == 0.0f && screen[1] == 0.0f) {
        hide_flare(nullptr, nullptr);
        return;
    }

    // the sun, and a query for how much of it shows
    Prim sun(2);
    std::array<float, 12> corners = {};
    const std::array<float, 8> uvs = {{0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f}};
    build_sun_quad(camera, &sun, &sun_pos, corners.data());
    blit_quad(&sun, corners.data(), colors, uvs.data());
    render::OcclusionRequest request{};
    request.callback = query_sun_visibility;
    request.data = 0;
    add_query(&request);
    uint32_t visible = 0;
    // NOLINTNEXTLINE(bugprone-assignment-in-if-condition): the original's, as written
    if (!get_query() || (visible = query_result(get_query())) == 0) {
        // not seen: the corona fades out over SunBurst_Out seconds, the lens flare maybe hidden
        const uint32_t unseen = get_since(visible_time());
        auto fade_time = static_cast<uint32_t>(get_truncated(static_cast<double>(sun_burst_out_time()) * 1000.0f));
        auto fade = static_cast<float>(static_cast<double>(unseen) / static_cast<double>(fade_time));
        fade = clamp_to_range(fade, 0.0f, 1.0f);
        hidden_time() = get_now();
        if (fade < 1.0f) {
            const float left = 1.0f - fade;
            corner_colors(colors, left);
            Prim corona(2);
            std::array<float, 12> corona_corners = {};
            const std::array<float, 8> corona_uvs = {{0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f}};
            build_sun_quad(camera, &corona, &sun_pos, corona_corners.data());
            if (get_corona_texture())
                corona.set_texture(get_corona_texture(), 0, 0);
            set_prim_state(1, 2);
            set_prim_state(4, 0);
            blit_quad(&corona, corona_corners.data(), colors, corona_uvs.data());
        }
        if (!flare_hidden()) {
            Struct* params = new_params();
            params->put_vec3(kSunPos, get_value(screen));
            if (!(static_cast<double>(morph_out_time()) * 1000.0f < static_cast<double>(unseen)))
                params->push_float(kMorphTime, morph_out_time());
            script::launch_script(kMaybeHideLensFlare, params, nullptr, false, nullptr);
            script::free_struct(params);
        }
    } else {
        // seen: the corona at half the colour, the lens flare shown (morphed in if it was hidden for a while)
        visible_time() = get_now();
        const uint32_t unseen = get_since(hidden_time());
        const float brightness =
            clamp_to_range(static_cast<float>(static_cast<double>(visible) / 50000.0f), 0.0f, 1.0f);
        (void)brightness; // (worked out, not used)
        corner_colors(colors, 0.5);
        if (get_corona_texture())
            sun.set_texture(get_corona_texture(), 0, 0);
        sun.blend_mode_ = kAdd;
        blit_quad(&sun, corners.data(), colors, uvs.data());
        if (static_cast<double>(morph_in_time()) * 1000.0f < static_cast<double>(unseen))
            show_flare(&screen, 0.0f);
        else
            show_flare(&screen, morph_in_time());
    }
}

} // namespace sun
