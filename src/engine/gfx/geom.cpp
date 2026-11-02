// The PC geom: see geom.h. The maths follows the original's x87 code (values kept in double precision where it keeps
// them in registers, rounded to float where it stores them).

#include "engine/gfx/geom.h"
#include "engine/core/down_cast.h"
#include "engine/gfx/pc_mesh.h"
#include "platform/d3dx.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <utility>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_geom.h"
#include "engine/maths/mat4.h"
#include "gpu/vertex_buffers.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/mesh_list.h"
#include "engine/gfx/pc_scene.h"
#include "engine/gfx/model.h"
#include "engine/core/ints.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/checked_index.h"
#include <span>
#include "gpu/vertex_view.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/mesh_pass.h"
#include <gsl/pointers>

namespace render {

using memory::ListNode;

namespace {

constexpr float kPer255 = 1.0f / 255.0f;    // 0x00645D70 (0x3B808081)
constexpr float kQuarterTurn = 1.57079637f; // 0x00645484

// A mesh's vertex buffers (not rewritten yet): buffer `index`, locked (offset, size - 0: all, flags; the vertices
// into *data) and unlocked.
VertexBuffer* get_vertex_buffer(MeshRef* mesh, int index) { return mesh->fetch_vertex_store(index); }
uint8_t* get_lock(VertexBuffer* buffer, uint32_t flags)
{
    uint8_t* data = nullptr;
    buffer->get_lock(0, 0, &data, flags);
    return data;
}
void get_unlock(VertexBuffer* buffer) { buffer->get_unlock(); }
constexpr uint32_t kReadOnly = 0x10; // D3DLOCK_READONLY

// A mesh's per-vertex colour's offset in a vertex, and the position its transform says (0x004D4D30).
uint8_t color_offset(const MeshRef* mesh) { return mesh->color_offset(); }
void mesh_position(MeshRef* mesh, maths::Vec4* out) { mesh->fetch_place(out); }
MaterialRef* material_of(MeshRef* mesh) { return mesh->material_value(); }
// A mesh's colour (flag 4) and its vertex shader (its vertex format's place).
std::array<float, 3>& mesh_tint(MeshRef* mesh) { return mesh->color_value(); }
uint32_t& mesh_vertex_shader(MeshRef* mesh) { return mesh->fvf_value(); }

// The instance's meshes (its mesh list's).
std::span<MeshRef*> scene_meshes(SceneRef* scene) { return scene->mesh_run(); }

// A new instance (0x64 bytes from the heap) of its mesh list, transform and bones.
gsl::owner<Singleton*> new_instance(SceneRef* scene, const maths::Mat4* transform, int bone_count, maths::Mat4* bones)
{
    void* memory = memory::alloc(sizeof(Singleton));
    if (!memory)
        return nullptr;
    return memory::construct_in<Singleton>(memory, scene, transform, bone_count, bones);
}
// A new UV wibble (0x004D1A60, on 0x30 bytes from the heap).
gsl::owner<UvOscillator*> new_uv_wibble()
{
    void* memory = memory::alloc(sizeof(UvOscillator));
    return memory ? memory::construct_in<UvOscillator>(memory) : nullptr;
}

void get_identity(maths::Mat4* m)
{
    const std::span<float, 16> f = m->floats();
    for (int i = 0; i < 16; ++i)
        core::at(f, i) = (i % 5 == 0) ? 1.0f : 0.0f;
}

// A byte colour channel as the float the meshes keep (x87: the integer times the float 1/255, rounded once).
float channel_to_float(uint32_t channel) { return static_cast<float>(static_cast<double>(channel) * kPer255); }
// And back (the C runtime's _ftol of the float times 255: truncated).
uint8_t float_to_channel(float value)
{
    return static_cast<uint8_t>(static_cast<int>(static_cast<double>(value) * 255.0));
}

// NOT ORIGINAL grouping - geom.cpp's state (the original's globals). Made on first use, never destroyed (geom_state()).
struct GeomState {
    maths::Vec4 static_position_{};
    uint32_t static_position_guard_{};
    maths::Mat4 static_orientation_{};
    uint32_t static_orientation_guard_{};
};

GeomState& geom_state()
{
    static core::NoDestroy<GeomState> state;
    return state.get();
}

constexpr uint32_t kVertexShaderUv58{0};
constexpr uint32_t kVertexShaderUv5c{0};
constexpr uint32_t kVertexShaderUv60{0};

// The static results of get_absolute_point / fetch_facing (with their initialisation guards' bits).
maths::Vec4& static_position()
{
    geom_state().static_position_guard_ |= 1;
    return geom_state().static_position_;
}
maths::Mat4& static_orientation()
{
    geom_state().static_orientation_guard_ |= 1;
    return geom_state().static_orientation_;
}

} // namespace

// 0x004B4460
PcGeom::PcGeom() { scale = {.x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f}; }

// 0x004B5EE0
PcGeom::~PcGeom()
{
    free_mesh_table();
    if (Singleton* inst = instance) {
        inst->~Singleton();
        memory::Hub::get_pull()->dealloc(inst);
        instance = nullptr;
    }
}

// 0x004B5F60
void PcGeom::start_mesh_list()
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap());
    load_list_ = ListNode::create_head();
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x004B52F0
void PcGeom::append_mesh(MeshRef* mesh)
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap());
    ListNode* node = ListNode::create(mesh);
    // at the end: before the head (no check: a failed allocation writes through null, as the original)
    ListNode* head = load_list_;
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x004B5360
void PcGeom::make_mesh_table(void* vertex_arena)
{
    if (meshes)
        return;
    auto is_head = [](const ListNode* node) { return node->data == memory::ListNode::head_marker(); };
    uint32_t count = 0;
    for (ListNode* node = load_list_->next; !is_head(node) && node; node = node->next)
        ++count;
    mesh_count_ = count;
    if (count) {
        meshes = static_cast<MeshRef**>(memory::alloc(4 * count));
        ListNode* node = load_list_->next;
        if (!is_head(node) && node) {
            for (int i = 0; node; ++i) {
                ListNode* next = is_head(node->next) ? nullptr : node->next;
                core::at(mesh_run(), i) = static_cast<MeshRef*>(node->data);
                ListNode::destroy(node); // (0x004B5E50: unlinked, freed)
                if (vertex_arena)
                    core::at(mesh_run(), i)->move_into_arena(static_cast<VertexArena*>(vertex_arena));
                node = next;
            }
        }
    }
    ListNode::destroy(load_list_); // (0x004B5E90)
    load_list_ = nullptr;
}

