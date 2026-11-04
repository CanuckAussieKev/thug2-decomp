// The PC billboards: see pc_billboards.h. The maths follows the original's x87 code: double precision where it keeps
// values in registers, floats where it stores them.

#include "engine/gfx/pc_billboards.h"
#include "gpu/d3d9_handles.h"
#include "platform/d3dx.h"

#include <windows.h>

#include <d3d9.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>

#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/surface_material.h"
#include "engine/gfx/sub_mesh.h"
#include "engine/gfx/render_camera.h"
#include "engine/gfx/render_state.h"
#include "gpu/vertex_buffers.h"
#include "platform/crt.h"
#include "engine/memory/arena_alloc.h"
#include "base/core/checked_index.h"
#include "base/core/cursor.h"
#include "base/core/span_copy.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/d3d9_state.h"
#include "engine/gfx/screen_2d.h"
#include "engine/gfx/view_setup.h"
#include "base/maths/rect.h"
#include "engine/gfx/screen.h"
#include "base/maths/mat4.h"
#include <span>
#include <bit>
#include <gsl/pointers>

namespace render {

constexpr std::size_t kBillboardConstantFloats = 12;
constexpr int kMeshActiveFlag = 0x40; // the mesh is on
constexpr int kPrimTriangleStrip = 5;
constexpr int kPrimTriangleFan = 6;
constexpr int kMinSourceStride = 0x10; // the vertex strides billboards can be made from
constexpr int kMaxSourceStride = 0x30;
constexpr float kNearFadeShare = 0.25F;
constexpr int kPositionBytes = 0xC;
constexpr int kZOffset = 8;
constexpr std::size_t kPassVertexBytes = 0x20;
constexpr int kMatrixW = 15;
constexpr std::size_t kZAxisOffset = 8;
constexpr int kPassFlagBillboard = 0x40;
constexpr int kWindowPoolSize = 10;

namespace {
// NOT ORIGINAL grouping - pc_billboards.cpp's state (the original's globals). Made on first use, never destroyed
// (pc_billboards_state()).
struct PcBillboardsState {
    bool billboards_ready_{};
    std::array<float, kBillboardConstantFloats> billboard_constants_{
        {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F}};
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

namespace {

void toggle_fog(bool on) { engine::enable_fog(on); }
VertexBuffer* get_vertex_buffer(MeshRef* mesh, int index) { return mesh->fetch_vertex_store(index); }

bool is_head(const memory::ListNode* node) { return node->data() == memory::ListNode::head_marker(); }
// The node after `node` (null at the head).
memory::ListNode* get_next_node(const memory::ListNode* node) { return is_head(node->next()) ? nullptr : node->next(); }
// A list's first node (null when empty).
memory::ListNode* head_of_scene_list(const memory::ListNode* head)
{
    return is_head(head->next()) ? nullptr : head->next();
}

memory::ListNode* new_head() { return memory::ListNode::create_head(); }

void order_batches(BillboardSet* manager)
{
    crt::get_qsort(static_cast<void*>(manager->batches().data()), static_cast<uint32_t>(manager->count()), 4,
                   compare_batches); // (the C runtime's qsort)
}

float sort_depth_of_batch(const BillboardBatch* batch)
{
    return batch->material() ? batch->material()->sort_key() : 0.0F;
}

void draw_list(const memory::ListNode* head, const std::array<float, 3>& y_axis, const std::array<float, 3>& x_axis,
               const std::array<float, 3>& z_axis)
{
    for (const memory::ListNode* node = head_of_scene_list(head); node;) {
        const memory::ListNode* next = get_next_node(node);
        MeshRef* mesh = static_cast<BillboardRec*>(node->data())->mesh;
        if ((mesh->flags() & kMeshActiveFlag) != 0U) {
            draw_billboard_mesh(mesh, y_axis, x_axis, z_axis);
        }
        node = next;
    }
}

} // namespace

// 0x004CD9D0
int compare_batches(const void* a, const void* b)
{
    const auto* first = *static_cast<BillboardBatch* const*>(a);
    const auto* second = *static_cast<BillboardBatch* const*>(b);
    if (!first) {
        return static_cast<int>(second != nullptr);
    }
    if (!second) {
        return -1;
    }
    if (sort_depth_of_batch(first) < sort_depth_of_batch(second)) {
        return -1;
    }
    return sort_depth_of_batch(first) > sort_depth_of_batch(second) ? 1 : 0;
}

// 0x004CD980
uint32_t primitive_count(PrimitiveRun run)
{
    const auto type = run.type;
    const auto count = run.count;
    switch (type) {
    case 1:
        return count;
    case 2:
        return count >> 1;
    case 3:
        return count - 1;
    case 4:
        return count / 3;
    case kPrimTriangleStrip:
    case kPrimTriangleFan:
        return count - 2;
    default:
        return 0;
    }
}

// 0x004CDD00
void draw_billboard_mesh(MeshRef* mesh, const std::array<float, 3>& y_axis, const std::array<float, 3>& x_axis,
                         const std::array<float, 3>& z_axis)
{
    if (mesh->stride() < kMinSourceStride || mesh->stride() > kMaxSourceStride) {
        return;
    }
    if (view_setup().distance_clipping() && view_setup().fog_setting()) { // (fog: its start a quarter back)
        const float end = renderer().render_camera().far_z_;
        renderer().set_fog_begin_distance(0.0F);
        renderer().set_fog_end(0.0F);
        auto start = static_cast<float>(
            end - ((static_cast<double>(end) - renderer().render_camera().near_z_) * kNearFadeShare));
        toggle_fog(true);
        d3d9::state().d3d_device()->SetRenderState(D3DRS_FOGSTART, std::bit_cast<DWORD>(start));
        d3d9::state().d3d_device()->SetRenderState(D3DRS_FOGEND, std::bit_cast<DWORD>(end));
    } else {
        toggle_fog(false);
    }
    if (renderer().shaders_available()) {
        if ((mesh->flags() & 4) != 0U) {
            mesh->set_pixel_shader_colored();
        } else {
            set_pixel_shader(mesh->pixel_shader(), nullptr);
        }
    }
    D3DVIEWPORT9 viewport;
    d3d9::state().d3d_device()->GetViewport(&viewport);
    IDirect3DVertexBuffer9* stream = nullptr;
    UINT offset = 0;
    UINT stream_stride = 0;
    d3d9::state().d3d_device()->GetStreamSource(0, &stream, &offset, &stream_stride);
    IDirect3DPixelShader9* pixel_shader = nullptr;
    d3d9::state().d3d_device()->GetPixelShader(&pixel_shader);
    IDirect3DVertexShader9* vertex_shader = nullptr;
    d3d9::state().d3d_device()->GetVertexShader(&vertex_shader);
    IDirect3DIndexBuffer9* indices = nullptr;
    d3d9::state().d3d_device()->GetIndices(&indices);
    const uint32_t primitives = primitive_count({.type = mesh->primitive_type(), .count = core::at(mesh->index_counts(), 0)});
    uint8_t* into_block = nullptr;
    get_vertex_buffer(mesh, 0)->get_lock(0, 0, &into_block, D3DLOCK_DISCARD);
    const size_t vertex_bytes = static_cast<size_t>(mesh->stride()) * mesh->vertex_count();
    core::Cursor<uint8_t> into(std::span<uint8_t>(into_block, vertex_bytes));
    core::Cursor<const uint8_t> from(std::span<const uint8_t>(get_vertex_buffer(mesh, 0)->data(), vertex_bytes));
    const auto& pivot = mesh->billboard()->pivot;
    const float px = core::at(pivot, 0);
    const float py = core::at(pivot, 1);
    const float pz = core::at(pivot, 2);
    uint32_t rest = mesh->stride() - kPositionBytes;
    std::span<const uint32_t> table;
    bool has_table = false;
    if (mesh->per_vertex_bytes()) { // (a value per vertex from the material's table)
        table = mesh->material()->anim_colour_run();
        has_table = true;
        rest -= 4;
    }
    const std::span<const uint8_t> vertex_bytes_of_mesh(mesh->per_vertex_bytes(), mesh->per_vertex_bytes() ? mesh->vertex_count() : 0);
    for (uint32_t i = 0; i < mesh->vertex_count(); ++i) {
        float x = 0.0F;
        float y = 0.0F;
        float z = 0.0F;
        std::memcpy(&x, from.rest().first(4).data(), 4);
        std::memcpy(&y, from.rest().subspan(4, 4).data(), 4);
        std::memcpy(&z, from.rest().subspan(kZOffset, 4).data(), 4);
        from += kPositionBytes;
        auto partial_x = static_cast<float>(static_cast<double>(x) * core::at(x_axis, 0));
        auto partial_y = static_cast<float>(static_cast<double>(x) * core::at(x_axis, 1));
        auto partial_z = static_cast<float>(static_cast<double>(x) * core::at(x_axis, 2));
        auto t2 = static_cast<float>(static_cast<double>(y) * core::at(y_axis, 2));
        partial_x = static_cast<float>((static_cast<double>(y) * core::at(y_axis, 0)) + partial_x);
        const double sum_y = (static_cast<double>(y) * core::at(y_axis, 1)) + partial_y;
        const double sum_z = static_cast<double>(partial_z) + t2;
        auto t3 = static_cast<float>(static_cast<double>(z) * core::at(z_axis, 2));
        partial_x = static_cast<float>((static_cast<double>(z) * core::at(z_axis, 0)) + partial_x);
        auto b2 = static_cast<float>((static_cast<double>(z) * core::at(z_axis, 1)) + sum_y);
        const double c2 = t3 + sum_z;
        std::array<float, 3> out = {{static_cast<float>(static_cast<double>(px) + partial_x),
                                     static_cast<float>(static_cast<double>(py) + b2), static_cast<float>(pz + c2)}};
        std::memcpy(into.rest().first(kPositionBytes).data(), out.data(), kPositionBytes);
        into += kPositionBytes;
        if (has_table) {
            auto index = static_cast<uint32_t>(int{static_cast<int8_t>(core::at(vertex_bytes_of_mesh, i))});
            uint32_t value = 0;
            if (index != 0U) {
                value = core::at(table, index - 1);
            } else {
                std::memcpy(&value, from.rest().first(4).data(), 4);
            }
            std::memcpy(into.rest().first(4).data(), &value, 4);
            from += 4;
            into += 4;
        }
        std::memcpy(into.rest().first(rest).data(), from.rest().first(rest).data(), rest);
        into += rest;
        from += rest;
    }
    get_vertex_buffer(mesh, 0)->get_unlock();
    d3d9::state().d3d_device()->SetStreamSource(0, d3d9::object(get_vertex_buffer(mesh, 0)->buffer()), 0, mesh->stride());
    // (the index buffer's Direct3D buffer, +0xC: through the getter 0x00492E50 folded with others)
    d3d9::state().d3d_device()->SetIndices(d3d9::object(core::at(mesh->index_buffers(), 0)->buffer()));
    DWORD format = 0;
    d3d9::state().d3d_device()->GetFVF(&format);
    d3d9::state().d3d_device()->SetFVF(mesh->fvf());
    auto type = static_cast<D3DPRIMITIVETYPE>(mesh->primitive_type());
    if (renderer().shaders_available()) {
        d3d9::state().d3d_device()->DrawIndexedPrimitive(type, 0, 0, mesh->vertex_count(), 0, primitives);
    } else {
        for (uint32_t pass = 0; pass < mesh->material()->pass_count(); ++pass) {
            mesh->material()->submit_pass(pass, ((mesh->flags() & 4) != 0U) ? mesh->color().data() : nullptr);
            d3d9::state().d3d_device()->DrawIndexedPrimitive(type, 0, 0, mesh->vertex_count(), 0, primitives);
        }
    }
    d3d9::state().d3d_device()->SetFVF(format);
    d3d9::state().d3d_device()->SetStreamSource(0, stream, 0, stream_stride); // (the offset not put back, as the original)
    if (stream) {
        stream->Release();
    }
    d3d9::state().d3d_device()->SetPixelShader(pixel_shader); // (the shaders not released, as the original)
    d3d9::state().d3d_device()->SetVertexShader(vertex_shader);
    d3d9::state().d3d_device()->SetIndices(indices);
    if (indices) {
        indices->Release();
    }
    toggle_fog(false);
    (void)viewport;
}

// 0x004CDA60
BillboardBatch* BillboardSet::get_find(const MaterialRef* material)
{
    for (int i = 0; i < count_; ++i) {
        if (core::at(batches_, i)->material() == material) {
            return core::at(batches_, i);
        }
    }
    return nullptr;
}

// 0x004CDAA0
void BillboardSet::setup_axes()
{
    const std::span<const float, 16> view = renderer().view_matrix().floats();
    const float vz = core::at(view, 10);
    core::at(view_, 0) = core::at(view, 2);
    core::at(view_, 1) = core::at(view, maths::kMatYz);
    core::at(view_, 3) = 1.0F;
    core::at(view_, 2) = vz;
    // right = forward x (0, 1, 0)
    auto zero = static_cast<float>(static_cast<double>(core::at(view_, 1)) * 0.0F);
    double rx = zero - static_cast<double>(core::at(view_, 2));
    double ry = (static_cast<double>(core::at(view_, 2)) * 0.0F) - (static_cast<double>(core::at(view_, 0)) * 0.0F);
    auto rz = static_cast<float>(static_cast<double>(core::at(view_, 0)) - zero);
    {
        const double length = std::sqrt((static_cast<double>(rz) * rz) + (ry * ry) + (rx * rx));
        if (length > 0.0F) {
            const double inverse = 1.0F / length;
            rx *= inverse;
            ry *= inverse;
            rz = static_cast<float>(rz * inverse);
        }
    }
    core::at(right_, 2) = rz;
    core::at(right_, 0) = static_cast<float>(rx);
    core::at(right_, 3) = 0.0F;
    core::at(right_, 1) = static_cast<float>(ry);
    // up = forward x right... as the original orders it
    double ux = (static_cast<double>(core::at(view_, 2)) * core::at(right_, 1)) -
                (static_cast<double>(core::at(view_, 1)) * core::at(right_, 2));
    double uy = (static_cast<double>(core::at(right_, 2)) * core::at(view_, 0)) -
                (static_cast<double>(core::at(view_, 2)) * core::at(right_, 0));
    const double uz_wide = (static_cast<double>(core::at(view_, 1)) * core::at(right_, 0)) -
                           (static_cast<double>(core::at(right_, 1)) * core::at(view_, 0));
    auto uz = static_cast<float>(uz_wide);
    {
        const double length =
            std::sqrt((uz_wide * uz) + (uy * uy) + (ux * ux)); // (the unrounded one times the stored one)
        if (length > 0.0F) {
            const double inverse = 1.0F / length;
            ux *= inverse;
            uy *= inverse;
            uz = static_cast<float>(uz * inverse);
        }
    }
    core::at(up_, 2) = uz;
    core::at(up_, 0) = static_cast<float>(ux);
    core::at(up_, 3) = 0.0F;
    core::at(up_, 1) = static_cast<float>(uy);
    // the forward and right along the ground
    auto flatten = [](const std::array<float, 4>& from, std::array<float, 4>& out) {
        double fx = core::at(from, 0);
        double fz = core::at(from, 2);
        float y = 0.0F;
        const double length = std::sqrt((fz * fz) + (fx * fx));
        if (length > 0.0F) {
            const double inverse = 1.0F / length;
            fx *= inverse;
            y = static_cast<float>(0.0F * inverse);
            fz *= inverse;
        }
        core::at(out, 0) = static_cast<float>(fx);
        core::at(out, 1) = y;
        core::at(out, 3) = 1.0F;
        core::at(out, 2) = static_cast<float>(fz);
    };
    flatten(view_, flat_forward_);
    flatten(right_, flat_right_);
}

// 0x004CEB30
BillboardBatch::BillboardBatch(MaterialRef* new_material)
    : vertex_bytes_((new_material->pass_count() + 2) * kPassVertexBytes),
      material_(new_material) // (the original first sets 0x40)
{
    core::at(lists_, 0) = new_head();
    core::at(lists_, 1) = new_head();
    core::at(lists_, 2) = new_head();
}

// 0x004CE1D0
void BillboardBatch::add_row(MeshRef* mesh)
{
    auto* entry = static_cast<BillboardRec*>(memory::alloc(sizeof(BillboardRec)));
    if (entry) {
        entry->mesh = mesh;
        entry->type = mesh->billboard()->type;
    }
    memory::ListNode* node = memory::ListNode::create(entry);
    // at the end of its list (no checks: written through null, as the original)
    memory::ListNode* head = core::at(lists_, entry->type);
    node->set_prev(head->prev());
    node->set_next(head);
    head->prev()->set_next(node);
    head->set_prev(node);
}

// 0x004CE250
void BillboardBatch::erase_record(MeshRef* mesh)
{
    for (const memory::ListNode* head : lists_) {
        memory::ListNode* node = head->next();
        if (is_head(node)) {
            continue;
        }
        while (node) {
            auto* entry = static_cast<BillboardRec*>(node->data());
            memory::ListNode* next = get_next_node(node);
            if (entry->mesh == mesh) {
                memory::ListNode::destroy(node); // (0x004CE960)
                memory::Hub::dealloc(entry);
                return;
            }
            node = next;
        }
    }
}

// 0x004CE2D0
void BillboardBatch::render()
{
    if (!material_) {
        return;
    }
    if (renderer().shaders_available()) {
        material_->hand_in();
    } else {
        material_->submit_states();
    }
    for (auto& list : lists_) { // (the first shown list's first mesh's pixel shader)
        const memory::ListNode* first = list->next();
        if (is_head(first) || !first) {
            continue;
        }
        const memory::ListNode* node = head_of_scene_list(list);
        if (renderer().shaders_available()) {
            set_pixel_shader(static_cast<BillboardRec*>(node->data())->mesh->pixel_shader(), nullptr);
        }
        break;
    }
    auto transpose = d3dx::matrix_transpose; // (D3DX)
    std::array<float, maths::kMatrixFloats> projection{};
    std::array<float, maths::kMatrixFloats> view{};
    std::array<float, maths::kMatrixFloats> identity = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    std::array<float, maths::kMatrixFloats> view_world{};
    std::array<float, maths::kMatrixFloats> all{};
    transpose(projection.data(), renderer().projection_matrix().data());
    transpose(view.data(), renderer().view_matrix().data());
    core::at(view, maths::kMatTx) = core::at(view, maths::kMatTy) = core::at(view, maths::kMatTz) =
        0.0F; // (its last row cleared)
    core::at(view, kMatrixW) = 1.0F;
    maths::matrix_multiply(view_world.data(), view.data(), identity.data());
    maths::matrix_multiply(all.data(), projection.data(), view_world.data());
    d3d9::state().d3d_device()->SetVertexShaderConstantF(0, all.data(), 4);
    auto* manager = &billboard_pool();
    const std::span<float> constants(pc_billboards_state().billboard_constants_); // c4-c6
    // (the first three floats of each of the three vectors given)
    auto set_axes = [&](std::span<const float> x, std::span<const float> y, std::span<const float> z) {
        core::copy_into(x.first(3), constants.first(3));
        core::copy_into(y.first(3), constants.subspan(4, 3));
        core::copy_into(z.first(3), constants.subspan(kZAxisOffset, 3));
    };
    set_axes(manager->right(), manager->up(), manager->view());
    d3d9::state().d3d_device()->SetVertexShaderConstantF(4, constants.data(), 3);
    {
        std::array<float, 3> x_axis{};
        std::array<float, 3> y_axis{};
        std::array<float, 3> z_axis{};
        core::copy_into(std::span<const float>(constants).first(3), std::span<float>(x_axis));
        core::copy_into(std::span<const float>(constants).subspan(4, 3), std::span<float>(y_axis));
        core::copy_into(std::span<const float>(constants).subspan(kZAxisOffset, 3), std::span<float>(z_axis));
        draw_list(core::at(lists_, 0), y_axis, x_axis, z_axis);
    }
    {
        const std::array<float, 3> flat_up = {{0.0F, 1.0F, 0.0F}};
        set_axes(manager->flat_right(), flat_up, manager->flat_forward());
        std::array<float, 3> x_axis{};
        const std::array<float, 3> y_axis = {{0.0F, 1.0F, 0.0F}};
        std::array<float, 3> z_axis{};
        core::copy_into(std::span<const float>(manager->flat_right()).first(3), std::span<float>(x_axis));
        core::copy_into(std::span<const float>(manager->flat_forward()).first(3), std::span<float>(z_axis));
        draw_list(core::at(lists_, 1), y_axis, x_axis, z_axis);
    }
    // About each mesh's own axis: across = forward x axis, out = across x axis, both normalised.
    for (const memory::ListNode* node = head_of_scene_list(core::at(lists_, 2)); node;) {
        const memory::ListNode* next = get_next_node(node);
        MeshRef* mesh = static_cast<BillboardRec*>(node->data())->mesh;
        if ((mesh->flags() & kMeshActiveFlag) != 0U) {
            const auto& axis = mesh->billboard()->axis;
            const auto& view_array = manager->view();
            double rx = (static_cast<double>(core::at(view_array, 1)) * core::at(axis, 2)) -
                        (static_cast<double>(core::at(view_array, 2)) * core::at(axis, 1));
            auto ry = static_cast<float>((static_cast<double>(core::at(view_array, 2)) * core::at(axis, 0)) -
                                         (static_cast<double>(core::at(view_array, 0)) * core::at(axis, 2)));
            auto rz = static_cast<float>((static_cast<double>(core::at(view_array, 0)) * core::at(axis, 1)) -
                                         (static_cast<double>(core::at(view_array, 1)) * core::at(axis, 0)));
            const double length =
                std::sqrt((rx * rx) + (static_cast<double>(rz) * rz) + (static_cast<double>(ry) * ry));
            if (length > 0.0F) {
                const double inverse = 1.0F / length;
                rx *= inverse;
                ry = static_cast<float>(ry * inverse);
                rz = static_cast<float>(rz * inverse);
            }
            double ux = (static_cast<double>(rz) * core::at(axis, 1)) - (static_cast<double>(ry) * core::at(axis, 2));
            double uy = (rx * core::at(axis, 2)) - (static_cast<double>(rz) * core::at(axis, 0));
            double uz = (static_cast<double>(ry) * core::at(axis, 0)) - (rx * core::at(axis, 1));
            auto uz_stored = static_cast<float>(uz);
            const double length2 = std::sqrt((ux * ux) + (uz * uz) + (uy * uy));
            if (length2 > 0.0F) {
                const double inverse = 1.0F / length2;
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
    if (!mesh->material() || !core::at(mesh->material()->textures(), 0)) {
        return;
    }
    BillboardBatch* batch = get_find(mesh->material());
    if (!batch) {
        void* memory = memory::alloc(sizeof(BillboardBatch));
        batch = memory ? memory::construct_in<BillboardBatch>(memory, mesh->material()) : nullptr;
        core::at(batches_, count_) = batch;
        ++count_;
        order_batches(this);
    }
    batch->add_row(mesh);
}

// 0x004CE9F0
void BillboardSet::remove_mesh(MeshRef* mesh)
{
    if (!mesh->material()) {
        return;
    }
    BillboardBatch* batch = get_find(mesh->material());
    if (!batch) {
        return;
    }
    batch->erase_record(mesh);
    for (const memory::ListNode* head : batch->lists()) {
        if (head->prev() != head) {
            return;
        }
    }
    for (int i = 0; i < count_; ++i) {
        if (core::at(batches_, i) != batch) {
            continue;
        }
        for (memory::ListNode* head : batch->lists()) {
            memory::ListNode::destroy(head); // (0x004CE9A0)
        }
        memory::Hub::dealloc(batch);
        core::at(batches_, i) = nullptr;
        break;
    }
    order_batches(this);
    --count_;
}

// 0x004CEAB0
void BillboardSet::render(unsigned int which)
{
    if ((which & 1) != 0U) {
        for (int i = 0; i < count_; ++i) {
            if ((core::at(core::at(batches_, i)->material()->pass_flags(), 0) & kPassFlagBillboard) == 0U) {
                core::at(batches_, i)->render();
            }
        }
    }
    if ((which & 2) != 0U) {
        for (int i = 0; i < count_; ++i) {
            if ((core::at(core::at(batches_, i)->material()->pass_flags(), 0) & kPassFlagBillboard) != 0U) {
                core::at(batches_, i)->render();
            }
        }
    }
}

// 0x004CD8A0
void create_window_pool()
{
    for (int i = 0; i < kWindowPoolSize; ++i) {
        auto* window =
            memory::make_owned<PcWindow>(maths::PixelPoint{.x = 0, .y = 0},
                                         maths::PixelSize{.width = kLayoutWidthPixels, .height = kLayoutHeightPixels});
        window->set_following(screen_2d().free_windows()); // (no check, as the original)
        screen_2d().set_free_windows(window);
    }
}

// 0x004CD940
void mark_billboards_ready()
{
    if (!pc_billboards_state().billboards_ready_) {
        pc_billboards_state().billboards_ready_ = true;
    }
}

// 0x004CD960
void set_flag_786e_b4() { view_setup().set_billboard_flag(1); }

// 0x004CD970
void clear_flag_786e_b4() { view_setup().set_billboard_flag(0); }

} // namespace render
