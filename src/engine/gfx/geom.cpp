// The PC geom: see geom.h. The maths follows the original's x87 code (values kept in double precision where it keeps
// them in registers, rounded to float where it stores them).

#include "engine/gfx/geom.h"
#include "engine/gfx/d3dx.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_geom.h"
#include "engine/core/code_address.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/mesh_list.h"
#include "engine/gfx/pc_scene.h"
#include "engine/gfx/model.h"
#include "engine/core/stubs.h"
#include "engine/gfx/shader_slots.h"

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
uint8_t color_offset(const MeshRef* mesh) { return mesh->unknown1c_[0]; }
void mesh_position(MeshRef* mesh, maths::Vec4* out) { mesh->fetch_place(reinterpret_cast<float*>(out)); }
MaterialRef* material_of(MeshRef* mesh) { return mesh->material; }
// A mesh's colour (flag 4) and its vertex shader (its vertex format's place).
float* mesh_tint_ptr(MeshRef* mesh) { return mesh->color; }
uint32_t& mesh_vertex_shader(MeshRef* mesh) { return mesh->fvf; }

// The instance's meshes (its mesh list's).
MeshRef** scene_meshes(SceneRef* scene) { return reinterpret_cast<MeshRef**>(scene->meshes); }

// A new instance (0x64 bytes from the heap) of its mesh list, transform and bones.
Singleton* new_instance(SceneRef* scene, const maths::Mat4* transform, int bone_count, maths::Mat4* bones)
{
    void* memory = memory::alloc(sizeof(Singleton));
    if (!memory)
        return nullptr;
    return new (memory) Singleton(scene, transform, bone_count, bones);
}
// A new UV wibble (0x004D1A60, on 0x30 bytes from the heap).
UvOscillator* new_uv_wibble()
{
    void* memory = memory::alloc(sizeof(UvOscillator));
    return memory ? new (memory) UvOscillator : nullptr;
}

void get_identity(maths::Mat4* m)
{
    auto* f = reinterpret_cast<float*>(m);
    for (int i = 0; i < 16; ++i)
        f[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

// A byte colour channel as the float the meshes keep (x87: the integer times the float 1/255, rounded once).
float channel_to_float(uint32_t channel) { return static_cast<float>(static_cast<double>(channel) * kPer255); }
// And back (the C runtime's _ftol of the float times 255: truncated).
uint8_t float_to_channel(float value)
{
    return static_cast<uint8_t>(static_cast<int>(static_cast<double>(value) * 255.0));
}

maths::Vec4 g_static_position;
uint32_t g_static_position_guard;
maths::Mat4 g_static_orientation;
uint32_t g_static_orientation_guard;
uint32_t g_vertex_shader_uv58;
uint32_t g_vertex_shader_uv_5c;
uint32_t g_vertex_shader_uv60;

// The static results of get_absolute_point / fetch_facing (with their initialisation guards' bits).
maths::Vec4& static_position()
{
    g_static_position_guard |= 1;
    return g_static_position;
}
maths::Mat4& static_orientation()
{
    g_static_orientation_guard |= 1;
    return g_static_orientation;
}

} // namespace

// 0x004B4460
PcGeom::PcGeom()
{
    load_list_ = nullptr;
    meshes = nullptr;
    instance = nullptr;
    visibility = 0xDEADBEEF;
    scale = {1.0f, 1.0f, 1.0f, 1.0f};
    active = true;
}

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
    manager->enter_heap_scope(manager->top_down_heap_);
    load_list_ = ListNode::create_head();
    memory::Hub::get_pull()->leave_heap_scope();
}

// 0x004B52F0
void PcGeom::append_mesh(MeshRef* mesh)
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap_);
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
    auto is_head = [](const ListNode* node) { return node->data == reinterpret_cast<void*>(-1); };
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
                meshes[i] = static_cast<MeshRef*>(node->data);
                ListNode::destroy(node); // (0x004B5E50: unlinked, freed)
                if (vertex_arena)
                    meshes[i]->move_into_arena(static_cast<VertexArena*>(vertex_arena));
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
        if (auto* list = *reinterpret_cast<SceneRef**>(scene + 0x280))
            list->detach_meshes(static_cast<int>(mesh_count_), meshes);
    }
    for (uint32_t i = 0; i < mesh_count_; ++i) {
        if (MeshRef* mesh = meshes[i]) {
            mesh->~MeshRef(); // (the destructor's body)
            memory::Hub::get_pull()->dealloc(mesh);
        }
    }
    memory::Hub::get_pull()->dealloc(meshes);
    meshes = nullptr;
}

