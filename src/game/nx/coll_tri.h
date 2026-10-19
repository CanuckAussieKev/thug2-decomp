#pragma once

#include <cstddef>
#include <cstdint>

#include "game/mth/geometry.h"
#include "game/mth/vector.h"

namespace Nx {

// A node of a collision geometry's BSP tree (8 bytes; an array of them, made with new[] - a count before the
// first). +0: the low 2 bits the split axis (0 x, 1 y, 2 z) or 3 for a leaf; the rest (>> 2, signed) the split
// position in sixteenths. A leaf has its face count at +2 and a pointer to its face indices (u16) at +4. A
// split node has its two children at +4 (the low 2 bits masked off): the one at +0 is the "less" side (below
// the split) unless bit 0 is set, which swaps them.
struct CCollBSPNode {
    int32_t m_split;
    uint32_t m_link;

    // 0x00414D70 (not a function in Ghidra: new[]'s constructor): zeroed.
    CCollBSPNode* Construct();

    bool IsLeaf() const { return (m_split & 3) == 3; }
    int Axis() const { return m_split & 3; }
    int Split() const { return m_split >> 2; }
    void SetSplit(int value) { m_split = (value << 2) | (m_split & 3); }
    uint16_t LeafFaceCount() const { return reinterpret_cast<const uint16_t*>(this)[1]; }
    uint16_t* LeafFaces() const { return reinterpret_cast<uint16_t*>(m_link); }
    CCollBSPNode* Children() const { return reinterpret_cast<CCollBSPNode*>(m_link & ~3u); }
    CCollBSPNode* LessChild() const { return Children() + ((m_link & 1) ? 1 : 0); }
    CCollBSPNode* GreaterChild() const { return Children() + ((m_link & 1) ? 0 : 1); }

    // 0x00415C90 / 0x00415CE0: the nodes in this subtree / the faces in its leaves.
    int CountNodes() const;
    int CountFaces() const;
    // 0x00415D30: the lowest of the leaves' face pointers (where their shared array starts).
    uint16_t* FirstFaceList() const;
    // 0x00415D80: the splits moved by `delta` (each rounded down to a sixteenth, toward zero).
    void Translate(const Mth::Vector* delta);
    // 0x00415E00: turned by `quarterTurns` (1-3) quarter turns about Y around `centre` (moved there and back
    // when `moveToCentre`): x and z splits swap axis, some negated (their sides swapped).
    void RotateY(const Mth::Vector* centre, int quarterTurns, bool moveToCentre);
    // 0x00415F30: scaled by `scale` around `centre` (moved there and back when `moveToCentre`).
    void Scale(const Mth::Vector* centre, const Mth::Vector* scale, bool moveToCentre);
    // 0x00416000 (cdecl): the tree read from a file fixed up: child offsets + `nodeBase`, leaf face offsets
    // (in u16s) into pointers from `faceBase`. Returns true.
    static bool Relocate(CCollBSPNode* node, uint32_t nodeBase, uint32_t faceBase);
    // 0x00416A10: a copy of the tree and its face indices (the argument unused); null if it has no nodes.
    CCollBSPNode* Clone(int unused) const;
};
static_assert(sizeof(CCollBSPNode) == 8, "layout must match the original");

// A collision object's geometry (Nx::CCollObjTriData, from a .col file): its vertices (floats, or 16-bit offsets
// from its box's minimum in sixteenths when "compressed"), faces and BSP tree.
struct CCollObjTriData {
    uint32_t m_checksum;      // +0x00 the sector's name
    uint16_t m_unknown04;
    uint16_t m_numVertices;   // +0x06
    uint16_t m_numFaces;      // +0x08 (also the size of a sector object's own buffer)
    bool m_smallFaces;        // +0x0A faces of 8 bytes (else 10)
    bool m_compressed;        // +0x0B the vertices are 16-bit
    uint8_t* m_faces;         // +0x0C each: u16 flags, u16 terrain, the three vertex indices (u8 / u16)
    Mth::Vector m_min;        // +0x10 the box (+0x20 its max)
    Mth::Vector m_max;
    void* m_vertices;         // +0x30 float[3] each, or uint16_t[3] when compressed
    CCollBSPNode* m_bsp;      // +0x34
    uint8_t* m_vertexBytes;   // +0x38 one byte per vertex (copied and freed with the geometry), or null
    uint32_t m_unknown3C;     // +0x3C (a movable triangle object keeps it)