// 0x004B5440
void PcGeom::free_mesh_table()
{
    if (!meshes)
        return;
    if (scene) {
        if (SceneRef* list = scene->meshes)
            list->detach_meshes(mesh_run());
    }
    for (uint32_t i = 0; i < mesh_count_; ++i) {
        if (MeshRef* mesh = core::at(mesh_run(), i)) {
            mesh->~MeshRef(); // (the destructor's body)
            memory::Hub::get_pull()->dealloc(mesh);
        }
    }
    memory::Hub::get_pull()->dealloc(static_cast<void*>(meshes));
    meshes = nullptr;
}

// 0x004B44D0
bool PcGeom::add_to_mesh_list(bool count)
{
    SceneRef* list = scene->meshes;
    if (count)
        list->mesh_total(mesh_run());
    else
        list->append_meshes(mesh_run());
    return true;
}

// 0x004B4510
SceneRef* PcGeom::create_mesh_list()
{
    void* memory = memory::alloc(sizeof(SceneRef));
    SceneRef* list = memory ? memory::construct_in<SceneRef>(memory) : nullptr;
    list->mesh_total(mesh_run());
    list->allocate_mesh_list();
    list->append_meshes(mesh_run());
    list->order_meshes();
    std::memcpy(&list->bounds_value(), &bounds, sizeof(bounds));
    list->update_sphere();
    return list;
}