// 0x004B44D0
bool PcGeom::add_to_mesh_list(bool count)
{
    auto* list = *reinterpret_cast<SceneRef**>(scene + 0x280);
    if (count)
        list->mesh_total(static_cast<int>(mesh_count_), meshes);
    else
        list->append_meshes(static_cast<int>(mesh_count_), meshes);
    return true;
}

// 0x004B4510
SceneRef* PcGeom::create_mesh_list()
{
    void* memory = memory::alloc(sizeof(SceneRef));
    SceneRef* list = memory ? new (memory) SceneRef : nullptr;
    list->mesh_total(static_cast<int>(mesh_count_), meshes);
    list->allocate_mesh_list();
    list->append_meshes(static_cast<int>(mesh_count_), meshes);
    list->order_meshes();
    std::memcpy(&list->bounds, &bounds, sizeof(bounds));
    list->update_sphere();
    return list;
}

// 0x004B58C0
Geom* PcGeom::plat_replicate(int instance_flag, void* scene_bytes)
{
    bool share = static_cast<uint8_t>(instance_flag) != 0;
    auto* new_scene = static_cast<uint8_t*>(scene_bytes);
    auto* geom = new PcGeom;
    // (no check: a failed allocation writes through null, as the original)
    geom->meshes = static_cast<MeshRef**>(memory::alloc(4 * mesh_count_));
    geom->mesh_count_ = mesh_count_;
    for (uint32_t i = 0; i < geom->mesh_count_; ++i)
        geom->meshes[i] = meshes[i]->replicate(share);
    if (!share) {
        geom->change_live(true);
        // into the scene's mesh list: its meshes kept aside, room made for these ones too, all added back
        auto* list = *reinterpret_cast<SceneRef**>(new_scene + 0x280);
        MeshRef** kept = list->mesh_count_ > 0 ? static_cast<MeshRef**>(memory::alloc(4 * list->mesh_count_)) : nullptr;
        geom->scene = new_scene;
        for (int i = 0; i < list->mesh_count_; ++i)
            kept[i] = list->meshes[i];
        int kept_count = list->mesh_count_;
        memory::Hub::get_pull()->dealloc(list->meshes);
        list->meshes = nullptr;
        list->mesh_total(static_cast<int>(mesh_count_), meshes); // (this one's)
        list->allocate_mesh_list();
        for (int i = 0; i < kept_count; ++i)
            list->meshes[i] = kept[i];
        memory::Hub::get_pull()->dealloc(kept);
        list->append_meshes(static_cast<int>(geom->mesh_count_), geom->meshes);
        list->order_meshes();
    } else {
        std::memcpy(&geom->bounds, &bounds, sizeof(bounds));
        SceneRef* list = geom->create_mesh_list();
        geom->change_live(true);
        maths::Mat4 identity;
        get_identity(&identity);
        Singleton* inst = new_instance(list, &identity, 1, nullptr);
        inst->flags |= 1;
        geom->instance = inst;
    }
    return geom;
}

