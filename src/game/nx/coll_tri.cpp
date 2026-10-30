// Collision geometry (CCollObjTriData) and its BSP tree (CCollBSPNode).

#include "game/nx/coll_tri.h"

#include <cmath>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_glue.h"
#include "hook/game.h"
#include "hook/game_global.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr float kSixteenth = 0.0625f;
constexpr float kSixteen = 16.0f;

// The shared index table every face of a geometry is listed by when there's no tree to narrow them down.
GAME_GLOBAL(uint16_t[2048], g_allFaces, 0x0067C6B8, 0xFFFF);
uint16_t* AllFaces() { return g_allFaces; }

GAME_GLOBAL(uint16_t*, g_faceBuffer, 0x00691968);
GAME_GLOBAL(int, g_faceCount, 0x0069196C);
GAME_GLOBAL(int, g_faceBufferSize, 0x00691970);

// GetFacesInBox's / IsPointBehindFaces' state while walking the tree.
uint16_t*& s_faceBuffer = g_faceBuffer;
int& s_faceCount = g_faceCount;
int& s_faceBufferSize = g_faceBufferSize;

// The compiler's float-to-integer conversion (_ftol: toward zero, the low 32 bits of the 64-bit result).
int Truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

float Component(const Mth::Vector* v, int axis) { return (&v->x)[axis]; }

} // namespace

// 0x0040B230
Mth::Vector CCollObjTriData::GetRawVertexPos(int index) const
{
    if (m_compressed) {
        const uint16_t* v = static_cast<const uint16_t*>(m_vertices) + 3 * index;
        return {v[0] * 0.0625f + m_min.x, v[1] * 0.0625f + m_min.y, v[2] * 0.0625f + m_min.z, 0.0f};
    }
    const float* v = static_cast<const float*>(m_vertices) + 3 * index;
    return {v[0], v[1], v[2], 0.0f};
}

// 0x0040B2D0 (the uncompressed vertex copied as raw words)
void CCollObjTriData::GetRawVertexPos(int index, Mth::Vector* out) const
{
    if (m_compressed) {
        const uint16_t* v = static_cast<const uint16_t*>(m_vertices) + 3 * index;
        out->x = v[0] * 0.0625f + m_min.x;
        out->y = v[1] * 0.0625f + m_min.y;
        out->z = v[2] * 0.0625f + m_min.z;
        out->w = 0.0f;
        return;
    }
    std::memcpy(out, static_cast<const float*>(m_vertices) + 3 * index, 12);
    out->w = 0.0f;
}

// 0x00414EE0 (x's offset isn't rounded to a float before it's scaled; y's and z's are)
void CCollObjTriData::SetRawVertexPos(int index, const Mth::Vector* position)
{
    if (m_compressed) {
        double x = static_cast<double>(position->x) - m_min.x;
        float y = static_cast<float>(static_cast<double>(position->y) - m_min.y);
        float z = static_cast<float>(static_cast<double>(position->z) - m_min.z);
        uint16_t* v = static_cast<uint16_t*>(m_vertices) + 3 * index;
        v[0] = static_cast<uint16_t>(Truncate(x * kSixteen));
        v[1] = static_cast<uint16_t>(Truncate(static_cast<double>(y) * kSixteen));
        v[2] = static_cast<uint16_t>(Truncate(static_cast<double>(z) * kSixteen));
        return;
    }
    std::memcpy(static_cast<float*>(m_vertices) + 3 * index, position, 12);
}

// 0x00414F90
void CCollObjTriData::GetRawVertices(Mth::Vector* out) const
{
    for (int i = 0; i < m_numVertices; ++i)
        out[i] = GetRawVertexPos(i);
}

// 0x00415C10
void CCollObjTriData::SetRawVertices(const Mth::Vector* positions)
{
    auto* box = reinterpret_cast<Mth::BBox*>(&m_min);
    box->min = {10000000.0f, 10000000.0f, 10000000.0f, 1.0f};
    box->max = {-10000000.0f, -10000000.0f, -10000000.0f, 1.0f};
    for (int i = 0; i < m_numVertices; ++i)
        box->AddPoint(positions[i]);
    for (int i = 0; i < m_numVertices; ++i)
        SetRawVertexPos(i, &positions[i]);
}