// 0x004B58C0
gsl::owner<Geom*> PcGeom::plat_replicate(int instance_flag, WorldScene* into)
{
    const bool share = static_cast<uint8_t>(instance_flag) != 0;
    auto* new_scene = core::down_cast<PcScene>(into);
    auto geom = memory::make_owned<PcGeom>();
    // (no check: a failed allocation writes through null, as the original)
    geom->meshes = static_cast<MeshRef**>(memory::alloc(4 * mesh_count_));
    geom->mesh_count_ = mesh_count_;
    for (uint32_t i = 0; i < geom->mesh_count_; ++i)
        core::at(geom->mesh_run(), i) = core::at(mesh_run(), i)->replicate(share);
    if (!share) {
        geom->change_live(true);
        // into the scene's mesh list: its meshes kept aside, room made for these ones too, all added back
        SceneRef* list = new_scene->meshes;
        MeshRef** kept = list->mesh_count() > 0 ? static_cast<MeshRef**>(memory::alloc(4 * list->mesh_count())) : nullptr;
        const std::span<MeshRef*> kept_run(kept, kept ? static_cast<size_t>(list->mesh_count()) : 0);
        geom->scene = new_scene;
        for (int i = 0; i < list->mesh_count(); ++i)
            core::at(kept_run, i) = core::at(list->mesh_run(), i);
        const int kept_count = list->mesh_count();
        memory::Hub::get_pull()->dealloc(static_cast<void*>(list->meshes_value()));
        list->set_meshes(nullptr);
        list->mesh_total(mesh_run()); // (this one's)
        list->allocate_mesh_list();
        for (int i = 0; i < kept_count; ++i)
            core::at(list->mesh_run(), i) = core::at(kept_run, i);
        memory::Hub::get_pull()->dealloc(static_cast<void*>(kept));
        list->append_meshes(geom->mesh_run());
        list->order_meshes();
    } else {
        std::memcpy(&geom->bounds, &bounds, sizeof(bounds));
        SceneRef* list = geom->create_mesh_list();
        geom->change_live(true);
        maths::Mat4 identity{};
        get_identity(&identity);
        gsl::owner<Singleton*> inst = new_instance(list, &identity, 1, nullptr);
        inst->flags |= 1;
        geom->instance = inst;
    }
    return geom;
}

// 0x004B5B80
gsl::owner<Geom*> PcGeom::clone_instance(int instance_flag, RenderModel* model)
{
    const bool share = static_cast<uint8_t>(instance_flag) != 0;
    auto scene_owner = std::make_unique<PcScene>(10); // (only for the copy)
    PcScene* temp_scene = scene_owner.get();
    temp_scene->meshes = instance->scene;
    auto geom = memory::make_owned<PcGeom>();
    geom->scene = temp_scene;
    auto count = static_cast<uint32_t>(instance->scene->mesh_count());
    geom->meshes = count ? static_cast<MeshRef**>(memory::alloc(4 * count)) : nullptr;
    geom->mesh_count_ = count;
    for (uint32_t i = 0; i < geom->mesh_count_; ++i)
        core::at(geom->mesh_run(), i) = core::at(scene_meshes(instance->scene), i)->replicate(share);
    std::memcpy(&geom->bounds, &bounds, sizeof(bounds));
    SceneRef* list = geom->create_mesh_list();
    // the scene was only for the copy: destroyed without its mesh list
    temp_scene->meshes = nullptr;
    scene_owner.reset();
    geom->scene = nullptr;
    geom->change_live(true);
    // bones: the model's count (room for one more), each the identity
    const int bone_count = model->bone_total();
    auto* bones = static_cast<maths::Mat4*>(memory::alloc((bone_count + 1) * sizeof(maths::Mat4)));
    const std::span<maths::Mat4> bone_run(bones, static_cast<size_t>(bone_count) + 1);
    for (int i = 0; i < bone_count; ++i)
        get_identity(&core::at(bone_run, i));
    maths::Mat4 identity{};
    get_identity(&identity);
    gsl::owner<Singleton*> inst = new_instance(list, &identity, bone_count, bones);
    inst->flags |= 1;
    geom->instance = inst;
    return geom;
}