// 0x004B5B80
Geom* PcGeom::clone_instance(int instance_flag, void* model_bytes)
{
    bool share = static_cast<uint8_t>(instance_flag) != 0;
    auto* model = static_cast<uint8_t*>(model_bytes);
    auto scene_owner = std::make_unique<PcScene>(10); // (only for the copy)
    auto* temp_scene = reinterpret_cast<uint8_t*>(scene_owner.get());
    SceneRef* source_list = instance->scene;
    *reinterpret_cast<SceneRef**>(temp_scene + 0x280) = source_list;
    auto* geom = new PcGeom;
    geom->scene = temp_scene;
    auto count = static_cast<uint32_t>(instance->scene->mesh_count_);
    geom->meshes = count ? static_cast<MeshRef**>(memory::alloc(4 * count)) : nullptr;
    geom->mesh_count_ = count;
    for (uint32_t i = 0; i < geom->mesh_count_; ++i)
        geom->meshes[i] = scene_meshes(instance->scene)[i]->replicate(share);
    std::memcpy(&geom->bounds, &bounds, sizeof(bounds));
    SceneRef* list = geom->create_mesh_list();
    // the scene was only for the copy: destroyed without its mesh list
    *reinterpret_cast<SceneRef**>(temp_scene + 0x280) = nullptr;
    scene_owner.reset();
    geom->scene = nullptr;
    geom->change_live(true);
    // bones: the model's count (room for one more), each the identity
    int bone_count = *reinterpret_cast<int*>(model + 8);
    auto* bones = static_cast<maths::Mat4*>(memory::alloc((bone_count + 1) * sizeof(maths::Mat4)));
    for (int i = 0; i < bone_count; ++i)
        get_identity(&bones[i]);
    maths::Mat4 identity;
    get_identity(&identity);
    Singleton* inst = new_instance(list, &identity, bone_count, bones);
    inst->flags |= 1;
    geom->instance = inst;
    return geom;
}

