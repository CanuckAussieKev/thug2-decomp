#include "engine/gfx/rt_poses.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "engine/gfx/gpu.h"
#include "engine/gfx/rt_scene.h"

// NOT ORIGINAL - the skinned meshes' poses for the rays (rt_poses.h).
namespace render::rt {

namespace {

constexpr uint32_t kPositionBytes = 3 * sizeof(float);
constexpr std::size_t kHashMix = 0x9E3779B97F4A7C15ULL;

// The layout's parts a pose's buffer and mesh are made for: another one means a new pose.
bool same_layout(const Geometry& a, const Geometry& b)
{
    return a.vertex_stride == b.vertex_stride && a.vertex_count == b.vertex_count &&
           a.position_offset == b.position_offset && a.indices == b.indices && a.index_offset == b.index_offset &&
           a.index_count == b.index_count && a.indices_32bit == b.indices_32bit && a.strip == b.strip &&
           a.cpu_indices == b.cpu_indices && a.uv_offset == b.uv_offset && a.uv_format == b.uv_format &&
           a.colour_offset == b.colour_offset && a.colour_format == b.colour_format && a.uv_sets == b.uv_sets &&
           a.normal_offset == b.normal_offset && a.normal_format == b.normal_format;
}

} // namespace

uint32_t pose_bytes(const Geometry& layout)
{
    if (layout.vertex_count == 0 || layout.vertex_stride < kPositionBytes) {
        return 0;
    }
    return layout.vertex_count * layout.vertex_stride;
}

std::size_t PoseStore::KeyHash::operator()(const Key& key) const
{
    return (std::bit_cast<uintptr_t>(key.owner) * kHashMix) ^ std::bit_cast<uintptr_t>(key.source);
}

void PoseStore::release(Pose& pose, Gpu& gpu, RayTracing* rt)
{
    if (rt != nullptr && pose.id != 0) {
        rt->remove_mesh(pose.id);
    }
    if (pose.buffer != nullptr) {
        gpu.release_vertex_buffer(pose.buffer);
    }
    pose = {};
}

MeshId PoseStore::pose(const void* owner, const void* source, const Geometry& layout, uint64_t frame,
                       const Writer& write, Gpu& gpu, RayTracing& rt)
{
    const uint32_t bytes = pose_bytes(layout);
    if (bytes == 0 || layout.position_offset + kPositionBytes > layout.vertex_stride) {
        return 0;
    }
    const auto [found, made] = poses_.try_emplace(Key{.owner = owner, .source = source});
    Pose& pose = found->second;
    if (!made && pose.frame == frame) {
        return pose.id; // (written this frame already)
    }
    if (!made && (pose.bytes != bytes || !same_layout(pose.geometry, layout))) {
        release(pose, gpu, &rt); // (another mesh at the instance's address, or the layout changed: made again)
    }
    const bool fresh = pose.buffer == nullptr;
    if (fresh && gpu.create_vertex_buffer(bytes, gpu_value::kUsageDynamic | gpu_value::kUsageWriteOnly, 0,
                                          gpu_value::kPoolDefault, &pose.buffer) != gpu_value::kResultOk) {
        pose.buffer = nullptr;
        poses_.erase(found);
        return 0;
    }
    void* data = nullptr;
    if (gpu.lock_vertex_buffer(pose.buffer, 0, 0, &data, gpu_value::kLockDiscard) != gpu_value::kResultOk ||
        data == nullptr) {
        release(pose, gpu, &rt);
        poses_.erase(found);
        return 0;
    }
    const bool written = write(std::span(static_cast<uint8_t*>(data), bytes));
    gpu.unlock_vertex_buffer(pose.buffer);
    if (!written) {
        release(pose, gpu, &rt);
        poses_.erase(found);
        return 0;
    }
    pose.frame = frame;
    pose.bytes = bytes;
    if (fresh) {
        pose.geometry = layout;
        pose.geometry.vertices = pose.buffer;
        pose.geometry.vertex_offset = 0;
        pose.geometry.vertex_buffer_bytes = bytes;
        pose.geometry.dynamic = true;
        pose.id = next_mesh_id();
        rt.add_mesh(pose.id, pose.geometry);
    } else {
        rt.mesh_changed(pose.id);
    }
    return pose.id;
}

void PoseStore::end_frame(uint64_t frame, Gpu& gpu, RayTracing& rt)
{
    for (auto it = poses_.begin(); it != poses_.end();) {
        if (it->second.frame != frame) {
            release(it->second, gpu, &rt);
            it = poses_.erase(it);
        } else {
            ++it;
        }
    }
}

void PoseStore::release_source(const void* source, Gpu& gpu, RayTracing& rt)
{
    for (auto it = poses_.begin(); it != poses_.end();) {
        if (it->first.source == source) {
            release(it->second, gpu, &rt);
            it = poses_.erase(it);
        } else {
            ++it;
        }
    }
}

void PoseStore::release_all(Gpu& gpu, RayTracing* rt)
{
    for (auto& [key, pose] : poses_) {
        release(pose, gpu, rt);
    }
    poses_.clear();
}

MeshId PoseStore::find(const void* owner, const void* source) const
{
    const auto found = poses_.find(Key{.owner = owner, .source = source});
    return found != poses_.end() ? found->second.id : 0;
}

const void* PoseStore::source_of(MeshId mesh) const
{
    if (mesh == 0) {
        return nullptr;
    }
    for (const auto& [key, pose] : poses_) {
        if (pose.id == mesh) {
            return key.source;
        }
    }
    return nullptr;
}

} // namespace render::rt
