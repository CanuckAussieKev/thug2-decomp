// The PC billboards: see pc_billboards.h. The maths follows the original's x87 code: double precision where it keeps
// values in registers, floats where it stores them.

#include "engine/gfx/pc_billboards.h"
#include "engine/gfx/d3dx.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/settings.h"
#include "engine/gfx/window2d.h"
#include "engine/core/code_address.h"
#include "engine/gfx/device.h"
#include "platform/crt.h"

namespace render {

static bool g_billboards_ready;
BillboardSet g_billboard_pool;
uint8_t g_billboard_flag;

static float g_billboard_constants[12]{0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};

namespace {

IDirect3DDevice9* input_port() { return g_d3d_device; }
bool vertex_shaders() { return g_shaders_available; }
void toggle_fog(bool on) { engine::enable_fog(on); }
VertexBuffer* get_vertex_buffer(MeshRef* mesh, int index) { return mesh->fetch_vertex_store(index); }

bool is_head(const memory::ListNode* node) { return node->data == reinterpret_cast<void*>(-1); }
// The node after `node` (null at the head).
memory::ListNode* get_next_node(const memory::ListNode* node) { return is_head(node->next) ? nullptr : node->next; }
// A list's first node (null when empty).
memory::ListNode* head_of_scene_list(const memory::ListNode* head)
{
    return is_head(head->next) ? nullptr : head->next;
}

memory::ListNode* new_head() { return memory::ListNode::create_head(); }

void order_batches(BillboardSet* manager)
{
    crt::get_qsort(manager->batches, static_cast<uint32_t>(manager->count), 4,
                   compare_batches); // (the C runtime's qsort)
}

float sort_depth_of_batch(const BillboardBatch* batch) { return batch->material ? batch->material->sort_key_ : 0.0f; }

void draw_list(const memory::ListNode* head, const float* y_axis, const float* x_axis, const float* z_axis)
{
    for (const memory::ListNode* node = head_of_scene_list(head); node;) {
        const memory::ListNode* next = get_next_node(node);
        MeshRef* mesh = static_cast<BillboardRec*>(node->data)->mesh;
        if (mesh->flags & 0x40)
            draw_billboard_mesh(mesh, y_axis, x_axis, z_axis);
        node = next;
    }
}

} // namespace

// 0x004CD9D0
int compare_batches(const void* a, const void* b)
{
    const auto* first = *static_cast<BillboardBatch* const*>(a);
    const auto* second = *static_cast<BillboardBatch* const*>(b);
    if (!first)
        return second != nullptr;
    if (!second)
        return -1;
    if (sort_depth_of_batch(first) < sort_depth_of_batch(second))
        return -1;
    return sort_depth_of_batch(first) > sort_depth_of_batch(second) ? 1 : 0;
}

// 0x004CD980
uint32_t primitive_count(uint32_t type, uint32_t count)
{
    switch (type) {
    case 1:
        return count;
    case 2:
        return count >> 1;
    case 3:
        return count - 1;
    case 4:
        return count / 3;
    case 5:
    case 6:
        return count - 2;
    default:
        return 0;
    }
}

// 0x004CDD00
void draw_billboard_mesh(MeshRef* mesh, const float* y_axis, const float* x_axis, const float* z_axis)
{
    if (mesh->stride < 0x10 || mesh->stride > 0x30)
        return;
    if (g_distance_clipping && g_fog_setting) { // (fog: its start a quarter back)
        float end = g_camera.far_z_;
        reinterpret_cast<uint32_t&>(g_fog_begin_distance) = 0;
        reinterpret_cast<uint32_t&>(g_fog_end) = 0;
        auto start = static_cast<float>(end - (static_cast<double>(end) - g_camera.near_z_) * 0.25f);
        toggle_fog(true);
        input_port()->SetRenderState(D3DRS_FOGSTART, *reinterpret_cast<DWORD*>(&start));
        input_port()->SetRenderState(D3DRS_FOGEND, *reinterpret_cast<DWORD*>(&end));
    } else {
        toggle_fog(false);
    }
    if (vertex_shaders()) {
        if (mesh->flags & 4)
            mesh->set_pixel_shader_colored();
        else
            set_pixel_shader(mesh->pixel_shader_, nullptr);
    }
    D3DVIEWPORT9 viewport;
    input_port()->GetViewport(&viewport);
    IDirect3DVertexBuffer9* stream = nullptr;
    UINT offset = 0, stream_stride = 0;
    input_port()->GetStreamSource(0, &stream, &offset, &stream_stride);
    IDirect3DPixelShader9* pixel_shader = nullptr;
    input_port()->GetPixelShader(&pixel_shader);
    IDirect3DVertexShader9* vertex_shader = nullptr;
    input_port()->GetVertexShader(&vertex_shader);
    IDirect3DIndexBuffer9* indices = nullptr;
    input_port()->GetIndices(&indices);
    const uint32_t primitives = primitive_count(mesh->primitive_type_, mesh->index_counts_[0]);
    uint8_t* into = nullptr;
    get_vertex_buffer(mesh, 0)->get_lock(0, 0, &into, D3DLOCK_DISCARD);
    const uint8_t* from = get_vertex_buffer(mesh, 0)->data;
    const float* pivot = mesh->billboard->pivot;
    const float px = pivot[0];
    const float py = pivot[1];
    const float pz = pivot[2];
    uint32_t rest = mesh->stride - 0xC;
    const uint32_t* table = nullptr;
    if (mesh->per_vertex_bytes_) { // (a value per vertex from the material's table)
        table = mesh->material->anim_colors_;
        rest -= 4;
    }
    for (uint32_t i = 0; i < mesh->vertex_count_; ++i) {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        std::memcpy(&x, from, 4);
        std::memcpy(&y, from + 4, 4);
        std::memcpy(&z, from + 8, 4);
        from += 0xC;
        auto a = static_cast<float>(static_cast<double>(x) * x_axis[0]);
        auto b = static_cast<float>(static_cast<double>(x) * x_axis[1]);
        auto c = static_cast<float>(static_cast<double>(x) * x_axis[2]);
        auto t2 = static_cast<float>(static_cast<double>(y) * y_axis[2]);
        a = static_cast<float>(static_cast<double>(y) * y_axis[0] + a);
        const double sum_y = static_cast<double>(y) * y_axis[1] + b;
        const double sum_z = static_cast<double>(c) + t2;
        auto t3 = static_cast<float>(static_cast<double>(z) * z_axis[2]);
        a = static_cast<float>(static_cast<double>(z) * z_axis[0] + a);
        auto b2 = static_cast<float>(static_cast<double>(z) * z_axis[1] + sum_y);
        const double c2 = t3 + sum_z;
        float out[3] = {static_cast<float>(static_cast<double>(px) + a),
                        static_cast<float>(static_cast<double>(py) + b2), static_cast<float>(pz + c2)};
        std::memcpy(into, out, 0xC);
        into += 0xC;
        if (table) {
            auto index = static_cast<uint32_t>(static_cast<int8_t>(mesh->per_vertex_bytes_[i]));
            uint32_t value = 0;
            if (index)
                value = table[index - 1];
            else
                std::memcpy(&value, from, 4);
            std::memcpy(into, &value, 4);
            from += 4;
            into += 4;
        }
        std::memcpy(into, from, rest);
        into += rest;
        from += rest;
    }
    get_vertex_buffer(mesh, 0)->get_unlock();
    input_port()->SetStreamSource(0, static_cast<IDirect3DVertexBuffer9*>(get_vertex_buffer(mesh, 0)->buffer), 0,
                                  mesh->stride);
    // (the index buffer's Direct3D buffer, +0xC: through the getter 0x00492E50 folded with others)
    input_port()->SetIndices(static_cast<IDirect3DIndexBuffer9*>(mesh->index_buffers_[0]->buffer));
    DWORD format = 0;
    input_port()->GetFVF(&format);
    input_port()->SetFVF(mesh->fvf);
    auto type = static_cast<D3DPRIMITIVETYPE>(mesh->primitive_type_);
    if (vertex_shaders()) {
        input_port()->DrawIndexedPrimitive(type, 0, 0, mesh->vertex_count_, 0, primitives);
    } else {
        for (uint32_t pass = 0; pass < mesh->material->pass_count_; ++pass) {
            mesh->material->submit_pass(pass, mesh->flags & 4 ? mesh->color : nullptr);
            input_port()->DrawIndexedPrimitive(type, 0, 0, mesh->vertex_count_, 0, primitives);
        }
    }
    input_port()->SetFVF(format);
    input_port()->SetStreamSource(0, stream, 0, stream_stride); // (the offset not put back, as the original)
    if (stream)
        stream->Release();
    input_port()->SetPixelShader(pixel_shader); // (the shaders not released, as the original)
    input_port()->SetVertexShader(vertex_shader);
    input_port()->SetIndices(indices);
    if (indices)
        indices->Release();
    toggle_fog(false);
    (void)viewport;
}

// 0x004CDA60
BillboardBatch* BillboardSet::get_find(const MaterialRef* material)
{
    for (int i = 0; i < count; ++i) {
        if (batches[i]->material == material)
            return batches[i];
    }
    return nullptr;
}

// 0x004CDAA0
void BillboardSet::setup_axes()
{
    const auto* m = reinterpret_cast<float*>(&g_view_matrix);
    const float vz = m[10];
    view[0] = m[2];
    view[1] = m[6];
    view[3] = 1.0f;
    view[2] = vz;
    // right = forward x (0, 1, 0)
    auto zero = static_cast<float>(static_cast<double>(view[1]) * 0.0f);
    double rx = zero - static_cast<double>(view[2]);
    double ry = static_cast<double>(view[2]) * 0.0f - static_cast<double>(view[0]) * 0.0f;
    auto rz = static_cast<float>(static_cast<double>(view[0]) - zero);
    {
        const double length = std::sqrt(static_cast<double>(rz) * rz + ry * ry + rx * rx);
        if (length > 0.0f) {
            const double inverse = 1.0f / length;
            rx *= inverse;
            ry *= inverse;
            rz = static_cast<float>(rz * inverse);
        }
    }
    right[2] = rz;
    right[0] = static_cast<float>(rx);
    right[3] = 0.0f;
    right[1] = static_cast<float>(ry);
    // up = forward x right... as the original orders it
    double ux = static_cast<double>(view[2]) * right[1] - static_cast<double>(view[1]) * right[2];
    double uy = static_cast<double>(right[2]) * view[0] - static_cast<double>(view[2]) * right[0];
    const double uz_wide = static_cast<double>(view[1]) * right[0] - static_cast<double>(right[1]) * view[0];
    auto uz = static_cast<float>(uz_wide);
    {
        const double length = std::sqrt(uz_wide * uz + uy * uy + ux * ux); // (the unrounded one times the stored one)
        if (length > 0.0f) {
            const double inverse = 1.0f / length;
            ux *= inverse;
            uy *= inverse;
            uz = static_cast<float>(uz * inverse);
        }
    }
    up[2] = uz;
    up[0] = static_cast<float>(ux);
    up[3] = 0.0f;
    up[1] = static_cast<float>(uy);
    // the forward and right along the ground
    auto flatten = [](float x, float z, float* out) {
        double fx = x, fz = z;
        float y = 0.0f;
        const double length = std::sqrt(fz * fz + fx * fx);
        if (length > 0.0f) {
            const double inverse = 1.0f / length;
            fx *= inverse;
            y = static_cast<float>(0.0f * inverse);
            fz *= inverse;
        }
        out[0] = static_cast<float>(fx);
        out[1] = y;
        out[3] = 1.0f;
        out[2] = static_cast<float>(fz);
    };
    flatten(view[0], view[2], flat_forward_);
    flatten(right[0], right[2], flat_right_);
}

// 0x004CEB30
BillboardBatch::BillboardBatch(MaterialRef* new_material) : material(new_material)
{
    lists[0] = new_head();
    lists[1] = new_head();
    lists[2] = new_head();
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set twice, as the original does
    vertex_bytes_ = 0x40;
    vertex_bytes_ = (new_material->pass_count_ + 2) * 0x20;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x004CE1D0
void BillboardBatch::add_row(MeshRef* mesh)
{
    auto* entry = static_cast<BillboardRec*>(memory::alloc(sizeof(BillboardRec)));
    if (entry) {
        entry->mesh = mesh;
        entry->type = mesh->billboard->type;
    }
    memory::ListNode* node = memory::ListNode::create(entry);
    // at the end of its list (no checks: written through null, as the original)
    memory::ListNode* head = lists[entry->type];
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}

// 0x004CE250
void BillboardBatch::erase_record(MeshRef* mesh)
{
    for (const memory::ListNode* head : lists) {
        memory::ListNode* node = head->next;
        if (is_head(node))
            continue;
        while (node) {
            auto* entry = static_cast<BillboardRec*>(node->data);
            memory::ListNode* next = get_next_node(node);
            if (entry->mesh == mesh) {
                memory::ListNode::destroy(node); // (0x004CE960)
                memory::Hub::get_pull()->dealloc(entry);
                return;
            }
            node = next;
        }
    }
}

// 0x004CE2D0
void BillboardBatch::render()
{
    if (!material)
        return;
    if (vertex_shaders())
        material->hand_in();
    else
        material->submit_states();
    for (auto& list : lists) { // (the first shown list's first mesh's pixel shader)
        const memory::ListNode* first = list->next;
        if (is_head(first) || !first)
            continue;
        const memory::ListNode* node = head_of_scene_list(list);
        if (vertex_shaders())
            set_pixel_shader(static_cast<BillboardRec*>(node->data)->mesh->pixel_shader_, nullptr);
        break;
    }
    auto transpose = d3dx::matrix_transpose; // (D3DX)
    auto multiply = d3dx::matrix_multiply;
    float projection[16], view[16], identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float view_world[16], all[16];
    transpose(projection, reinterpret_cast<float*>(&g_projection_matrix));
    transpose(view, reinterpret_cast<float*>(&g_view_matrix));
    view[12] = view[13] = view[14] = 0.0f; // (its last row cleared)
    view[15] = 1.0f;
    multiply(view_world, view, identity);
    multiply(all, projection, view_world);
    input_port()->SetVertexShaderConstantF(0, all, 4);
    auto* manager = &g_billboard_pool;
    float* constants = g_billboard_constants; // c4-c6
    auto set_axes = [&](const float* x, const float* y, const float* z) {
        std::memcpy(constants, x, 12);
        std::memcpy(constants + 4, y, 12);
        std::memcpy(constants + 8, z, 12);
    };
    set_axes(manager->right, manager->up, manager->view);
    input_port()->SetVertexShaderConstantF(4, constants, 3);
    {
        float x_axis[3], y_axis[3], z_axis[3];
        std::memcpy(x_axis, constants, 12);
        std::memcpy(y_axis, constants + 4, 12);
        std::memcpy(z_axis, constants + 8, 12);
        draw_list(lists[0], y_axis, x_axis, z_axis);
    }
    {
        const float flat_up[3] = {0.0f, 1.0f, 0.0f};
        set_axes(manager->flat_right_, flat_up, manager->flat_forward_);
        float x_axis[3], y_axis[3] = {0.0f, 1.0f, 0.0f}, z_axis[3];
        std::memcpy(x_axis, manager->flat_right_, 12);
        std::memcpy(z_axis, manager->flat_forward_, 12);
        draw_list(lists[1], y_axis, x_axis, z_axis);
    }
    // About each mesh's own axis: across = forward x axis, out = across x axis, both normalised.
    for (const memory::ListNode* node = head_of_scene_list(lists[2]); node;) {
        const memory::ListNode* next = get_next_node(node);
        MeshRef* mesh = static_cast<BillboardRec*>(node->data)->mesh;
        if (mesh->flags & 0x40) {
            const float* axis = mesh->billboard->axis;
            const float* d = manager->view;
            double rx = static_cast<double>(d[1]) * axis[2] - static_cast<double>(d[2]) * axis[1];
            auto ry = static_cast<float>(static_cast<double>(d[2]) * axis[0] - static_cast<double>(d[0]) * axis[2]);
            auto rz = static_cast<float>(static_cast<double>(d[0]) * axis[1] - static_cast<double>(d[1]) * axis[0]);
            const double length = std::sqrt(rx * rx + static_cast<double>(rz) * rz + static_cast<double>(ry) * ry);
            if (length > 0.0f) {
                const double inverse = 1.0f / length;
                rx *= inverse;
                ry = static_cast<float>(ry * inverse);
                rz = static_cast<float>(rz * inverse);
            }
            double ux = static_cast<double>(rz) * axis[1] - static_cast<double>(ry) * axis[2];
            double uy = rx * axis[2] - static_cast<double>(rz) * axis[0];
            double uz = static_cast<double>(ry) * axis[0] - rx * axis[1];
            auto uz_stored = static_cast<float>(uz);
            const double length2 = std::sqrt(ux * ux + uz * uz + uy * uy);
            if (length2 > 0.0f) {
                const double inverse = 1.0f / length2;
                ux *= inverse;
                uy *= inverse;
                uz *= inverse;
                uz_stored = static_cast<float>(uz);
            }
            float across[3] = {static_cast<float>(rx), ry, rz};
            float out[3] = {static_cast<float>(ux), static_cast<float>(uy), uz_stored};
            float along[3] = {axis[0], axis[1], axis[2]};
            set_axes(across, along, out);
            draw_billboard_mesh(mesh, along, across, out);
        }
        node = next;
    }
    g_vertex_format_locked = 0; // (the vertex format unlocked)
}

// 0x004CEBE0
void BillboardSet::append_mesh(MeshRef* mesh)
{
    if (!mesh->material || !mesh->material->textures[0])
        return;
    BillboardBatch* batch = get_find(mesh->material);
    if (!batch) {
        void* memory = memory::alloc(sizeof(BillboardBatch));
        batch = memory ? memory::construct_in<BillboardBatch>(memory, mesh->material) : nullptr;
        batches[count] = batch;
        ++count;
        order_batches(this);
    }
    batch->add_row(mesh);
}

// 0x004CE9F0
void BillboardSet::remove_mesh(MeshRef* mesh)
{
    if (!mesh->material)
        return;
    BillboardBatch* batch = get_find(mesh->material);
    if (!batch)
        return;
    batch->erase_record(mesh);
    for (const memory::ListNode* head : batch->lists) {
        if (head->prev != head)
            return;
    }
    for (int i = 0; i < count; ++i) {
        if (batches[i] != batch)
            continue;
        for (memory::ListNode* head : batch->lists) {
            memory::ListNode::destroy(head); // (0x004CE9A0)
        }
        memory::Hub::get_pull()->dealloc(batch);
        batches[i] = nullptr;
        break;
    }
    order_batches(this);
    --count;
}

// 0x004CEAB0
void BillboardSet::render(unsigned int which)
{
    if (which & 1) {
        for (int i = 0; i < count; ++i) {
            if (!(batches[i]->material->pass_flags_[0] & 0x40))
                batches[i]->render();
        }
    }
    if (which & 2) {
        for (int i = 0; i < count; ++i) {
            if (batches[i]->material->pass_flags_[0] & 0x40)
                batches[i]->render();
        }
    }
}

// 0x004CD8A0
void create_window_pool()
{
    for (int i = 0; i < 10; ++i) {
        auto* window = new PcWindow(0, 0, 640, 480);
        window->following_ = g_free_windows; // (no check, as the original)
        g_free_windows = window;
    }
}

// 0x004CD940
void mark_billboards_ready()
{
    if (!g_billboards_ready)
        g_billboards_ready = true;
}

// 0x004CD960
void set_flag_786e_b4() { g_billboard_flag = 1; }

// 0x004CD970
void clear_flag_786e_b4() { g_billboard_flag = 0; }

} // namespace render
