#pragma once

#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <span>

#include "formats/byte_stream.h"
#include "engine/core/pointer_cell.h"
#include "engine/core/raw_view.h"

// NOT ORIGINAL - the collision file format (.col, PC: "x.col.xbx": a level's or a skinned model's collision), its
// header read through util::ByteReader, its sections laid out and checked before the game fixes it up in place.
//
// A collision file (little-endian; the sections aligned as addresses, the file loaded 16-byte aligned):
//   header  u32 version (10), i32 object count, u32 vertex byte count (one per vertex), u32 large face count
//           (10-byte faces), u32 small face count (8-byte faces), u32 float vertex count (12 bytes), u32 packed vertex
//           count (6 bytes: 16-bit sixteenths from the object's box), u32 0
//   objects one 0x40-byte render::TriMeshSource each (used in place: the file-format view in coll_tri.h), its array
//           pointers stored as offsets into the sections below
//   vertices (16-aligned): the float vertices, then the packed ones; then the vertex bytes
//   faces   (4-aligned): the faces, then one byte per large face (the static colliders' own buffers), padded to 4
//   tree    u32 the nodes' size in bytes, the BSP nodes (render::HitTreeNode, 8 bytes; child offsets in bytes from
//           the first node), then the leaves' face lists (u16 face indices; a leaf's offset in u16s)
namespace collision_format {

constexpr int kSmallFaceBytes = 8;       // a face record of a small object
constexpr int kLargeFaceBytes = 10;      // and of a large one
constexpr int kSmallRejectionByte = 7;   // the byte of a small face that tells whether the line can pass

constexpr size_t kHeaderSize = 0x20;
constexpr size_t kObjectSize = 0x40;

// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct CollisionHeader {
    uint32_t version = 0;
    int32_t object_count = 0;
    uint32_t vertex_byte_count = 0;
    uint32_t large_face_count = 0;
    uint32_t small_face_count = 0;
    uint32_t float_vertex_count = 0;
    uint32_t packed_vertex_count = 0;
    uint32_t unknown1c = 0;
};
// NOLINTEND(readability-identifier-naming)

bool read_collision_header(util::ByteReader& reader, CollisionHeader* out);

// The sections' addresses in the file as loaded at `base` (each aligned as the game aligns them).
// NOLINTBEGIN(readability-identifier-naming): a plain record: fields named as plain data
struct CollisionLayout {
    uintptr_t objects = 0;
    uintptr_t vertices = 0;
    uintptr_t vertex_bytes = 0;
    uintptr_t faces = 0;
    uintptr_t face_buffers = 0; // the large faces' bytes (the static colliders')
    uintptr_t node_size_word = 0;
    uintptr_t nodes = 0;
    uintptr_t face_lists = 0;
    uintptr_t end = 0; // the file's end
};
// NOLINTEND(readability-identifier-naming)

// The large faces' buffers (the static colliders' own bytes) as laid out.
inline uint8_t* face_buffers(const CollisionLayout& layout) { return cell::at_address<uint8_t>(layout.face_buffers); }

// The header read and the sections laid out from the file's bytes (loaded 16-byte aligned). False if the header or a
// section isn't within the file.
bool lay_out_collision(std::span<const uint8_t> file, CollisionHeader* header, CollisionLayout* layout);

// Every object's arrays and its tree (before the fix-up: offsets) within their sections. False at the first that
// isn't.
bool check_collision_objects(std::span<const uint8_t> file, const CollisionHeader& header, const CollisionLayout& layout);

// The fix-up of object `index`'s record in place (the objects start at the file's +0x20): its array offsets (faces
// +0x0C, vertices +0x30, tree +0x34, vertex bytes +0x38) made addresses in their sections, and its tree's links -
// a split node's children (the offset in bytes from the first node, its two flag bits kept) and a leaf's face list
// (the offset in u16s into the face lists) - made addresses too, the less child's subtree first.
void relocate_object(std::span<uint8_t> file, int index, const CollisionLayout& layout);
// Object `index`'s record (the file's objects start at +0x20) as the game's own type for it (of the record's size and
// layout), used in place.
template <typename Record>
Record* object_record(std::span<uint8_t> file, int index)
{
    static_assert(sizeof(Record) == kObjectSize, "the collision file's object record");
    return raw::as<Record>(file.subspan(kHeaderSize + (static_cast<size_t>(index) * kObjectSize), kObjectSize).data());
}

// The place the first object's record is (whether or not the file holds one: the original took it so).
template <typename Record>
Record* first_object_record(std::span<uint8_t> file)
{
    return raw::as<Record>(file.subspan(std::min(kHeaderSize, file.size())).data());
}

// A BSP node's link word once fixed up (x86's 32 bits hold an address): a leaf's face list, a split node's children
// (the low two bits its flags), and the link that points at `target` (no flags).
inline uint16_t* tree_leaf_faces(uint32_t link) { return cell::pointer<uint16_t>(link); }
template <typename Node>
Node* tree_children(uint32_t link)
{
    return cell::pointer<Node>(link & ~3u);
}
inline uint32_t tree_link_to(const void* target) { return cell::of(target); }
// A link of a tree copied elsewhere: the same place in the copy (`from` the copied block's start, `to` the copy's),
// the flag bits kept.
inline uint32_t tree_link_moved(uint32_t link, const void* from, const void* to)
{
    return link + (cell::of(to) - cell::of(from));
}

// A collision object's faces, used in place: `count` records of 10 bytes (u16 flags, u16 terrain, three u16 vertex
// indices) or, for "small" objects, of 8 bytes (u16 flags, u16 terrain, three u8 indices and a pad byte).
class FaceTable {
public:
    // `bytes`: the faces' records (as many as the object has).
    FaceTable(std::span<uint8_t> bytes, bool small_faces) : bytes_(bytes), stride_(small_faces ? kSmallFaceBytes : kLargeFaceBytes), small_(small_faces) {}

    [[nodiscard]] uint16_t flags(int face) const { return word(face, 0); }
    [[nodiscard]] uint16_t terrain(int face) const { return word(face, 2); }
    void set_flags(int face, uint16_t flags) { set_word(face, 0, flags); }
    void set_terrain(int face, uint16_t terrain) { set_word(face, 2, terrain); }
    // Corner `corner` (0-2)'s vertex index.
    [[nodiscard]] uint32_t vertex(int face, int corner) const
    {
        return small_ ? byte(face, 4 + corner) : word(face, 4 + (2 * corner));
    }
    void set_vertex(int face, int corner, uint32_t index)
    {
        if (small_)
            byte(face, 4 + corner) = static_cast<uint8_t>(index);
        else
            set_word(face, 4 + (2 * corner), static_cast<uint16_t>(index));
    }
    // A small face's rejection byte (its last; the large faces keep theirs in a buffer of their own).
    [[nodiscard]] uint8_t small_rejection(int face) const { return byte(face, kSmallRejectionByte); }
    [[nodiscard]] int stride() const { return stride_; }

private:
    [[nodiscard]] uint8_t& byte(int face, int offset) const;
    [[nodiscard]] uint16_t word(int face, int offset) const;
    void set_word(int face, int offset, uint16_t value) const;

    std::span<uint8_t> bytes_;
    int stride_;
    bool small_;
};

} // namespace collision_format