// 0x00414D80 (one of the edge's z kept unrounded for the normal's x, as the original's x87 code does)
Mth::Vector CCollObjTriData::GetFaceNormal(int face) const
{
    Mth::Vector v0 = GetRawVertexPos(FaceVertex(face, 0));
    Mth::Vector v1 = GetRawVertexPos(FaceVertex(face, 1));
    Mth::Vector v2 = GetRawVertexPos(FaceVertex(face, 2));
    float ax = static_cast<float>(static_cast<double>(v1.x) - v0.x);
    float ay = static_cast<float>(static_cast<double>(v1.y) - v0.y);
    float az = static_cast<float>(static_cast<double>(v1.z) - v0.z);
    float bx = static_cast<float>(static_cast<double>(v2.x) - v0.x);
    float by = static_cast<float>(static_cast<double>(v2.y) - v0.y);
    double bzExact = static_cast<double>(v2.z) - v0.z;
    float bz = static_cast<float>(bzExact);
    double nx = bzExact * ay - static_cast<double>(by) * az;
    double ny = static_cast<double>(az) * bx - static_cast<double>(bz) * ax;
    double nz = static_cast<double>(by) * ax - static_cast<double>(ay) * bx;
    double length = std::sqrt((nz * nz + ny * ny) + nx * nx);
    if (length > 0.0) {
        double inverse = 1.0 / length;
        nx *= inverse;
        ny *= inverse;
        nz *= inverse;
    }
    return {static_cast<float>(nx), static_cast<float>(ny), static_cast<float>(nz), 0.0f};
}

// 0x00415B70
CCollObjTriData* CCollObjTriData::CopyFrom(const CCollObjTriData* other)
{
    *this = *other;
    return this;
}

// 0x00416BF0 (a failed allocation of the copy itself crashes, as in the original; so does a missing tree)
CCollObjTriData* CCollObjTriData::Clone(int bspArgument, bool needVertices) const
{
    if (needVertices && m_numVertices == 0)
        return nullptr;
    void* memory = Mem::New(sizeof(CCollObjTriData));
    CCollObjTriData* copy = memory ? static_cast<CCollObjTriData*>(memory)->CopyFrom(this) : nullptr;
    uint32_t vertexSize = m_numVertices * (m_compressed ? 6u : 12u);
    copy->m_vertices = Mem::New(vertexSize);
    uint32_t faceSize = m_numFaces * (m_smallFaces ? 8u : 10u);
    copy->m_faces = static_cast<uint8_t*>(Mem::New(faceSize));
    if (!copy->m_vertices || !copy->m_faces)
        return nullptr;
    std::memcpy(copy->m_vertices, m_vertices, vertexSize);
    std::memcpy(copy->m_faces, m_faces, faceSize);
    if (m_vertexBytes) {
        copy->m_vertexBytes = static_cast<uint8_t*>(Mem::New(m_numVertices));
        std::memcpy(copy->m_vertexBytes, m_vertexBytes, m_numVertices);
    }
    copy->m_bsp = m_bsp->Clone(bspArgument);
    return copy;
}

// 0x00416970
void CCollObjTriData::Destruct()
{
    if (m_vertices)
        Mem::Manager::Get()->Free(m_vertices);
    if (m_vertexBytes)
        Mem::Manager::Get()->Free(m_vertexBytes);
    if (m_faces)
        Mem::Manager::Get()->Free(m_faces);
    DeleteBSP();
}

// 0x00416080 (the nodes' destructor - 0x005480A0, run by delete[] for each - is empty)
bool CCollObjTriData::DeleteBSP()
{
    if (!m_bsp)
        return false;
    if (uint16_t* faces = m_bsp->FirstFaceList())
        Mem::Manager::Get()->Free(faces);
    if (m_bsp)
        Mem::Manager::Get()->Free(reinterpret_cast<uint8_t*>(m_bsp) - 4);
    m_bsp = nullptr;
    return true;
}

// 0x004169C0
bool CCollObjTriData::Relocate(uint32_t, uint32_t vertexBase, uint32_t vertexByteBase, uint32_t faceBase,
                               uint32_t nodeBase, uint32_t faceListBase)
{
    m_vertices = reinterpret_cast<void*>(reinterpret_cast<uint32_t>(m_vertices) + vertexBase);
    m_vertexBytes = reinterpret_cast<uint8_t*>(reinterpret_cast<uint32_t>(m_vertexBytes) + vertexByteBase);
    m_faces = reinterpret_cast<uint8_t*>(reinterpret_cast<uint32_t>(m_faces) + faceBase);
    m_bsp = reinterpret_cast<CCollBSPNode*>(reinterpret_cast<uint32_t>(m_bsp) + nodeBase);
    CCollBSPNode::Relocate(m_bsp, nodeBase, faceListBase);
    return true;
}

