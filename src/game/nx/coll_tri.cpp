// Collision geometry (CTriMeshSource) and its BSP tree (CHitTreeNode).

#include "game/nx/coll_tri.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/engine_glue.h"

namespace Engine3D {

namespace {

constexpr float kSixteenth = 0.0625f;
constexpr float kSixteen = 16.0f;

// The shared index table every face of a geometry is listed by when there's no tree to narrow them down.
uint16_t g_allFaces[2048]{0xFFFF};
uint16_t* AllFaces() { return g_allFaces; }

uint16_t* g_faceBuffer;
int g_faceCount;
int g_faceBufferSize;

// GetFacesInBox's / IsPointBehindFaces' state while walking the tree.
uint16_t*& s_faceBuffer = g_faceBuffer;
int& s_faceCount = g_faceCount;
int& s_faceBufferSize = g_faceBufferSize;

// The compiler's float-to-integer conversion (_ftol: toward zero, the low 32 bits of the 64-bit result).
int Truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

float Component(const Maths::Vec4* v, int axis) { return (&v->x)[axis]; }

} // namespace

// 0x0040B230
Maths::Vec4 CTriMeshSource::ReadStoredVertex(int index) const
{
    if (m_compressed) {
        const uint16_t* v = static_cast<const uint16_t*>(m_vertices) + 3 * index;
        return {v[0] * 0.0625f + m_lowerLimit.x, v[1] * 0.0625f + m_lowerLimit.y, v[2] * 0.0625f + m_lowerLimit.z, 0.0f};
    }
    const float* v = static_cast<const float*>(m_vertices) + 3 * index;
    return {v[0], v[1], v[2], 0.0f};
}

// 0x0040B2D0 (the uncompressed vertex copied as raw words)
void CTriMeshSource::ReadStoredVertex(int index, Maths::Vec4* out) const
{
    if (m_compressed) {
        const uint16_t* v = static_cast<const uint16_t*>(m_vertices) + 3 * index;
        out->x = v[0] * 0.0625f + m_lowerLimit.x;
        out->y = v[1] * 0.0625f + m_lowerLimit.y;
        out->z = v[2] * 0.0625f + m_lowerLimit.z;
        out->w = 0.0f;
        return;
    }
    std::memcpy(out, static_cast<const float*>(m_vertices) + 3 * index, 12);
    out->w = 0.0f;
}

// 0x00414EE0 (x's offset isn't rounded to a float before it's scaled; y's and z's are)
void CTriMeshSource::WriteStoredVertex(int index, const Maths::Vec4* position)
{
    if (m_compressed) {
        double x = static_cast<double>(position->x) - m_lowerLimit.x;
        float y = static_cast<float>(static_cast<double>(position->y) - m_lowerLimit.y);
        float z = static_cast<float>(static_cast<double>(position->z) - m_lowerLimit.z);
        uint16_t* v = static_cast<uint16_t*>(m_vertices) + 3 * index;
        v[0] = static_cast<uint16_t>(Truncate(x * kSixteen));
        v[1] = static_cast<uint16_t>(Truncate(static_cast<double>(y) * kSixteen));
        v[2] = static_cast<uint16_t>(Truncate(static_cast<double>(z) * kSixteen));
        return;
    }
    std::memcpy(static_cast<float*>(m_vertices) + 3 * index, position, 12);
}

// 0x00414F90
void CTriMeshSource::GetStoredVertexArray(Maths::Vec4* out) const
{
    for (int i = 0; i < m_numVertices; ++i)
        out[i] = ReadStoredVertex(i);
}

// 0x00415C10
void CTriMeshSource::ChooseRawVerts(const Maths::Vec4* positions)
{
    auto* box = reinterpret_cast<Maths::BBox*>(&m_lowerLimit);
    box->min = {10000000.0f, 10000000.0f, 10000000.0f, 1.0f};
    box->max = {-10000000.0f, -10000000.0f, -10000000.0f, 1.0f};
    for (int i = 0; i < m_numVertices; ++i)
        box->AppendSpot(positions[i]);
    for (int i = 0; i < m_numVertices; ++i)
        WriteStoredVertex(i, &positions[i]);
}

// 0x00414D80 (one of the edge's z kept unrounded for the normal's x, as the original's x87 code does)
Maths::Vec4 CTriMeshSource::GetTriangleNormal(int face) const
{
    Maths::Vec4 v0 = ReadStoredVertex(FaceVertex(face, 0));
    Maths::Vec4 v1 = ReadStoredVertex(FaceVertex(face, 1));
    Maths::Vec4 v2 = ReadStoredVertex(FaceVertex(face, 2));
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
CTriMeshSource* CTriMeshSource::CopyFrom(const CTriMeshSource* other)
{
    *this = *other;
    return this;
}

// 0x00416BF0 (a failed allocation of the copy itself crashes, as in the original; so does a missing tree)
CTriMeshSource* CTriMeshSource::Replicate(int bspArgument, bool needVertices) const
{
    if (needVertices && m_numVertices == 0)
        return nullptr;
    void* memory = MemSys::Alloc(sizeof(CTriMeshSource));
    CTriMeshSource* copy = memory ? static_cast<CTriMeshSource*>(memory)->CopyFrom(this) : nullptr;
    uint32_t vertexSize = m_numVertices * (m_compressed ? 6u : 12u);
    copy->m_vertices = MemSys::Alloc(vertexSize);
    uint32_t faceSize = m_numFaces * (m_smallFaces ? 8u : 10u);
    copy->m_faces = static_cast<uint8_t*>(MemSys::Alloc(faceSize));
    if (!copy->m_vertices || !copy->m_faces)
        return nullptr;
    std::memcpy(copy->m_vertices, m_vertices, vertexSize);
    std::memcpy(copy->m_faces, m_faces, faceSize);
    if (m_vertexBytes) {
        copy->m_vertexBytes = static_cast<uint8_t*>(MemSys::Alloc(m_numVertices));
        std::memcpy(copy->m_vertexBytes, m_vertexBytes, m_numVertices);
    }
    copy->m_bsp = m_bsp->Replicate(bspArgument);
    return copy;
}

// 0x00416970
void CTriMeshSource::Destruct()
{
    if (m_vertices)
        MemSys::Hub::Pull()->Dealloc(m_vertices);
    if (m_vertexBytes)
        MemSys::Hub::Pull()->Dealloc(m_vertexBytes);
    if (m_faces)
        MemSys::Hub::Pull()->Dealloc(m_faces);
    DeleteBSP();
}

// 0x00416080 (the nodes' destructor - 0x005480A0, run by delete[] for each - is empty)
bool CTriMeshSource::DeleteBSP()
{
    if (!m_bsp)
        return false;
    if (uint16_t* faces = m_bsp->FirstFaceList())
        MemSys::Hub::Pull()->Dealloc(faces);
    if (m_bsp)
        MemSys::Hub::Pull()->Dealloc(reinterpret_cast<uint8_t*>(m_bsp) - 4);
    m_bsp = nullptr;
    return true;
}

// 0x004169C0
bool CTriMeshSource::Relocate(uint32_t, uint32_t vertexBase, uint32_t vertexByteBase, uint32_t faceBase,
                               uint32_t nodeBase, uint32_t faceListBase)
{
    m_vertices = reinterpret_cast<void*>(reinterpret_cast<uint32_t>(m_vertices) + vertexBase);
    m_vertexBytes = reinterpret_cast<uint8_t*>(reinterpret_cast<uint32_t>(m_vertexBytes) + vertexByteBase);
    m_faces = reinterpret_cast<uint8_t*>(reinterpret_cast<uint32_t>(m_faces) + faceBase);
    m_bsp = reinterpret_cast<CHitTreeNode*>(reinterpret_cast<uint32_t>(m_bsp) + nodeBase);
    CHitTreeNode::Relocate(m_bsp, nodeBase, faceListBase);
    return true;
}

// 0x00416400
void CTriMeshSource::Slide(const Maths::Vec4* delta)
{
    if (m_bsp)
        m_bsp->Slide(delta);
    if (!m_compressed) {
        float* v = static_cast<float*>(m_vertices);
        for (int i = 0; i < m_numVertices; ++i, v += 3) {
            v[0] = static_cast<float>(static_cast<double>(v[0]) + delta->x);
            v[1] = static_cast<float>(static_cast<double>(v[1]) + delta->y);
            v[2] = static_cast<float>(static_cast<double>(delta->z) + v[2]);
        }
    }
    auto moved = [delta](const Maths::Vec4& p) {
        return Maths::Vec4{static_cast<float>(static_cast<double>(p.x) + delta->x),
                           static_cast<float>(static_cast<double>(p.y) + delta->y),
                           static_cast<float>(static_cast<double>(p.z) + delta->z),
                           static_cast<float>(static_cast<double>(p.w) + delta->w)};
    };
    Maths::Vec4 min = moved(m_lowerLimit);
    Maths::Vec4 max = moved(m_upperLimit);
    m_lowerLimit = min;
    m_upperLimit = max;
}

// 0x004164F0 (a failed allocation of the temporary array crashes - the vertices are read into null - as in the
// original; the array's element constructor, 0x005902E0, does nothing)
void CTriMeshSource::RotateY(const Maths::Vec4* centre, int quarterTurns)
{
    Maths::Vec4 back{-centre->x, -centre->y, -centre->z, -centre->w};
    Slide(&back);
    if (m_bsp)
        m_bsp->RotateY(centre, quarterTurns, false);
    Maths::Vec4* positions = nullptr;
    if (m_compressed) {
        MemSys::EnterHeapScope(MemSys::TopDownHeap());
        positions = static_cast<Maths::Vec4*>(MemSys::Alloc(m_numVertices * sizeof(Maths::Vec4)));
        MemSys::LeaveHeapScope();
        GetStoredVertexArray(positions);
    }
    for (int i = 0; i < m_numVertices; ++i) {
        if (positions) {
            positions[i].TurnQuarterAboutY(quarterTurns);
            continue;
        }
        Maths::Vec4 v;
        ReadStoredVertex(i, &v);
        v.TurnQuarterAboutY(quarterTurns);
        float* stored = static_cast<float*>(m_vertices) + 3 * i;
        stored[0] = v.x;
        stored[1] = v.y;
        stored[2] = v.z;
    }
    Maths::Vec4 min = m_lowerLimit;
    Maths::Vec4 max = m_upperLimit;
    min.TurnQuarterAboutY(quarterTurns);
    max.TurnQuarterAboutY(quarterTurns);
    auto* box = reinterpret_cast<Maths::BBox*>(&m_lowerLimit);
    box->min = {10000000.0f, 10000000.0f, 10000000.0f, 1.0f};
    box->max = {-10000000.0f, -10000000.0f, -10000000.0f, 1.0f};
    box->AppendSpot(min);
    box->AppendSpot(max);
    if (positions) {
        for (int i = 0; i < m_numVertices; ++i)
            WriteStoredVertex(i, &positions[i]);
        MemSys::Hub::Pull()->Dealloc(positions);
    }
    Slide(centre);
    m_unknown3C = 0;
}

// 0x00416730 (a failed allocation crashes, as in RotateY)
void CTriMeshSource::StretchBy(const Maths::Vec4* centre, const Maths::Vec4* scale)
{
    Maths::Vec4 back{-centre->x, -centre->y, -centre->z, -centre->w};
    Slide(&back);
    if (m_bsp)
        m_bsp->StretchBy(centre, scale, false);
    Maths::Vec4* positions = nullptr;
    if (m_compressed) {
        MemSys::EnterHeapScope(MemSys::TopDownHeap());
        positions = static_cast<Maths::Vec4*>(MemSys::Alloc(m_numVertices * sizeof(Maths::Vec4)));
        MemSys::LeaveHeapScope();
        GetStoredVertexArray(positions);
    }
    for (int i = 0; i < m_numVertices; ++i) {
        float* v = positions ? &positions[i].x : static_cast<float*>(m_vertices) + 3 * i;
        v[0] *= scale->x;
        v[1] *= scale->y;
        v[2] *= scale->z;
    }
    m_lowerLimit = {m_lowerLimit.x * scale->x, m_lowerLimit.y * scale->y, m_lowerLimit.z * scale->z, m_lowerLimit.w};
    m_upperLimit = {m_upperLimit.x * scale->x, m_upperLimit.y * scale->y, m_upperLimit.z * scale->z, m_upperLimit.w};
    if (positions) {
        for (int i = 0; i < m_numVertices; ++i)
            WriteStoredVertex(i, &positions[i]);
        MemSys::Hub::Pull()->Dealloc(positions);
    }
    Slide(centre);
    m_unknown3C = 0;
}

// 0x00414D30
bool CTriMeshSource::SetUpAllFaces()
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
void CTriMeshSource::AddOcclusionPolys()
{
    constexpr float kSameDirection = 0.05f;
    Maths::Vec4 points[4];
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
            Maths::Vec4 a = ReadStoredVertex(FaceVertex(i, 0));
            Maths::Vec4 b = ReadStoredVertex(FaceVertex(i, 1));
            Maths::Vec4 c = ReadStoredVertex(FaceVertex(i, 2));
            double cx = static_cast<double>(c.x) - a.x;
            double cy = static_cast<double>(c.y) - a.y;
            double cz = static_cast<double>(c.z) - a.z;
            float bx = static_cast<float>(static_cast<double>(b.x) - a.x);
            float by = static_cast<float>(static_cast<double>(b.y) - a.y);
            double bz = static_cast<double>(b.z) - a.z;
            float n1x = static_cast<float>(by * cz - bz * cy);
            float n1y = static_cast<float>(bz * cx - cz * bx);
            float n1z = static_cast<float>(cy * bx - by * cx);
            Maths::Vec4 a2 = ReadStoredVertex(FaceVertex(j, 0));
            Maths::Vec4 b2 = ReadStoredVertex(FaceVertex(j, 1));
            Maths::Vec4 c2 = ReadStoredVertex(FaceVertex(j, 2));
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
            auto corner = [this, i](int k) { return ReadStoredVertex(FaceVertex(i, k)); };
            if (shared[0] >= 0 && shared[1] >= 0) {
                points[0] = corner(0);
                points[1] = ReadStoredVertex(extra);
                points[2] = corner(1);
                points[3] = corner(2);
            } else if (shared[0] < 0 && shared[1] >= 0 && shared[2] >= 0) {
                points[0] = corner(0);
                points[1] = corner(1);
                points[2] = ReadStoredVertex(extra);
                points[3] = corner(2);
            } else if (shared[0] >= 0 && shared[2] >= 0) {
                points[0] = corner(0);
                points[1] = corner(1);
                points[2] = corner(2);
                points[3] = ReadStoredVertex(extra);
            }
            // (otherwise - not possible with two shared corners - face j's corners as they are)
            AddDebugShape(4, points, m_checksum);
            break;
        }
    }
}