// 0x004B54B0
bool PcGeom::plat_init_instance(SubMesh* sub_mesh, RenderModel* model, int /*unused*/)
{
    auto* mesh = core::down_cast<PcMesh>(sub_mesh);
    const int bone_count = model->bone_total();
    source = mesh;
    maths::Mat4 identity{};
    get_identity(&identity);
    void* memory = memory::alloc(sizeof(Singleton));
    gsl::owner<Singleton*> inst = nullptr;
    if (memory) {
        SceneRef* list = mesh->scene->meshes;
        maths::Mat4* bones = model->skin_palette(); // (the model's bones: 0x00492E50)
        inst = memory::construct_in<Singleton>(memory, list, &identity, bone_count, bones);
    }
    instance = inst;
    inst->model = model; // (no check, as the original)
    return true;
}

// 0x004B45F0
void PcGeom::clean_instance_strips()
{
    if (!instance || !instance->scene)
        return;
    SceneRef* list = instance->scene;
    for (int i = 0; i < list->mesh_count(); ++i)
        core::at(scene_meshes(list), i)->clean_strip();
}

// 0x004B48B0
void PcGeom::change_tint(uint32_t rgba)
{
    const uint8_t r = rgba & 0xFF;
    const uint8_t g = (rgba >> 8) & 0xFF;
    const uint8_t b = (rgba >> 16) & 0xFF;
    if ((r == 0x80 || r == 0x81) && g == 0x80 && b == 0x80) {
        wipe_tint();
        return;
    }
    if (instance) {
        instance->flags |= 4;
        std::memcpy(instance->color.data(), &rgba, 4);
        return;
    }
    if (!meshes)
        return;
    const float red = channel_to_float(r);
    const float green = channel_to_float(g);
    const float blue = channel_to_float(b);
    for (uint32_t i = 0; i < mesh_count_; ++i) {
        MeshRef* mesh = core::at(mesh_run(), i);
        core::at(mesh_tint(mesh), 0) = red;
        core::at(mesh_tint(mesh), 1) = green;
        mesh->flags_value() |= 4;
        core::at(mesh_tint(mesh), 2) = blue;
    }
}

// 0x004B4B20
uint32_t* PcGeom::plat_get_color(uint32_t* out)
{
    std::array<uint8_t, 4> color = {{0, 0, 0, 0}};
    MeshRef* mesh = nullptr;
    if (instance) {
        if (instance->scene->mesh_count() > 0)
            mesh = core::at(scene_meshes(instance->scene), 0);
    } else if (meshes) {
        mesh = core::at(mesh_run(), 0);
    }
    if (mesh && (mesh->flags_value() & 4)) {
        core::at(color, 0) = float_to_channel(core::at(mesh_tint(mesh), 0));
        core::at(color, 1) = float_to_channel(core::at(mesh_tint(mesh), 1));
        core::at(color, 2) = float_to_channel(core::at(mesh_tint(mesh), 2));
    }
    std::memcpy(out, color.data(), 4);
    return out;
}

// 0x004B4970
void PcGeom::wipe_tint()
{
    if (instance) {
        std::memset(instance->color.data(), 0x80, 4);
        instance->flags &= ~4u;
        return;
    }
    if (!meshes)
        return;
    for (uint32_t i = 0; i < mesh_count_; ++i)
        core::at(mesh_run(), i)->flags_value() &= ~4u;
}

// 0x004B49C0
bool PcGeom::change_material_tint(uint32_t checksum, int pass, uint32_t rgba)
{
    bool found = false;
    if (!instance || !instance->scene)
        return false;
    for (int i = 0; i < instance->scene->mesh_count(); ++i) {
        MaterialRef* material = material_of(core::at(scene_meshes(instance->scene), i));
        if (!material)
            continue;
        const int index = pass_from_checksum(material, checksum, pass);
        if (index < 0 || ints::as_unsigned(index) >= material->pass_count())
            continue;
        if (static_cast<int8_t>(core::at(material->pass_flags(), index) & 0xFF) < 0) // (the colour is fixed)
            continue;
        found = true;
        core::at(core::at(material->pass_color(), index), 0) = channel_to_float(rgba & 0xFF);
        core::at(core::at(material->pass_color(), index), 1) = channel_to_float((rgba >> 8) & 0xFF);
        core::at(core::at(material->pass_color(), index), 2) = channel_to_float((rgba >> 16) & 0xFF);
    }
    return found;
}

