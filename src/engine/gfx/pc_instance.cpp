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
#include "platform/crt.h"
#include "engine/core/checked_index.h"
#include "engine/gfx/model.h"
#include "platform/d3d_matrix.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/light_sets.h"
#include "engine/gfx/mesh_pass.h"

namespace render {

constexpr std::size_t kVisibleInstanceCount = 4096;
constexpr int kLowByteMask = 0xFF;
constexpr float kByteMaxFloat = 255.0F;
// Indexes into the light constants: the two lamps' diffuse colours and directions.
constexpr int kLampADiffuseR = 8;
constexpr int kLampADiffuseG = 9;
constexpr int kLampADiffuseB = 10;
constexpr int kLampADirectionY = 5;
constexpr int kLampADirectionZ = 6;
constexpr int kLampBDiffuseR = 16;
constexpr int kLampBDiffuseG = 17;
constexpr int kLampBDiffuseB = 18;
constexpr int kLampBDirectionX = 12;
constexpr int kLampBDirectionY = 13;
constexpr int kLampBDirectionZ = 14;
constexpr uint32_t kOpaqueAlphaBits = 0xFF000000U;
constexpr int kByte1Shift = 8;
constexpr int kByte2Shift = 16;
constexpr unsigned kSkippedPassBit = 8U;
constexpr int kRenderFlagUnculled = 8;
constexpr float kSkinnedMinRadius = 72.0F;
constexpr int kRenderFlagSkinA = 0x80;
constexpr int kRenderFlagSkinB = 0x100;

namespace {
// NOT ORIGINAL grouping - pc_instance.cpp's state (the original's globals). Made on first use, never destroyed
// (pc_instance_state()).
struct PcInstanceState {
    D3DLIGHT9 device_light1_{.Type = D3DLIGHT_DIRECTIONAL, .Diffuse = {.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F}};
    D3DLIGHT9 device_light0_{.Type = D3DLIGHT_DIRECTIONAL,
                             .Diffuse = {.r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F},
                             .Specular = {.r = 1.0F, .g = 1.0F, .b = 1.0F, .a = 0.0F}};
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

namespace {

const maths::Mat4*& stored_last_bones()
{
    static const maths::Mat4* last_bones{};
    return last_bones;
}

} // namespace

const maths::Mat4* last_bones() { return stored_last_bones(); }
void set_last_bones(const maths::Mat4* value) { stored_last_bones() = value; }

namespace {
// NOT ORIGINAL grouping - the instances' table (the original's globals). Made on first use, never destroyed
// (instance_table()).
struct InstanceTable {
    Singleton* instances_{};
    std::array<Singleton*, kVisibleInstanceCount> visible_instances_{}; // draw_instance_batch' list
};

InstanceTable& instance_table()
{
    static core::NoDestroy<InstanceTable> state;
    return state.get();
}

Singleton*& instances() { return instance_table().instances_; }
std::array<Singleton*, kVisibleInstanceCount>& get_visible() { return instance_table().visible_instances_; }

const D3DMATRIX* get_view_world()
{
    return d3d::matrix(renderer().world_matrix().data());
} // the world transform to put back

constexpr uint32_t kMeshLit = 0x10;       // a mesh's +0x38: it asks for lighting
constexpr uint32_t kMeshNotLit = 0x20000; // a mesh's flags
constexpr uint32_t kMeshShown = 0x40;     // a mesh's flags
constexpr int kMaxBones = 0x3A;
constexpr RenderStateId kStateLighting = RenderStateId::kLighting; // change_phase's

int bones_on_device(const SceneRef* scene) { return static_cast<int>(scene->unknown78()); }
// Its bones drawn by the vertex shader: it has bones, and its scene doesn't skin them on the CPU (+0x78).
bool skinned_by_shader(const Singleton* instance) { return instance->bones() && bones_on_device(instance->scene()) <= 0; }
// The scene's first byte's top bit: drawn unlit.
bool scene_unlit(const SceneRef* scene) { return static_cast<int8_t>(scene->flags() & kLowByteMask) < 0; }

uint32_t mesh_traits(const MeshRef* mesh) { return mesh->flags(); }
uint32_t& mesh_flags_ref(MeshRef* mesh) { return mesh->flags(); }

const maths::Vec4* get_position(const Singleton* instance)
{
    return &core::at(instance->transform().row, 3);
}

// A colour channel (0-1) as a byte, as the original's _ftol (truncated through 64 bits).
uint32_t get_channel(float value)
{
    return static_cast<uint32_t>(static_cast<int64_t>(static_cast<double>(value) * kByteMaxFloat)) & kLowByteMask;
}

// The model's lights for the instance's position (the model's +0x2C: plat_apply_scene_lights, with its radius), or the
// scene's.
void refresh_instance_lighting(Singleton* instance)
{
    ModelLights* lights = instance->model() ? instance->model()->lights_ptr().get() : nullptr;
    if (!lights) {
        reset_lights();
        return;
    }
    lights->plat_apply_scene_lights(get_position(instance), lights->dynamic_radius());
}

// The renderer's light constants into the device's lights 0 and 1 (direction, diffuse colour) and its ambient
// colour.
void send_lights()
{
    D3DLIGHT9& lamp_a = pc_instance_state().device_light0_;
    lamp_a.Diffuse.r = core::at(light_sets().light_constants(), kLampADiffuseR);
    lamp_a.Diffuse.g = core::at(light_sets().light_constants(), kLampADiffuseG);
    lamp_a.Diffuse.b = core::at(light_sets().light_constants(), kLampADiffuseB);
    lamp_a.Direction.x = core::at(light_sets().light_constants(), 4);
    lamp_a.Direction.y = core::at(light_sets().light_constants(), kLampADirectionY);
    lamp_a.Direction.z = core::at(light_sets().light_constants(), kLampADirectionZ);
    renderer().d3d_device()->SetLight(0, &lamp_a);
    D3DLIGHT9& lamp_b = pc_instance_state().device_light1_;
    lamp_b.Diffuse.r = core::at(light_sets().light_constants(), kLampBDiffuseR);
    lamp_b.Diffuse.g = core::at(light_sets().light_constants(), kLampBDiffuseG);
    lamp_b.Diffuse.b = core::at(light_sets().light_constants(), kLampBDiffuseB);
    lamp_b.Direction.x = core::at(light_sets().light_constants(), kLampBDirectionX);
    lamp_b.Direction.y = core::at(light_sets().light_constants(), kLampBDirectionY);
    lamp_b.Direction.z = core::at(light_sets().light_constants(), kLampBDirectionZ);
    renderer().d3d_device()->SetLight(1, &lamp_b);
    const uint32_t r = get_channel(core::at(light_sets().light_constants(), 0));
    const uint32_t g = get_channel(core::at(light_sets().light_constants(), 1));
    const uint32_t b = get_channel(core::at(light_sets().light_constants(), 2));
    renderer().d3d_device()->SetRenderState(D3DRS_AMBIENT, kOpaqueAlphaBits | r << kByte2Shift | g << kByte1Shift | b);
}

void matrix_multiply(maths::Mat4* out, const maths::Mat4* a, const maths::Mat4* b) { d3dx::matrix_multiply(out, a, b); }

} // namespace

// 0x004D1030
Singleton::Singleton(SceneRef* list, const maths::Mat4* matrix, int count, maths::Mat4* bone_matrices)
    : transform_(*matrix), bones_(bone_matrices), bone_count_(count), scene_(list), next_(instances())
{
    instances() = this;
    if (bones_ && bones_on_device(scene_) <= 0)
        return;
    for (const MeshRef* mesh : scene_->mesh_run()) {
        if (!(mesh->fvf() & kMeshLit) || (mesh_traits(mesh) & kMeshNotLit))
            return;
    }
    flags_ |= 2;
}

// 0x004D10C0
Singleton::~Singleton()
{
    if (flags_ & 1) {
        if (SceneRef* list = scene_) {
            list->destroy_body();
            memory::Hub::get_pull()->dealloc(list);
            scene_ = nullptr;
        }
        if (bones_) {
            memory::Hub::get_pull()->dealloc(bones_);
            bones_ = nullptr;
        }
    }
    for (Singleton** link = &instances(); *link; link = &(*link)->next_) {
        if (*link == this) {
            *link = next_;
            return;
        }
    }
}

// 0x004D1130
void Singleton::render(uint32_t draw_bits)
{
    if (skinned_by_shader(this)) {
        set_world_transform(transform_.data());
        if (last_bones() != bones_) {
            if (scene_unlit(scene_)) {
                renderer().d3d_device()->SetRenderState(D3DRS_LIGHTING, FALSE);
            } else {
                refresh_instance_lighting(this);
                renderer().d3d_device()->SetRenderState(D3DRS_LIGHTING, TRUE);
                send_lights();
            }
            set_last_bones(bones_);
        }
        renderer().d3d_device()->SetTransform(D3DTS_WORLD, d3d::matrix(transform_.data()));
        pc_instance_state().palette_base_ = bones_;
        pc_instance_state().bone_count_ = bone_count_;
        mesh_pass().set_current_instance(this);
        if (scene_unlit(scene_)) {
            draw_scene(scene_, {.flags = draw_bits | kRenderNoCulling, .viewport = 0});
        } else {
            if (flags_ & 4) {
                renderer().set_instance_colour_on(1); // the instance's colour on
                renderer().set_instance_colour(color_);
            }
            draw_scene(scene_, {.flags = draw_bits | kRenderNoCulling, .viewport = 0});
            if (flags_ & 4)
                renderer().set_instance_colour_on(0);
        }
        renderer().set_skipped_passes(renderer().skipped_passes() & ~kSkippedPassBit);
        renderer().set_vertex_format_locked(0);
        renderer().set_pixel_shader_locked(nullptr);
        renderer().set_submit_locked(0);
        set_texture(3, nullptr);
        pc_instance_state().palette_base_ = nullptr;
        pc_instance_state().bone_count_ = 0;
        mesh_pass().set_current_instance(nullptr);
        return;
    }

    set_last_bones(nullptr);
    if ((flags_ & 2) && !scene_unlit(scene_)) {
        refresh_instance_lighting(this);
        change_phase(kStateLighting, 1);
        send_lights();
    } else {
        change_phase(kStateLighting, 0);
    }
    if (bones_on_device(scene_) != 0) {
        const int count = bone_count_ < kMaxBones ? bone_count_ : kMaxBones;
        if (count > 0) {
            const std::span<const maths::Mat4> bone_run(bones_, static_cast<std::size_t>(bone_count_));
            for (int bone = 0; bone < count; ++bone) {
                maths::Mat4 world = transform_;
                matrix_multiply(&world, &core::at(bone_run, bone), &world);
                set_world_transform(world.data());
                renderer().d3d_device()->SetTransform(D3DTS_WORLD, d3d::matrix(world.data()));
                for (MeshRef* mesh : scene_->mesh_run()) {
                    if (static_cast<int8_t>(mesh->bone()) == bone) // (its bone)
                        mesh_flags_ref(mesh) |= kMeshShown;
                    else
                        mesh_flags_ref(mesh) &= ~kMeshShown;
                }
                draw_scene(scene_, {.flags = draw_bits | kRenderNoCulling, .viewport = 0});
            }
            return;
        }
    }
    set_world_transform(transform_.data());
    renderer().d3d_device()->SetTransform(D3DTS_WORLD, d3d::matrix(transform_.data()));
    draw_scene(scene_, {.flags = draw_bits | kRenderNoCulling, .viewport = 0});
}

// 0x004D16B0
void draw_one_instance(Singleton* instance, uint32_t flags)
{
    set_last_bones(nullptr);
    if (!instance->active())
        return;
    set_world_transform((instance->transform()).data());
    const maths::Vec4 sphere = sphere_of(instance->scene()->centre(), instance->scene()->radius());
    const maths::Vec4* centre = &sphere;
    if ((flags & kRenderFlagUnculled) ||
        (sphere_in_view(centre, instance->scene()->radius()) && !sphere_occluded(centre, {.radius = instance->scene()->radius(), .weight = 1}))) {
        if (instance->bones()) {
            set_flag_786e_b4();
            instance->render(kRenderOpaque | kRenderTransparent);
            clear_flag_786e_b4();
        } else {
            instance->render(kRenderOpaque | kRenderTransparent);
        }
    }
    renderer().d3d_device()->SetTransform(D3DTS_WORLD, get_view_world());
    set_world_transform(nullptr);
}

// 0x004D1760
void draw_instance_batch(BatchPass batch_pass)
{
    const auto flags = batch_pass.flags;
    const auto viewport = batch_pass.viewport;
    const uint32_t mask = 1U << (viewport & 0x1F);
    std::array<float, maths::kMatrixFloats> culling = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}; // the identity at each one's position
    int count = 0;
    set_last_bones(nullptr);
    if (instances()) {
        for (Singleton* instance = instances(); instance; instance = instance->next()) {
            if (!instance->active() || !(instance->viewports() & mask))
                continue;
            const SceneRef* list = instance->scene();
            if (!((flags & kRenderOpaque) && list->mesh_count() > list->transparent_count()) &&
                !((flags & kRenderTransparent) && list->transparent_count() > 0))
                continue;
            const maths::Vec4& position = *get_position(instance);
            for (int k = 0; k < 4; ++k)
                core::at(culling, maths::kMatTx + k) = position[k];
            set_world_transform(culling.data());
            float radius = instance->scene()->radius();
            if (instance->bones() && bones_on_device(instance->scene()) == 0 && radius < kSkinnedMinRadius)
                radius = kSkinnedMinRadius; // (a skinned model's sphere at least this big)
            const maths::Vec4 sphere = sphere_of(instance->scene()->centre(), instance->scene()->radius());
            if (!sphere_occluded(&sphere, {.radius = radius, .weight = 1}))
                core::at(get_visible(), count++) = instance;
        }
        if (count > 0) {
            crt::get_qsort(static_cast<void*>(get_visible().data()), static_cast<uint32_t>(count), 4,
                           compare_instances); // (the C runtime's qsort)
            change_phase(kStateLighting, 1);
            renderer().d3d_device()->SetRenderState(D3DRS_COLORVERTEX, TRUE);
            renderer().d3d_device()->LightEnable(0, TRUE);
            renderer().d3d_device()->LightEnable(1, TRUE);
            set_flag_786e_b4();
            int i = 0;
            for (; i < count; ++i) {
                Singleton* instance = core::at(get_visible(), i);
                if (!skinned_by_shader(instance))
                    break;
                if ((flags & kRenderOpaque) || ((flags & kRenderTransparent) && (flags & kRenderFlagSkinA)))
                    instance->render(flags);
            }
            clear_flag_786e_b4();
            const std::array<float, 3> camera = {{core::at(renderer().render_camera().position, 0), core::at(renderer().render_camera().position, 1), core::at(renderer().render_camera().position, 2)}};
            const double near_distance = static_cast<double>(script::fetch_float(0x4E7DC608U)) * 12.0F;
            auto near_squared = static_cast<float>(near_distance * near_distance);
            for (; i < count; ++i) {
                Singleton* instance = core::at(get_visible(), i);
                const maths::Vec4& position = *get_position(instance);
                const double dx = static_cast<double>(position.x) - core::at(camera, 0);
                const double dy = static_cast<double>(position.y) - core::at(camera, 1);
                const double dz = static_cast<double>(position.z) - core::at(camera, 2);
                renderer().set_near_camera(!((dz * dz) + (dy * dy) + (dx * dx) > near_squared)); // near the camera
                if ((flags & kRenderOpaque) || ((flags & kRenderTransparent) && (flags & kRenderFlagSkinB)))
                    instance->render(flags);
            }
            renderer().set_near_camera(true);
            change_phase(kStateLighting, 0);
            renderer().d3d_device()->SetRenderState(D3DRS_COLORVERTEX, FALSE);
            renderer().d3d_device()->LightEnable(0, FALSE);
            renderer().d3d_device()->LightEnable(1, FALSE);
        }
    }
    renderer().d3d_device()->SetTransform(D3DTS_WORLD, get_view_world());
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
        if (first->bones() != second->bones())
            return second->bones() < first->bones() ? 1 : -1;
        const int x = first->scene()->transparent_count();
        const int y = second->scene()->transparent_count();
        return x == y ? 0 : x > y ? 1 : -1;
    }
    if (skinned_by_shader(second))
        return 1;
    const int x = first->scene()->transparent_count();
    const int y = second->scene()->transparent_count();
    if (x != y)
        return x > y ? 1 : -1;
    if (x <= 0)
        return 0;
    const MaterialRef* m1 = core::at(first->scene()->mesh_run(), first->scene()->first_transparent())->material();
    const MaterialRef* m2 = core::at(second->scene()->mesh_run(), second->scene()->first_transparent())->material();
    if (!m1 || !m2)
        return 0;
    const float k1 = m1->sort_key();
    const float k2 = m2->sort_key();
    if (k1 > k2)
        return 1;
    return k1 < k2 ? -1 : 0;
}

// 0x004D1A60
UvOscillator::UvOscillator()
{
    core::at(velocity_, 0) = core::at(velocity_, 1) = 0;
    core::at(frequency_, 0) = core::at(frequency_, 1) = 0;
    core::at(amplitude_, 0) = core::at(amplitude_, 1) = 0;
    core::at(phase_, 0) = core::at(phase_, 1) = 0;
    core::at(matrix_, 0) = 1.0F;
    core::at(matrix_, 1) = 0;
    core::at(offset_, 0) = core::at(offset_, 1) = 0;
}

// 0x004D1A80
void relaunch(TextureAnim& anim)
{
    anim.start_time_ = frame_clock::get_time();
}

} // namespace render
