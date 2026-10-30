// The rail editor's sectors (rail_editor.h): each point's piece of rail and post - copies of Sk5Ed_RA_Dynamic /
// Sk5Ed_RAp_Dynamic whose vertices are moved to fit - the dotted line to the cursor, the highlight colours, and the
// checks they use (too far, too steep, too sharp a corner).

#include "game/ed/rail_editor.h"

#include <cstdint>
#include <cstring>

#include "game/core/mem.h"
#include "game/ed/park.h"
#include "game/ed/park_editor.h"
#include "game/ed/rail_editor_internal.h"
#include "game/mth/matrix.h"
#include "game/mth/x87_math.h"
#include "game/nx/coll_tri.h"
#include "game/nx/feeler.h"
#include "game/park/park_memory.h"
#include "game/script/struct.h"
#include "game/gfx/engine.h"
#include "game/gfx/sector.h"
#include "game/gfx/engine_geom.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Ed {

using namespace RailEditorInternal;
using Mth::Vector;

namespace {

float FloatBits(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// The source sectors cloned for the rails, posts and the dotted line (red when the segment wouldn't be valid).
constexpr uint32_t kRailSource = 0x66FE99BF;    // Sk5Ed_RA_Dynamic
constexpr uint32_t kPostSource = 0x2756C52D;    // Sk5Ed_RAp_Dynamic
constexpr uint32_t kDottedGreen = 0x5DC3C690;   // Sk5Ed_RAdot_Dynamic_Green
constexpr uint32_t kDottedRed = 0x8DDA17D6;     // Sk5Ed_RAdot_Dynamic

// Script values
constexpr uint32_t kMaxJoinAngle = 0xB1459A78;          // RailEditorMaxJoinAngle (degrees)
constexpr uint32_t kCornerLeaveAngle = 0x76C1DA15;      // Rail_Corner_Leave_Angle (degrees)
constexpr uint32_t kMinimumSeparation = 0x8BEA1C03;     // RailEditorMinimumPointSeparation
constexpr uint32_t kMaxSlope = 0x9FD761AF;              // RailEditorMaxSlope (degrees)
constexpr uint32_t kFlashRate = 0x4551E901;             // RailEditorHighlightFlashRate
constexpr uint32_t kRedLineFlashRate = 0xEEBE4394;      // RailEditorRedLineFlashRate
constexpr uint32_t kHighlightR = 0xAD28B18B, kHighlightG = 0xC0F55560, kHighlightB = 0xB09FA1EF;
constexpr uint32_t kBadR = 0x89B66FB9, kBadG = 0xE46B8B52, kBadB = 0x94017FDD;

const float kDegToRad = FloatBits(0x3C8EFA35);  // 0x00645464
constexpr float kMaxSegment = 4000.0f;          // 0x0064B8F8
constexpr float kMaxReach = 4500.0f;            // 0x0064B8FC
const float kNearlyVertical = FloatBits(0x3F7FFF58); // 0x0064B900 (0.99999)
constexpr float kPostWidth = 2.625f;            // 0x0064B908 (also the least post height drawn)
constexpr float kGroundProbe = 10000.0f;        // 0x0064B308
constexpr float kFar = 1e6f;                    // 0x0064B904

// The flashing's frame counter (shared by the highlight and the dotted line).
int& FlashCounter() { return GameVariable<int>(0x007CCD2C); }
// A zero vector (0x007CCD30): drawing in place (no move).
const Vector& Zero() { return GameVariable<Vector>(0x007CCD30); }

// The visibility mask for a flashing frame: shown in the first half of every `rate` frames.
uint32_t FlashMask(int rate)
{
    int& counter = FlashCounter();
    uint32_t mask = counter >= rate / 2 ? 0u : 0xFFFFFFFFu;
    if (++counter >= rate)
        counter = 0;
    return mask;
}

// The sector calls (the geometry's slots are virtual calls through its vtable; its collision is the word at +8).
template <typename Fn>
Fn GeometrySlot(const uint8_t* geometry, int slot)
{
    return reinterpret_cast<Fn>((*reinterpret_cast<void* const* const*>(geometry))[slot]);
}
uint8_t* GetSector(uint32_t name) { return static_cast<uint8_t*>(Nx::Engine::GetSector(name)); }
uint8_t* GetGeometry(uint8_t* sector) { return static_cast<uint8_t*>(reinterpret_cast<Nx::CSector*>(sector)->mp_geom); }
int VertexCount(uint8_t* geometry) { return GeometrySlot<int(__thiscall*)(uint8_t*)>(geometry, 36)(geometry); }
void GetVertices(uint8_t* geometry, Vector* out)
{
    GeometrySlot<void(__thiscall*)(uint8_t*, Vector*)>(geometry, 37)(geometry, out);
}
void SetVertices(uint8_t* geometry, Vector* vertices)
{
    GeometrySlot<void(__thiscall*)(uint8_t*, Vector*)>(geometry, 39)(geometry, vertices);
}
Nx::CCollObjTriData* GetCollision(uint8_t* geometry)
{
    return static_cast<Nx::CCollObjTriData*>(reinterpret_cast<Nx::CGeom*>(geometry)->mp_collTriData);
}
uint16_t CollisionVertexCount(const Nx::CCollObjTriData* collision)
{
    return *reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(collision) + 6);
}
void SetCollisionVertices(uint8_t* sector, Vector* vertices)
{
    reinterpret_cast<Nx::CSector*>(sector)->SetRawVertices(vertices);
}

// One point turned about y: x' = x cos + z sin, z' = z cos - x sin from the 53-bit differences, the sine rounded to
// float and the cosine at full precision, as the original's x87 code does it (x' rounded to float, z' to 53 bits).
void TurnAboutY(double deltaX, double deltaZ, float angle, float* outX, double* outZ)
{
    float sine;
    float x;
    double z;
    float degToRad = kDegToRad;
    __asm {
        fld angle
        fmul degToRad
        fld st(0)
        fsin
        fstp sine
        fcos
        fld deltaX
        fmul st(0), st(1)
        fld deltaZ
        fmul sine
        faddp st(1), st(0)
        fstp x
        fld deltaZ
        fmul st(0), st(1)
        fld deltaX
        fmul sine
        fsubp st(1), st(0)
        fstp z
        fstp st(0)
    }
    *outX = x;
    *outZ = z;
}

// Whether sin(angle) (at full precision) is at least `value` (or either isn't a number).
bool SinAtLeast(double angle, float value)
{
    uint16_t status;
    __asm {
        fld angle
        fsin
        fcomp value
        fnstsw status
    }
    return (status & 0x0500) != 0x0100;
}

// The clone of a source sector made for the park (its pieces' counts marked dirty, the collision rebuilt).
uint8_t* CloneSector(uint32_t source)
{
    uint32_t name = CParkManager::sInstance()->mp_generator->CloneSector(source, 0, 1, 0);
    SetCountsDirty();
    CParkManager::sInstance()->mp_generator->RebuildCollision();
    return GetSector(name);
}

// The bytes of a colour word (red, green, blue; alpha 0) as the highlight writes them.
struct Colour {
    uint8_t r, g, b;
    uint32_t Word() const { return r | static_cast<uint32_t>(g) << 8 | static_cast<uint32_t>(b) << 16; }
};

// The template's vertices of the segment as Stretch turns them: the 10 at the start of the piece, then the 10 at its
// end (0x0064... built on the stack by the original).
const int kStartVertices[10] = {1, 9, 3, 10, 14, 19, 0, 13, 2, 16};
const int kEndVertices[10] = {6, 11, 7, 8, 15, 18, 4, 12, 5, 17};

} // namespace