// 0x00416400
void CCollObjTriData::Translate(const Mth::Vector* delta)
{
    if (m_bsp)
        m_bsp->Translate(delta);
    if (!m_compressed) {
        float* v = static_cast<float*>(m_vertices);
        for (int i = 0; i < m_numVertices; ++i, v += 3) {
            v[0] = static_cast<float>(static_cast<double>(v[0]) + delta->x);
            v[1] = static_cast<float>(static_cast<double>(v[1]) + delta->y);
            v[2] = static_cast<float>(static_cast<double>(delta->z) + v[2]);
        }
    }
    auto moved = [delta](const Mth::Vector& p) {
        return Mth::Vector{static_cast<float>(static_cast<double>(p.x) + delta->x),
                           static_cast<float>(static_cast<double>(p.y) + delta->y),
                           static_cast<float>(static_cast<double>(p.z) + delta->z),
                           static_cast<float>(static_cast<double>(p.w) + delta->w)};
    };
    Mth::Vector min = moved(m_min);
    Mth::Vector max = moved(m_max);
    m_min = min;
    m_max = max;
}

// 0x004164F0 (a failed allocation of the temporary array crashes - the vertices are read into null - as in the
// original; the array's element constructor, 0x005902E0, does nothing)
void CCollObjTriData::RotateY(const Mth::Vector* centre, int quarterTurns)
{
    Mth::Vector back{-centre->x, -centre->y, -centre->z, -centre->w};
    Translate(&back);
    if (m_bsp)
        m_bsp->RotateY(centre, quarterTurns, false);
    Mth::Vector* positions = nullptr;
    if (m_compressed) {
        Mem::PushContext(Mem::TopDownHeap());
        positions = static_cast<Mth::Vector*>(Mem::New(m_numVertices * sizeof(Mth::Vector)));
        Mem::PopContext();
        GetRawVertices(positions);
    }
    for (int i = 0; i < m_numVertices; ++i) {
        if (positions) {
            positions[i].RotateY90(quarterTurns);
            continue;
        }
        Mth::Vector v;
        GetRawVertexPos(i, &v);
        v.RotateY90(quarterTurns);
        float* stored = static_cast<float*>(m_vertices) + 3 * i;
        stored[0] = v.x;
        stored[1] = v.y;
        stored[2] = v.z;
    }
    Mth::Vector min = m_min;
    Mth::Vector max = m_max;
    min.RotateY90(quarterTurns);
    max.RotateY90(quarterTurns);
    auto* box = reinterpret_cast<Mth::BBox*>(&m_min);
    box->min = {10000000.0f, 10000000.0f, 10000000.0f, 1.0f};
    box->max = {-10000000.0f, -10000000.0f, -10000000.0f, 1.0f};
    box->AddPoint(min);
    box->AddPoint(max);
    if (positions) {
        for (int i = 0; i < m_numVertices; ++i)
            SetRawVertexPos(i, &positions[i]);
        Mem::Manager::Get()->Free(positions);
    }
    Translate(centre);
    m_unknown3C = 0;
}

// 0x00416730 (a failed allocation crashes, as in RotateY)
void CCollObjTriData::Scale(const Mth::Vector* centre, const Mth::Vector* scale)
{
    Mth::Vector back{-centre->x, -centre->y, -centre->z, -centre->w};
    Translate(&back);
    if (m_bsp)
        m_bsp->Scale(centre, scale, false);
    Mth::Vector* positions = nullptr;
    if (m_compressed) {
        Mem::PushContext(Mem::TopDownHeap());
        positions = static_cast<Mth::Vector*>(Mem::New(m_numVertices * sizeof(Mth::Vector)));
        Mem::PopContext();
        GetRawVertices(positions);
    }
    for (int i = 0; i < m_numVertices; ++i) {
        float* v = positions ? &positions[i].x : static_cast<float*>(m_vertices) + 3 * i;
        v[0] *= scale->x;
        v[1] *= scale->y;
        v[2] *= scale->z;
    }
    m_min = {m_min.x * scale->x, m_min.y * scale->y, m_min.z * scale->z, m_min.w};
    m_max = {m_max.x * scale->x, m_max.y * scale->y, m_max.z * scale->z, m_max.w};
    if (positions) {
        for (int i = 0; i < m_numVertices; ++i)
            SetRawVertexPos(i, &positions[i]);
        Mem::Manager::Get()->Free(positions);
    }
    Translate(centre);
    m_unknown3C = 0;
}

