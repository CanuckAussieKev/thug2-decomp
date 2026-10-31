// The engine's flexible particle systems: emission, the member functions scripts reach through the flexible particle
// component, and the update (see particle_system.h). From the disassembly of 0x00496FE0-0x0049778E,
// 0x00497B00-0x0049868F, 0x004987F0-0x00498F7D and 0x00499EE0-0x0049A080. The x87 code is followed at 53 bits: what
// it keeps on its stack is a double here, what it stores to memory a float.

#include "game/gfx/particle_system.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/gfx/view_cam.h"
#include "game/gfx/engine.h"
#include "game/gfx/viewport.h"
#include "game/engine3d/probe_ray.h"
#include "game/obj/entity.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"
#include "game/skate/skate_module.h"
#include "game/gfx/flexible_particle_manager.h"
#include "game/sys/crt.h"

namespace Engine3D {

namespace {

constexpr float kRandomUnit = std::bit_cast<float>(0x38000100u); // 0x00646510: 1 / 32767
constexpr float kPi = std::bit_cast<float>(0x40490FDBu);          // 0x006493C0
constexpr float kDegrees = std::bit_cast<float>(0x3C8EFA35u);     // 0x00645464: pi / 180
constexpr float kFarAway = 1024.0f;                                 // where a removed particle's positions go

int Rand() { return Crt::Rand(); } // (the game's C runtime's)
// _ftol: truncated to a 64-bit integer, its low half.
int Truncate(double value) { return static_cast<int>(static_cast<int64_t>(value)); }
// A random number in [-1, 1].
float RandomSigned()
{
    double v = static_cast<double>(Rand()) * kRandomUnit;
    return static_cast<float>(v + v - 1.0);
}
// min + (max - min) times a random fraction.
double RandomBetween(float min, float max)
{
    double t = static_cast<double>(Rand()) * kRandomUnit;
    return t * (static_cast<double>(max) - min) + min;
}

int ViewportCount() { return GameModules::g_viewportCount; }
void* ViewportCamera(int viewport) { return FetchLiveCamera(viewport); }
// 0x0048C490: whether a sphere is out of the camera's view.
int SphereHidden(const Maths::Vec4* centre, float radius)
{
    return Engine::SphereVisibleInOneViewport(centre, radius);
}

// The system's virtual functions.
void DeleteSystem(CEmitterInstance* system)
{
    reinterpret_cast<void(__thiscall*)(CEmitterInstance*, unsigned int)>(system->m_vtable[0])(system, 1);
}
int NumVertexLists(CEmitterInstance* system)
{
    return reinterpret_cast<int(__thiscall*)(CEmitterInstance*)>(system->m_vtable[2])(system);
}
int NumColours(CEmitterInstance* system)
{
    return reinterpret_cast<int(__thiscall*)(CEmitterInstance*)>(system->m_vtable[3])(system);
}

float* PositionOf(CEmitterInstance* system, int particle, int step)
{
    return system->m_positionArray + 3 * (system->m_maxParticles * step + particle);
}

// The member functions' names and parameters.
enum : uint32_t {
    kSetEmitAngle = 0x1CFBF078,
    kSetPos = 0x099CD17B,
    kSetParticleSize = 0x2A479569,
    kSetParticleSizeScale = 0x2E66815E,
    kEmitRate = 0x35B65BD4,
    kSetColor = 0x514B2584,
    kEmpty = 0x482482C6, // (its name unknown)
    kSpeedSpanCmd = 0x63274395,
    kEmit = 0x711B1A6A,
    kSetAngleSpread = 0x7911855B,
    kApplyEnvBrightness = 0x96896315,
    kSetEmitRange = 0x8FFDEF38,
    kSetForce = 0xA075EE45,
    kSetEmitTarget = 0xB373FB6E,
    kSetLife = 0xD3F1D333,
    kSetEmitterIndependent = 0xE3309AB5,
    kRandomSpread = 0xDB8621F7, // (its name unknown)
    kSetCircularEmit = 0xE985A9A6,
    kSetLODParams = 0xFBB6720D,