// 0x00416320
uint16_t* CTriMeshSource::GetFacesInBox(const Maths::BBox* box, int* count, uint16_t* buffer, int bufferSize)
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
void CTriMeshSource::GatherFaces(const CHitTreeNode* node, const Maths::BBox* box)
{
    while (!node->IsEndNode()) {
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
bool CTriMeshSource::IsPointBehindFaces(const Maths::Vec4* point, int unused)
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
bool CTriMeshSource::CountFacesBehind(const CHitTreeNode* node, const Maths::Vec4* point, int unused)
{
    if (!node->IsEndNode()) {
        if (!(node->Split() * static_cast<double>(kSixteenth) > Component(point, node->Axis())))
            return true;
        return CountFacesBehind(node->LessChild(), point, unused);
    }
    uint32_t n = node->LeafFaceCount();
    for (uint32_t i = 0; i < n; ++i) {
        int face = node->LeafFaces()[i];
        Maths::Vec4 normal = GetTriangleNormal(face);
        Maths::Vec4 corner = ReadStoredVertex(FaceVertex(node->LeafFaces()[i], 0));
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
CHitTreeNode* CHitTreeNode::MakeNew()
{
    m_axisTag = 0;
    m_chainLink = 0;
    return this;
}

// 0x00415C90
int CHitTreeNode::TallyNodes() const
{
    if (IsEndNode())
        return 1;
    int greater = GreaterChild()->TallyNodes();
    return greater + LessChild()->TallyNodes() + 1;
}

// 0x00415CE0
int CHitTreeNode::CountFaces() const
{
    if (IsEndNode())
        return LeafFaceCount();
    int greater = GreaterChild()->CountFaces();
    return LessChild()->CountFaces() + greater;
}

// 0x00415D30
uint16_t* CHitTreeNode::FirstFaceList() const
{
    if (IsEndNode())
        return LeafFaces();
    uint16_t* less = LessChild()->FirstFaceList();
    uint16_t* greater = GreaterChild()->FirstFaceList();
    return less < greater ? less : greater;
}

// 0x00415D80
void CHitTreeNode::Slide(const Maths::Vec4* delta)
{
    CHitTreeNode* node = this;
    while (!node->IsEndNode()) {
        double at = node->Split() * static_cast<double>(kSixteenth) + Component(delta, node->Axis());
        node->SetSplit(Truncate(at * kSixteen));
        node->LessChild()->Slide(delta);
        node = node->GreaterChild();
    }
}

// 0x00415E00
void CHitTreeNode::RotateY(const Maths::Vec4* centre, int quarterTurns, bool moveToCentre)
{
    if (moveToCentre) {
        Maths::Vec4 back{-centre->x, -centre->y, -centre->z, -centre->w};
        Slide(&back);
    }
    if (!IsEndNode()) {
        int axis = Axis();
        if (axis != 1) {
            int swapped = axis == 0 ? 2 : 0;
            auto negate = [this]() {
                SetSplit(-Split());
                m_chainLink ^= 1; // the sides swap
            };
            switch (quarterTurns) {
            case 1:
                if (axis == 0)
                    negate();
                m_axisTag = (m_axisTag & ~3) | swapped;
                break;
            case 2:
                negate();
                break;
            case 3:
                if (axis == 2)
                    negate();
                m_axisTag = (m_axisTag & ~3) | swapped;
                break;
            default:
                break;
            }
        }
        LessChild()->RotateY(centre, quarterTurns, false);
        GreaterChild()->RotateY(centre, quarterTurns, false);
    }
    if (moveToCentre)
        Slide(centre);
}

// 0x00415F30
void CHitTreeNode::StretchBy(const Maths::Vec4* centre, const Maths::Vec4* scale, bool moveToCentre)
{
    if (moveToCentre) {
        Maths::Vec4 back{-centre->x, -centre->y, -centre->z, -centre->w};
        Slide(&back);
    }
    if (!IsEndNode()) {
        double at = static_cast<double>(Split()) * Component(scale, Axis()) * kSixteenth;
        SetSplit(Truncate(at * kSixteen));
        LessChild()->StretchBy(centre, scale, false);
        GreaterChild()->StretchBy(centre, scale, false);
    }
    if (moveToCentre)
        Slide(centre);
}

// 0x00416000
bool CHitTreeNode::Relocate(CHitTreeNode* node, uint32_t nodeBase, uint32_t faceBase)
{
    if (node->IsEndNode()) {
        node->m_chainLink = faceBase + node->m_chainLink * 2;
        return true;
    }
    node->m_chainLink = ((node->m_chainLink & ~3u) + nodeBase) | (node->m_chainLink & 3);
    Relocate(node->LessChild(), nodeBase, faceBase);
    Relocate(node->GreaterChild(), nodeBase, faceBase);
    return true;
}

// 0x00416A10 (a failed allocation crashes, as in the original)
CHitTreeNode* CHitTreeNode::Replicate(int) const
{
    int nodes = TallyNodes();
    int faces = CountFaces();
    if (nodes <= 0)
        return nullptr;
    uint16_t* oldFaces = FirstFaceList();
    // new CHitTreeNode[nodes]: the count, then the nodes
    auto* memory = static_cast<uint32_t*>(MemSys::Alloc(nodes * 8 + 4));
    CHitTreeNode* copy = nullptr;
    if (memory) {
        *memory = nodes;
        copy = reinterpret_cast<CHitTreeNode*>(memory + 1);
        for (int i = 0; i < nodes; ++i)
            copy[i].MakeNew();
    }
    auto* newFaces = static_cast<uint16_t*>(MemSys::Alloc(faces * 2));
    std::memcpy(copy, this, nodes * 8);
    std::memcpy(newFaces, oldFaces, faces * 2);
    uint32_t nodeDelta = reinterpret_cast<uint32_t>(copy) - reinterpret_cast<uint32_t>(this);
    uint32_t faceDelta = reinterpret_cast<uint32_t>(newFaces) - reinterpret_cast<uint32_t>(oldFaces);
    for (int i = 0; i < nodes; ++i) {
        CHitTreeNode& node = copy[i];
        if (node.IsEndNode())
            node.m_chainLink += faceDelta;
        else
            node.m_chainLink = ((node.m_chainLink & ~3u) + nodeDelta) | (node.m_chainLink & 3);
    }
    return copy;
}

} // namespace Nx