// 0x004F3E00 (cdecl)
uint8_t* __cdecl CloneRailSector(uint32_t source) { return CloneSector(source); }

// 0x004F3D80
Vector* TransformRailPoint(Vector* out, const Vector* point, const Vector* origin, const Vector* position, float angle)
{
    double dx = static_cast<double>(point->x) - origin->x;
    float dy = static_cast<float>(static_cast<double>(point->y) - origin->y);
    double dz = static_cast<double>(point->z) - origin->z;
    float x;
    double z;
    TurnAboutY(dx, dz, angle, &x, &z);
    // (w: the original adds an unset local - garbage - to the position's; here 0)
    out->x = static_cast<float>(static_cast<double>(x) + position->x);
    out->y = static_cast<float>(static_cast<double>(dy) + position->y);
    out->z = static_cast<float>(static_cast<double>(static_cast<float>(z)) + position->z);
    out->w = static_cast<float>(0.0 + position->w);
    return out;
}

// 0x004F3E40
bool RailCornerOk(const Vector* a, const Vector* b, const Vector* c)
{
    // The two directions on the ground plane, each normalised by its length rounded to float (y 0, times the
    // inverse); the cosine between them rounded to float.
    auto direction = [](const Vector* from, const Vector* to, double* x, double* y, double* z) {
        *x = static_cast<double>(to->x) - from->x;
        *z = static_cast<double>(to->z) - from->z;
        *y = 0.0;
        double exact = X87::Sqrt(*z * *z + *x * *x);
        float length = static_cast<float>(exact);
        if (exact > 0.0) {
            float inverse = static_cast<float>(1.0 / length);
            *x = *x * inverse;
            *y = 0.0 * inverse;
            *z = *z * inverse;
        }
    };
    double x1, y1, z1, x2, y2, z2;
    direction(a, b, &x1, &y1, &z1);
    direction(b, c, &x2, &y2, &z2);
    float dot = static_cast<float>((z2 * z1 + y2 * y1) + x2 * x1);
    return !X87::CosAbove(static_cast<double>(Script::GetFloat(kCornerLeaveAngle)) * kDegToRad, dot);
}

