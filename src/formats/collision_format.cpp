#include "formats/collision_format.h"

#include <cstring>
#include <span>
#include "engine/core/checked_index.h"
#include "engine/core/pointer_cell.h"

namespace collision_format {

namespace {

uintptr_t align(uintptr_t at, uintptr_t to) { return (at + to - 1) & ~(to - 1); }

// An object's fields as stored (the TriMeshSource view's offsets).
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct StoredObject {
    uint16_t vertex_count;
    uint16_t face_count;
    bool small_faces;
    bool packed;
    uint32_t faces;
    uint32_t vertices;
    uint32_t tree;
    uint32_t vertex_bytes;
};
// NOLINTEND(readability-identifier-naming)

StoredObject read_object(std::span<const uint8_t> object)
{
    util::ByteReader reader(object);
    StoredObject out{};
    reader.seek(6);
    out.vertex_count = reader.u16();
    out.face_count = reader.u16();
    out.small_faces = reader.u8() != 0;
    out.packed = reader.u8() != 0;
    out.faces = reader.u32();
    reader.seek(0x30);
    out.vertices = reader.u32();
    out.tree = reader.u32();
    out.vertex_bytes = reader.u32();
    return out;
}

bool within(uint64_t offset, uint64_t length, uint64_t section)
{
    return offset <= section && length <= section - offset;
}

// A subtree: its nodes within the node section, its leaves' face lists within theirs and their faces the object's.
bool check_tree(std::span<const uint8_t> nodes, std::span<const uint8_t> lists, uint32_t node, uint32_t face_count,
                int depth, uint32_t* visits)
{
    if (depth > 64 || ++*visits > nodes.size() / 8 || !within(node, 8, nodes.size()))
        return false;
    util::ByteReader reader(nodes.subspan(node, 8));
    const uint32_t tag = reader.u32();
    const uint32_t link = reader.u32();
    if ((tag & 3) == 3) {
        const uint32_t count = (tag >> 16) & 0xFFFF;
        const uint64_t at = static_cast<uint64_t>(link) * 2;
        if (!within(at, static_cast<uint64_t>(count) * 2, lists.size()))
            return false;
        util::ByteReader faces(lists.subspan(static_cast<size_t>(at), static_cast<size_t>(count) * 2));
        for (uint32_t i = 0; i < count; ++i) {
            if (faces.u16() >= face_count)
                return false;
        }
        return true;
    }
    const uint32_t children = link & ~3u;
    return check_tree(nodes, lists, children, face_count, depth + 1, visits) &&
           check_tree(nodes, lists, children + 8, face_count, depth + 1, visits);
}

} // namespace

bool read_collision_header(util::ByteReader& reader, CollisionHeader* out)
{
    out->version = reader.u32();
    out->object_count = reader.i32();
    out->vertex_byte_count = reader.u32();
    out->large_face_count = reader.u32();
    out->small_face_count = reader.u32();
    out->float_vertex_count = reader.u32();
    out->packed_vertex_count = reader.u32();
    out->unknown1c = reader.u32();
    return reader.ok() && out->object_count >= 0;
}

bool lay_out_collision(std::span<const uint8_t> file, CollisionHeader* header, CollisionLayout* layout)
{
    const size_t size = file.size();
    util::ByteReader reader(file);
    if (!read_collision_header(reader, header))
        return false;
    auto start = cell::address(file.data());
    // (as the game computes them - in 32 bits on its platform; counts past what a file can hold fail below)
    const uint64_t objects_size = static_cast<uint64_t>(header->object_count) * kObjectSize;
    const uint64_t vertex_size = 6 * (static_cast<uint64_t>(header->packed_vertex_count) +
                                      2 * static_cast<uint64_t>(header->float_vertex_count));
    const uint64_t face_size =
        11 * static_cast<uint64_t>(header->large_face_count) + 8 * static_cast<uint64_t>(header->small_face_count);
    if (objects_size + vertex_size + header->vertex_byte_count + face_size > size)
        return false;
    layout->objects = start + kHeaderSize;
    layout->vertices = align(layout->objects + static_cast<uintptr_t>(objects_size), 16);
    layout->vertex_bytes = layout->vertices + static_cast<uintptr_t>(vertex_size);
    layout->faces = align(layout->vertex_bytes + header->vertex_byte_count, 4);
    layout->face_buffers = layout->faces + 10 * header->large_face_count + 8 * header->small_face_count;
    layout->node_size_word = layout->faces + align(static_cast<uintptr_t>(face_size), 4);
    layout->end = start + size;
    if (layout->node_size_word + 4 > layout->end || layout->node_size_word < start)
        return false;
    const uint32_t node_size = util::ByteReader(file.subspan(layout->node_size_word - start, 4)).u32();
    layout->nodes = layout->node_size_word + 4;
    layout->face_lists = layout->nodes + node_size;
    return node_size <= layout->end - layout->nodes;
}

bool check_collision_objects(std::span<const uint8_t> file, const CollisionHeader& header, const CollisionLayout& layout)
{
    const uintptr_t start = cell::address(file.data());
    const uint64_t vertex_section = layout.vertex_bytes - layout.vertices;
    const uint64_t face_section = layout.face_buffers - layout.faces;
    const std::span<const uint8_t> nodes = file.subspan(layout.nodes - start, layout.face_lists - layout.nodes);
    const std::span<const uint8_t> lists = file.subspan(layout.face_lists - start);
    for (int i = 0; i < header.object_count; ++i) {
        const StoredObject object =
            read_object(file.subspan(kHeaderSize + (static_cast<size_t>(i) * kObjectSize), kObjectSize));
        uint32_t visits = 0;
        if (!within(object.vertices, static_cast<uint64_t>(object.vertex_count) * (object.packed ? 6 : 12),
                    vertex_section) ||
            !within(object.vertex_bytes, object.vertex_count, header.vertex_byte_count) ||
            !within(object.faces, static_cast<uint64_t>(object.face_count) * (object.small_faces ? 8 : 10),
                    face_section) ||
            !check_tree(nodes, lists, object.tree, object.face_count, 0, &visits))
            return false;
    }
    return true;
}

namespace {

uint32_t word_at(std::span<const uint8_t> bytes, size_t offset)
{
    return core::at(bytes, offset) | core::at(bytes, offset + 1) << 8 | core::at(bytes, offset + 2) << 16 |
           static_cast<uint32_t>(core::at(bytes, offset + 3)) << 24;
}
void set_word_at(std::span<uint8_t> bytes, size_t offset, uint32_t value)
{
    for (size_t k = 0; k < 4; ++k)
        core::at(bytes, offset + k) = static_cast<uint8_t>(value >> (8 * k));
}
void add_base(std::span<uint8_t> bytes, size_t offset, uintptr_t base)
{
    set_word_at(bytes, offset, word_at(bytes, offset) + static_cast<uint32_t>(base));
}

// 0x00416000 (render::HitTreeNode's, cdecl). `nodes`: the node section; `node`: a node's offset in it. The children's
// offsets are the links' (the flag bits off) before they are made addresses.
void relocate_tree(std::span<uint8_t> nodes, size_t node, uint32_t node_base, uint32_t face_base)
{
    const uint32_t tag = word_at(nodes, node);
    const uint32_t link = word_at(nodes, node + 4);
    if ((tag & 3) == 3) {
        set_word_at(nodes, node + 4, face_base + (link * 2));
        return;
    }
    const uint32_t moved = ((link & ~3u) + node_base) | (link & 3);
    set_word_at(nodes, node + 4, moved);
    const size_t children = link & ~3u;
    const bool swapped = (moved & 1) != 0;
    relocate_tree(nodes, children + (swapped ? 8 : 0), node_base, face_base);
    relocate_tree(nodes, children + (swapped ? 0 : 8), node_base, face_base);
}

} // namespace


void relocate_object(std::span<uint8_t> file, int index, const CollisionLayout& layout)
{
    const uintptr_t start = cell::address(file.data());
    const std::span<uint8_t> record =
        file.subspan(kHeaderSize + (static_cast<size_t>(index) * kObjectSize), kObjectSize);
    const uint32_t tree = word_at(record, 0x34);
    add_base(record, 0x30, layout.vertices);
    add_base(record, 0x38, layout.vertex_bytes);
    add_base(record, 0x0C, layout.faces);
    add_base(record, 0x34, layout.nodes);
    relocate_tree(file.subspan(layout.nodes - start, layout.face_lists - layout.nodes), tree,
                  static_cast<uint32_t>(layout.nodes), static_cast<uint32_t>(layout.face_lists));
}

uint8_t& FaceTable::byte(int face, int offset) const
{
    return core::at(bytes_, (static_cast<ptrdiff_t>(face) * stride_) + offset);
}

uint16_t FaceTable::word(int face, int offset) const
{
    return static_cast<uint16_t>(byte(face, offset) | byte(face, offset + 1) << 8);
}

void FaceTable::set_word(int face, int offset, uint16_t value) const
{
    byte(face, offset) = static_cast<uint8_t>(value);
    byte(face, offset + 1) = static_cast<uint8_t>(value >> 8);
}

} // namespace collision_format