// 0x004B4870
void PcGeom::toggle_shown(uint32_t mask)
{
    visibility = mask;
    if (!meshes)
        return;
    for (uint32_t i = 0; i < mesh_count_; ++i)
        core::at(mesh_run(), i)->set_viewports(static_cast<uint8_t>(mask));
}

// 0x004B4120
uint32_t PcGeom::fetch_shown() { return visibility | 0xFFFFFF00u; }

// 0x004B4630
void PcGeom::change_live(bool on)
{
    if (instance) {
        instance->active = on;
        return;
    }
    active = on;
    if (!meshes)
        return;
    for (uint32_t i = 0; i < mesh_count_; ++i) {
        if (on)
            core::at(mesh_run(), i)->flags_value() |= 0x40;
        else
            core::at(mesh_run(), i)->flags_value() &= ~0x40u;
    }
}

// 0x004B4690
bool PcGeom::is_operative() { return instance ? instance->active : active; }

// 0x004B4730
void PcGeom::change_bound_sphere(const maths::Vec4* sphere)
{
    if (instance && instance->scene) {
        SceneRef* list = instance->scene;
        std::memcpy(list->centre_value().data(), sphere, 12);
        std::memcpy(&list->radius_value(), &sphere->w, 4);
    }
}

// 0x004B4760
maths::Vec4* PcGeom::plat_get_bounding_sphere(maths::Vec4* out)
{
    if (instance && instance->scene) {
        const SceneRef* list = instance->scene;
        *out = {.x = core::at(list->centre_value(), 0), .y = core::at(list->centre_value(), 1), .z = core::at(list->centre_value(), 2), .w = list->radius_value()};
    } else {
        *out = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 10000.0f};
    }
    return out;
}

// 0x004B55D0
void PcGeom::place_at_absolute_point(const maths::Vec4* position)
{
    const maths::Vec4 pos = {.x = position->x, .y = position->y, .z = position->z, .w = 1.0f};
    if (instance) {
        core::at(instance->transform.row, 3) = pos;
        return;
    }
    for (uint32_t i = 0; i < mesh_count_; ++i)
        core::at(mesh_run(), i)->move_to(&pos);
}

// 0x004B46A0
maths::Vec4* PcGeom::get_absolute_point()
{
    maths::Vec4& pos = static_position();
    if (instance) {
        pos = core::at(instance->transform.row, 3);
        return &pos;
    }
    pos = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    if (core::at(mesh_run(), 0)) // (no check of the array itself, as the original)
        mesh_position(core::at(mesh_run(), 0), &pos);
    return &pos;
}

// 0x004B5650
void PcGeom::change_facing(const maths::Mat4* orientation)
{
    if (!instance)
        return;
    maths::Mat4 m{};
    m = instance->transform;
    core::at(m.row, 0) = core::at(orientation->row, 0);
    core::at(m.row, 1) = core::at(orientation->row, 1);
    core::at(m.row, 2) = core::at(orientation->row, 2);
    instance->transform = m;
}