namespace {
// The distance between two points (the squares summed z, x, then y - as 0x004F3F30 does).
double Distance(const Vector* a, const Vector* b)
{
    double dx = static_cast<double>(a->x) - b->x;
    double dy = static_cast<double>(a->y) - b->y;
    double dz = static_cast<double>(a->z) - b->z;
    return X87::Sqrt((dz * dz + dx * dx) + dy * dy);
}
// The same summed z, y, then x (as MovePoint does).
double DistanceZYX(const Vector* a, const Vector* b)
{
    double dx = static_cast<double>(a->x) - b->x;
    double dy = static_cast<double>(a->y) - b->y;
    double dz = static_cast<double>(a->z) - b->z;
    return X87::Sqrt((dz * dz + dy * dy) + dx * dx);
}
} // namespace

// 0x004F3F30
int RailTooFar(const Vector* a, const Vector* b) { return Distance(a, b) > kMaxSegment ? 1 : 0; }

// 0x004F3F70
int RailOutOfReach(const Vector* a, const Vector* b) { return Distance(a, b) > kMaxReach ? 1 : 0; }

// 0x004F3FB0
bool RailSegmentValid(const Vector* a, const Vector* b)
{
    double dx = static_cast<double>(b->x) - a->x;
    double dy = static_cast<double>(b->y) - a->y;
    double dz = static_cast<double>(b->z) - a->z;
    if (X87::Sqrt((dz * dz + dy * dy) + dx * dx) > kMaxSegment)
        return false;
    Vector d = {static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz),
                static_cast<float>(static_cast<double>(b->w) - a->w)};
    float length = static_cast<float>(X87::Sqrt((static_cast<double>(d.z) * d.z + static_cast<double>(d.y) * d.y) +
                                                static_cast<double>(d.x) * d.x));
    if (Script::GetFloat(kMinimumSeparation) > length)
        return false;
    d.Normalize();
    float slope = d.y < 0.0f ? -d.y : d.y;
    return SinAtLeast(static_cast<double>(Script::GetFloat(kMaxSlope)) * kDegToRad, slope);
}

// 0x004F4B90
void OrientRailVertices(const Vector* direction, const Vector* translation, Vector* vertices, const int* indices,
                        int count)
{
    const Vector origin = vertices[indices[0]];
    float x = direction->x, y = direction->y, z = direction->z;
    double length = X87::Sqrt((static_cast<double>(z) * z + static_cast<double>(y) * y) + static_cast<double>(x) * x);
    if (length > 0.0) {
        double inverse = 1.0 / length;
        x = static_cast<float>(x * inverse);
        y = static_cast<float>(y * inverse);
        z = static_cast<float>(z * inverse);
    }
    // A side across the direction on the ground (straight up or down: z), and the up between them.
    Vector side;
    float sideW;
    if ((y < 0.0f ? -y : y) > kNearlyVertical) {
        side = {0.0f, 0.0f, 1.0f, 0.0f}; // (its w isn't written: the last row's w is 1)
        sideW = 1.0f;
    } else {
        double a = static_cast<double>(y) * 0.0f;
        side = {static_cast<float>(z - a), static_cast<float>(static_cast<double>(x) * 0.0f - static_cast<double>(z) * 0.0f),
                static_cast<float>(a - x), 0.0f};
        side.Normalize();
        sideW = side.w;
    }
    double ux = static_cast<double>(side.z) * y - static_cast<double>(side.y) * z;
    double uy = static_cast<double>(z) * side.x - static_cast<double>(side.z) * x;
    double uz = static_cast<double>(side.y) * x - static_cast<double>(y) * side.x;
    float uzF = static_cast<float>(uz);
    double upLength = X87::Sqrt((uz * uzF + uy * uy) + ux * ux);
    if (upLength > 0.0) {
        double inverse = 1.0 / upLength;
        ux *= inverse;
        uy *= inverse;
        uzF = static_cast<float>(uzF * inverse);
    }
    Mth::Matrix m;
    m.row[0] = {x, y, z, direction->w};
    m.row[1] = {static_cast<float>(ux), static_cast<float>(uy), uzF, 0.0f};
    m.row[2] = {side.x, side.y, side.z, sideW};
    // (the original leaves the last row's x, y, z unset - only its w is written; here 0)
    m.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int i = 0; i < count; ++i) {
        Vector& v = vertices[indices[i]];
        Vector offset = {static_cast<float>(static_cast<double>(v.x) - origin.x),
                         static_cast<float>(static_cast<double>(v.y) - origin.y),
                         static_cast<float>(static_cast<double>(v.z) - origin.z),
                         static_cast<float>(static_cast<double>(v.w) - origin.w)};
        Vector turned = offset * m;
        v.x = static_cast<float>(static_cast<double>(turned.x) + translation->x);
        v.y = static_cast<float>(static_cast<double>(turned.y) + translation->y);
        v.z = static_cast<float>(static_cast<double>(turned.z) + translation->z);
        v.w = static_cast<float>(static_cast<double>(turned.w) + translation->w);
    }
}