// 0x00414D30
bool CCollObjTriData::SetUpAllFaces()
{
    uint16_t* table = AllFaces();
    if (table[0] != 0) {
        for (int i = 0; i < 0x800; ++i)
            table[i] = static_cast<uint16_t>(i);
    }
    return m_bsp != nullptr;
}

// 0x00414FE0 (the original also reads each face's flags and tests a sign they can't have, and reads the first
// face's corners before each pass; neither has an effect)
void CCollObjTriData::AddOcclusionPolys()
{
    constexpr float kSameDirection = 0.05f;
    Mth::Vector points[4];
    for (int i = 0; i < m_numFaces - 1; ++i) {
        for (int j = i + 1; j < m_numFaces; ++j) {
            // the corners shared: shared[k] is face i's corner k if face j has it, other[m] face j's corner m if
            // face i has it
            int shared[3] = {-1, -1, -1};
            int other[3] = {-1, -1, -1};
            int matches = 0;
            for (int k = 0; k < 3; ++k) {
                uint16_t corner = static_cast<uint16_t>(FaceVertex(i, k));
                for (int m = 0; m < 3; ++m) {
                    if (corner == static_cast<uint16_t>(FaceVertex(j, m))) {
                        shared[k] = corner;
                        other[m] = static_cast<int>(FaceVertex(j, m));
                        ++matches;
                        break;
                    }
                }
            }
            if (matches != 2)
                continue;
            // both normals ((v1 - v0) x (v2 - v0)), rounded where the original's x87 code stores them
            Mth::Vector a = GetRawVertexPos(FaceVertex(i, 0));
            Mth::Vector b = GetRawVertexPos(FaceVertex(i, 1));
            Mth::Vector c = GetRawVertexPos(FaceVertex(i, 2));
            double cx = static_cast<double>(c.x) - a.x;
            double cy = static_cast<double>(c.y) - a.y;
            double cz = static_cast<double>(c.z) - a.z;
            float bx = static_cast<float>(static_cast<double>(b.x) - a.x);
            float by = static_cast<float>(static_cast<double>(b.y) - a.y);
            double bz = static_cast<double>(b.z) - a.z;
            float n1x = static_cast<float>(by * cz - bz * cy);
            float n1y = static_cast<float>(bz * cx - cz * bx);
            float n1z = static_cast<float>(cy * bx - by * cx);
            Mth::Vector a2 = GetRawVertexPos(FaceVertex(j, 0));
            Mth::Vector b2 = GetRawVertexPos(FaceVertex(j, 1));
            Mth::Vector c2 = GetRawVertexPos(FaceVertex(j, 2));
            points[0] = a2;
            points[1] = b2;
            points[2] = c2;
            auto difference = [](float p, float q) { return static_cast<float>(static_cast<double>(p) - q); };
            float cx2 = difference(c2.x, a2.x), cy2 = difference(c2.y, a2.y), cz2 = difference(c2.z, a2.z);
            float bx2 = difference(b2.x, a2.x), by2 = difference(b2.y, a2.y), bz2 = difference(b2.z, a2.z);
            double n2x = static_cast<double>(by2) * cz2 - static_cast<double>(bz2) * cy2;
            float n2y = static_cast<float>(static_cast<double>(bz2) * cx2 - static_cast<double>(cz2) * bx2);
            float n2z = static_cast<float>(static_cast<double>(cy2) * bx2 - static_cast<double>(by2) * cx2);
            double unit1x = n1x;
            double length1 = std::sqrt((unit1x * n1x + static_cast<double>(n1z) * n1z) + static_cast<double>(n1y) * n1y);
            if (length1 > 0.0) {
                double inverse = 1.0 / length1;
                unit1x = n1x * inverse;
                n1y = static_cast<float>(n1y * inverse);
                n1z = static_cast<float>(n1z * inverse);
            }
            double length2 = std::sqrt((n2x * n2x + static_cast<double>(n2z) * n2z) + static_cast<double>(n2y) * n2y);
            if (length2 > 0.0) {
                double inverse = 1.0 / length2;
                n2x *= inverse;
                n2y = static_cast<float>(n2y * inverse);
                n2z = static_cast<float>(n2z * inverse);
            }
            if (!(std::fabs(n2x - unit1x) <= kSameDirection) ||
                !(std::fabs(static_cast<double>(n2y) - n1y) <= kSameDirection) ||
                !(std::fabs(static_cast<double>(n2z) - n1z) <= kSameDirection))
                continue;
            // the quad: face i's corners with face j's other corner between the two shared ones
            int extra = -1;
            for (int m = 0; m < 3; ++m) {
                if (other[m] == -1) {
                    extra = static_cast<int>(FaceVertex(j, m));
                    break;
                }
            }
            auto corner = [this, i](int k) { return GetRawVertexPos(FaceVertex(i, k)); };
            if (shared[0] >= 0 && shared[1] >= 0) {
                points[0] = corner(0);
                points[1] = GetRawVertexPos(extra);
                points[2] = corner(1);
                points[3] = corner(2);
            } else if (shared[0] < 0 && shared[1] >= 0 && shared[2] >= 0) {
                points[0] = corner(0);
                points[1] = corner(1);
                points[2] = GetRawVertexPos(extra);
                points[3] = corner(2);
            } else if (shared[0] >= 0 && shared[2] >= 0) {
                points[0] = corner(0);
                points[1] = corner(1);
                points[2] = corner(2);
                points[3] = GetRawVertexPos(extra);
            }
            // (otherwise - not possible with two shared corners - face j's corners as they are)
            AddDebugShape(4, points, m_checksum);
            break;
        }
    }
}

