#include "engine/render/collision_format.h"

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

StoredObject read_object(const uint8_t* object)
{
    util::ByteReader reader(object, kObjectSize);
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
bool check_tree(const uint8_t* nodes, uint32_t node_size, const uint8_t* lists, uint64_t list_size, uint32_t node,
                uint32_t face_count, int depth, uint32_t* visits)
{
    if (depth > 64 || ++*visits > node_size / 8 || !within(node, 8, node_size))
        return false;
    util::ByteReader reader(nodes + node, 8);
    uint32_t tag = reader.u32();
    uint32_t link = reader.u32();
    if ((tag & 3) == 3) {
        uint32_t count = (tag >> 16) & 0xFFFF;
        uint64_t at = static_cast<uint64_t>(link) * 2;
        if (!within(at, static_cast<uint64_t>(count) * 2, list_size))
            return false;
        util::ByteReader faces(lists + at, static_cast<size_t>(count) * 2);
        for (uint32_t i = 0; i < count; ++i) {
            if (faces.u16() >= face_count)
                return false;
        }
        return true;
    }
    uint32_t children = link & ~3u;
    return check_tree(nodes, node_size, lists, list_size, children, face_count, depth + 1, visits) &&
           check_tree(nodes, node_size, lists, list_size, children + 8, face_count, depth + 1, visits);
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

bool lay_out_collision(const uint8_t* base, size_t size, CollisionHeader* header, CollisionLayout* layout)
{
    if (size == 0)
        size = util::ByteReader::kUnknownSize; // (a size not passed on)
    util::ByteReader reader(base, size);
    if (!read_collision_header(reader, header))
        return false;
    auto start = reinterpret_cast<uintptr_t>(base);
    // (as the game computes them - in 32 bits on its platform; counts past what a file can hold fail below)
    uint64_t objects_size = static_cast<uint64_t>(header->object_count) * kObjectSize;
    uint64_t vertex_size = 6 * (static_cast<uint64_t>(header->packed_vertex_count) +
                                2 * static_cast<uint64_t>(header->float_vertex_count));
    uint64_t face_size =
        11 * static_cast<uint64_t>(header->large_face_count) + 8 * static_cast<uint64_t>(header->small_face_count);
    bool known = size != util::ByteReader::kUnknownSize;
    if (known && objects_size + vertex_size + header->vertex_byte_count + face_size > size)
        return false;
    layout->objects = start + kHeaderSize;
    layout->vertices = align(layout->objects + static_cast<uintptr_t>(objects_size), 16);
    layout->vertex_bytes = layout->vertices + static_cast<uintptr_t>(vertex_size);
    layout->faces = align(layout->vertex_bytes + header->vertex_byte_count, 4);
    layout->face_buffers = layout->faces + 10 * header->large_face_count + 8 * header->small_face_count;
    layout->node_size_word = layout->faces + align(static_cast<uintptr_t>(face_size), 4);
    layout->end = known ? start + size : ~static_cast<uintptr_t>(0);
    if (layout->node_size_word + 4 > layout->end || layout->node_size_word < start)
        return false;
    uint32_t node_size = util::ByteReader(reinterpret_cast<const uint8_t*>(layout->node_size_word), 4).u32();
    layout->nodes = layout->node_size_word + 4;
    layout->face_lists = layout->nodes + node_size;
    return !known || (node_size <= layout->end - layout->nodes);
}

bool check_collision_objects(const uint8_t* base, const CollisionHeader& header, const CollisionLayout& layout)
{
    if (layout.end == ~static_cast<uintptr_t>(0))
        return true;
    uint64_t vertex_section = layout.vertex_bytes - layout.vertices;
    uint64_t face_section = layout.face_buffers - layout.faces;
    auto node_size = static_cast<uint32_t>(layout.face_lists - layout.nodes);
    uint64_t list_size = layout.end - layout.face_lists;
    for (int i = 0; i < header.object_count; ++i) {
        StoredObject object = read_object(base + kHeaderSize + static_cast<size_t>(i) * kObjectSize);
        uint32_t visits = 0;
        if (!within(object.vertices, static_cast<uint64_t>(object.vertex_count) * (object.packed ? 6 : 12),
                    vertex_section) ||
            !within(object.vertex_bytes, object.vertex_count, header.vertex_byte_count) ||
            !within(object.faces, static_cast<uint64_t>(object.face_count) * (object.small_faces ? 8 : 10),
                    face_section) ||
            !check_tree(reinterpret_cast<const uint8_t*>(layout.nodes), node_size,
                        reinterpret_cast<const uint8_t*>(layout.face_lists), list_size, object.tree,
                        object.face_count, 0, &visits))
            return false;
    }
    return true;
}

} // namespace collision_format