// 0x004F4E70
void StretchRailSector(uint32_t source, const Vector* from, const Vector* previous, const Vector* to, uint8_t* sector,
                       uint8_t* previousSector, bool updateCollision)
{
    uint8_t* sourceGeometry = GetGeometry(GetSector(source));
    int count = VertexCount(sourceGeometry);
    uint32_t bytes = static_cast<uint32_t>(count) << 4;
    auto* vertices = static_cast<Vector*>(Mem::Malloc(bytes));
    GetVertices(sourceGeometry, vertices);
    double dx = static_cast<double>(to->x) - from->x;
    double dy = static_cast<double>(to->y) - from->y;
    float dz = static_cast<float>(static_cast<double>(to->z) - from->z);
    float dw = static_cast<float>(static_cast<double>(to->w) - from->w);
    if (previousSector) {
        // Joined on: the start turned halfway between the previous segment's direction and this one's, and the
        // previous segment's end vertices moved onto this one's start.
        double px = static_cast<double>(from->x) - previous->x;
        float py = static_cast<float>(static_cast<double>(from->y) - previous->y);
        float pz = static_cast<float>(static_cast<double>(from->z) - previous->z);
        float pw = static_cast<float>(static_cast<double>(from->w) - previous->w);
        float sx = static_cast<float>(px + dx);
        float sy = static_cast<float>(static_cast<double>(py) + dy);
        double sz = static_cast<double>(pz) + dz;
        float sw = static_cast<float>(static_cast<double>(pw) + dw);
        Vector halfway = {static_cast<float>(static_cast<double>(sx) * 0.5f),
                          static_cast<float>(static_cast<double>(sy) * 0.5f), static_cast<float>(sz * 0.5f),
                          static_cast<float>(static_cast<double>(sw) * 0.5f)};
        OrientRailVertices(&halfway, from, vertices, kStartVertices, 10);
        uint8_t* previousGeometry = GetGeometry(previousSector);
        auto* previousVertices = static_cast<Vector*>(Mem::Malloc(static_cast<uint32_t>(VertexCount(previousGeometry)) << 4));
        GetVertices(previousGeometry, previousVertices);
        for (int i = 0; i < 10; ++i)
            previousVertices[kEndVertices[i]] = vertices[kStartVertices[i]];
        SetVertices(previousGeometry, previousVertices);
        Mem::FreeMalloc(previousVertices);
    } else {
        Vector direction = {static_cast<float>(dx), static_cast<float>(dy), dz, dw};
        OrientRailVertices(&direction, from, vertices, kStartVertices, 10);
    }
    Vector direction = {static_cast<float>(static_cast<double>(to->x) - from->x),
                        static_cast<float>(static_cast<double>(to->y) - from->y),
                        static_cast<float>(static_cast<double>(to->z) - from->z),
                        static_cast<float>(static_cast<double>(to->w) - from->w)};
    OrientRailVertices(&direction, to, vertices, kEndVertices, 10);
    SetVertices(GetGeometry(sector), vertices);
    if (updateCollision) {
        // The collision's vertices moved as the drawn ones were: each found among the source geometry's (as read
        // back now) by its position - or by x and z alone, keeping its height over that vertex.
        auto* before = static_cast<Vector*>(Mem::Malloc(bytes));
        GetVertices(sourceGeometry, before);
        Nx::CCollObjTriData* collision = GetCollision(sourceGeometry);
        int collisionCount = CollisionVertexCount(collision);
        auto* raw = static_cast<Vector*>(Mem::Malloc(static_cast<uint32_t>(collisionCount) << 4));
        collision->GetRawVertices(raw);
        for (int v = 0; v < collisionCount; ++v) {
            Vector vertex = raw[v];
            bool found = false;
            for (int i = 0; i < count && !found; ++i) {
                if (vertex.x == before[i].x && vertex.y == before[i].y && vertex.z == before[i].z) {
                    raw[v] = vertices[i];
                    found = true;
                }
            }
            for (int i = 0; i < count && !found; ++i) {
                if (vertex.x == before[i].x && vertex.z == before[i].z) {
                    double above = static_cast<double>(vertex.y) - before[i].y;
                    raw[v] = vertices[i];
                    raw[v].y = static_cast<float>(above + vertices[i].y);
                    found = true;
                }
            }
            if (!found)
                vertex.Print();
        }
        SetCollisionVertices(sector, raw);
        Mem::FreeMalloc(raw);
        Mem::FreeMalloc(before);
        CParkManager::sInstance()->mp_generator->RebuildCollision();
    }
    Mem::FreeMalloc(vertices);
}

// --- CEditedRailPoint ---------------------------------------------------------------------------------------------