// 0x004B56E0
maths::Mat4* PcGeom::fetch_facing()
{
    maths::Mat4& m = static_orientation();
    if (instance)
        m = instance->transform;
    else
        get_identity(&m);
    core::at(m.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    return &m;
}

// 0x004B57C0
void PcGeom::rotate_y(int turns)
{
    if (!turns)
        return;
    if (!instance) {
        for (uint32_t i = 0; i < mesh_count_; ++i)
            core::at(mesh_run(), i)->turn_y(turns);
        return;
    }
    maths::Mat4 m{};
    m = instance->transform;
    core::at(m.row, 3) = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    maths::Mat4 rotation{};
    maths::make_yaw_matrix(rotation, static_cast<float>(static_cast<double>(turns) * kQuarterTurn));
    // D3DX (linked library code): rotation = m * rotation
    d3dx::matrix_multiply(&rotation, &m, &rotation);
    core::at(rotation.row, 3) = core::at(instance->transform.row, 3);
    instance->transform = rotation;
}

// 0x004B4DB0
void PcGeom::change_scale(const maths::Vec4* new_scale)
{
    scale = *new_scale;
    if (!meshes)
        return;
    for (uint32_t i = 0; i < mesh_count_; ++i) {
        MeshRef* mesh = core::at(mesh_run(), i);
        maths::Vec4 centre = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
        mesh_position(mesh, &centre);
        VertexBuffer* buffer = get_vertex_buffer(mesh, 0);
        const gpu::VertexWriter vertices(get_lock(buffer, 0), mesh->stride_value(), mesh->vertex_count());
        for (uint32_t v = 0; v < mesh->vertex_count(); ++v) {
            auto p = vertices.view().point<maths::Vec4>(v);
            for (int k = 0; k < 3; ++k) {
                const double c = centre[k];
                p[k] = static_cast<float>((static_cast<double>(p[k]) - c) * (*new_scale)[k] + c);
            }
            vertices.set_point(v, 0, p);
        }
        get_unlock(get_vertex_buffer(mesh, 0));
    }
}

// 0x004B4ED0
maths::Vec4* PcGeom::fetch_magnification(maths::Vec4* out)
{
    *out = scale;
    return out;
}

// 0x004B47B0
bool PcGeom::upload_skinning_palette(const maths::Mat4* transform, const maths::Mat4* bones, int count, int, int)
{
    if (instance) {
        instance->transform = *transform;
        std::memcpy(instance->bones, bones, count * sizeof(maths::Mat4));
    }
    return true;
}

// 0x004B47F0
void PcGeom::copy_bones(const maths::Mat4* bones, int count)
{
    if (instance)
        std::memcpy(instance->bones, bones, count * sizeof(maths::Mat4));
}

// 0x004B4820
void PcGeom::set_bones(maths::Mat4* bones, int count)
{
    if (instance) {
        instance->bones = bones;
        instance->bone_count_ = count;
    }
}

// 0x004B4840
bool PcGeom::break_strips(uint32_t mask)
{
    if (source)
        instance->scene->break_strips(mask, source->breaks, source->break_count_);
    return true;
}

// 0x004B4130
uint32_t PcGeom::fetch_vertex_tally()
{
    uint32_t total = 0;
    if (meshes) {
        for (uint32_t i = 0; i < mesh_count_; ++i)
            total += core::at(mesh_run(), i)->vertex_count();
    }
    return total;
}

// 0x004B4BB0
void PcGeom::get_vertices(std::span<maths::Vec4> out)
{
    if (!meshes)
        return;
    size_t out_at = 0;
    for (uint32_t i = 0; i < mesh_count_; ++i) {
        MeshRef* mesh = core::at(mesh_run(), i);
        const gpu::VertexView vertices(get_lock(get_vertex_buffer(mesh, 0), kReadOnly), mesh->stride_value(), mesh->vertex_count());
        for (uint32_t v = 0; v < mesh->vertex_count(); ++v, ++out_at)
            core::at(out, out_at) = vertices.point<maths::Vec4>(v);
        get_unlock(get_vertex_buffer(mesh, 0));
    }
}

// 0x004B4C50
void PcGeom::set_vertices(std::span<const maths::Vec4> in)
{
    if (!meshes)
        return;
    size_t in_at = 0;
    for (uint32_t i = 0; i < mesh_count_; ++i) {
        MeshRef* mesh = core::at(mesh_run(), i);
        const gpu::VertexWriter vertices(get_lock(get_vertex_buffer(mesh, 0), 0), mesh->stride_value(), mesh->vertex_count());
        maths::BBox box;
        for (uint32_t v = 0; v < mesh->vertex_count(); ++v, ++in_at) {
            const maths::Vec4& position = core::at(in, in_at);
            vertices.set_point(v, 0, position);
            box.append_spot(position);
        }
        get_unlock(get_vertex_buffer(mesh, 0));
        // the sphere around the box (x87: the half sizes and the centre's z kept in double where the original does)
        const double dx = static_cast<double>(box.max.x) - box.min.x;
        const double dy = static_cast<double>(box.max.y) - box.min.y;
        auto dz = static_cast<float>(static_cast<double>(box.max.z) - box.min.z);
        auto half_x = static_cast<float>(dx * 0.5);
        auto cx = static_cast<float>(static_cast<double>(box.min.x) + half_x);
        auto cy = static_cast<float>(dy * 0.5 + box.min.y);
        const double cz = static_cast<double>(dz) * 0.5 + box.min.z;
        core::at(mesh->centre_value(), 0) = cx;
        core::at(mesh->centre_value(), 1) = cy;
        core::at(mesh->centre_value(), 2) = static_cast<float>(cz);
        const double ex = static_cast<double>(box.max.x) - cx;
        const double ey = static_cast<double>(box.max.y) - cy;
        const double ez = static_cast<double>(box.max.z) - cz;
        mesh->set_radius(static_cast<float>(std::sqrt(ez * ez + ey * ey + ex * ex)));
    }
}

// 0x004B4160
void PcGeom::plat_get_render_colors(std::span<uint32_t> out)
{
    if (!meshes)
        return;
    size_t out_at = 0;
    for (uint32_t i = 0; i < mesh_count_; ++i) {
        MeshRef* mesh = core::at(mesh_run(), i);
        const gpu::VertexView vertices(get_lock(get_vertex_buffer(mesh, 0), kReadOnly), mesh->stride_value(), mesh->vertex_count());
        for (uint32_t v = 0; v < mesh->vertex_count(); ++v, ++out_at) {
            // (the vertex's BGRA bytes as an RGBA word)
            const auto color = vertices.value<std::array<uint8_t, 4>>(v, color_offset(mesh));
            core::at(out, out_at) = core::at(color, 2) | (uint32_t{core::at(color, 1)} << 8) |
                                    (uint32_t{core::at(color, 0)} << 16) | (uint32_t{core::at(color, 3)} << 24);
        }
        get_unlock(get_vertex_buffer(mesh, 0));
    }
}

// 0x004B4210
void PcGeom::change_colors(std::span<const uint32_t> in)
{
    if (!meshes)
        return;
    size_t in_at = 0;
    for (uint32_t i = 0; i < mesh_count_; ++i) {
        MeshRef* mesh = core::at(mesh_run(), i);
        const size_t mesh_start = in_at;
        for (int b = 0; std::cmp_less(b, mesh->vertex_buffer_count()); ++b) {
            in_at = mesh_start; // (every buffer of the mesh gets the same colours)
            const gpu::VertexWriter vertices(get_lock(get_vertex_buffer(mesh, b), 0), mesh->stride_value(), mesh->vertex_count());
            for (uint32_t v = 0; v < mesh->vertex_count(); ++v, ++in_at) {
                // (an RGBA word into the vertex's BGRA bytes)
                const uint32_t word = core::at(in, in_at);
                const std::array<uint8_t, 4> bytes = {static_cast<uint8_t>(word >> 16), static_cast<uint8_t>(word >> 8),
                                                      static_cast<uint8_t>(word), static_cast<uint8_t>(word >> 24)};
                vertices.set_value(v, color_offset(mesh), bytes);
            }
            get_unlock(get_vertex_buffer(mesh, b));
        }
    }
}

// 0x004B4300
void PcGeom::set_uv_wibble_params(float u_vel, float u_amp, float u_freq, float u_phase, float v_vel, float v_amp,
                                  float v_freq, float v_phase)
{
    if (!meshes)
        return;
    for (uint32_t i = 0; i < mesh_count_; ++i) {
        const MaterialRef* material = material_of(core::at(mesh_run(), i));
        if (!material)
            continue;
        for (uint32_t p = 0; p < material->pass_count(); ++p) {
            if (UvOscillator* wibble = core::at(material->uv_wibble(), p)) {
                core::at(wibble->velocity, 0) = u_vel;
                core::at(wibble->velocity, 1) = v_vel;
                core::at(wibble->amplitude, 0) = u_amp;
                core::at(wibble->amplitude, 1) = v_amp;
                core::at(wibble->frequency, 0) = u_freq;
                core::at(wibble->frequency, 1) = v_freq;
                core::at(wibble->phase, 0) = u_phase;
                core::at(wibble->phase, 1) = v_phase;
                break;
            }
        }
    }
}

// 0x004B4EF0
void PcGeom::enable_explicit_uv_wibble(int /*unused*/)
{
    if (!instance || !instance->scene)
        return;
    for (int i = 0; i < instance->scene->mesh_count(); ++i) {
        MaterialRef* material = material_of(core::at(scene_meshes(instance->scene), i));
        if (!material)
            continue;
        for (uint32_t p = 0; p < material->pass_count(); ++p) {
            if (core::at(material->uv_wibble(), p))
                core::at(material->pass_flags(), p) |= 0x4000;
        }
    }
}

// 0x004B43E0
void PcGeom::set_uv_wibble_offsets(float u, float v)
{
    if (!meshes)
        return;
    for (uint32_t i = 0; i < mesh_count_; ++i) {
        const MaterialRef* material = material_of(core::at(mesh_run(), i));
        if (!material)
            continue;
        for (uint32_t p = 0; p < material->pass_count(); ++p) {
            if (UvOscillator* wibble = core::at(material->uv_wibble(), p)) {
                core::at(wibble->offset, 0) = u;
                core::at(wibble->offset, 1) = v;
                enable_explicit_uv_wibble(1);
                break;
            }
        }
    }
}

// 0x004B4F60
bool PcGeom::choose_uv_matrix(uint32_t checksum, int pass, const maths::Mat4* matrix)
{
    if (!instance || !instance->scene)
        return true;
    for (int i = 0; i < instance->scene->mesh_count(); ++i) {
        MeshRef* mesh = core::at(scene_meshes(instance->scene), i);
        MaterialRef* material = material_of(mesh);
        if (!material)
            continue;
        const int index = pass_from_checksum(material, checksum, pass);
        if (index < 0 || ints::as_unsigned(index) >= material->pass_count())
            continue;
        if (!core::at(material->uv_wibble(), index)) {
            auto wibble = new_uv_wibble(); // (the material's: its passes' array owns them)
            core::at(material->uv_wibble(), index) = wibble;
            material->set_has_uv_wibble(true);
            core::at(material->pass_flags(), index) |= 0x4001;
        }
        // the vertex shader for the UV matrix (0x00775B70-78 -> 0x00775B58-60)
        uint32_t& shader = mesh_vertex_shader(mesh);
        if (shader == core::at(mesh_pass().vertex_shaders(), 5))
            shader = kVertexShaderUv60;
        else if (shader == core::at(mesh_pass().vertex_shaders(), 4))
            shader = kVertexShaderUv5c;
        else if (shader == core::at(mesh_pass().vertex_shaders(), 3))
            shader = kVertexShaderUv58;
        UvOscillator* wibble = core::at(material->uv_wibble(), index);
        core::at(wibble->matrix, 0) = core::at(matrix->row, 0).x;
        core::at(wibble->matrix, 1) = core::at(matrix->row, 0).y;
        core::at(wibble->offset, 0) = core::at(matrix->row, 3).x;
        core::at(wibble->offset, 1) = core::at(matrix->row, 3).y;
    }
    return true;
}

// 0x004B5130
bool PcGeom::create_uv_anim_params(uint32_t checksum, int pass)
{
    if (!instance || !instance->scene)
        return false;
    bool made = false;
    for (int i = 0; i < instance->scene->mesh_count(); ++i) {
        MaterialRef* material = material_of(core::at(scene_meshes(instance->scene), i));
        if (!material)
            continue;
        const int index = pass_from_checksum(material, checksum, pass);
        if (index < 0 || ints::as_unsigned(index) >= material->pass_count() || core::at(material->uv_wibble(), index))
            continue;
        auto wibble = new_uv_wibble(); // (the material's: its passes' array owns them)
        core::at(material->uv_wibble(), index) = wibble;
        material->set_has_uv_wibble(true);
        core::at(material->pass_flags(), index) |= 0x4001;
        made = true;
    }
    return made;
}

// 0x004B52A0
void PcGeom::reset_texture_anims()
{
    if (instance || !meshes)
        return;
    for (uint32_t i = 0; i < mesh_count_; ++i) {
        const MaterialRef* material = material_of(core::at(mesh_run(), i));
        if (material && material->texture_anim())
            material->texture_anim()->relaunch();
    }
}

} // namespace render