// 0x00416320
uint16_t* CCollObjTriData::GetFacesInBox(const Mth::BBox* box, int* count, uint16_t* buffer, int bufferSize)
{
    if (!m_bsp) {
        *count = m_numFaces;
        return AllFaces();
    }
    s_faceBuffer = buffer;
    s_faceBufferSize = bufferSize;
    s_faceCount = 0;
    GatherFaces(m_bsp, box);
    uint16_t* result;
    if (s_faceCount < 0) {
        *count = m_numFaces;
        result = AllFaces();
    } else {
        *count = s_faceCount;
        result = s_faceBuffer;
    }
    s_faceBuffer = nullptr;
    s_faceBufferSize = 0;
    return result;
}

// 0x004160E0 (the less side when the box starts strictly below the split, the greater when it ends at or
// above it; NaN takes neither; too many faces mark the count -1)
void CCollObjTriData::GatherFaces(const CCollBSPNode* node, const Mth::BBox* box)
{
    while (!node->IsLeaf()) {
        int axis = node->Axis();
        float split = static_cast<float>(node->Split() * static_cast<double>(kSixteenth));
        if (Component(&box->min, axis) < split)
            GatherFaces(node->LessChild(), box);
        if (!(Component(&box->max, axis) >= split) || s_faceCount < 0)
            return;
        node = node->GreaterChild();
    }
    uint32_t n = node->LeafFaceCount();
    uint16_t* out = s_faceBuffer + s_faceCount;
    s_faceCount += n;
    if (s_faceCount > m_numFaces) {
        s_faceCount = -1;
        return;
    }
    const uint16_t* faces = node->LeafFaces();
    for (uint32_t i = 0; i < n; ++i)
        out[i] = faces[i];
}

// 0x004163B0
bool CCollObjTriData::IsPointBehindFaces(const Mth::Vector* point, int unused)
{
    if (!m_bsp)
        return false;
    s_faceCount = 0;
    if (!CountFacesBehind(m_bsp, point, unused))
        return false;
    return s_faceCount == m_numFaces;
}

