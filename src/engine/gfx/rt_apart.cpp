#include "engine/gfx/rt_apart.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

#include "engine/gfx/gpu.h"
#include "engine/gfx/rt_scene.h"

// NOT ORIGINAL - the frame's triangles of what is drawn apart, for the rays (rt_apart.h).
namespace render::rt {

namespace {

constexpr std::size_t kHashMix = 0x9E3779B97F4A7C15ULL;
constexpr uint32_t kPositionBytes = 3 * sizeof(float);

// The layout's parts a slot's buffer and mesh are made for: another one means a new slot.
bool same_layout(const Geometry& a, const Geometry& b)
{
    return a.vertex_stride == b.vertex_stride && a.position_offset == b.position_offset &&
           a.uv_offset == b.uv_offset && a.uv_format == b.uv_format && a.colour_offset == b.colour_offset &&
           a.colour_format == b.colour_format && a.uv_sets == b.uv_sets && a.normal_offset == b.normal_offset &&
           a.normal_format == b.normal_format;
}

} // namespace

uint32_t apart_capacity(uint32_t vertices)
{
    uint32_t capacity = ApartStore::kLeastVertices;
    while (capacity < vertices && capacity <= std::numeric_limits<uint32_t>::max() / 2) {
        capacity *= 2;
    }
    return capacity;
}

std::size_t ApartStore::KeyHash::operator()(const Key& key) const
{
    return (std::bit_cast<uintptr_t>(key.key) * kHashMix) ^ key.layout_key;
}

void ApartStore::release(Slot& slot, Gpu& gpu, RayTracing* rt)
{
    if (rt != nullptr && slot.id != 0) {
        rt->remove_mesh(slot.id);
    }
    if (slot.buffer != nullptr) {
        gpu.release_vertex_buffer(slot.buffer);
    }
    slot = {};
}

void ApartStore::commit(std::span<const Batch> batches, Gpu& gpu, RayTracing& rt, std::vector<MeshId>* meshes)
{
    ++round_;
    meshes->clear();
    for (const Batch& batch : batches) {
        const uint32_t stride = batch.layout.vertex_stride;
        const auto count = stride > 0 ? static_cast<uint32_t>(batch.vertices.size() / stride) : 0U;
        const uint32_t triangles_vertices = count - (count % 3);
        if (triangles_vertices == 0 || stride < kPositionBytes || batch.layout.position_offset + kPositionBytes > stride) {
            meshes->push_back(0);
            continue;
        }
        auto [found, made] = slots_.try_emplace(Key{.key = batch.key, .layout_key = batch.layout_key});
        Slot& slot = found->second;
        const uint32_t capacity = apart_capacity(triangles_vertices);
        if (!made && (!same_layout(slot.geometry, batch.layout) || slot.capacity < capacity)) {
            release(slot, gpu, &rt); // (grown, or another layout: made again)
            ++released_;
        }
        const bool fresh = slot.buffer == nullptr;
        if (fresh) {
            const uint32_t bytes = capacity * stride;
            if (gpu.create_vertex_buffer(bytes, gpu_value::kUsageDynamic | gpu_value::kUsageWriteOnly, 0,
                                         gpu_value::kPoolDefault, &slot.buffer) != gpu_value::kResultOk) {
                slot.buffer = nullptr;
                slots_.erase(found);
                meshes->push_back(0);
                continue;
            }
            slot.capacity = capacity;
            ++made_;
        }
        void* data = nullptr;
        if (gpu.lock_vertex_buffer(slot.buffer, 0, 0, &data, gpu_value::kLockDiscard) != gpu_value::kResultOk ||
            data == nullptr) {
            release(slot, gpu, &rt);
            slots_.erase(found);
            meshes->push_back(0);
            continue;
        }
        const std::span<uint8_t> out(static_cast<uint8_t*>(data), std::size_t{slot.capacity} * stride);
        const std::size_t live = std::size_t{triangles_vertices} * stride;
        std::memcpy(out.data(), batch.vertices.data(), live);
        // the rest inactive: a position whose x is not a number makes its triangle no triangle (the structure's build
        // leaves it out); the other bytes cleared
        std::memset(out.subspan(live).data(), 0, out.size() - live);
        const float nothing = std::numeric_limits<float>::quiet_NaN();
        for (std::size_t at = live; at < out.size(); at += stride) {
            std::memcpy(out.subspan(at + batch.layout.position_offset, sizeof(float)).data(), &nothing, sizeof(float));
        }
        gpu.unlock_vertex_buffer(slot.buffer);
        slot.round = round_;
        if (fresh) {
            slot.geometry = batch.layout;
            slot.geometry.vertices = slot.buffer;
            slot.geometry.vertex_offset = 0;
            slot.geometry.vertex_count = slot.capacity;
            slot.geometry.vertex_buffer_bytes = slot.capacity * stride;
            slot.geometry.indices = nullptr;
            slot.geometry.index_offset = 0;
            slot.geometry.index_count = 0;
            slot.geometry.cpu_indices = nullptr;
            slot.geometry.strip = false;
            slot.geometry.dynamic = true;
            slot.geometry.rebuilt = true;
            slot.id = next_mesh_id();
            rt.add_mesh(slot.id, slot.geometry);
        } else {
            rt.mesh_changed(slot.id);
        }
        meshes->push_back(slot.id);
    }
    for (auto it = slots_.begin(); it != slots_.end();) {
        if (it->second.round + kKeepRounds <= round_) {
            release(it->second, gpu, &rt);
            ++released_;
            it = slots_.erase(it);
        } else {
            ++it;
        }
    }
}

void ApartStore::release_all(Gpu& gpu, RayTracing* rt)
{
    for (auto& [key, slot] : slots_) {
        release(slot, gpu, rt);
    }
    slots_.clear();
}

bool ApartStore::owns(MeshId mesh) const
{
    return mesh != 0 && std::ranges::any_of(slots_, [mesh](const auto& entry) { return entry.second.id == mesh; });
}

} // namespace render::rt
