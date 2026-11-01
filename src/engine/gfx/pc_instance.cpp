// The PC instances (sSingleton, geom.h): constructed, destroyed, drawn on their own and all together; and two small
// material helpers (the UV wibble's constructor, the texture animation's restart).

#include <windows.h>

#include <d3d9.h>

#include <cstdint>

#include "engine/core/manager.h"
#include "engine/gfx/d3dx.h"
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

namespace render {

D3DLIGHT9 g_device_light1{D3DLIGHT_DIRECTIONAL, {0.0f, 0.0f, 0.0f, 1.0f}};
D3DLIGHT9 g_device_light0{D3DLIGHT_DIRECTIONAL, {0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 0.0f}};
maths::Mat4* g_palette_base;
int g_bone_count;
Singleton* g_current_instance;
const maths::Mat4* g_last_bones;
Singleton* g_instances;

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }
Singleton*& instances() { return g_instances; }
Singleton* g_visible_instances[4096]; // draw_instance_batch' list
Singleton** get_visible() { return g_visible_instances; }
const maths::Mat4* last_bones() { return g_last_bones; }
void set_last_bones(const maths::Mat4* bones) { g_last_bones = bones; }
const D3DMATRIX* get_view_world()
{
    return reinterpret_cast<D3DMATRIX*>(&g_world_matrix);
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
    return reinterpret_cast<const maths::Vec4*>(reinterpret_cast<const float*>(&instance->transform) + 12);
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
    ModelLights* lights =
        instance->model ? *reinterpret_cast<ModelLights**>(static_cast<uint8_t*>(instance->model) + 0x2C) : nullptr;
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
    D3DLIGHT9& lamp_a = g_device_light0;
    lamp_a.Diffuse.r = g_light_constants[8];
    lamp_a.Diffuse.g = g_light_constants[9];
    lamp_a.Diffuse.b = g_light_constants[10];
    lamp_a.Direction.x = g_light_constants[4];
    lamp_a.Direction.y = g_light_constants[5];
    lamp_a.Direction.z = g_light_constants[6];
    input_port()->SetLight(0, &lamp_a);
    D3DLIGHT9& lamp_b = g_device_light1;
    lamp_b.Diffuse.r = g_light_constants[16];
    lamp_b.Diffuse.g = g_light_constants[17];
    lamp_b.Diffuse.b = g_light_constants[18];
    lamp_b.Direction.x = g_light_constants[12];
    lamp_b.Direction.y = g_light_constants[13];
    lamp_b.Direction.z = g_light_constants[14];
    input_port()->SetLight(1, &lamp_b);
    uint32_t r = get_channel(g_light_constants[0]);
    uint32_t g = get_channel(g_light_constants[1]);
    uint32_t b = get_channel(g_light_constants[2]);
    input_port()->SetRenderState(D3DRS_AMBIENT, 0xFF000000u | r << 16 | g << 8 | b);
}

void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

} // namespace

// 0x004D1030
Singleton::Singleton(SceneRef* list, const maths::Mat4* matrix, int count, maths::Mat4* bone_matrices)
{
    transform = *matrix;
    bones = bone_matrices;
    bone_count_ = count;
    scene = list;
    model = nullptr;
    active = true;
    flags = 0;
    next = instances();
    instances() = this;
    viewports = 0xFFFFFFFF;
    if (bones && bones_on_device(scene) <= 0)
        return;
    for (int i = 0; i < scene->mesh_count_; ++i) {
        MeshRef* mesh = scene->meshes[i];
        if (!(mesh->fvf & kMeshLit) || (mesh_traits(mesh) & kMeshNotLit))
            return;
    }
    flags |= 2;
}

// 0x004D10C0
void Singleton::destroy_body()
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
        set_world_transform(reinterpret_cast<const float*>(&transform));
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
        input_port()->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(&transform));
        g_palette_base = bones;
        g_bone_count = bone_count_;
        g_current_instance = this;
        if (scene_unlit(scene)) {
            draw_scene(scene, draw_bits | kRenderNoCulling, 0);
        } else {
            if (flags & 4) {
                g_instance_colour_on = 1; // the instance's colour on
                reinterpret_cast<uint32_t&>(g_instance_colour[0]) = *reinterpret_cast<const uint32_t*>(color);
            }
            draw_scene(scene, draw_bits | kRenderNoCulling, 0);
            if (flags & 4)
                g_instance_colour_on = 0;
        }
        g_skipped_passes &= ~8u;
        g_vertex_format_locked = 0;
        g_pixel_shader_locked = 0;
        g_submit_locked = 0;
        set_texture(3, nullptr);
        g_palette_base = nullptr;
        g_bone_count = 0;
        g_current_instance = nullptr;
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
        int count = bone_count_ < kMaxBones ? bone_count_ : kMaxBones;
        if (count > 0) {
            for (int bone = 0; bone < count; ++bone) {
                maths::Mat4 world = transform;
                matrix_multiply(&world, &bones[bone], &world);
                set_world_transform(reinterpret_cast<const float*>(&world));
                input_port()->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(&world));
                for (int i = 0; i < scene->mesh_count_; ++i) {
                    MeshRef* mesh = scene->meshes[i];
                    if (static_cast<int8_t>(mesh->unknown1c_[3]) == bone) // (its bone)
                        mesh_flags_ref(mesh) |= kMeshShown;
                    else
                        mesh_flags_ref(mesh) &= ~kMeshShown;
                }
                draw_scene(scene, draw_bits | kRenderNoCulling, 0);
            }
            return;
        }
    }
    set_world_transform(reinterpret_cast<const float*>(&transform));
    input_port()->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(&transform));
    draw_scene(scene, draw_bits | kRenderNoCulling, 0);
}