    // 0x0040B230: vertex `index` (w = 0).
    Mth::Vector GetRawVertexPos(int index) const;
    // 0x0040B2D0: the same into `out`.
    void GetRawVertexPos(int index, Mth::Vector* out) const;
    // 0x00414EE0: vertex `index` set (compressed: in sixteenths from the box's minimum, toward zero).
    void SetRawVertexPos(int index, const Mth::Vector* position);
    // 0x00414F90 (0x00415C00 jumps to it): every vertex into `out` (w = 0).
    void GetRawVertices(Mth::Vector* out) const;
    // 0x00415C10: every vertex set from `positions`, the box made from them first.
    void SetRawVertices(const Mth::Vector* positions);
    // 0x00414D80: face `face`'s normal ((v1 - v0) x (v2 - v0), normalised unless zero; w = 0).
    Mth::Vector GetFaceNormal(int face) const;
    // 0x00415B70: every field copied (the arrays shared). Returns this.
    CCollObjTriData* CopyFrom(const CCollObjTriData* other);
    // 0x00416BF0: a copy with its own vertices, faces, vertex bytes and tree (`bspArgument` passed to the tree's
    // Clone). Null when `needVertices` and there are none, or an array couldn't be allocated.
    CCollObjTriData* Clone(int bspArgument, bool needVertices) const;
    // 0x00416970: the arrays freed, the tree deleted.
    void Destruct();
    // 0x00416080: the tree and its face indices freed (false if there was none).
    bool DeleteBSP();
    // 0x004169C0: read from a file: the arrays' offsets turned into pointers (the first argument unused).
    bool Relocate(uint32_t unused, uint32_t vertexBase, uint32_t vertexByteBase, uint32_t faceBase,
                  uint32_t nodeBase, uint32_t faceListBase);
    // 0x00416400: vertices (uncompressed), box and tree moved by `delta`.
    void Translate(const Mth::Vector* delta);
    // 0x00416320: the faces in leaves `box` reaches, gathered into `buffer` (`count` set). Without a tree, or
    // more faces than the geometry has, every face: the shared index table 0x0067C6B8.
    uint16_t* GetFacesInBox(const Mth::BBox* box, int* count, uint16_t* buffer, int bufferSize);
    // 0x004160E0: GetFacesInBox's walk.
    void GatherFaces(const CCollBSPNode* node, const Mth::BBox* box);
    // 0x004163B0: whether `point` is behind every face of the leaf it falls in, counted against all faces (the
    // second argument unused).
    bool IsPointBehindFaces(const Mth::Vector* point, int unused);
    // 0x004161D0: its walk (always true).
    bool CountFacesBehind(const CCollBSPNode* node, const Mth::Vector* point, int unused);

    uint32_t FaceVertex(int face, int corner) const
    {
        return m_smallFaces ? m_faces[face * 8 + 4 + corner]
                            : reinterpret_cast<const uint16_t*>(m_faces + face * 10 + 4)[corner];
    }
};
static_assert(offsetof(CCollObjTriData, m_compressed) == 0x0B, "layout must match the original");
static_assert(offsetof(CCollObjTriData, m_min) == 0x10, "layout must match the original");
static_assert(offsetof(CCollObjTriData, m_vertices) == 0x30, "layout must match the original");
static_assert(sizeof(CCollObjTriData) == 0x40, "layout must match the original");

} // namespace Nx