// 0x004B54B0
bool PcGeom::plat_init_instance(void* mesh_bytes, void* model_bytes, int /*unused*/)
{
    auto* mesh = static_cast<uint8_t*>(mesh_bytes);
    auto* model = static_cast<uint8_t*>(model_bytes);
    int bone_count = *reinterpret_cast<int*>(model + 8);
    source = mesh;
    maths::Mat4 identity;
    get_identity(&identity);
    void* memory = memory::alloc(sizeof(Singleton));
    Singleton* inst = nullptr;
    if (memory) {
        auto* list = *reinterpret_cast<SceneRef**>(*reinterpret_cast<uint8_t**>(mesh + 0xC4) + 0x280);
        auto* bones = *reinterpret_cast<maths::Mat4**>(model + 0xC); // (the model's bones: 0x00492E50)
        inst = new (memory) Singleton(list, &identity, bone_count, bones);
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
    for (int i = 0; i < list->mesh_count_; ++i)
        scene_meshes(list)[i]->clean_strip();
}

// 0x004B48B0
void PcGeom::change_tint(uint32_t rgba)
{
    uint8_t r = rgba & 0xFF, g = (rgba >> 8) & 0xFF, b = (rgba >> 16) & 0xFF;
    if ((r == 0x80 || r == 0x81) && g == 0x80 && b == 0x80) {
        wipe_tint();
        return;
    }
    if (instance) {
        instance->flags |= 4;
        std::memcpy(instance->color, &rgba, 4);
        return;
    }
    if (!meshes)
        return;
    float red = channel_to_float(r), green = channel_to_float(g), blue = channel_to_float(b);
    for (uint32_t i = 0; i < mesh_count_; ++i) {
        MeshRef* mesh = meshes[i];
        mesh_tint_ptr(mesh)[0] = red;
        mesh_tint_ptr(mesh)[1] = green;
        mesh->flags |= 4;
        mesh_tint_ptr(mesh)[2] = blue;
    }
}

// 0x004B4B20
uint32_t* PcGeom::plat_get_color(uint32_t* out)
{
    uint8_t color[4] = {0, 0, 0, 0};
    MeshRef* mesh = nullptr;
    if (instance) {
        if (instance->scene->mesh_count_ > 0)
            mesh = scene_meshes(instance->scene)[0];
    } else if (meshes) {
        mesh = meshes[0];
    }
    if (mesh && (mesh->flags & 4)) {
        color[0] = float_to_channel(mesh_tint_ptr(mesh)[0]);
        color[1] = float_to_channel(mesh_tint_ptr(mesh)[1]);
        color[2] = float_to_channel(mesh_tint_ptr(mesh)[2]);
    }
    std::memcpy(out, color, 4);
    return out;
}

// 0x004B4970
void PcGeom::wipe_tint()
{
    if (instance) {
        std::memset(instance->color, 0x80, 4);
        instance->flags &= ~4u;
        return;
    }
    if (!meshes)
        return;
    for (uint32_t i = 0; i < mesh_count_; ++i)
        meshes[i]->flags &= ~4u;
}

// 0x004B49C0
bool PcGeom::change_material_tint(uint32_t checksum, int pass, uint32_t rgba)
{
    bool found = false;
    if (!instance || !instance->scene)
        return false;
    for (int i = 0; i < instance->scene->mesh_count_; ++i) {
        MaterialRef* material = material_of(scene_meshes(instance->scene)[i]);
        if (!material)
            continue;
        int index = pass_from_checksum(material, checksum, pass);
        if (index < 0 || static_cast<uint32_t>(index) >= material->pass_count_)
            continue;
        if (static_cast<int8_t>(material->pass_flags_[index] & 0xFF) < 0) // (the colour is fixed)
            continue;
        found = true;
        material->pass_color_[index][0] = channel_to_float(rgba & 0xFF);
        material->pass_color_[index][1] = channel_to_float((rgba >> 8) & 0xFF);
        material->pass_color_[index][2] = channel_to_float((rgba >> 16) & 0xFF);
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
        meshes[i]->viewports = static_cast<uint8_t>(mask);
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
            meshes[i]->flags |= 0x40;
        else
            meshes[i]->flags &= ~0x40u;
    }
}

// 0x004B4690
bool PcGeom::is_operative() { return instance ? instance->active : active; }

// 0x004B4730
void PcGeom::change_bound_sphere(const maths::Vec4* sphere)
{
    if (instance && instance->scene) {
        SceneRef* list = instance->scene;
        std::memcpy(list->centre, sphere, 12);
        std::memcpy(&list->radius, &sphere->w, 4);
    }
}

// 0x004B4760
maths::Vec4* PcGeom::plat_get_bounding_sphere(maths::Vec4* out)
{
    if (instance && instance->scene) {
        SceneRef* list = instance->scene;
        *out = {list->centre[0], list->centre[1], list->centre[2], list->radius};
    } else {
        *out = {0.0f, 0.0f, 0.0f, 10000.0f};
    }
    return out;
}

// 0x004B55D0
void PcGeom::place_at_absolute_point(const maths::Vec4* position)
{
    maths::Vec4 pos = {position->x, position->y, position->z, 1.0f};
    if (instance) {
        instance->transform.row[3] = pos;
        return;
    }
    for (uint32_t i = 0; i < mesh_count_; ++i)
        meshes[i]->move_to(&pos);
}

// 0x004B46A0
maths::Vec4* PcGeom::get_absolute_point()
{
    maths::Vec4& pos = static_position();
    if (instance) {
        pos = instance->transform.row[3];
        return &pos;
    }
    pos = {0.0f, 0.0f, 0.0f, 1.0f};
    if (meshes[0]) // (no check of the array itself, as the original)
        mesh_position(meshes[0], &pos);
    return &pos;
}

// 0x004B5650
void PcGeom::change_facing(const maths::Mat4* orientation)
{
    if (!instance)
        return;
    maths::Mat4 m;
    m = instance->transform;
    m.row[0] = orientation->row[0];
    m.row[1] = orientation->row[1];
    m.row[2] = orientation->row[2];
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
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    return &m;
}

// 0x004B57C0
void PcGeom::rotate_y(int turns)
{
    if (!turns)
        return;
    if (!instance) {
        for (uint32_t i = 0; i < mesh_count_; ++i)
            meshes[i]->turn_y(turns);
        return;
    }
    maths::Mat4 m;
    m = instance->transform;
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    maths::Mat4 rotation;
    maths::make_yaw_matrix(rotation, static_cast<float>(static_cast<double>(turns) * kQuarterTurn));
    // D3DX (linked library code): rotation = m * rotation
    d3dx::matrix_multiply(&rotation, &m, &rotation);
    rotation.row[3] = instance->transform.row[3];
    instance->transform = rotation;
}

// 0x004B4DB0
void PcGeom::change_scale(const maths::Vec4* new_scale)
{
    scale = *new_scale;
    if (!meshes)
        return;
    for (uint32_t i = 0; i < mesh_count_; ++i) {
        MeshRef* mesh = meshes[i];
        maths::Vec4 centre = {0.0f, 0.0f, 0.0f, 1.0f};
        mesh_position(mesh, &centre);
        VertexBuffer* buffer = get_vertex_buffer(mesh, 0);
        uint8_t* vertex = get_lock(buffer, 0);
        for (uint32_t v = 0; v < mesh->vertex_count_; ++v, vertex += mesh->stride) {
            auto* p = reinterpret_cast<float*>(vertex);
            for (int k = 0; k < 3; ++k) {
                double c = (&centre.x)[k];
                p[k] = static_cast<float>((static_cast<double>(p[k]) - c) * (&new_scale->x)[k] + c);
            }
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
        instance->scene->break_strips(mask, *reinterpret_cast<uint32_t**>(source + 0xBC),
                                      *reinterpret_cast<uint32_t*>(source + 0xC0));
    return true;
}

// 0x004B4130
uint32_t PcGeom::fetch_vertex_tally()
{
    uint32_t total = 0;
    if (meshes) {
        for (uint32_t i = 0; i < mesh_count_; ++i)
            total += meshes[i]->vertex_count_;
    }
    return total;
}

// 0x004B4BB0
void PcGeom::get_vertices(maths::Vec4* out)
{
    if (!meshes)
        return;
    for (uint32_t i = 0; i < mesh_count_; ++i) {
        MeshRef* mesh = meshes[i];
        uint8_t* vertex = get_lock(get_vertex_buffer(mesh, 0), kReadOnly);
        for (uint32_t v = 0; v < mesh->vertex_count_; ++v, vertex += mesh->stride, ++out) {
            auto* p = reinterpret_cast<float*>(vertex);
            *out = {p[0], p[1], p[2], 1.0f};
        }
        get_unlock(get_vertex_buffer(mesh, 0));
    }
}

// 0x004B4C50
void PcGeom::set_vertices(const maths::Vec4* in)
{
    if (!meshes)
        return;
    for (uint32_t i = 0; i < mesh_count_; ++i) {
        MeshRef* mesh = meshes[i];
        uint8_t* vertex = get_lock(get_vertex_buffer(mesh, 0), 0);
        maths::BBox box;
        for (uint32_t v = 0; v < mesh->vertex_count_; ++v, vertex += mesh->stride, ++in) {
            std::memcpy(vertex, in, 12);
            box.append_spot(*in);
        }
        get_unlock(get_vertex_buffer(mesh, 0));
        // the sphere around the box (x87: the half sizes and the centre's z kept in double where the original does)
        double dx = static_cast<double>(box.max.x) - box.min.x;
        double dy = static_cast<double>(box.max.y) - box.min.y;
        auto dz = static_cast<float>(static_cast<double>(box.max.z) - box.min.z);
        auto half_x = static_cast<float>(dx * 0.5);
        auto cx = static_cast<float>(static_cast<double>(box.min.x) + half_x);
        auto cy = static_cast<float>(dy * 0.5 + box.min.y);
        double cz = static_cast<double>(dz) * 0.5 + box.min.z;
        mesh->centre[0] = cx;
        mesh->centre[1] = cy;
        mesh->centre[2] = static_cast<float>(cz);
        double ex = static_cast<double>(box.max.x) - cx;
        double ey = static_cast<double>(box.max.y) - cy;
        double ez = static_cast<double>(box.max.z) - cz;
        mesh->radius = static_cast<float>(std::sqrt(ez * ez + ey * ey + ex * ex));
    }
}

// 0x004B4160
void PcGeom::plat_get_render_colors(uint8_t* out)
{
    if (!meshes)
        return;
    for (uint32_t i = 0; i < mesh_count_; ++i) {
        MeshRef* mesh = meshes[i];
        uint8_t* color = get_lock(get_vertex_buffer(mesh, 0), kReadOnly) + color_offset(mesh);
        for (uint32_t v = 0; v < mesh->vertex_count_; ++v, color += mesh->stride, out += 4) {
            out[0] = color[2];
            out[1] = color[1];
            out[2] = color[0];
            out[3] = color[3];
        }
        get_unlock(get_vertex_buffer(mesh, 0));
    }
}

// 0x004B4210
void PcGeom::change_colors(const uint8_t* in)
{
    if (!meshes)
        return;
    for (uint32_t i = 0; i < mesh_count_; ++i) {
        MeshRef* mesh = meshes[i];
        const uint8_t* mesh_colors = in;
        for (int b = 0; b < mesh->vertex_buffer_count_; ++b) {
            in = mesh_colors;
            uint8_t* color = get_lock(get_vertex_buffer(mesh, b), 0) + color_offset(mesh);
            for (uint32_t v = 0; v < mesh->vertex_count_; ++v, color += mesh->stride, in += 4) {
                color[2] = in[0];
                color[1] = in[1];
                color[0] = in[2];
                color[3] = in[3];
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
        MaterialRef* material = material_of(meshes[i]);
        if (!material)
            continue;
        for (uint32_t p = 0; p < material->pass_count_; ++p) {
            if (UvOscillator* wibble = material->uv_wibble_[p]) {
                wibble->velocity[0] = u_vel;
                wibble->velocity[1] = v_vel;
                wibble->amplitude[0] = u_amp;
                wibble->amplitude[1] = v_amp;
                wibble->frequency[0] = u_freq;
                wibble->frequency[1] = v_freq;
                wibble->phase[0] = u_phase;
                wibble->phase[1] = v_phase;
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
    for (int i = 0; i < instance->scene->mesh_count_; ++i) {
        MaterialRef* material = material_of(scene_meshes(instance->scene)[i]);
        if (!material)
            continue;
        for (uint32_t p = 0; p < material->pass_count_; ++p) {
            if (material->uv_wibble_[p])
                material->pass_flags_[p] |= 0x4000;
        }
    }
}

// 0x004B43E0
void PcGeom::set_uv_wibble_offsets(float u, float v)
{
    if (!meshes)
        return;
    for (uint32_t i = 0; i < mesh_count_; ++i) {
        MaterialRef* material = material_of(meshes[i]);
        if (!material)
            continue;
        for (uint32_t p = 0; p < material->pass_count_; ++p) {
            if (UvOscillator* wibble = material->uv_wibble_[p]) {
                wibble->offset[0] = u;
                wibble->offset[1] = v;
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
    for (int i = 0; i < instance->scene->mesh_count_; ++i) {
        MeshRef* mesh = scene_meshes(instance->scene)[i];
        MaterialRef* material = material_of(mesh);
        if (!material)
            continue;
        int index = pass_from_checksum(material, checksum, pass);
        if (index < 0 || static_cast<uint32_t>(index) >= material->pass_count_)
            continue;
        if (!material->uv_wibble_[index]) {
            material->uv_wibble_[index] = new_uv_wibble();
            material->has_uv_wibble_ = true;
            material->pass_flags_[index] |= 0x4001;
        }
        // the vertex shader for the UV matrix (0x00775B70-78 -> 0x00775B58-60)
        uint32_t& shader = mesh_vertex_shader(mesh);
        if (shader == g_vertex_shaders[5])
            shader = g_vertex_shader_uv60;
        else if (shader == g_vertex_shaders[4])
            shader = g_vertex_shader_uv_5c;
        else if (shader == g_vertex_shaders[3])
            shader = g_vertex_shader_uv58;
        UvOscillator* wibble = material->uv_wibble_[index];
        wibble->matrix[0] = matrix->row[0].x;
        wibble->matrix[1] = matrix->row[0].y;
        wibble->offset[0] = matrix->row[3].x;
        wibble->offset[1] = matrix->row[3].y;
    }
    return true;
}

// 0x004B5130
bool PcGeom::create_uv_anim_params(uint32_t checksum, int pass)
{
    if (!instance || !instance->scene)
        return false;
    bool made = false;
    for (int i = 0; i < instance->scene->mesh_count_; ++i) {
        MaterialRef* material = material_of(scene_meshes(instance->scene)[i]);
        if (!material)
            continue;
        int index = pass_from_checksum(material, checksum, pass);
        if (index < 0 || static_cast<uint32_t>(index) >= material->pass_count_ || material->uv_wibble_[index])
            continue;
        material->uv_wibble_[index] = new_uv_wibble();
        material->has_uv_wibble_ = true;
        material->pass_flags_[index] |= 0x4001;
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
        MaterialRef* material = material_of(meshes[i]);
        if (material && material->texture_anim_)
            material->texture_anim_->relaunch();
    }
}

} // namespace render
