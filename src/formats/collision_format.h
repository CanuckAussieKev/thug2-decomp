#pragma once

#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>

#include "base/core/api.h"
#include "formats/byte_stream.h"
#include "base/core/enum_ops.h"
#include "base/core/address.h"
#include "base/core/raw_view.h"

#include <gsl/assert>

// NOT ORIGINAL - the collision file format (.col, PC: "x.col.xbx": a level's or a skinned model's collision), its
// header read through util::ByteReader, its sections laid out and checked before the game fixes it up in place (its
// offsets made links: link_from below).
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

// A face's 16 flag bits (the first word of its record). The collision file carries any word, so bits the game never
// names stay representable; the face table reads and writes the raw word once.
enum class FaceFlag : uint16_t {
    kNone = 0,
    kSkatable = 0x1,
    kNotSkatable = 0x2,      // not stood on
    kWall = 0x4,
    kVert = 0x8,             // a vert face (acid drops, wallplants and recoveries treat it so)
    kNotCollidable = 0x10,   // passed through (a face the editor takes out carries it too)
    kShadeSurface = 0x20,    // the faces the shading and lighting probes look for
    kTrigger = 0x40,         // a trigger: the one flag a hit record keeps (0 or 0x40)
    kCameraPasses = 0x80,    // the cameras' probes pass through it
    kNoLandingProbe = 0x800, // the landing adjustment's probe passes through it
    kNoParkEdit = 0x1000,    // counts only outside the park editor (rails); a tag's rays pass through it
    kNoTagging = 0x2000,     // a tag's rays pass through it
};
constexpr bool enable_flag_operators(FaceFlag /*unused*/) { return true; }

constexpr int kSmallFaceBytes = 8;       // a face record of a small object
constexpr int kLargeFaceBytes = 10;      // and of a large one
constexpr int kSmallRejectionByte = 7;   // the byte of a small face that tells whether the line can pass

constexpr size_t kHeaderSize = 0x20;
constexpr size_t kObjectSize = 0x40;

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

bool read_collision_header(util::ByteReader& reader, CollisionHeader* out);

// The sections' addresses in the file as loaded at `base` (each aligned as the game aligns them).
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

// The large faces' buffers (the static colliders' own bytes) as laid out.
inline uint8_t* face_buffers(const CollisionLayout& layout) { return core::at_address<uint8_t>(layout.face_buffers); }

// The header read and the sections laid out from the file's bytes (loaded 16-byte aligned). False if the header or a
// section isn't within the file.
bool lay_out_collision(std::span<const uint8_t> file, CollisionHeader* header, CollisionLayout* layout);

// Every object's arrays and its tree (before the fix-up: offsets) within their sections, its tree's first node
// 4-aligned (as every node is: a node's links are distances from it). False at the first that isn't.
bool check_collision_objects(std::span<const uint8_t> file, const CollisionHeader& header, const CollisionLayout& layout);

// The fix-up of object `index`'s record in place (the objects start at the file's +0x20): its array offsets (faces
// +0x0C, vertices +0x30, tree +0x34, vertex bytes +0x38) made links from the record to their places in their
// sections, and its tree's offsets - a split node's children (the offset in bytes from the first node, its two flag
// bits kept) and a leaf's face list (the offset in u16s into the face lists) - links from the node, the less child's
// subtree first. (The original made them all addresses.)
void relocate_object(std::span<uint8_t> file, int index, const CollisionLayout& layout);
// Object `index`'s record (the file's objects start at +0x20) as the game's own type for it (of the record's size and
// layout), used in place.
template <typename Record>
Record* object_record(std::span<uint8_t> file, int index)
{
    static_assert(sizeof(Record) == kObjectSize, "the collision file's object record");
    return raw::as<Record>(file.subspan(kHeaderSize + (static_cast<size_t>(index) * kObjectSize), kObjectSize).data());
}

// NOT ORIGINAL - an object record made at the start of a block of our own that also holds its arrays (`block`: the
// record, then the arrays), so that its links stay short on any build: separate allocations can lie more than 2 GB
// apart on x64 (link_from). Value-initialised; the block keeps it (nothing to end: the record is plain data).
template <typename Record>
Record* record_in_block(std::span<uint8_t> block)
{
    static_assert(sizeof(Record) == kObjectSize && std::is_trivially_destructible_v<Record>, "an object record");
    Expects(block.size() >= kObjectSize && core::address(block.data()) % alignof(Record) == 0);
    return std::construct_at(raw::as<Record>(block.data()));
}

// The place the first object's record is (whether or not the file holds one: the original took it so).
template <typename Record>
Record* first_object_record(std::span<uint8_t> file)
{
    return raw::as<Record>(file.subspan(std::min(kHeaderSize, file.size())).data());
}

