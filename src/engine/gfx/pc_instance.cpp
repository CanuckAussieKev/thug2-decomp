// The PC instances (sSingleton, geom.h): constructed, destroyed, drawn on their own and all together; and two small
// material helpers (the UV wibble's constructor, the texture animation's restart).

#include <windows.h>

#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <array>
#include <cstring>
#include <span>

#include "engine/core/manager.h"
#include "engine/maths/vec4.h"
#include "platform/d3dx.h"
#include "engine/gfx/pc_instance.h"
#include "engine/gfx/geom.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/mesh_list.h"
#include "engine/gfx/occlusion.h"
#include "engine/gfx/pc_billboards.h"
#include "engine/gfx/pc_lights.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/scene_render.h"
#include "engine/maths/mat4.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/gfx/device.h"
#include "platform/crt.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/model.h"
#include "platform/d3d_matrix.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/shared_state.h"

namespace render {

namespace {
// NOT ORIGINAL grouping - pc_instance.cpp's state (the original's globals). Made on first use, never destroyed
// (pc_instance_state()).
struct PcInstanceState {
    D3DLIGHT9 device_light1_{.Type = D3DLIGHT_DIRECTIONAL, .Diffuse = {.r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f}};
    D3DLIGHT9 device_light0_{.Type = D3DLIGHT_DIRECTIONAL, .Diffuse = {.r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f}, .Specular = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 0.0f}};
    maths::Mat4* palette_base_{};
    int bone_count_{};
};

PcInstanceState& pc_instance_state()
{
    static core::NoDestroy<PcInstanceState> state;
    return state.get();
}

} // namespace
std::span<maths::Mat4> bone_palette()
{
    return {pc_instance_state().palette_base_, static_cast<std::size_t>(pc_instance_state().bone_count_)};
}
PcInstanceShared& pc_instance_shared()
{
    static core::NoDestroy<PcInstanceShared> state;
    return state.get();
}