// 0x004F5460
void CEditedRailPoint::DrawSegment(const Vector* origin, const Vector* position, float angle)
{
    if (!mp_next) {
        DeleteSegment();
        return;
    }
    if (!mp_segment)
        mp_segment = CloneRailSector(kRailSource);
    CEditedRailPoint* next = mp_next;
    if (next->mp_next) {
        // Joined on to the previous segment unless the turn is sharper than RailEditorMaxJoinAngle.
        CEditedRailPoint* nextNext = next->mp_next;
        Vector before = {static_cast<float>(static_cast<double>(next->m_pos.x) - nextNext->m_pos.x),
                         static_cast<float>(static_cast<double>(next->m_pos.y) - nextNext->m_pos.y),
                         static_cast<float>(static_cast<double>(next->m_pos.z) - nextNext->m_pos.z),
                         static_cast<float>(static_cast<double>(next->m_pos.w) - nextNext->m_pos.w)};
        Vector here = {static_cast<float>(static_cast<double>(m_pos.x) - next->m_pos.x),
                       static_cast<float>(static_cast<double>(m_pos.y) - next->m_pos.y),
                       static_cast<float>(static_cast<double>(m_pos.z) - next->m_pos.z),
                       static_cast<float>(static_cast<double>(m_pos.w) - next->m_pos.w)};
        before.Normalize();
        here.Normalize();
        float dot = static_cast<float>((static_cast<double>(here.z) * before.z + static_cast<double>(here.y) * before.y) +
                                       static_cast<double>(here.x) * before.x);
        bool join = !X87::CosAbove(static_cast<double>(Script::GetFloat(kMaxJoinAngle)) * kDegToRad, dot);
        Vector c, b, a;
        TransformRailPoint(&c, &nextNext->m_pos, origin, position, angle);
        TransformRailPoint(&b, &next->m_pos, origin, position, angle);
        TransformRailPoint(&a, &m_pos, origin, position, angle);
        StretchRailSector(kRailSource, &b, &c, &a, mp_segment, join ? next->mp_segment : nullptr, true);
        return;
    }
    Vector b, a;
    TransformRailPoint(&b, &next->m_pos, origin, position, angle);
    TransformRailPoint(&a, &m_pos, origin, position, angle);
    // (the original passes an unset local as the previous point: unread without a previous sector)
    StretchRailSector(kRailSource, &b, &b, &a, mp_segment, nullptr, true);
}

// 0x004F56D0
void CEditedRailPoint::DrawPost(const Vector* origin, const Vector* position, float angle)
{
    if (!m_hasPost || m_height < kPostWidth) {
        DeletePost();
        return;
    }
    uint8_t* geometry = GetGeometry(GetSector(kPostSource));
    int count = VertexCount(geometry);
    auto* vertices = static_cast<Vector*>(Mem::Malloc(static_cast<uint32_t>(count) << 4));
    GetVertices(geometry, vertices);
    float low = kFar, high = -kFar;
    for (int i = 0; i < count; ++i) {
        float y = vertices[i].y;
        if (y < low)
            low = y;
        if (y > high)
            high = y;
    }
    // Leaning along the rail at an inner point: the post narrower as the rail climbs.
    float width = kPostWidth;
    if (mp_next && mp_prev) {
        double ax = static_cast<double>(mp_prev->m_pos.x) - m_pos.x;
        double ay = static_cast<double>(mp_prev->m_pos.y) - m_pos.y;
        float az = static_cast<float>(static_cast<double>(mp_prev->m_pos.z) - m_pos.z);
        float aw = static_cast<float>(static_cast<double>(mp_prev->m_pos.w) - m_pos.w);
        double bx = static_cast<double>(m_pos.x) - mp_next->m_pos.x;
        float by = static_cast<float>(static_cast<double>(m_pos.y) - mp_next->m_pos.y);
        float bz = static_cast<float>(static_cast<double>(m_pos.z) - mp_next->m_pos.z);
        float bw = static_cast<float>(static_cast<double>(m_pos.w) - mp_next->m_pos.w);
        float sx = static_cast<float>(bx + ax);
        double sy = static_cast<double>(by) + ay;
        double sz = static_cast<double>(bz) + az;
        float sw = static_cast<float>(static_cast<double>(bw) + aw);
        Vector along = {static_cast<float>(static_cast<double>(sx) * 0.5f), static_cast<float>(sy * 0.5f),
                        static_cast<float>(sz * 0.5f), static_cast<float>(static_cast<double>(sw) * 0.5f)};
        along.Normalize();
        width = static_cast<float>(X87::Sqrt(1.0f - static_cast<double>(along.y) * along.y) * kPostWidth);
    }
    Vector at;
    TransformRailPoint(&at, &m_pos, origin, position, angle);
    double drop = static_cast<double>(width) + high;
    for (int i = 0; i < count; ++i) {
        Vector& v = vertices[i];
        v.x = static_cast<float>(static_cast<double>(at.x) + v.x);
        v.y = static_cast<float>(static_cast<double>(at.y) + v.y);
        v.z = static_cast<float>(static_cast<double>(at.z) + v.z);
        v.w = static_cast<float>(static_cast<double>(at.w) + v.w);
        v.y = static_cast<float>(static_cast<double>(v.y) - drop);
    }
    // The foot's vertices down to the ground.
    double foot = (static_cast<double>(m_height) - drop) - high;
    for (int i : {8, 9, 10, 11, 1, 2, 5, 6})
        vertices[i].y = static_cast<float>(static_cast<double>(vertices[i].y) - foot);
    if (mp_next) {
        // Turned to face along the rail.
        double dx = static_cast<double>(m_pos.x) - mp_next->m_pos.x;
        double dz = static_cast<double>(m_pos.z) - mp_next->m_pos.z;
        double length = X87::Sqrt(dz * dz + dx * dx);
        if (length > 0.0) {
            double inverse = 1.0 / length;
            dx *= inverse;
            dz *= inverse;
        }
        float minusZ = static_cast<float>(-dz);
        for (int i = 0; i < count; ++i) {
            Vector& v = vertices[i];
            double rx = static_cast<double>(v.x) - at.x;
            double rz = static_cast<double>(v.z) - at.z;
            v.x = static_cast<float>((dx * rx + rz * minusZ) + at.x);
            v.z = static_cast<float>((dz * rx + dx * rz) + at.z);
        }
    }
    if (!mp_post)
        mp_post = CloneSector(kPostSource);
    SetVertices(GetGeometry(mp_post), vertices);
    Mem::FreeMalloc(vertices);
}

