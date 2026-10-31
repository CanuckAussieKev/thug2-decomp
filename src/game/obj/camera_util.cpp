// The cameras' shared helpers: see camera_util.h. From the disassembly of 0x004E4340-0x004E4857.

#include "game/obj/camera_util.h"

#include <cmath>
#include <cstdint>

#include "game/nx/feeler.h"
#include "game/obj/smtptr.h"

namespace Obj {

namespace {

// The collision feeler (Nx's ray, 0xB0 bytes; not rewritten yet): its line from +0x00 to +0x10, the hit's
// fraction of it at +0x74 and point at +0x78; what it hit held by a smart pointer at +0x98.
class Feeler {
public:
    Feeler() { AsFeeler()->Construct(); }
    ~Feeler() { reinterpret_cast<CSmtPtr<CRefCounted>*>(m_bytes + 0x98)->~CSmtPtr(); }
    Feeler(const Feeler&) = delete;
    Feeler& operator=(const Feeler&) = delete;
    // 0x004170F0: faces with any of `skip` (or without all of `need`) are passed through.
    void SetIgnore(uint32_t skip, uint32_t need)
    {
        AsFeeler()->SetIgnore(static_cast<uint16_t>(skip), static_cast<uint16_t>(need));
    }
    // 0x004171D0
    void SetLine(const Mth::Vector& start, const Mth::Vector& end)
    {
        AsFeeler()->SetLine(&start, &end);
    }
    // 0x004172F0
    bool GetCollision(int a, int b) { return AsFeeler()->GetCollision(a != 0, b != 0) != 0; }
    // 0x00417710: the line set, then GetCollision.
    bool GetCollision(const Mth::Vector& start, const Mth::Vector& end, int a)
    {
        return AsFeeler()->GetCollisionAlong(&start, &end, a != 0) != 0;
    }
    const Mth::Vector& Start() const { return *reinterpret_cast<const Mth::Vector*>(m_bytes); }
    const Mth::Vector& End() const { return *reinterpret_cast<const Mth::Vector*>(m_bytes + 0x10); }
    float Fraction() const { return *reinterpret_cast<const float*>(m_bytes + 0x74); }
    const Mth::Vector& Point() const { return *reinterpret_cast<const Mth::Vector*>(m_bytes + 0x78); }

private:
    Nx::CFeeler* AsFeeler() { return reinterpret_cast<Nx::CFeeler*>(m_bytes); }
    alignas(16) uint8_t m_bytes[0xB0];
};

// 0x0042BBF0: x, y and z scaled to `length` (left alone at length 0).
void SetLength(Mth::Vector& v, float length) { v.SetLength(length); }

// 0x00402550's maths (each component in double precision; w 0).
Mth::Vector Cross(const Mth::Vector& a, const Mth::Vector& b)
{
    Mth::Vector out;
    out.x = static_cast<float>(static_cast<double>(a.y) * b.z - static_cast<double>(a.z) * b.y);
    out.y = static_cast<float>(static_cast<double>(a.z) * b.x - static_cast<double>(a.x) * b.z);
    out.z = static_cast<float>(static_cast<double>(a.x) * b.y - static_cast<double>(a.y) * b.x);
    out.w = 0.0f;
    return out;
}

constexpr float kMinimumDistance = 13.899999618530273f; // 0x0064B2CC
constexpr float kSideProbe = 8.0f;

} // namespace

// 0x004E4340
double __cdecl FrameBlend(float rate, float seconds)
{
    double scaled = static_cast<double>(seconds) * 60.0f * rate;
    return scaled / ((1.0f - static_cast<double>(rate)) + scaled);
}

// 0x004E4360
void __cdecl AvoidCameraCollisions(Mth::Vector* position, Mth::Matrix* matrix, const Mth::Vector* target,
                                   const Mth::Vector* focus, bool sideways, bool refocus)
{
    Mth::Vector& pos = *position;
    // The direction from the target, normalised (z unrounded until then); w's difference as it is.
    float dw = pos.w - target->w;
    double dz = static_cast<double>(pos.z) - target->z;
    float dy = pos.y - target->y;
    float dx = pos.x - target->x;
    float dirX = dx, dirY = dy, dirZ = static_cast<float>(dz);
    double length = std::sqrt((static_cast<double>(dx) * dx + static_cast<double>(dy) * dy) + dz * dz);
    if (length > 0.0) {
        double inverse = 1.0 / length;
        dirX = static_cast<float>(dx * inverse);
        dirY = static_cast<float>(dy * inverse);
        dirZ = static_cast<float>(dz * inverse);
    }
    Feeler feeler;
    feeler.SetIgnore(0x90, 0);
    // From the target to 2 beyond the camera.
    Mth::Vector beyond;
    beyond.x = static_cast<float>(dirX + dirX) + pos.x;
    beyond.y = static_cast<float>(dirY + dirY) + pos.y;
    beyond.z = static_cast<float>(dirZ + dirZ) + pos.z;
    beyond.w = static_cast<float>(static_cast<double>(dw) + dw + pos.w);
    feeler.SetLine(*target, beyond);
    if (feeler.GetCollision(1, 0)) {
        const Mth::Vector& s = feeler.Start();
        const Mth::Vector& e = feeler.End();
        double lx = static_cast<double>(e.x) - s.x, ly = static_cast<double>(e.y) - s.y,
               lz = static_cast<double>(e.z) - s.z;
        float lineLength = static_cast<float>(std::sqrt((lx * lx + ly * ly) + lz * lz));
        double fraction = feeler.Fraction();
        if (fraction * lineLength < kMinimumDistance)
            fraction = static_cast<double>(kMinimumDistance) / lineLength;
        double pull = fraction * lineLength - 2.0f;
        float pw = static_cast<float>(dw * pull);
        float pz = static_cast<float>(dirZ * pull);
        float py = static_cast<float>(dirY * pull);
        float px = static_cast<float>(pull * dirX);
        pos.x = px + target->x;
        pos.y = py + target->y;
        pos.z = pz + target->z;
        pos.w = pw + target->w;
    }
    if (!sideways)
        return;
    // Off a wall 8 to the right, else to the left.
    const Mth::Vector& right = matrix->row[0];
    Mth::Vector probe = {right.x, 0.0f, right.z, 0.0f};
    SetLength(probe, kSideProbe);
    probe.x += pos.x;
    probe.y += pos.y;
    probe.z += pos.z;
    probe.w += pos.w;
    bool hit = feeler.GetCollision(pos, probe, 1);
    if (hit) {
        const Mth::Vector& p = feeler.Point();
        probe.x -= p.x;
        probe.y -= p.y;
        probe.z -= p.z;
        probe.w -= p.w;
        pos.x -= probe.x;
        pos.y -= probe.y;
        pos.z -= probe.z;
        pos.w -= probe.w;
    } else {
        probe = {-right.x, 0.0f, -right.z, 0.0f};
        SetLength(probe, kSideProbe);
        probe.x += pos.x;
        probe.y += pos.y;
        probe.z += pos.z;
        probe.w += pos.w;
        hit = feeler.GetCollision(pos, probe, 1);
        if (hit) {
            const Mth::Vector& p = feeler.Point();
            double ox = static_cast<double>(probe.x) - p.x, oy = static_cast<double>(probe.y) - p.y;
            float oz = probe.z - p.z, ow = probe.w - p.w;
            pos.x = static_cast<float>(pos.x - ox);
            pos.y = static_cast<float>(pos.y - oy);
            pos.z -= oz;
            pos.w -= ow;
        }
    }
    if (!refocus || !hit)
        return;
    // Look from the new position at the focus.
    Mth::Vector& at = matrix->row[2];
    Mth::Vector& up = matrix->row[1];
    Mth::Vector& across = matrix->row[0];
    at = {focus->x - pos.x, focus->y - pos.y, focus->z - pos.z, 1.0f};
    at.Normalize();
    across = Cross(up, at);
    across.Normalize();
    up = Cross(at, across);
    up.Normalize();
    across = {-across.x, -across.y, -across.z, -across.w};
    at = {-at.x, -at.y, -at.z, -at.w};
    across.w = 0.0f;
    up.w = 0.0f;
    at.w = 0.0f;
    matrix->row[3].w = 1.0f;
}

} // namespace Obj
