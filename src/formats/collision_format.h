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

} // namespace collision_format