// 0x004F5D10
float __fastcall CEditedRailPoint::GroundHeight(const Vector* position)
{
    Nx::CFeeler feeler;
    feeler.Construct();
    Vector start = {position->x, static_cast<float>(static_cast<double>(position->y) + kGroundProbe), position->z,
                    position->w};
    feeler.SetStart(&start);
    Vector end = {position->x, static_cast<float>(static_cast<double>(position->y) - kGroundProbe), position->z,
                  position->w};
    feeler.SetEnd(&end);
    if (feeler.GetCollision(true, false)) {
        float height = feeler.m_point.y;
        feeler.m_object.Release();
        return height;
    }
    feeler.m_object.Release();
    return 0.0f;
}

// 0x004F5E30
void CEditedRailPoint::Highlight(bool flash, bool includeEndPosts)
{
    if (m_highlighted)
        return;
    uint32_t mask = flash ? FlashMask(Script::GetInteger(kFlashRate)) : 0xFFFFFFFFu;
    Colour normal = {static_cast<uint8_t>(Script::GetInteger(kHighlightR)),
                     static_cast<uint8_t>(Script::GetInteger(kHighlightG)),
                     static_cast<uint8_t>(Script::GetInteger(kHighlightB))};
    Colour bad = {static_cast<uint8_t>(Script::GetInteger(kBadR)), static_cast<uint8_t>(Script::GetInteger(kBadG)),
                  static_cast<uint8_t>(Script::GetInteger(kBadB))};
    // Too long a segment either side: everything in the bad colour.
    bool tooLong = false;
    if (mp_next && RailTooFar(&m_pos, &mp_next->m_pos))
        tooLong = true;
    if (mp_prev && RailTooFar(&m_pos, &mp_prev->m_pos))
        tooLong = true;
    auto cornerOk = [](const CEditedRailPoint* p) {
        return !p->mp_next || !p->mp_prev || RailCornerOk(&p->mp_next->m_pos, &p->m_pos, &p->mp_prev->m_pos);
    };
    auto paint = [mask](uint8_t* sector, const Colour& colour) {
        SetSectorColour(sector, colour.Word());
        SetSectorVisibility(sector, mask);
    };
    // (the colour last chosen carries on to the segment when there's no next point to judge it by)
    Colour colour = normal;
    if (mp_post) {
        colour = cornerOk(this) && !tooLong ? normal : bad;
        paint(mp_post, colour);
    }
    if (mp_segment) {
        if (mp_next)
            colour = cornerOk(mp_next) && cornerOk(this) && !tooLong ? normal : bad;
        paint(mp_segment, colour);
    }
    if (mp_prev && mp_prev->mp_segment)
        paint(mp_prev->mp_segment, cornerOk(mp_prev) && cornerOk(this) && !tooLong ? normal : bad);
    if (includeEndPosts) {
        if (mp_next && !mp_next->mp_next && mp_next->mp_post)
            paint(mp_next->mp_post, normal);
        if (mp_prev && !mp_prev->mp_prev && mp_prev->mp_post)
            paint(mp_prev->mp_post, normal);
    }
    m_highlighted = true;
}

// --- CEditedRail --------------------------------------------------------------------------------------------------

// 0x004F4370
void CEditedRail::Transform(const Vector* origin, const Vector* position, float angle)
{
    // (TransformRailPoint's turn, but z' not rounded to float before the position is added)
    for (CEditedRailPoint* p = mp_points; p; p = p->mp_next) {
        double dx = static_cast<double>(p->m_pos.x) - origin->x;
        float dy = static_cast<float>(static_cast<double>(p->m_pos.y) - origin->y);
        double dz = static_cast<double>(p->m_pos.z) - origin->z;
        float x;
        double z;
        TurnAboutY(dx, dz, angle, &x, &z);
        p->m_pos.z = static_cast<float>(z + position->z);
        p->m_pos.w = static_cast<float>(0.0 + position->w); // (the original adds an unset local - here 0)
        p->m_pos.x = static_cast<float>(static_cast<double>(x) + position->x);
        p->m_pos.y = static_cast<float>(static_cast<double>(dy) + position->y);
    }
}