const maths::Mat4* g_last_bones;
namespace {
// NOT ORIGINAL grouping - the instances' table (the original's globals). Made on first use, never destroyed
// (instance_table()).
struct InstanceTable {
    Singleton* instances_{};
    std::array<Singleton*, 4096> visible_instances_{}; // draw_instance_batch' list
};

InstanceTable& instance_table()
{
    static core::NoDestroy<InstanceTable> state;
    return state.get();
}

IDirect3DDevice9* input_port() { return g_d3d_device; }
Singleton*& instances() { return instance_table().instances_; }
std::array<Singleton*, 4096>& get_visible() { return instance_table().visible_instances_; }
const maths::Mat4* last_bones() { return g_last_bones; }
void set_last_bones(const maths::Mat4* bones) { g_last_bones = bones; }
const D3DMATRIX* get_view_world()
{
    return d3d::matrix(g_world_matrix.data());
} // the world transform to put back

constexpr uint32_t kMeshLit = 0x10;       // a mesh's +0x38: it asks for lighting
constexpr uint32_t kMeshNotLit = 0x20000; // a mesh's flags
constexpr uint32_t kMeshShown = 0x40;     // a mesh's flags
constexpr int kMaxBones = 0x3A;
constexpr int kStateLighting = 0x16; // change_phase's

int bones_on_device(const SceneRef* scene) { return static_cast<int>(scene->unknown78); }
// Its bones drawn by the vertex shader: it has bones, and its scene doesn't skin them on the CPU (+0x78).
bool skinned_by_shader(const Singleton* instance) { return instance->bones && bones_on_device(instance->scene) <= 0; }
// The scene's first byte's top bit: drawn unlit.
bool scene_unlit(const SceneRef* scene) { return static_cast<int8_t>(scene->flags & 0xFF) < 0; }

uint32_t mesh_traits(const MeshRef* mesh) { return mesh->flags; }
uint32_t& mesh_flags_ref(MeshRef* mesh) { return mesh->flags; }

const maths::Vec4* get_position(const Singleton* instance)
{
    return &core::at(instance->transform.row, 3);
}

// A colour channel (0-1) as a byte, as the original's _ftol (truncated through 64 bits).
uint32_t get_channel(float value)
{
    return static_cast<uint32_t>(static_cast<int64_t>(static_cast<double>(value) * 255.0f)) & 0xFF;
}

// The model's lights for the instance's position (the model's +0x2C: plat_apply_scene_lights, with its radius), or the
// scene's.
void refresh_instance_lighting(Singleton* instance)
{
    ModelLights* lights = instance->model ? instance->model->lights_ptr_.get() : nullptr;
    if (!lights) {
        reset_lights();
        return;
    }
    lights->plat_apply_scene_lights(get_position(instance), lights->dynamic_radius_);
}

// The renderer's light constants into the device's lights 0 and 1 (direction, diffuse colour) and its ambient
// colour.
void send_lights()
{
    D3DLIGHT9& lamp_a = pc_instance_state().device_light0_;
    lamp_a.Diffuse.r = core::at(pc_lights_shared().light_constants_, 8);
    lamp_a.Diffuse.g = core::at(pc_lights_shared().light_constants_, 9);
    lamp_a.Diffuse.b = core::at(pc_lights_shared().light_constants_, 10);
    lamp_a.Direction.x = core::at(pc_lights_shared().light_constants_, 4);
    lamp_a.Direction.y = core::at(pc_lights_shared().light_constants_, 5);
    lamp_a.Direction.z = core::at(pc_lights_shared().light_constants_, 6);
    input_port()->SetLight(0, &lamp_a);
    D3DLIGHT9& lamp_b = pc_instance_state().device_light1_;
    lamp_b.Diffuse.r = core::at(pc_lights_shared().light_constants_, 16);
    lamp_b.Diffuse.g = core::at(pc_lights_shared().light_constants_, 17);
    lamp_b.Diffuse.b = core::at(pc_lights_shared().light_constants_, 18);
    lamp_b.Direction.x = core::at(pc_lights_shared().light_constants_, 12);
    lamp_b.Direction.y = core::at(pc_lights_shared().light_constants_, 13);
    lamp_b.Direction.z = core::at(pc_lights_shared().light_constants_, 14);
    input_port()->SetLight(1, &lamp_b);
    const uint32_t r = get_channel(core::at(pc_lights_shared().light_constants_, 0));
    const uint32_t g = get_channel(core::at(pc_lights_shared().light_constants_, 1));
    const uint32_t b = get_channel(core::at(pc_lights_shared().light_constants_, 2));
    input_port()->SetRenderState(D3DRS_AMBIENT, 0xFF000000u | r << 16 | g << 8 | b);
}

void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

} // namespace

// 0x004D1030
Singleton::Singleton(SceneRef* list, const maths::Mat4* matrix, int count, maths::Mat4* bone_matrices)
    : transform(*matrix), bones(bone_matrices), bone_count_(count), scene(list), next(instances())
{
    instances() = this;
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    viewports = 0xFFFFFFFF;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
    if (bones && bones_on_device(scene) <= 0)
        return;
    for (const MeshRef* mesh : scene->mesh_run()) {
        if (!(mesh->fvf & kMeshLit) || (mesh_traits(mesh) & kMeshNotLit))
            return;
    }
    flags |= 2;
}

// 0x004D10C0
Singleton::~Singleton()
{
    if (flags & 1) {
        if (SceneRef* list = scene) {
            list->destroy_body();
            memory::Hub::get_pull()->dealloc(list);
            scene = nullptr;
        }
        if (bones) {
            memory::Hub::get_pull()->dealloc(bones);
            bones = nullptr;
        }
    }
    for (Singleton** link = &instances(); *link; link = &(*link)->next) {
        if (*link == this) {
            *link = next;
            return;
        }
    }
}