// 0x004161D0 (a point at or above a split ends the walk; in a leaf, the count stops at the first face the point
// is in front of)
bool CCollObjTriData::CountFacesBehind(const CCollBSPNode* node, const Mth::Vector* point, int unused)
{
    if (!node->IsLeaf()) {
        if (!(node->Split() * static_cast<double>(kSixteenth) > Component(point, node->Axis())))
            return true;
        return CountFacesBehind(node->LessChild(), point, unused);
    }
    uint32_t n = node->LeafFaceCount();
    for (uint32_t i = 0; i < n; ++i) {
        int face = node->LeafFaces()[i];
        Mth::Vector normal = GetFaceNormal(face);
        Mth::Vector corner = GetRawVertexPos(FaceVertex(node->LeafFaces()[i], 0));
        float dx = static_cast<float>(static_cast<double>(point->x) - corner.x);
        double dy = static_cast<double>(point->y) - corner.y;
        double dz = static_cast<double>(point->z) - corner.z;
        double distance = (normal.z * dz + dy * normal.y) + static_cast<double>(dx) * normal.x;
        if (distance > 0.0)
            break;
        ++s_faceCount;
    }
    return true;
}

// 0x00414D70
CCollBSPNode* CCollBSPNode::Construct()
{
    m_split = 0;
    m_link = 0;
    return this;
}

// 0x00415C90
int CCollBSPNode::CountNodes() const
{
    if (IsLeaf())
        return 1;
    int greater = GreaterChild()->CountNodes();
    return greater + LessChild()->CountNodes() + 1;
}

// 0x00415CE0
int CCollBSPNode::CountFaces() const
{
    if (IsLeaf())
        return LeafFaceCount();
    int greater = GreaterChild()->CountFaces();
    return LessChild()->CountFaces() + greater;
}

// 0x00415D30
uint16_t* CCollBSPNode::FirstFaceList() const
{
    if (IsLeaf())
        return LeafFaces();
    uint16_t* less = LessChild()->FirstFaceList();
    uint16_t* greater = GreaterChild()->FirstFaceList();
    return less < greater ? less : greater;
}

// 0x00415D80
void CCollBSPNode::Translate(const Mth::Vector* delta)
{
    CCollBSPNode* node = this;
    while (!node->IsLeaf()) {
        double at = node->Split() * static_cast<double>(kSixteenth) + Component(delta, node->Axis());
        node->SetSplit(Truncate(at * kSixteen));
        node->LessChild()->Translate(delta);
        node = node->GreaterChild();
    }
}

// 0x00415E00
void CCollBSPNode::RotateY(const Mth::Vector* centre, int quarterTurns, bool moveToCentre)
{
    if (moveToCentre) {
        Mth::Vector back{-centre->x, -centre->y, -centre->z, -centre->w};
        Translate(&back);
    }
    if (!IsLeaf()) {
        int axis = Axis();
        if (axis != 1) {
            int swapped = axis == 0 ? 2 : 0;
            auto negate = [this]() {
                SetSplit(-Split());
                m_link ^= 1; // the sides swap
            };
            switch (quarterTurns) {
            case 1:
                if (axis == 0)
                    negate();
                m_split = (m_split & ~3) | swapped;
                break;
            case 2:
                negate();
                break;
            case 3:
                if (axis == 2)
                    negate();
                m_split = (m_split & ~3) | swapped;
                break;
            default:
                break;
            }
        }
        LessChild()->RotateY(centre, quarterTurns, false);
        GreaterChild()->RotateY(centre, quarterTurns, false);
    }
    if (moveToCentre)
        Translate(centre);
}

// 0x00415F30
void CCollBSPNode::Scale(const Mth::Vector* centre, const Mth::Vector* scale, bool moveToCentre)
{
    if (moveToCentre) {
        Mth::Vector back{-centre->x, -centre->y, -centre->z, -centre->w};
        Translate(&back);
    }
    if (!IsLeaf()) {
        double at = static_cast<double>(Split()) * Component(scale, Axis()) * kSixteenth;
        SetSplit(Truncate(at * kSixteen));
        LessChild()->Scale(centre, scale, false);
        GreaterChild()->Scale(centre, scale, false);
    }
    if (moveToCentre)
        Translate(centre);
}

// 0x00416000
bool CCollBSPNode::Relocate(CCollBSPNode* node, uint32_t nodeBase, uint32_t faceBase)
{
    if (node->IsLeaf()) {
        node->m_link = faceBase + node->m_link * 2;
        return true;
    }
    node->m_link = ((node->m_link & ~3u) + nodeBase) | (node->m_link & 3);
    Relocate(node->LessChild(), nodeBase, faceBase);
    Relocate(node->GreaterChild(), nodeBase, faceBase);
    return true;
}

