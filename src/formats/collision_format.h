#pragma once

#include <cstddef>
#include <cstdint>

#include "formats/byte_stream.h"

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

// The header read and the sections laid out from `base`. `size`: the file's bytes, or 0 /
// util::ByteReader::kUnknownSize when not known (then nothing past the header is checked). False if the header or a
// section isn't within the file.
bool lay_out_collision(const uint8_t* base, size_t size, CollisionHeader* header, CollisionLayout* layout);

// Every object's arrays and its tree (before the fix-up: offsets) within their sections. False at the first that
// isn't. (Not checked when the size isn't known.)
bool check_collision_objects(const uint8_t* base, const CollisionHeader& header, const CollisionLayout& layout);

// A collision object's faces, used in place: `count` records of 10 bytes (u16 flags, u16 terrain, three u16 vertex
// indices) or, for "small" objects, of 8 bytes (u16 flags, u16 terrain, three u8 indices and a pad byte).
class FaceTable {
public:
    FaceTable(uint8_t* bytes, bool small_faces) : bytes_(bytes), stride_(small_faces ? 8 : 10), small_(small_faces) {}

    [[nodiscard]] uint16_t flags(int face) const { return word(face, 0); }
    [[nodiscard]] uint16_t terrain(int face) const { return word(face, 2); }
    void set_flags(int face, uint16_t flags) { set_word(face, 0, flags); }
    void set_terrain(int face, uint16_t terrain) { set_word(face, 2, terrain); }
    // Corner `corner` (0-2)'s vertex index.
    [[nodiscard]] uint32_t vertex(int face, int corner) const
    {
        return small_ ? at(face, 4 + corner)[0] : word(face, 4 + (2 * corner));
    }
    void set_vertex(int face, int corner, uint32_t index)
    {
        if (small_)
            at(face, 4 + corner)[0] = static_cast<uint8_t>(index);
        else
            set_word(face, 4 + (2 * corner), static_cast<uint16_t>(index));
    }
    // A small face's rejection byte (its last; the large faces keep theirs in a buffer of their own).
    [[nodiscard]] uint8_t small_rejection(int face) const { return at(face, 7)[0]; }
    [[nodiscard]] int stride() const { return stride_; }

private:
    [[nodiscard]] uint8_t* at(int face, int offset) const;
    [[nodiscard]] uint16_t word(int face, int offset) const;
    void set_word(int face, int offset, uint16_t value) const;

    uint8_t* bytes_;
    int stride_;
    bool small_;
};

} // namespace collision_format