// The links of a laid-out collision file - an object record's arrays, a BSP node's children and its face list - are
// 32 bits each: the distance in bytes from the record or node that holds the link to what it links to (the original
// wrote 32-bit addresses there). The same on any build and wherever the data is: a tree copied as one block keeps its
// children's links as they are. A distance is less than 2 GB either way; on x64 a target farther than that stops the
// game (GSL's contract check) instead of losing the distance's top bits.
inline uint32_t link_from(const void* holder, const void* target) noexcept
{
    const uintptr_t distance = core::address(target) - core::address(holder);
    if constexpr (sizeof(uintptr_t) > sizeof(uint32_t)) {
        const auto signed_distance = static_cast<intptr_t>(distance);
        Expects(signed_distance >= INT32_MIN && signed_distance <= INT32_MAX); // (x64: too far for a link)
    }
    return static_cast<uint32_t>(distance);
}
// What a link held at `holder` links to. (The address round trip is std::bit_cast's own builtin - the same on MSVC,
// GCC and Clang - written out here and in record_array: a debug build calls every function, std::bit_cast too, and
// the collision queries resolve links in their inner loops - a face's record, its vertices.)
template <typename T>
T* linked(const void* holder, uint32_t link) noexcept
{
    const auto distance = static_cast<intptr_t>(static_cast<int32_t>(link));
    return __builtin_bit_cast(T*, __builtin_bit_cast(uintptr_t, holder) + static_cast<uintptr_t>(distance));
}

// An object record's array link: null as 0 (an array is never the record itself).
inline uint32_t record_link(const void* record, const void* target)
{
    if (!target) {
        return 0;
    }
    Expects(target != record);
    return link_from(record, target);
}
template <typename T>
T* record_array(const void* record, uint32_t link) noexcept
{
    const auto distance = static_cast<intptr_t>(static_cast<int32_t>(link));
    return link != 0 ? __builtin_bit_cast(T*, __builtin_bit_cast(uintptr_t, record) + static_cast<uintptr_t>(distance)) : nullptr;
}

// A BSP node's link word (collision_format.h's tree, render::HitTreeNode): a leaf's face list, a split node's
// children (the link's low two bits its flags), and the link from `node` to `target` (no flags).
inline uint16_t* tree_leaf_faces(const void* node, uint32_t link) noexcept { return linked<uint16_t>(node, link); }
template <typename Node>
Node* tree_children(const Node* node, uint32_t link) noexcept
{
    return linked<Node>(node, link & ~3U);
}
inline uint32_t tree_link_to(const void* node, const void* target) noexcept { return link_from(node, target); }
// A leaf copied: its node from `node` to `copy`, the block of face lists its link reaches from `faces_from` to
// `faces_to`.
struct LeafCopy {
    const void* node = nullptr;
    const void* copy = nullptr;
    const void* faces_from = nullptr;
    const void* faces_to = nullptr;
};
// The copied leaf's link: the same place in the copied face lists. (A split node's link needs nothing: its children
// are copied with it, the same distance away.)
inline uint32_t tree_leaf_link_moved(uint32_t link, const LeafCopy& leaf)
{
    const uintptr_t target =
        core::address(tree_leaf_faces(leaf.node, link)) - core::address(leaf.faces_from) + core::address(leaf.faces_to);
    return link_from(leaf.copy, core::at_address<const void>(target));
}

// A collision object's faces, used in place: `count` records of 10 bytes (u16 flags, u16 terrain, three u16 vertex
// indices) or, for "small" objects, of 8 bytes (u16 flags, u16 terrain, three u8 indices and a pad byte).
class FaceTable {
public:
    // `bytes`: the faces' records (as many as the object has).
    FaceTable(std::span<uint8_t> bytes, bool small_faces) : bytes_(bytes), stride_(small_faces ? kSmallFaceBytes : kLargeFaceBytes), small_(small_faces) {}

    [[nodiscard]] FaceFlag flags(int face) const { return static_cast<FaceFlag>(word(face, 0)); }
    [[nodiscard]] uint16_t terrain(int face) const { return word(face, 2); }
    void set_flags(int face, FaceFlag flags) { set_word(face, 0, core::to_underlying(flags)); }
    void set_terrain(int face, uint16_t terrain) { set_word(face, 2, terrain); }
    // Corner `corner` (0-2)'s vertex index.
    [[nodiscard]] uint32_t vertex(int face, int corner) const
    {
        return small_ ? byte(face, 4 + corner) : word(face, 4 + (2 * corner));
    }
    void set_vertex(int face, int corner, uint32_t index)
    {
        if (small_) {
            byte(face, 4 + corner) = static_cast<uint8_t>(index);
        } else {
            set_word(face, 4 + (2 * corner), static_cast<uint16_t>(index));
        }
    }
    // A small face's rejection byte (its last; the large faces keep theirs in a buffer of their own).
    [[nodiscard]] uint8_t small_rejection(int face) const { return byte(face, kSmallRejectionByte); }
    [[nodiscard]] int stride() const { return stride_; }

private:
    [[nodiscard]] THUG2_API uint8_t& byte(int face, int offset) const;
    [[nodiscard]] THUG2_API uint16_t word(int face, int offset) const;
    THUG2_API void set_word(int face, int offset, uint16_t value) const;

    std::span<uint8_t> bytes_;
    int stride_;
    bool small_;
};

} // namespace collision_format