// 0x004D1130
void Singleton::render(uint32_t draw_bits)
{
    if (skinned_by_shader(this)) {
        set_world_transform(transform.data());
        if (last_bones() != bones) {
            if (scene_unlit(scene)) {
                input_port()->SetRenderState(D3DRS_LIGHTING, FALSE);
            } else {
                refresh_instance_lighting(this);
                input_port()->SetRenderState(D3DRS_LIGHTING, TRUE);
                send_lights();
            }
            set_last_bones(bones);
        }
        input_port()->SetTransform(D3DTS_WORLD, d3d::matrix(transform.data()));
        pc_instance_state().palette_base_ = bones;
        pc_instance_state().bone_count_ = bone_count_;
        pc_instance_shared().current_instance_ = this;
        if (scene_unlit(scene)) {
            draw_scene(scene, draw_bits | kRenderNoCulling, 0);
        } else {
            if (flags & 4) {
                g_instance_colour_on = 1; // the instance's colour on
                std::memcpy(g_instance_colour.data(), color.data(), 4);
            }
            draw_scene(scene, draw_bits | kRenderNoCulling, 0);
            if (flags & 4)
                g_instance_colour_on = 0;
        }
        g_skipped_passes &= ~8u;
        g_vertex_format_locked = 0;
        g_pixel_shader_locked = nullptr;
        g_submit_locked = 0;
        set_texture(3, nullptr);
        pc_instance_state().palette_base_ = nullptr;
        pc_instance_state().bone_count_ = 0;
        pc_instance_shared().current_instance_ = nullptr;
        return;
    }

    set_last_bones(nullptr);
    if ((flags & 2) && !scene_unlit(scene)) {
        refresh_instance_lighting(this);
        change_phase(kStateLighting, 1);
        send_lights();
    } else {
        change_phase(kStateLighting, 0);
    }
    if (bones_on_device(scene) != 0) {
        const int count = bone_count_ < kMaxBones ? bone_count_ : kMaxBones;
        if (count > 0) {
            const std::span<const maths::Mat4> bone_run(bones, static_cast<std::size_t>(bone_count_));
            for (int bone = 0; bone < count; ++bone) {
                maths::Mat4 world = transform;
                matrix_multiply(&world, &core::at(bone_run, bone), &world);
                set_world_transform(world.data());
                input_port()->SetTransform(D3DTS_WORLD, d3d::matrix(world.data()));
                for (MeshRef* mesh : scene->mesh_run()) {
                    if (static_cast<int8_t>(mesh->bone_) == bone) // (its bone)
                        mesh_flags_ref(mesh) |= kMeshShown;
                    else
                        mesh_flags_ref(mesh) &= ~kMeshShown;
                }
                draw_scene(scene, draw_bits | kRenderNoCulling, 0);
            }
            return;
        }
    }
    set_world_transform(transform.data());
    input_port()->SetTransform(D3DTS_WORLD, d3d::matrix(transform.data()));
    draw_scene(scene, draw_bits | kRenderNoCulling, 0);
}

// 0x004D16B0
void draw_one_instance(Singleton* instance, uint32_t flags)
{
    set_last_bones(nullptr);
    if (!instance->active)
        return;
    set_world_transform((instance->transform).data());
    const maths::Vec4 sphere = sphere_of(instance->scene->centre, instance->scene->radius);
    const maths::Vec4* centre = &sphere;
    if ((flags & 8) ||
        (sphere_in_view(centre, instance->scene->radius) && !sphere_occluded(centre, instance->scene->radius, 1))) {
        if (instance->bones) {
            set_flag_786e_b4();
            instance->render(kRenderOpaque | kRenderTransparent);
            clear_flag_786e_b4();
        } else {
            instance->render(kRenderOpaque | kRenderTransparent);
        }
    }
    input_port()->SetTransform(D3DTS_WORLD, get_view_world());
    set_world_transform(nullptr);
}