    kX = 0x7323E97C,
    kY = 0x0424D9EA,
    kZ = 0x9D2D8850,
    kAngle = 0xFF7EBAF6,
    kPos = 0x7F261953,
    kSw = 0x745523AA,
    kSh = 0xF95D2E5F,
    kEw = 0x68CD967D,
    kEh = 0xE5C59B88,
    kScale = 0x5B1D3822, // (SetParticleSizeScale's)
    kRate = 0x2013C0C6,
    kCorner = 0xFEC1C2FB,
    kMidTime = 0x856BC5E2,
    kMin = 0x5E84E22F,
    kMax = 0x6289DD76,
    kNum = 0x23BC5091,
    kSpread = 0x834A5F87,
    kEveryUpdate = 0x652F5D69, // (ApplyEnvBrightness's flag)
    kWidth = 0x73E5BAD0,
    kHeight = 0x0AB21AF0,
    kDepth = 0x055CE396,
    kForce = 0x68977CD7,
    kTarget = 0xB990D003,
    kRandom = 0xE9C4252A,
    kCircular = 0x9D2E07B3,
    kMinParticles = 0x6C4E18EB,
    kLodDist1 = 0x4EEDFAE7,
    kLodDist2 = 0xD7E4AB5D,
};

// The colour channels setColor reads: start, mid and end; the alphas halved.
const uint32_t kEndChannels[4] = {0x18A762F2, 0x757A8619, 0x05107296, 0x9C19232C};   // er, eg, eb, ea
const uint32_t kMidChannels[4] = {0xD07EE8FA, 0xBDA30C11, 0xCDC9F89E, 0x54C0A924};   // mr, mg, mb, ma
const uint32_t kStartChannels[4] = {0x043FD725, 0x69E233CE, 0x1988C741, 0x808196FB}; // sr, sg, sb, sa

// A vector parameter, or x, y and z ones.
void GetVectorOrXyz(Scripts::CStruct* params, uint32_t name, float& x, float& y, float& z)
{
    Scripts::VectorValue v;
    if (params->FetchVec(name, &v, 0)) {
        x = v.x;
        y = v.y;
        z = v.z;
        return;
    }
    params->FetchFloat(kX, &x, 0);
    params->FetchFloat(kY, &y, 0);
    params->FetchFloat(kZ, &z, 0);
}

// Sizes from the system's, scaled (unless the scale is exactly 1).
void SetSizes(CEmitterInstance* system, sFlexibleParticle& p)
{
    p.startSize[0] = system->m_extent[0];
    p.startSize[1] = system->m_extent[1];
    p.endSize[0] = system->m_extent[2];
    p.endSize[1] = system->m_extent[3];
    if (std::bit_cast<uint32_t>(system->m_sizeScale) != 0x3F800000) {
        p.startSize[0] *= system->m_sizeScale;
        p.startSize[1] *= system->m_sizeScale;
        p.endSize[0] *= system->m_sizeScale;
        p.endSize[1] *= system->m_sizeScale;
    }
}

} // namespace

// 0x00496FE0
void CEmitterInstance::Emit(int count)
{
    if (m_fadingOut)
        return;
    int first = m_unknown8C;
    int total = count + first;
    m_unknown8C = total > m_maxParticles ? m_maxParticles : total;
    int last = m_unknown8C;
    Maths::Mat4 m;
    GetEmitMatrix(&m);
    for (int i = first; i < last; ++i) {
        sFlexibleParticle& p = m_particleList[i];
        if (!m_moving) {
            SetSizes(this, p);
            p.age = 0.0f;
            p.lifetime = static_cast<float>(RandomBetween(m_ageLeft[0], m_ageLeft[1]));
            for (int step = 0; step < NumVertexLists(this); ++step) {
                float* pos = PositionOf(this, i, step);
                pos[0] = 0.0f;
                pos[1] = 0.0f;
                pos[2] = 0.0f;
            }
            continue;
        }
        // the direction: a point on the disc of the spread about the third row
        float a = RandomSigned(), b = RandomSigned(), c = RandomSigned();
        double spread = static_cast<double>(m_angleSpread) * 0.25f * kPi;
        double x = a * spread;
        double y = spread * b;
        double z = 1.0 - (x * x + y * y);
        float zx = static_cast<float>(m.row[2].x * z), zy = static_cast<float>(m.row[2].y * z),
              zz = static_cast<float>(m.row[2].z * z);
        float yx = static_cast<float>(m.row[1].x * y), yy = static_cast<float>(m.row[1].y * y),
              yz = static_cast<float>(m.row[1].z * y);
        float xx = static_cast<float>(m.row[0].x * x), xy = static_cast<float>(m.row[0].y * x);
        double xz = x * m.row[0].z;
        float sumY = static_cast<float>(static_cast<double>(xy) + yy);
        float sumZ = static_cast<float>(xz + yz);
        float dirX = static_cast<float>((static_cast<double>(xx) + yx) + zx);
        float dirY = static_cast<float>(static_cast<double>(sumY) + zy);
        float dirZ = static_cast<float>(static_cast<double>(sumZ) + zz);
        double speed = RandomBetween(m_speed[0], m_speed[1]);
        float velocity[3] = {static_cast<float>(dirX * speed), static_cast<float>(dirY * speed),
                             static_cast<float>(dirZ * speed)};
        // the position in the emit box (a new random point with the random spread; on its circle when circular)
        if (m_randomDirection) {
            a = RandomSigned();
            b = RandomSigned();
            c = RandomSigned();
        }
        if (m_circularEmit) {
            double f = 1.0 / std::sqrt(static_cast<double>(b) * b + static_cast<double>(a) * a);
            a = static_cast<float>(std::fabs(a) * f * a);
            b = static_cast<float>(std::fabs(b) * f * b);
        }
        double ax = static_cast<double>(a) * m_emitRange[0];
        double by = static_cast<double>(b) * m_emitRange[1];
        double cz = static_cast<double>(c) * m_emitRange[2];
        float czx = static_cast<float>(m.row[2].x * cz), czy = static_cast<float>(m.row[2].y * cz),
              czz = static_cast<float>(m.row[2].z * cz);
        float byx = static_cast<float>(m.row[1].x * by), byy = static_cast<float>(m.row[1].y * by);
        double byz = by * m.row[1].z;
        float axz = static_cast<float>(m.row[0].z * ax);
        float px = static_cast<float>(m.row[0].x * ax + byx);
        float py = static_cast<float>(m.row[0].y * ax + byy);
        double pz = static_cast<double>(axz) + byz;
        float posX = static_cast<float>(static_cast<double>(px) + czx);
        float posY = static_cast<float>(static_cast<double>(py) + czy);
        float posZ = static_cast<float>(pz + czz);
        if (m_fillPath) { // somewhere along the way it came
            float dx = static_cast<float>(static_cast<double>(m_lastPos.x) - m_atPoint.x);
            float dy = static_cast<float>(static_cast<double>(m_lastPos.y) - m_atPoint.y);
            double dz53 = static_cast<double>(m_lastPos.z) - m_atPoint.z;
            float dz = static_cast<float>(dz53);
            double length = (dz53 * dz + static_cast<double>(dy) * dy) + static_cast<double>(dx) * dx;
            if (!(length == 0.0)) {
                double r = static_cast<double>(Rand()) * kRandomUnit;
                double u = (r + r - 1.0 + 1.0) * 0.5;
                float fx = static_cast<float>(dx * u), fy = static_cast<float>(dy * u);
                double fz = u * dz;
                float newZ = static_cast<float>(fz + posZ);
                posX = static_cast<float>(static_cast<double>(fx) + posX);
                posY = static_cast<float>(static_cast<double>(fy) + posY);
                posZ = newZ;
            }
        }
        for (int k = 0; k < 3; ++k)
            p.force[k] = m_force[k];
        SetSizes(this, p);
        p.age = 0.0f;
        p.lifetime = static_cast<float>(RandomBetween(m_ageLeft[0], m_ageLeft[1]));
        for (int k = 0; k < 3; ++k)
            p.velocity[k] = velocity[k];
        for (int step = 0; step < NumVertexLists(this); ++step) {
            float* pos = PositionOf(this, i, step);
            if (m_emitterIndependent) {
                float worldZ = static_cast<float>(static_cast<double>(posZ) + m_atPoint.z);
                float worldY = static_cast<float>(static_cast<double>(posY) + m_atPoint.y);
                pos[0] = static_cast<float>(static_cast<double>(posX) + m_atPoint.x);
                pos[1] = worldY;
                pos[2] = worldZ;
            } else {
                pos[0] = posX;
                pos[1] = posY;
                pos[2] = posZ;
            }
        }
    }
}

// 0x00497B00
bool CEmitterInstance::InvokeMethodByName(uint32_t name, Scripts::CStruct* params, Scripts::CScript*)
{
    switch (name) {
    case kSetEmitAngle: { // (degrees)
        float x = 0.0f, y = 0.0f, z = 0.0f;
        GetVectorOrXyz(params, kAngle, x, y, z);
        ChangeSpin(static_cast<float>(static_cast<double>(kDegrees) * x),
                    static_cast<float>(static_cast<double>(y) * kDegrees),
                    static_cast<float>(static_cast<double>(z) * kDegrees));
        break;
    }
    case kSetPos: {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        GetVectorOrXyz(params, kPos, x, y, z);
        m_atPoint.x = x;
        m_atPoint.y = y;
        m_atPoint.z = z;
        break;
    }
    case kSetParticleSize: { // (sh defaults to sw; ew to sw; eh to ew when that's given, else to sh)
        float sw = 0.0f;
        params->FetchFloat(kSw, &sw, 0);
        float sh = sw;
        params->FetchFloat(kSh, &sh, 0);
        m_extent[0] = sw;
        m_extent[1] = sh;
        float ew = sw;
        float eh = params->FetchFloat(kEw, &ew, 0) ? ew : sh;
        params->FetchFloat(kEh, &eh, 0);
        m_extent[2] = ew;
        m_extent[3] = eh;
        break;
    }
    case kSetParticleSizeScale: {
        float scale = 1.0f;
        params->FetchFloat(kScale, &scale, 0);
        m_sizeScale = scale;
        break;
    }
    case kEmitRate: {
        float rate = 1.0f;
        params->FetchFloat(kRate, &rate, 0);
        m_emitRate = rate;
        break;
    }
    case kSetColor: {
        int first, last, value;
        if (params->FetchIntValue(kCorner, &value, 0)) {
            first = last = value;
        } else {
            first = 0;
            last = NumColours(this) - 1;
        }
        if (first < NumColours(this) && last < NumColours(this)) {
            for (int i = first; i < last + 1; ++i) {
                if (i >= NumColours(this))
                    continue;
                uint8_t* end = mp_endColour + i * 4;
                uint8_t* mid = mp_midColour + i * 4;
                uint8_t* start = mp_startColour + i * 4;
                for (int k = 0; k < 4; ++k) {
                    if (params->FetchIntValue(kEndChannels[k], &value, 0)) {
                        end[k] = k == 3 ? static_cast<uint8_t>(static_cast<uint8_t>(value) >> 1)
                                        : static_cast<uint8_t>(value);
                        m_endColourSet = true;
                    }
                }
                for (int k = 0; k < 4; ++k) {
                    if (params->FetchIntValue(kMidChannels[k], &value, 0))
                        mid[k] = k == 3 ? static_cast<uint8_t>(static_cast<uint8_t>(value) >> 1)
                                        : static_cast<uint8_t>(value);
                }
                for (int k = 0; k < 4; ++k) { // (the end too, unless it was given)
                    if (params->FetchIntValue(kStartChannels[k], &value, 0)) {
                        uint8_t byte = k == 3 ? static_cast<uint8_t>(static_cast<uint8_t>(value) >> 1)
                                              : static_cast<uint8_t>(value);
                        start[k] = byte;
                        if (!m_endColourSet)
                            end[k] = byte;
                    }
                }
            }
        }
        params->FetchFloat(kMidTime, &m_colourMid, 0);
        break;
    }
    case kEmpty:
        m_unknown8C = 0;
        break;
    case kSpeedSpanCmd: {
        float min = 0.0f;
        params->FetchFloat(kMin, &min, 0);
        float max = min;
        params->FetchFloat(kMax, &max, 0);
        m_speed[0] = min;
        m_speed[1] = max;
        break;
    }
    case kEmit: {
        int count = 1;
        params->FetchIntValue(kNum, &count, 0);
        Emit(count);
        break;
    }
    case kSetAngleSpread: {
        float spread = 0.0f;
        params->FetchFloat(kSpread, &spread, 0);
        m_angleSpread = spread;
        break;
    }
    case kApplyEnvBrightness:
        if (params->HasBareFlag(kEveryUpdate)) {
            m_updateBrightness = true;
        } else {
            m_updateBrightness = false;
            UpdateGroundBrightness(true);
        }
        break;
    case kSetEmitRange: {
        float width = 0.0f;
        params->FetchFloat(kWidth, &width, 0);
        float height = width;
        params->FetchFloat(kHeight, &height, 0);
        float depth = 0.0f;
        params->FetchFloat(kDepth, &depth, 0);
        SetEmitRange(width, height, depth);
        break;
    }
    case kSetForce: {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        GetVectorOrXyz(params, kForce, x, y, z);
        SetForce(x, y, z);
        break;
    }
    case kSetEmitTarget: {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        GetVectorOrXyz(params, kTarget, x, y, z);
        if (!(x == 0.0f) || !(y == 1.0f) || !(z == 0.0f)) // (up is the default; NaN isn't)
            SetEmitTarget(x, y, z);
        break;
    }
    case kSetLife: {
        float min = 0.0f;
        params->FetchFloat(kMin, &min, 0);
        float max = min;
        params->FetchFloat(kMax, &max, 0);
        m_ageLeft[0] = min;
        m_ageLeft[1] = max;
        m_moving = true;
        break;
    }
    case kSetEmitterIndependent: {
        int value = 0;
        params->FetchIntValue(0u, &value, 0);
        m_emitterIndependent = value == 1;
        break;
    }
    case kRandomSpread: {
        int value = 0;
        params->FetchIntValue(kRandom, &value, 0);
        SetFlag(4, value > 0);
        m_randomDirection = value > 0;
        break;
    }
    case kSetCircularEmit: {
        int value = 0;
        params->FetchIntValue(kCircular, &value, 0);
        SetFlag(8, value > 0);
        m_circularEmit = value > 0;
        break;
    }
    case kSetLODParams: {
        int minParticles = 0;
        params->FetchIntValue(kMinParticles, &minParticles, 0); // (the bool-assert getter, 0x00476CB0)
        float near = 0.0f, far = 0.0f;
        params->FetchFloat(kLodDist1, &near, 0);
        params->FetchFloat(kLodDist2, &far, 0);
        m_lodMinParticles = minParticles;
        m_lodNear = near * near;
        m_lodFar = far * far;
        break;
    }
    default:
        break;
    }
    return true;
}

void CEmitterInstance::UpdateGroundBrightness(bool movables)
{
    CProbeRay feeler;
    feeler.MakeNew();
    float y = m_atPoint.y;
    feeler.m_line.start = {m_atPoint.x, static_cast<float>(static_cast<double>(y) + 1.0), m_atPoint.z, m_atPoint.w};
    feeler.m_line.end = {m_atPoint.x, static_cast<float>(static_cast<double>(y) - 4800.0), m_atPoint.z, m_atPoint.w};
    if (feeler.GetHitShape(movables, false)) {
        double value = feeler.GetHitVertexValue();
        m_unknownEC = std::bit_cast<uint32_t>(static_cast<float>(value + value));
    }
    feeler.Destruct();
}

void CEmitterInstance::RemoveParticle(int index, bool throughAccessors)
{
    --m_unknown8C;
    m_particleList[index] = m_particleList[m_unknown8C];
    for (int step = 0; step < NumVertexLists(this); ++step) {
        if (throughAccessors) {
            float x, y, z;
            FetchPlace(m_unknown8C, step, &x, &y, &z);
            ChangePlace(index, step, x, y, z);
        } else {
            const float* from = PositionOf(this, m_unknown8C, step);
            float* to = PositionOf(this, index, step);
            to[0] = from[0];
            to[1] = from[1];
            to[2] = from[2];
        }
        float* gone = PositionOf(this, m_unknown8C, step);
        gone[0] = kFarAway;
        gone[1] = kFarAway;
        gone[2] = kFarAway;
    }
}

// 0x004987F0
void CEmitterInstance::TickFrame(float time, bool skip)
{
    if (m_fadingOut && m_unknown8C == 0) {
        DeleteSystem(this);
        return;
    }
    if (m_unknown78 && !GameObjects::FindObject(m_unknown78)) { // (following an object that's gone)
        DeleteSystem(this);
        return;
    }
    if (skip || !m_unknown12F)
        return;
    RunEmitScript();
    UpdateBounds();
    m_unknown130 = true;
    if (ViewportCount() == 1 && ViewportCamera(0) && m_unknown133 && SphereHidden(&m_bounds, m_bounds.w)) {
        m_unknown130 = false;
        if (m_emitRate == 0.0f) { // out of view and not emitting: the particles only age
            for (int i = 0; i < m_unknown8C; ++i) {
                sFlexibleParticle& p = m_particleList[i];
                double age = static_cast<double>(time) + p.age;
                p.age = static_cast<float>(age);
                if (age > p.lifetime) {
                    RemoveParticle(i, true);
                    --i;
                }
            }
            return;
        }
    }
    if (!(m_emitRate == 0.0f)) {
        double owed = static_cast<double>(time) + m_emitOwed;
        m_emitOwed = static_cast<float>(owed);
        int count = Truncate(owed * m_emitRate);
        if (count != 0) {
            m_emitOwed = static_cast<float>(owed - static_cast<double>(count) / m_emitRate);
            Emit(count);
        }
    }
    if (mp_updateScript)
        mp_updateScript->TickFrame();
    if (!m_moving || time == 0.0f)
        return;
    if (m_updateBrightness)
        UpdateGroundBrightness(false);
    float dt = static_cast<float>(static_cast<double>(g_frameTimeScale) * time);
    for (int i = 0; i < m_unknown8C; ++i) {
        sFlexibleParticle& p = m_particleList[i];
        p.age = static_cast<float>(static_cast<double>(time) + p.age);
        for (int k = 0; k < 3; ++k)
            p.velocity[k] = static_cast<float>(static_cast<double>(dt) * p.force[k] + p.velocity[k]);
        for (int step = NumVertexLists(this) - 1; step > 0; --step) { // the history moves on a step
            const float* from = PositionOf(this, i, step - 1);
            float* to = PositionOf(this, i, step);
            to[0] = from[0];
            to[1] = from[1];
            to[2] = from[2];
        }
        float* pos = PositionOf(this, i, 0);
        for (int k = 0; k < 3; ++k)
            pos[k] = static_cast<float>(static_cast<double>(dt) * p.velocity[k] + pos[k]);
    }
    for (int i = 0; i < m_unknown8C; ++i) { // the expired
        if (m_particleList[i].age > m_particleList[i].lifetime) {
            RemoveParticle(i, false);
            --i;
        }
    }
    int lod = LodParticleCount(m_unknown8C);
    for (int i = 0; i < m_unknown8C; ++i) { // beyond the LOD count
        if (lod < m_unknown8C) {
            RemoveParticle(i, false);
            --i;
        }
    }
    m_lastPos = m_atPoint;
}

// 0x00499EE0
uint32_t* CModelParticles::LerpColour(uint32_t* out, const uint8_t* from, const uint8_t* to, float t)
{
    double green = static_cast<double>(m_colourScale[1]) * std::bit_cast<float>(m_unknownEC);
    float blue = static_cast<float>(static_cast<double>(m_colourScale[2]) * std::bit_cast<float>(m_unknownEC));
    auto lerp = [&](int k) {
        return static_cast<uint8_t>(Truncate(static_cast<double>(to[k] - from[k]) * t) + from[k]);
    };
    uint8_t r = lerp(0), g = lerp(1), b = lerp(2), a = lerp(3);
    double red = static_cast<double>(m_colourScale[0]) * std::bit_cast<float>(m_unknownEC);
    auto clamp = [](double value) {
        if (value < 0.0)
            value = 0.0;
        else if (value > 255.0)
            value = 255.0;
        return static_cast<uint8_t>(Truncate(value));
    };
    uint8_t bytes[4] = {clamp(red * r), clamp(green * g), clamp(static_cast<double>(b) * blue), a};
    std::memcpy(out, bytes, 4);
    return out;
}

} // namespace Nx