// 0x00416A10 (a failed allocation crashes, as in the original)
CCollBSPNode* CCollBSPNode::Clone(int) const
{
    int nodes = CountNodes();
    int faces = CountFaces();
    if (nodes <= 0)
        return nullptr;
    uint16_t* oldFaces = FirstFaceList();
    // new CCollBSPNode[nodes]: the count, then the nodes
    auto* memory = static_cast<uint32_t*>(Mem::New(nodes * 8 + 4));
    CCollBSPNode* copy = nullptr;
    if (memory) {
        *memory = nodes;
        copy = reinterpret_cast<CCollBSPNode*>(memory + 1);
        for (int i = 0; i < nodes; ++i)
            copy[i].Construct();
    }
    auto* newFaces = static_cast<uint16_t*>(Mem::New(faces * 2));
    std::memcpy(copy, this, nodes * 8);
    std::memcpy(newFaces, oldFaces, faces * 2);
    uint32_t nodeDelta = reinterpret_cast<uint32_t>(copy) - reinterpret_cast<uint32_t>(this);
    uint32_t faceDelta = reinterpret_cast<uint32_t>(newFaces) - reinterpret_cast<uint32_t>(oldFaces);
    for (int i = 0; i < nodes; ++i) {
        CCollBSPNode& node = copy[i];
        if (node.IsLeaf())
            node.m_link += faceDelta;
        else
            node.m_link = ((node.m_link & ~3u) + nodeDelta) | (node.m_link & 3);
    }
    return copy;
}

} // namespace Nx

REPLACE(0x0040B230, static_cast<Mth::Vector (Nx::CCollObjTriData::*)(int) const>(&Nx::CCollObjTriData::GetRawVertexPos));
REPLACE(0x0040B2D0,
        static_cast<void (Nx::CCollObjTriData::*)(int, Mth::Vector*) const>(&Nx::CCollObjTriData::GetRawVertexPos));
REPLACE(0x00414EE0, &Nx::CCollObjTriData::SetRawVertexPos);
REPLACE(0x00414F90, &Nx::CCollObjTriData::GetRawVertices);
REPLACE_SOURCE_ONLY(0x00415C00, &Nx::CCollObjTriData::GetRawVertices); // 5 bytes: JMP 0x00414F90
REPLACE(0x00415C10, &Nx::CCollObjTriData::SetRawVertices);
REPLACE(0x00414D80, &Nx::CCollObjTriData::GetFaceNormal);
REPLACE(0x00415B70, &Nx::CCollObjTriData::CopyFrom);
REPLACE(0x00416BF0, &Nx::CCollObjTriData::Clone);
REPLACE(0x00416970, &Nx::CCollObjTriData::Destruct);
REPLACE(0x00416080, &Nx::CCollObjTriData::DeleteBSP);
REPLACE(0x004169C0, &Nx::CCollObjTriData::Relocate);
REPLACE(0x00416400, &Nx::CCollObjTriData::Translate);
REPLACE(0x00416320, &Nx::CCollObjTriData::GetFacesInBox);
REPLACE(0x004164F0, &Nx::CCollObjTriData::RotateY);
REPLACE(0x00416730, &Nx::CCollObjTriData::Scale);
REPLACE(0x00414D30, &Nx::CCollObjTriData::SetUpAllFaces);
REPLACE(0x00414FE0, &Nx::CCollObjTriData::AddOcclusionPolys);
REPLACE(0x004160E0, &Nx::CCollObjTriData::GatherFaces);
REPLACE(0x004163B0, &Nx::CCollObjTriData::IsPointBehindFaces);
REPLACE(0x004161D0, &Nx::CCollObjTriData::CountFacesBehind);
REPLACE(0x00414D70, &Nx::CCollBSPNode::Construct);
REPLACE(0x00415C90, &Nx::CCollBSPNode::CountNodes);
REPLACE(0x00415CE0, &Nx::CCollBSPNode::CountFaces);
REPLACE(0x00415D30, &Nx::CCollBSPNode::FirstFaceList);
REPLACE(0x00415D80, &Nx::CCollBSPNode::Translate);
REPLACE(0x00415E00, &Nx::CCollBSPNode::RotateY);
REPLACE(0x00415F30, &Nx::CCollBSPNode::Scale);
REPLACE(0x00416000, &Nx::CCollBSPNode::Relocate);
REPLACE(0x00416A10, &Nx::CCollBSPNode::Clone);