// 0x004D1760
void draw_instance_batch(uint32_t flags, int viewport)
{
    const uint32_t mask = 1u << (viewport & 0x1F);
    std::array<float, 16> culling = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}; // the identity at each one's position
    int count = 0;
    set_last_bones(nullptr);
    if (instances()) {
        for (Singleton* instance = instances(); instance; instance = instance->next) {
            if (!instance->active || !(instance->viewports & mask))
                continue;
            const SceneRef* list = instance->scene;
            if (!((flags & kRenderOpaque) && list->mesh_count_ > list->transparent_count_) &&
                !((flags & kRenderTransparent) && list->transparent_count_ > 0))
                continue;
            const maths::Vec4& position = *get_position(instance);
            for (int k = 0; k < 4; ++k)
                core::at(culling, 12 + k) = position[k];
            set_world_transform(culling.data());
            float radius = instance->scene->radius;
            if (instance->bones && bones_on_device(instance->scene) == 0 && radius < 72.0f)
                radius = 72.0f; // (a skinned model's sphere at least this big)
            const maths::Vec4 sphere = sphere_of(instance->scene->centre, instance->scene->radius);
            if (!sphere_occluded(&sphere, radius, 1))
                core::at(get_visible(), count++) = instance;
        }
        if (count > 0) {
            crt::get_qsort(static_cast<void*>(get_visible().data()), static_cast<uint32_t>(count), 4,
                           compare_instances); // (the C runtime's qsort)
            change_phase(kStateLighting, 1);
            input_port()->SetRenderState(D3DRS_COLORVERTEX, TRUE);
            input_port()->LightEnable(0, TRUE);
            input_port()->LightEnable(1, TRUE);
            set_flag_786e_b4();
            int i = 0;
            for (; i < count; ++i) {
                Singleton* instance = core::at(get_visible(), i);
                if (!skinned_by_shader(instance))
                    break;
                if ((flags & kRenderOpaque) || ((flags & kRenderTransparent) && (flags & 0x80)))
                    instance->render(flags);
            }
            clear_flag_786e_b4();
            const std::array<float, 3> camera = {{core::at(g_camera.position, 0), core::at(g_camera.position, 1), core::at(g_camera.position, 2)}};
            const double near_distance = static_cast<double>(script::fetch_float(0x4E7DC608u)) * 12.0f;
            auto near_squared = static_cast<float>(near_distance * near_distance);
            for (; i < count; ++i) {
                Singleton* instance = core::at(get_visible(), i);
                const maths::Vec4& position = *get_position(instance);
                const double dx = static_cast<double>(position.x) - core::at(camera, 0);
                const double dy = static_cast<double>(position.y) - core::at(camera, 1);
                const double dz = static_cast<double>(position.z) - core::at(camera, 2);
                g_near_camera = !(dz * dz + dy * dy + dx * dx > near_squared); // near the camera
                if ((flags & kRenderOpaque) || ((flags & kRenderTransparent) && (flags & 0x100)))
                    instance->render(flags);
            }
            g_near_camera = true;
            change_phase(kStateLighting, 0);
            input_port()->SetRenderState(D3DRS_COLORVERTEX, FALSE);
            input_port()->LightEnable(0, FALSE);
            input_port()->LightEnable(1, FALSE);
        }
    }
    input_port()->SetTransform(D3DTS_WORLD, get_view_world());
    set_world_transform(nullptr);
}

// 0x004D0F40
int compare_instances(const void* a, const void* b)
{
    const Singleton* first = *static_cast<Singleton* const*>(a);
    const Singleton* second = *static_cast<Singleton* const*>(b);
    if (skinned_by_shader(first)) {
        if (!skinned_by_shader(second))
            return -1;
        if (first->bones != second->bones)
            return second->bones < first->bones ? 1 : -1;
        const int x = first->scene->transparent_count_;
        const int y = second->scene->transparent_count_;
        return x == y ? 0 : x > y ? 1 : -1;
    }
    if (skinned_by_shader(second))
        return 1;
    const int x = first->scene->transparent_count_;
    const int y = second->scene->transparent_count_;
    if (x != y)
        return x > y ? 1 : -1;
    if (x <= 0)
        return 0;
    const MaterialRef* m1 = core::at(first->scene->mesh_run(), first->scene->first_transparent_)->material;
    const MaterialRef* m2 = core::at(second->scene->mesh_run(), second->scene->first_transparent_)->material;
    if (!m1 || !m2)
        return 0;
    const float k1 = m1->sort_key_;
    const float k2 = m2->sort_key_;
    if (k1 > k2)
        return 1;
    return k1 < k2 ? -1 : 0;
}

// 0x004D1A60
UvOscillator::UvOscillator()
{
    core::at(velocity, 0) = core::at(velocity, 1) = 0;
    core::at(frequency, 0) = core::at(frequency, 1) = 0;
    core::at(amplitude, 0) = core::at(amplitude, 1) = 0;
    core::at(phase, 0) = core::at(phase, 1) = 0;
    core::at(matrix, 0) = 1.0f;
    core::at(matrix, 1) = 0;
    core::at(offset, 0) = core::at(offset, 1) = 0;
}

// 0x004D1A80
void TextureAnim::relaunch() { start_time_ = frame_clock::get_time(); }

} // namespace render