// 0x004D16B0
void __cdecl draw_one_instance(Singleton* instance, uint32_t flags)
{
    set_last_bones(nullptr);
    if (!instance->active)
        return;
    set_world_transform(reinterpret_cast<const float*>(&instance->transform));
    const auto* centre = reinterpret_cast<const maths::Vec4*>(instance->scene->centre);
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
void __cdecl draw_instance_batch(uint32_t flags, int viewport)
{
    uint32_t mask = 1u << (viewport & 0x1F);
    float culling[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // the identity at each one's position
    int count = 0;
    set_last_bones(nullptr);
    if (instances()) {
        for (Singleton* instance = instances(); instance; instance = instance->next) {
            if (!instance->active || !(instance->viewports & mask))
                continue;
            SceneRef* list = instance->scene;
            if (!((flags & kRenderOpaque) && list->mesh_count_ > list->transparent_count_) &&
                !((flags & kRenderTransparent) && list->transparent_count_ > 0))
                continue;
            const float* position = reinterpret_cast<const float*>(get_position(instance));
            for (int k = 0; k < 4; ++k)
                culling[12 + k] = position[k];
            set_world_transform(culling);
            float radius = instance->scene->radius;
            if (instance->bones && bones_on_device(instance->scene) == 0 && radius < 72.0f)
                radius = 72.0f; // (a skinned model's sphere at least this big)
            if (!sphere_occluded(reinterpret_cast<const maths::Vec4*>(instance->scene->centre), radius, 1))
                get_visible()[count++] = instance;
        }
        if (count > 0) {
            crt::get_qsort(get_visible(), static_cast<uint32_t>(count), 4,
                           compare_instances); // (the C runtime's qsort)
            change_phase(kStateLighting, 1);
            input_port()->SetRenderState(D3DRS_COLORVERTEX, TRUE);
            input_port()->LightEnable(0, TRUE);
            input_port()->LightEnable(1, TRUE);
            set_flag_786e_b4();
            int i = 0;
            for (; i < count; ++i) {
                Singleton* instance = get_visible()[i];
                if (!skinned_by_shader(instance))
                    break;
                if ((flags & kRenderOpaque) || ((flags & kRenderTransparent) && (flags & 0x80)))
                    instance->render(flags);
            }
            clear_flag_786e_b4();
            float camera[3] = {g_camera.position[0], g_camera.position[1], g_camera.position[2]};
            double near_distance = static_cast<double>(script::fetch_float(0x4E7DC608u)) * 12.0f;
            float near_squared = static_cast<float>(near_distance * near_distance);
            for (; i < count; ++i) {
                Singleton* instance = get_visible()[i];
                const float* position = reinterpret_cast<const float*>(get_position(instance));
                double dx = static_cast<double>(position[0]) - camera[0];
                double dy = static_cast<double>(position[1]) - camera[1];
                double dz = static_cast<double>(position[2]) - camera[2];
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
int __cdecl compare_instances(const void* a, const void* b)
{
    const Singleton* first = *static_cast<Singleton* const*>(a);
    const Singleton* second = *static_cast<Singleton* const*>(b);
    if (skinned_by_shader(first)) {
        if (!skinned_by_shader(second))
            return -1;
        if (first->bones != second->bones)
            return second->bones < first->bones ? 1 : -1;
        int x = first->scene->transparent_count_, y = second->scene->transparent_count_;
        return x == y ? 0 : x > y ? 1 : -1;
    }
    if (skinned_by_shader(second))
        return 1;
    int x = first->scene->transparent_count_, y = second->scene->transparent_count_;
    if (x != y)
        return x > y ? 1 : -1;
    if (x <= 0)
        return 0;
    const MaterialRef* m1 = first->scene->meshes[first->scene->first_transparent_]->material;
    const MaterialRef* m2 = second->scene->meshes[second->scene->first_transparent_]->material;
    if (!m1 || !m2)
        return 0;
    float k1 = m1->sort_key_, k2 = m2->sort_key_;
    if (k1 > k2)
        return 1;
    return k1 < k2 ? -1 : 0;
}

// 0x004D1A60
UvOscillator::UvOscillator()
{
    velocity[0] = velocity[1] = 0;
    frequency[0] = frequency[1] = 0;
    amplitude[0] = amplitude[1] = 0;
    phase[0] = phase[1] = 0;
    matrix[0] = 1.0f;
    matrix[1] = 0;
    offset[0] = offset[1] = 0;
}

// 0x004D1A80
void TextureAnim::relaunch() { start_time_ = frame_clock::get_time(); }

} // namespace render
