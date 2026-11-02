// The PC billboards: see pc_billboards.h. The maths follows the original's x87 code: double precision where it keeps
// values in registers, floats where it stores them.

#include "engine/gfx/pc_billboards.h"
#include "platform/d3dx.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/settings.h"
#include "gpu/vertex_buffers.h"
#include "platform/crt.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/checked_index.h"
#include "engine/core/cursor.h"
#include "engine/core/span_copy.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/shared_state.h"
#include "engine/gfx/renderer.h"
#include <span>
#include <bit>
#include <gsl/pointers>

namespace render {

namespace {
// NOT ORIGINAL grouping - pc_billboards.cpp's state (the original's globals). Made on first use, never destroyed
// (pc_billboards_state()).
struct PcBillboardsState {
    bool billboards_ready_{};
    std::array<float, 12> billboard_constants_{{0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
};

PcBillboardsState& pc_billboards_state()
{
    static core::NoDestroy<PcBillboardsState> state;
    return state.get();
}

} // namespace
BillboardSet& billboard_pool()
{
    static BillboardSet billboard_pool{};
    return billboard_pool;
}

PcBillboardsShared& pc_billboards_shared()
{
    static core::NoDestroy<PcBillboardsShared> state;
    return state.get();
}

namespace {

void toggle_fog(bool on) { engine::enable_fog(on); }
VertexBuffer* get_vertex_buffer(MeshRef* mesh, int index) { return mesh->fetch_vertex_store(index); }

bool is_head(const memory::ListNode* node) { return node->data == memory::ListNode::head_marker(); }
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
    crt::get_qsort(static_cast<void*>(manager->batches.data()), static_cast<uint32_t>(manager->count), 4,
                   compare_batches); // (the C runtime's qsort)
}

float sort_depth_of_batch(const BillboardBatch* batch) { return batch->material ? batch->material->sort_key_ : 0.0f; }

void draw_list(const memory::ListNode* head, const std::array<float, 3>& y_axis, const std::array<float, 3>& x_axis,
               const std::array<float, 3>& z_axis)
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
void draw_billboard_mesh(MeshRef* mesh, const std::array<float, 3>& y_axis, const std::array<float, 3>& x_axis,
                         const std::array<float, 3>& z_axis)
{
    if (mesh->stride < 0x10 || mesh->stride > 0x30)
        return;
    if (distance_clipping() && fog_setting()) { // (fog: its start a quarter back)
        const float end = renderer().render_camera().far_z_;
        renderer().set_fog_begin_distance(0.0f);
        renderer().set_fog_end(0.0f);
        auto start = static_cast<float>(end - (static_cast<double>(end) - renderer().render_camera().near_z_) * 0.25f);
        toggle_fog(true);
        renderer().d3d_device()->SetRenderState(D3DRS_FOGSTART, std::bit_cast<DWORD>(start));
        renderer().d3d_device()->SetRenderState(D3DRS_FOGEND, std::bit_cast<DWORD>(end));
    } else {
        toggle_fog(false);
    }
    if (renderer().shaders_available()) {
        if (mesh->flags & 4)
            mesh->set_pixel_shader_colored();
        else
            set_pixel_shader(mesh->pixel_shader_, nullptr);
    }
    D3DVIEWPORT9 viewport;
    renderer().d3d_device()->GetViewport(&viewport);
    IDirect3DVertexBuffer9* stream = nullptr;
    UINT offset = 0, stream_stride = 0;
    renderer().d3d_device()->GetStreamSource(0, &stream, &offset, &stream_stride);
    IDirect3DPixelShader9* pixel_shader = nullptr;
    renderer().d3d_device()->GetPixelShader(&pixel_shader);
    IDirect3DVertexShader9* vertex_shader = nullptr;
    renderer().d3d_device()->GetVertexShader(&vertex_shader);
    IDirect3DIndexBuffer9* indices = nullptr;
    renderer().d3d_device()->GetIndices(&indices);
    const uint32_t primitives = primitive_count(mesh->primitive_type_, core::at(mesh->index_counts_, 0));
    uint8_t* into_block = nullptr;
    get_vertex_buffer(mesh, 0)->get_lock(0, 0, &into_block, D3DLOCK_DISCARD);
    const size_t vertex_bytes = static_cast<size_t>(mesh->stride) * mesh->vertex_count_;
    core::Cursor<uint8_t> into(std::span<uint8_t>(into_block, vertex_bytes));
    core::Cursor<const uint8_t> from(std::span<const uint8_t>(get_vertex_buffer(mesh, 0)->data, vertex_bytes));
    const auto& pivot = mesh->billboard->pivot;
    const float px = core::at(pivot, 0);
    const float py = core::at(pivot, 1);
    const float pz = core::at(pivot, 2);
    uint32_t rest = mesh->stride - 0xC;
    std::span<const uint32_t> table;
    bool has_table = false;
    if (mesh->per_vertex_bytes_) { // (a value per vertex from the material's table)
        table = mesh->material->anim_colour_run();
        has_table = true;
        rest -= 4;
    }
    const std::span<const uint8_t> vertex_bytes_of_mesh(mesh->per_vertex_bytes_, mesh->per_vertex_bytes_ ? mesh->vertex_count_ : 0);
    for (uint32_t i = 0; i < mesh->vertex_count_; ++i) {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        std::memcpy(&x, from.rest().first(4).data(), 4);
        std::memcpy(&y, from.rest().subspan(4, 4).data(), 4);
        std::memcpy(&z, from.rest().subspan(8, 4).data(), 4);
        from += 0xC;
        auto a = static_cast<float>(static_cast<double>(x) * core::at(x_axis, 0));
        auto b = static_cast<float>(static_cast<double>(x) * core::at(x_axis, 1));
        auto c = static_cast<float>(static_cast<double>(x) * core::at(x_axis, 2));
        auto t2 = static_cast<float>(static_cast<double>(y) * core::at(y_axis, 2));
        a = static_cast<float>(static_cast<double>(y) * core::at(y_axis, 0) + a);
        const double sum_y = static_cast<double>(y) * core::at(y_axis, 1) + b;
        const double sum_z = static_cast<double>(c) + t2;
        auto t3 = static_cast<float>(static_cast<double>(z) * core::at(z_axis, 2));
        a = static_cast<float>(static_cast<double>(z) * core::at(z_axis, 0) + a);
        auto b2 = static_cast<float>(static_cast<double>(z) * core::at(z_axis, 1) + sum_y);
        const double c2 = t3 + sum_z;
        std::array<float, 3> out = {{static_cast<float>(static_cast<double>(px) + a),
                        static_cast<float>(static_cast<double>(py) + b2), static_cast<float>(pz + c2)}};
        std::memcpy(into.rest().first(0xC).data(), out.data(), 0xC);
        into += 0xC;
        if (has_table) {
            auto index = static_cast<uint32_t>(int{static_cast<int8_t>(core::at(vertex_bytes_of_mesh, i))});
            uint32_t value = 0;
            if (index)
                value = core::at(table, index - 1);
            else
                std::memcpy(&value, from.rest().first(4).data(), 4);
            std::memcpy(into.rest().first(4).data(), &value, 4);
            from += 4;
            into += 4;
        }
        std::memcpy(into.rest().first(rest).data(), from.rest().first(rest).data(), rest);
        into += rest;
        from += rest;
    }
    get_vertex_buffer(mesh, 0)->get_unlock();
    renderer().d3d_device()->SetStreamSource(0, static_cast<IDirect3DVertexBuffer9*>(get_vertex_buffer(mesh, 0)->buffer), 0,
                                  mesh->stride);
    // (the index buffer's Direct3D buffer, +0xC: through the getter 0x00492E50 folded with others)
    renderer().d3d_device()->SetIndices(static_cast<IDirect3DIndexBuffer9*>(core::at(mesh->index_buffers_, 0)->buffer));
    DWORD format = 0;
    renderer().d3d_device()->GetFVF(&format);
    renderer().d3d_device()->SetFVF(mesh->fvf);
    auto type = static_cast<D3DPRIMITIVETYPE>(mesh->primitive_type_);
    if (renderer().shaders_available()) {
        renderer().d3d_device()->DrawIndexedPrimitive(type, 0, 0, mesh->vertex_count_, 0, primitives);
    } else {
        for (uint32_t pass = 0; pass < mesh->material->pass_count_; ++pass) {
            mesh->material->submit_pass(pass, mesh->flags & 4 ? mesh->color.data() : nullptr);
            renderer().d3d_device()->DrawIndexedPrimitive(type, 0, 0, mesh->vertex_count_, 0, primitives);
        }
    }
    renderer().d3d_device()->SetFVF(format);
    renderer().d3d_device()->SetStreamSource(0, stream, 0, stream_stride); // (the offset not put back, as the original)
    if (stream)
        stream->Release();
    renderer().d3d_device()->SetPixelShader(pixel_shader); // (the shaders not released, as the original)
    renderer().d3d_device()->SetVertexShader(vertex_shader);
    renderer().d3d_device()->SetIndices(indices);
    if (indices)
        indices->Release();
    toggle_fog(false);
    (void)viewport;
}

// 0x004CDA60
BillboardBatch* BillboardSet::get_find(const MaterialRef* material)
{
    for (int i = 0; i < count; ++i) {
        if (core::at(batches, i)->material == material)
            return core::at(batches, i);
    }
    return nullptr;
}

// 0x004CDAA0
void BillboardSet::setup_axes()
{
    const std::span<const float, 16> m = renderer().view_matrix().floats();
    const float vz = core::at(m, 10);
    core::at(view, 0) = core::at(m, 2);
    core::at(view, 1) = core::at(m, 6);
    core::at(view, 3) = 1.0f;
    core::at(view, 2) = vz;
    // right = forward x (0, 1, 0)
    auto zero = static_cast<float>(static_cast<double>(core::at(view, 1)) * 0.0f);
    double rx = zero - static_cast<double>(core::at(view, 2));
    double ry = static_cast<double>(core::at(view, 2)) * 0.0f - static_cast<double>(core::at(view, 0)) * 0.0f;
    auto rz = static_cast<float>(static_cast<double>(core::at(view, 0)) - zero);
    {
        const double length = std::sqrt(static_cast<double>(rz) * rz + ry * ry + rx * rx);
        if (length > 0.0f) {
            const double inverse = 1.0f / length;
            rx *= inverse;
            ry *= inverse;
            rz = static_cast<float>(rz * inverse);
        }
    }
    core::at(right, 2) = rz;
    core::at(right, 0) = static_cast<float>(rx);
    core::at(right, 3) = 0.0f;
    core::at(right, 1) = static_cast<float>(ry);
    // up = forward x right... as the original orders it
    double ux = static_cast<double>(core::at(view, 2)) * core::at(right, 1) - static_cast<double>(core::at(view, 1)) * core::at(right, 2);
    double uy = static_cast<double>(core::at(right, 2)) * core::at(view, 0) - static_cast<double>(core::at(view, 2)) * core::at(right, 0);
    const double uz_wide = static_cast<double>(core::at(view, 1)) * core::at(right, 0) - static_cast<double>(core::at(right, 1)) * core::at(view, 0);
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
    core::at(up, 2) = uz;
    core::at(up, 0) = static_cast<float>(ux);
    core::at(up, 3) = 0.0f;
    core::at(up, 1) = static_cast<float>(uy);
    // the forward and right along the ground
    auto flatten = [](float x, float z, std::array<float, 4>& out) {
        double fx = x, fz = z;
        float y = 0.0f;
        const double length = std::sqrt(fz * fz + fx * fx);
        if (length > 0.0f) {
            const double inverse = 1.0f / length;
            fx *= inverse;
            y = static_cast<float>(0.0f * inverse);
            fz *= inverse;
        }
        core::at(out, 0) = static_cast<float>(fx);
        core::at(out, 1) = y;
        core::at(out, 3) = 1.0f;
        core::at(out, 2) = static_cast<float>(fz);
    };
    flatten(core::at(view, 0), core::at(view, 2), flat_forward_);
    flatten(core::at(right, 0), core::at(right, 2), flat_right_);
}

// 0x004CEB30
BillboardBatch::BillboardBatch(MaterialRef* new_material) : material(new_material)
{
    core::at(lists, 0) = new_head();
    core::at(lists, 1) = new_head();
    core::at(lists, 2) = new_head();
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
    memory::ListNode* head = core::at(lists, entry->type);
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
    if (renderer().shaders_available())
        material->hand_in();
    else
        material->submit_states();
    for (auto& list : lists) { // (the first shown list's first mesh's pixel shader)
        const memory::ListNode* first = list->next;
        if (is_head(first) || !first)
            continue;
        const memory::ListNode* node = head_of_scene_list(list);
        if (renderer().shaders_available())
            set_pixel_shader(static_cast<BillboardRec*>(node->data)->mesh->pixel_shader_, nullptr);
        break;
    }
    auto transpose = d3dx::matrix_transpose; // (D3DX)
    auto multiply = d3dx::matrix_multiply;
    std::array<float, 16> projection{};
    std::array<float, 16> view{};
    std::array<float, 16> identity = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    std::array<float, 16> view_world{};
    std::array<float, 16> all{};
    transpose(projection.data(), renderer().projection_matrix().data());
    transpose(view.data(), renderer().view_matrix().data());
    core::at(view, 12) = core::at(view, 13) = core::at(view, 14) = 0.0f; // (its last row cleared)
    core::at(view, 15) = 1.0f;
    multiply(view_world.data(), view.data(), identity.data());
    multiply(all.data(), projection.data(), view_world.data());
    renderer().d3d_device()->SetVertexShaderConstantF(0, all.data(), 4);
    auto* manager = &billboard_pool();
    const std::span<float> constants(pc_billboards_state().billboard_constants_); // c4-c6
    // (the first three floats of each of the three vectors given)
    auto set_axes = [&](std::span<const float> x, std::span<const float> y, std::span<const float> z) {
        core::copy_into(x.first(3), constants.first(3));
        core::copy_into(y.first(3), constants.subspan(4, 3));
        core::copy_into(z.first(3), constants.subspan(8, 3));
    };
    set_axes(manager->right, manager->up, manager->view);
    renderer().d3d_device()->SetVertexShaderConstantF(4, constants.data(), 3);
    {
        std::array<float, 3> x_axis{};
        std::array<float, 3> y_axis{};
        std::array<float, 3> z_axis{};
        core::copy_into(std::span<const float>(constants).first(3), std::span<float>(x_axis));
        core::copy_into(std::span<const float>(constants).subspan(4, 3), std::span<float>(y_axis));
        core::copy_into(std::span<const float>(constants).subspan(8, 3), std::span<float>(z_axis));
        draw_list(core::at(lists, 0), y_axis, x_axis, z_axis);
    }
    {
        const std::array<float, 3> flat_up = {{0.0f, 1.0f, 0.0f}};
        set_axes(manager->flat_right_, flat_up, manager->flat_forward_);
        std::array<float, 3> x_axis{};
        const std::array<float, 3> y_axis = {{0.0f, 1.0f, 0.0f}};
        std::array<float, 3> z_axis{};
        core::copy_into(std::span<const float>(manager->flat_right_).first(3), std::span<float>(x_axis));
        core::copy_into(std::span<const float>(manager->flat_forward_).first(3), std::span<float>(z_axis));
        draw_list(core::at(lists, 1), y_axis, x_axis, z_axis);
    }
    // About each mesh's own axis: across = forward x axis, out = across x axis, both normalised.
    for (const memory::ListNode* node = head_of_scene_list(core::at(lists, 2)); node;) {
        const memory::ListNode* next = get_next_node(node);
        MeshRef* mesh = static_cast<BillboardRec*>(node->data)->mesh;
        if (mesh->flags & 0x40) {
            const auto& axis = mesh->billboard->axis;
            const auto& d = manager->view;
            double rx = static_cast<double>(core::at(d, 1)) * core::at(axis, 2) - static_cast<double>(core::at(d, 2)) * core::at(axis, 1);
            auto ry = static_cast<float>(static_cast<double>(core::at(d, 2)) * core::at(axis, 0) - static_cast<double>(core::at(d, 0)) * core::at(axis, 2));
            auto rz = static_cast<float>(static_cast<double>(core::at(d, 0)) * core::at(axis, 1) - static_cast<double>(core::at(d, 1)) * core::at(axis, 0));
            const double length = std::sqrt(rx * rx + static_cast<double>(rz) * rz + static_cast<double>(ry) * ry);
            if (length > 0.0f) {
                const double inverse = 1.0f / length;
                rx *= inverse;
                ry = static_cast<float>(ry * inverse);
                rz = static_cast<float>(rz * inverse);
            }
            double ux = static_cast<double>(rz) * core::at(axis, 1) - static_cast<double>(ry) * core::at(axis, 2);
            double uy = rx * core::at(axis, 2) - static_cast<double>(rz) * core::at(axis, 0);
            double uz = static_cast<double>(ry) * core::at(axis, 0) - rx * core::at(axis, 1);
            auto uz_stored = static_cast<float>(uz);
            const double length2 = std::sqrt(ux * ux + uz * uz + uy * uy);
            if (length2 > 0.0f) {
                const double inverse = 1.0f / length2;
                ux *= inverse;
                uy *= inverse;
                uz *= inverse;
                uz_stored = static_cast<float>(uz);
            }
            std::array<float, 3> across = {{static_cast<float>(rx), ry, rz}};
            std::array<float, 3> out = {{static_cast<float>(ux), static_cast<float>(uy), uz_stored}};
            std::array<float, 3> along = {{core::at(axis, 0), core::at(axis, 1), core::at(axis, 2)}};
            set_axes(across, along, out);
            draw_billboard_mesh(mesh, along, across, out);
        }
        node = next;
    }
    renderer().set_vertex_format_locked(0); // (the vertex format unlocked)
}

// 0x004CEBE0
void BillboardSet::append_mesh(MeshRef* mesh)
{
    if (!mesh->material || !core::at(mesh->material->textures, 0))
        return;
    BillboardBatch* batch = get_find(mesh->material);
    if (!batch) {
        void* memory = memory::alloc(sizeof(BillboardBatch));
        batch = memory ? memory::construct_in<BillboardBatch>(memory, mesh->material) : nullptr;
        core::at(batches, count) = batch;
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
        if (core::at(batches, i) != batch)
            continue;
        for (memory::ListNode* head : batch->lists) {
            memory::ListNode::destroy(head); // (0x004CE9A0)
        }
        memory::Hub::get_pull()->dealloc(batch);
        core::at(batches, i) = nullptr;
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
            if (!(core::at(core::at(batches, i)->material->pass_flags_, 0) & 0x40))
                core::at(batches, i)->render();
        }
    }
    if (which & 2) {
        for (int i = 0; i < count; ++i) {
            if (core::at(core::at(batches, i)->material->pass_flags_, 0) & 0x40)
                core::at(batches, i)->render();
        }
    }
}

// 0x004CD8A0
void create_window_pool()
{
    for (int i = 0; i < 10; ++i) {
        auto window = memory::make_owned<PcWindow>(0, 0, 640, 480);
        window->following_ = window2d_shared().free_windows_; // (no check, as the original)
        window2d_shared().free_windows_ = window;
    }
}

// 0x004CD940
void mark_billboards_ready()
{
    if (!pc_billboards_state().billboards_ready_)
        pc_billboards_state().billboards_ready_ = true;
}

// 0x004CD960
void set_flag_786e_b4() { pc_billboards_shared().billboard_flag_ = 1; }

// 0x004CD970
void clear_flag_786e_b4() { pc_billboards_shared().billboard_flag_ = 0; }

} // namespace render