// 0x004F61C0
void CEditedRail::DrawSegments(const Vector* origin, const Vector* position, float angle)
{
    CEditedRailPoint* last = nullptr;
    for (CEditedRailPoint* p = mp_points; p; p = p->mp_next)
        last = p;
    for (CEditedRailPoint* p = last; p; p = p->mp_prev)
        p->DrawSegment(origin, position, angle);
}

// 0x004F6210
void CEditedRail::DrawPosts(const Vector* origin, const Vector* position, float angle)
{
    for (CEditedRailPoint* p = mp_points; p; p = p->mp_next)
        p->DrawPost(origin, position, angle);
}

// 0x004F63B0
bool CEditedRail::MovePoint(int index, const Vector* position, bool /*updateSuperSectors*/)
{
    CEditedRailPoint* p = GetPoint(index);
    bool valid = true;
    if (p->mp_next && !RailSegmentValid(&p->mp_next->m_pos, position))
        valid = false;
    if (p->mp_prev && !RailSegmentValid(&p->mp_prev->m_pos, position))
        valid = false;
    if (!CParkEditor::IsInsidePark(*position, 10.0f))
        valid = false;
    // Moved (even when not valid) unless a neighbour would be out of reach.
    if (p->mp_next && DistanceZYX(position, &p->mp_next->m_pos) > kMaxReach)
        return valid;
    if (p->mp_prev && DistanceZYX(position, &p->mp_prev->m_pos) > kMaxReach)
        return valid;
    p->m_pos = *position;
    p->m_height = static_cast<float>(static_cast<double>(p->m_pos.y) - CEditedRailPoint::GroundHeight(&p->m_pos));
    p->DrawPost(&Zero(), &Zero(), 0.0f);
    p->DrawSegment(&Zero(), &Zero(), 0.0f);
    if (p->mp_prev)
        p->mp_prev->DrawSegment(&Zero(), &Zero(), 0.0f);
    return valid;
}

// --- CRailEditorComponent -----------------------------------------------------------------------------------------

// 0x004F6510
void CRailEditorComponent::RestoreHeights()
{
    SetActive(false);
    for (CEditedRail* rail = mp_rails; rail; rail = rail->mp_next) {
        for (CEditedRailPoint* p = rail->mp_points; p; p = p->mp_next) {
            if (p->m_hasPost)
                p->m_pos.y = static_cast<float>(static_cast<double>(CEditedRailPoint::GroundHeight(&p->m_pos)) +
                                                p->m_height);
        }
    }
    SetActive(true);
}

// 0x004F6560
void CRailEditorComponent::StoreHeights()
{
    for (CEditedRail* rail = mp_rails; rail; rail = rail->mp_next) {
        for (CEditedRailPoint* p = rail->mp_points; p; p = p->mp_next)
            p->m_height = static_cast<float>(static_cast<double>(p->m_pos.y) - CEditedRailPoint::GroundHeight(&p->m_pos));
    }
}

// 0x004F65A0
void CRailEditorComponent::DrawAllSegments()
{
    for (CEditedRail* rail = mp_rails; rail; rail = rail->mp_next)
        rail->DrawSegments(&Zero(), &Zero(), 0.0f);
}

// 0x004F6600
void CRailEditorComponent::DrawAllPosts()
{
    for (CEditedRail* rail = mp_rails; rail; rail = rail->mp_next)
        rail->DrawPosts(&Zero(), &Zero(), 0.0f);
}

// 0x004F6650
void CRailEditorComponent::Rebuild()
{
    CParkManager::sInstance()->mp_generator->RebuildCollision();
    StoreHeights();
    DrawAllSegments();
    DrawAllPosts();
}

// 0x004F6AF0
void CRailEditorComponent::DrawDottedLine(const Vector* position)
{
    if (!mp_current)
        return;
    CEditedRailPoint* head = mp_current->mp_points;
    if (!head)
        return;
    uint32_t source = kDottedGreen;
    if (RailTooFar(&head->m_pos, position)) {
        if (RailOutOfReach(&head->m_pos, position)) {
            DeleteDottedLine();
            return;
        }
        source = kDottedRed;
    } else if (head->mp_next && !RailCornerOk(&head->mp_next->m_pos, &head->m_pos, position)) {
        source = kDottedRed;
    }
    if (!mp_dottedLine || source != m_dottedLineName) {
        DeleteDottedLine();
        mp_dottedLine = CloneRailSector(source);
        m_dottedLineName = source;
    }
    // (the original passes an unset local as the previous point: unread without a previous sector)
    StretchRailSector(m_dottedLineName, &head->m_pos, position, position, mp_dottedLine, nullptr, false);
    if (source == kDottedRed)
        SetSectorVisibility(mp_dottedLine, FlashMask(Script::GetInteger(kRedLineFlashRate)));
}

// --- The register-argument entry points ---------------------------------------------------------------------------

// 0x004F3D80: EAX out, ECX position, EDX origin, ESI the point; the angle on the stack (the caller pops it). EAX
// back; ECX and EDX kept (a caller reuses ECX).
__declspec(naked) void TransformRailPointStub()
{
    __asm {
        push edx
        push ecx
        push dword ptr [esp + 0xc]
        push ecx
        push edx
        push esi
        push eax
        call TransformRailPoint
        add esp, 0x14
        pop ecx
        pop edx
        ret
    }
}

// 0x004F3E40: EAX, ECX, EDX the three points; AL back.
__declspec(naked) void RailCornerOkStub()
{
    __asm {
        push ecx
        push edx
        push edx
        push ecx
        push eax
        call RailCornerOk
        add esp, 0xc
        pop edx
        pop ecx
        ret
    }
}

// 0x004F3F30 / 0x004F3F70: ECX and EAX the two points; EAX back (ECX kept: a caller reuses it).
__declspec(naked) void RailTooFarStub()
{
    __asm {
        push ecx
        push edx
        push eax
        push ecx
        call RailTooFar
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

__declspec(naked) void RailOutOfReachStub()
{
    __asm {
        push ecx
        push edx
        push eax
        push ecx
        call RailOutOfReach
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// 0x004F3FB0: ECX and EDX the two points; AL back.
__declspec(naked) void RailSegmentValidStub()
{
    __asm {
        push ecx
        push edx
        push edx
        push ecx
        call RailSegmentValid
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// 0x004F4B90: ECX the direction, EDI the translation; the vertices, indices and count on the stack (the caller pops
// them).
__declspec(naked) void OrientRailVerticesStub()
{
    __asm {
        push ecx
        push edx
        push dword ptr [esp + 0x14]
        push dword ptr [esp + 0x14]
        push dword ptr [esp + 0x14]
        push edi
        push ecx
        call OrientRailVertices
        add esp, 0x14
        pop edx
        pop ecx
        ret
    }
}

// 0x004F4E70: EAX the source sector's name, ECX the start; the previous point, the end, the sector, the previous
// sector and the collision flag on the stack (the caller pops them).
__declspec(naked) void StretchRailSectorStub()
{
    __asm {
        push ecx
        push edx
        push dword ptr [esp + 0x1c]
        push dword ptr [esp + 0x1c]
        push dword ptr [esp + 0x1c]
        push dword ptr [esp + 0x1c]
        push dword ptr [esp + 0x1c]
        push ecx
        push eax
        call StretchRailSector
        add esp, 0x1c
        pop edx
        pop ecx
        ret
    }
}

} // namespace Ed

REPLACE(0x004F3E00, Ed::CloneRailSector);
REPLACE_UNCHECKED(0x004F3D80, Ed::TransformRailPointStub);
REPLACE_UNCHECKED(0x004F3E40, Ed::RailCornerOkStub);
REPLACE_UNCHECKED(0x004F3F30, Ed::RailTooFarStub);
REPLACE_UNCHECKED(0x004F3F70, Ed::RailOutOfReachStub);
REPLACE_UNCHECKED(0x004F3FB0, Ed::RailSegmentValidStub);
REPLACE_UNCHECKED(0x004F4B90, Ed::OrientRailVerticesStub);
REPLACE_UNCHECKED(0x004F4E70, Ed::StretchRailSectorStub);
REPLACE(0x004F5460, &Ed::CEditedRailPoint::DrawSegment);
REPLACE(0x004F56D0, &Ed::CEditedRailPoint::DrawPost);
REPLACE(0x004F5D10, Ed::CEditedRailPoint::GroundHeight);
REPLACE(0x004F5E30, &Ed::CEditedRailPoint::Highlight);
REPLACE(0x004F4370, &Ed::CEditedRail::Transform);
REPLACE(0x004F61C0, &Ed::CEditedRail::DrawSegments);
REPLACE(0x004F6210, &Ed::CEditedRail::DrawPosts);
REPLACE(0x004F63B0, &Ed::CEditedRail::MovePoint);
REPLACE(0x004F6510, &Ed::CRailEditorComponent::RestoreHeights);
REPLACE(0x004F6560, &Ed::CRailEditorComponent::StoreHeights);
REPLACE(0x004F65A0, &Ed::CRailEditorComponent::DrawAllSegments);
REPLACE(0x004F6600, &Ed::CRailEditorComponent::DrawAllPosts);
REPLACE(0x004F6650, &Ed::CRailEditorComponent::Rebuild);
REPLACE(0x004F6AF0, &Ed::CRailEditorComponent::DrawDottedLine);
