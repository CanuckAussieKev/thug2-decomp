// The particle component (GameObjects::CEffectsEmitterModule, particle_component.h): its life and its script member functions.
// The Nx particle systems themselves have no source yet: their methods are called as the game's.

#include "engine/objects/particle_component.h"
#include "engine/gfx/d3dx.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/particle_sets.h"
#include "engine/gfx/pc_particles.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"
#include "engine/net/gamenet_manager.h"
#include "engine/render/probe_ray.h"
#include "engine/objects/suspend_component.h"
#include "engine/script/cscript.h"
#include "engine/script/names.h"
#include "engine/script/param_struct.h"
#include "platform/game_clock.h"
#include "engine/objects/control_pad.h"
#include "engine/objects/collide_and_die.h"
#include "engine/core/code_address.h"
#include "engine/core/stubs.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/entity.h"

namespace GameObjects {

uint32_t g_particleComponentMemberFunctions[126]{
            0x88BE2262, 0xA9DB601E, 0, 0, 0, 0,
            0xBB6018F4, 0xA9DB601E, 0, 0, 0, 0,
            0xE5134689, 0xA9DB601E, 0, 0, 0, 0,
            0xF71D696E, 0xA9DB601E, 0, 0, 0, 0,
            0x0AAB006D, 0xA9DB601E, 0, 0, 0, 0,
            0xE2B99038, 0xA9DB601E, 0, 0, 0, 0,
            0xC036F4D0, 0xA9DB601E, 0, 0, 0, 0,
            0xC6525C4E, 0xA9DB601E, 0, 0, 0, 0,
            0x31FCC8ED, 0xA9DB601E, 0, 0, 0, 0,
            0x19AF74D1, 0xA9DB601E, 0, 0, 0, 0,
            0x4AA40FB5, 0xA9DB601E, 0, 0, 0, 0,
            0x234CEABB, 0xA9DB601E, 0, 0, 0, 0,
            0x9E100F60, 0xA9DB601E, 0, 0, 0, 0,
            0x2F0DA461, 0xA9DB601E, 0, 0, 0, 0,
            0xEEFFAE18, 0xA9DB601E, 0, 0, 0, 0,
            0x0FE869E8, 0xA9DB601E, 0, 0, 0, 0,
            0x19AA4CD3, 0xA9DB601E, 0, 0, 0, 0,
            0x5446AF14, 0xA9DB601E, 0, 0, 0, 0,
            0x55835CF2, 0xA9DB601E, 0, 0, 0, 0,
            0x3D05AFE0, 0xA9DB601E, 0, 0, 0, 0,
            0, 0xA9DB601E, 0, 0, 0, 0};
Engine3D::CEffectsDirector* g_particleHub;

namespace {

extern void** const kVtable;
constexpr uint32_t kSuspend = 0xCE0CA665; // the suspend component

// The particle system's methods (Nx, no source yet; thiscall).
void Refresh(void* particle) { reinterpret_cast<Engine3D::CPcParticleSystem*>(particle)->EngineUpdate(); }
// The unnamed float (required). The original reads it into its own `params` argument's slot: not given, the
// value is the pointer's bits.
float UnnamedFloat(Scripts::CStruct* params)
{
    float value;
    std::memcpy(&value, &params, 4);
    params->FetchFloat(0u, &value, 1);
    return value;
}

// Its parameters (particle_component.h).
const uint8_t* Params(void* particle) { return ParticleSystemParams(particle); }
// 0x00494EE0: its +0x38 set (the model's +0x22C, or the ground test's value), then its vtable slot 6.
void SetGroundValue(void* particle, float value) { reinterpret_cast<Engine3D::CPcParticleSystem*>(particle)->SetValue38(value); }

// The parameters a particle system is made from (Engine3D::CSparkleConfig, gfx/particles.h; constructed by 0x00496240,
// given to the particle manager's 0x00496510, kept as the system's +4).
using ParticleParams = Engine3D::CSparkleConfig;

// D3DXMatrixMultiply (the library's, 0x005BFA8C): out = a * b.
void MatrixMultiply(Maths::Mat4* out, const Maths::Mat4* a, const Maths::Mat4* b)
{
    D3dx::MatrixMultiply(out, a, b);
}

// An array's first three integers' low bytes into `rgb`; `last` the third.
void GetRGB(Scripts::CArray* array, uint8_t* rgb, uint32_t* last)
{
    for (uint32_t i = 0; i < 3; ++i) {
        *last = array->FetchPart(i);
        rgb[i] = static_cast<uint8_t>(*last);
    }
}

constexpr uint32_t kEmitting = 0x2299A9F2;   // (its name unknown): the particle system's vtable slot 7
constexpr uint32_t kGroundTest = 0x96896315; // (its name unknown)

// The unnamed array's first four integers' low bytes (r, g, b, a).
struct Colour {
    uint8_t rgba[4];
};
Colour ColourOf(Scripts::CArray* array)
{
    Colour colour;
    for (uint32_t i = 0; i < 4; ++i)
        colour.rgba[i] = static_cast<uint8_t>(array->FetchPart(i));
    return colour;
}

} // namespace

// --- life ---

// 0x0042A000
CObjectModule* __cdecl CEffectsEmitterModule::Fabricate()
{
    void* memory = MemSys::AllocZeroed(sizeof(CEffectsEmitterModule));
    return memory ? static_cast<CEffectsEmitterModule*>(memory)->MakeNew() : nullptr;
}

// 0x00429940
void __cdecl CEffectsEmitterModule::PublishMethodTable()
{
    Scripts::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), g_particleComponentMemberFunctions, 0x18);
}

// 0x00429B20
CEffectsEmitterModule* CEffectsEmitterModule::MakeNew()
{
    CObjectModule::MakeNew();
    m_vtable = kVtable;
    m_kindTag = kType;
    m_emitTarget = 0;
    m_effectHandle = nullptr;
    m_meshInstance = nullptr;
    m_lifespan = 0;
    m_startTime = FrameClock::GameClockElapsed() + g_clockOffset;
    return this;
}

// 0x00429B90
void CEffectsEmitterModule::DestroyBody()
{
    m_vtable = kVtable;
    if (m_effectHandle)
        g_particleHub->RetireParticle(reinterpret_cast<Engine3D::CSparkle*>(m_effectHandle));
    CObjectModule::DestroyBody();
}

// 0x0042A060
CEffectsEmitterModule* CEffectsEmitterModule::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Hub::Pull()->Dealloc(this);
    return this;
}

// 0x00429BF0
void CEffectsEmitterModule::Seal() { m_suspend = m_entity->FetchModule(kSuspend); }

// 0x00429960
void CEffectsEmitterModule::Conceal(bool hide) { reinterpret_cast<Engine3D::CPcParticleSystem*>(m_effectHandle)->Conceal(hide); }

// --- script member functions ---

// 0x00429970
bool CEffectsEmitterModule::SetEmitRate(Scripts::CStruct* params, Scripts::CScript*)
{
    float value = UnnamedFloat(params);
    if (m_effectHandle)
        reinterpret_cast<Engine3D::CPcParticleSystem*>(m_effectHandle)->SetRate(value);
    return true;
}

// 0x004299A0
bool CEffectsEmitterModule::SetLifespan(Scripts::CStruct* params, Scripts::CScript*)
{
    float value = UnnamedFloat(params);
    if (m_effectHandle) {
        reinterpret_cast<Engine3D::CPcParticleSystem*>(m_effectHandle)->SetLifespan(value);
        Refresh(m_effectHandle);
    }
    return true;
}

// 0x004299E0
bool CEffectsEmitterModule::SetFloat19AF74D1(Scripts::CStruct* params, Scripts::CScript*)
{
    float value = UnnamedFloat(params);
    if (m_effectHandle) {
        reinterpret_cast<Engine3D::CPcParticleSystem*>(m_effectHandle)->SetColourMidPercent(value);
        Refresh(m_effectHandle);
    }
    return true;
}

// 0x00429A20
bool CEffectsEmitterModule::ChangeMidpointFraction(Scripts::CStruct* params, Scripts::CScript*)
{
    float value = UnnamedFloat(params);
    if (m_effectHandle) {
        SetParticleValue34(m_effectHandle, value);
        Refresh(m_effectHandle);
    }
    return true;
}

namespace {
bool SetReach(CEffectsEmitterModule* component, Scripts::CStruct* params, int which)
{
    float value = UnnamedFloat(params);
    if (component->m_effectHandle) {
        reinterpret_cast<Engine3D::CPcParticleSystem*>(component->m_effectHandle)->ChangeExtent(which, value);
        Refresh(component->m_effectHandle);
    }
    return true;
}
bool ChangeLocation(CEffectsEmitterModule* component, Scripts::CStruct* params, int which)
{
    Maths::Vec4 pos;
    params->FetchVec(0u, reinterpret_cast<Scripts::VectorValue*>(&pos), 1);
    if (component->m_effectHandle) {
        reinterpret_cast<Engine3D::CSparkle*>(component->m_effectHandle)->ChangePlace(which, &pos);
        Refresh(component->m_effectHandle);
    }
    return true;
}
bool SetBox(CEffectsEmitterModule* component, Scripts::CStruct* params, int which)
{
    Maths::Vec4 box;
    params->FetchVec(0u, reinterpret_cast<Scripts::VectorValue*>(&box), 1);
    if (component->m_effectHandle)
        reinterpret_cast<Engine3D::CSparkle*>(component->m_effectHandle)->SetBox(which, &box);
    return true;
}
bool ChangeColour(CEffectsEmitterModule* component, Scripts::CStruct* params, int which)
{
    Scripts::CArray* array;
    params->FetchList(0u, &array, 1);
    if (component->m_effectHandle) {
        Colour colour = ColourOf(array);
        reinterpret_cast<Engine3D::CPcParticleSystem*>(component->m_effectHandle)->ChangeColour(which, reinterpret_cast<const uint32_t*>(&colour));
    }
    return true;
}
} // namespace

bool CEffectsEmitterModule::SetStartRadius(Scripts::CStruct* params, Scripts::CScript*) { return SetReach(this, params, 0); } // 0x00429A60
bool CEffectsEmitterModule::SetMidRadius(Scripts::CStruct* params, Scripts::CScript*) { return SetReach(this, params, 1); }   // 0x00429AA0
bool CEffectsEmitterModule::SetEndRadius(Scripts::CStruct* params, Scripts::CScript*) { return SetReach(this, params, 2); }   // 0x00429AE0
bool CEffectsEmitterModule::SetStartPos(Scripts::CStruct* params, Scripts::CScript*) { return ChangeLocation(this, params, 0); }       // 0x00429C10
bool CEffectsEmitterModule::SetMidPos(Scripts::CStruct* params, Scripts::CScript*) { return ChangeLocation(this, params, 1); }         // 0x00429C50
bool CEffectsEmitterModule::SetEndPos(Scripts::CStruct* params, Scripts::CScript*) { return ChangeLocation(this, params, 2); }         // 0x00429C90
bool CEffectsEmitterModule::SetStartBox(Scripts::CStruct* params, Scripts::CScript*) { return SetBox(this, params, 0); }       // 0x00429CD0
bool CEffectsEmitterModule::SetMidBox(Scripts::CStruct* params, Scripts::CScript*) { return SetBox(this, params, 1); }         // 0x00429D10
bool CEffectsEmitterModule::SetEndBox(Scripts::CStruct* params, Scripts::CScript*) { return SetBox(this, params, 2); }         // 0x00429D50
bool CEffectsEmitterModule::SetStartColor(Scripts::CStruct* params, Scripts::CScript*) { return ChangeColour(this, params, 0); } // 0x00429DD0
bool CEffectsEmitterModule::SetMidColor(Scripts::CStruct* params, Scripts::CScript*) { return ChangeColour(this, params, 1); }   // 0x00429E50
bool CEffectsEmitterModule::SetEndColor(Scripts::CStruct* params, Scripts::CScript*) { return ChangeColour(this, params, 2); }   // 0x00429ED0

// 0x00429D90
bool CEffectsEmitterModule::SetEmitTarget(Scripts::CStruct* params, Scripts::CScript*)
{
    params->ReadHashValue(0u, &m_emitTarget, 1);
    if (m_effectHandle)
        reinterpret_cast<Engine3D::CSparkle*>(m_effectHandle)->SetEmitScript(m_emitTarget, m_entity->m_ident);
    return true;
}

// 0x00429F50
bool CEffectsEmitterModule::SetFlag5446AF14(Scripts::CStruct* params, Scripts::CScript*)
{
    int value = 0;
    params->FetchIntValue(0u, &value, 0);
    if (m_effectHandle)
        static_cast<uint8_t*>(m_effectHandle)[0x15B] = value == 1;
    return true;
}

// 0x00429F90
bool CEffectsEmitterModule::SetColor55835CF2(Scripts::CStruct* params, Scripts::CScript*)
{
    Scripts::CArray* array;
    params->FetchList(0u, &array, 1);
    if (m_effectHandle) {
        Colour colour = ColourOf(array);
        reinterpret_cast<Engine3D::CSparkle*>(m_effectHandle)->ChangeColour(reinterpret_cast<const uint8_t*>(&colour));
    }
    return true;
}

// 0x0042A090
void CEffectsEmitterModule::ConfigureFromData(Scripts::CStruct* params)
{
    ParticleParams p;
    p.MakeNew();
    float scale = 1.0f;
    p.m_emitScript = 0;
    if (params->ReadHashValue(0x092276DBu, &m_emitTarget, 0)) {
        p.m_emitScript = m_emitTarget;
        p.m_entityKey = m_entity->m_ident;
    }
    params->FetchIntValue(0x5A235299u, reinterpret_cast<int*>(&m_lifespan), 0);
    params->ReadHashValue(0xA1DC81F9u, &p.m_label, 1);
    params->ReadHashValue("GroupID", &p.m_groupId, 0);
    params->ReadHashValue(0x7321A8D6u, &p.m_unknown04, 0);
    p.m_flag151 = params->HasBareFlag(0xA7A5BFFBu);
    p.m_flag157 = params->HasBareFlag(0x5446AF14u);
    params->FetchFloat(0x13B9DA7Bu, &scale, 0);
    params->FetchFloat(0x3BEE67C8u, &p.m_extent[0], 0);
    params->FetchFloat(0x8E39913Du, &p.m_extent[1], 0);
    params->FetchFloat(0x3F243A3Cu, &p.m_extent[2], 0);

    // The orientation: the identity, or turned to face 0x0C4C809E (normalised).
    Maths::Mat4 orient;
    orient.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    orient.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    orient.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    orient.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (params->HasBareFlag(0x7045843Au)) {
        Maths::Vec4 at;
        params->FetchVec(0x0C4C809Eu, reinterpret_cast<Scripts::VectorValue*>(&at), 1);
        at.MakeUnit();
        orient.row[2] = at;
        const Maths::Vec4 up = {0.0f, 1.0f, 0.0f, 1.0f};
        orient.row[0].w = 0.0f;
        if (orient.row[2] == up) {
            // (straight up: across from (0, 0, -1))
            float t = static_cast<float>(static_cast<double>(at.z) * 0.0);
            orient.row[0].x = static_cast<float>(static_cast<double>(at.y) * -1.0 - t);
            orient.row[0].y = static_cast<float>(t - static_cast<double>(at.x) * -1.0);
            orient.row[0].z = static_cast<float>(static_cast<double>(at.x) * 0.0 - static_cast<double>(at.y) * 0.0);
        } else {
            // (across from (0, 1, 0))
            double y0 = static_cast<double>(at.y) * 0.0;
            orient.row[0].x = static_cast<float>(y0 - at.z);
            orient.row[0].y = static_cast<float>(static_cast<double>(at.z) * 0.0 - static_cast<double>(at.x) * 0.0);
            orient.row[0].z = static_cast<float>(at.x - y0);
        }
        const Maths::Vec4& r = orient.row[0];
        orient.row[1].w = 0.0f;
        orient.row[1].x = static_cast<float>(static_cast<double>(at.y) * r.z - static_cast<double>(at.z) * r.y);
        orient.row[1].y = static_cast<float>(static_cast<double>(at.z) * r.x - static_cast<double>(at.x) * r.z);
        orient.row[1].z = static_cast<float>(static_cast<double>(at.x) * r.y - static_cast<double>(at.y) * r.x);
    }
    for (float& size : p.m_extent)
        size = static_cast<float>(static_cast<double>(size) * scale);
    params->FetchFloat(0x05400A25u, &p.m_radius[0], 0);
    params->FetchFloat(0xFF7D9AA9u, &p.m_radius[1], 0);
    params->FetchFloat(0x5539FCEEu, &p.m_radius[2], 0);
    for (float& radius : p.m_radius)
        radius = static_cast<float>(static_cast<double>(radius) * scale);
    params->FetchIntValue(0x9E52A20Fu, &p.m_unknown24, 0);
    params->FetchFloat(0x35B65BD4u, &p.m_unknown28, 0);
    params->FetchFloat(0xC218CF77u, &p.m_unknown2C, 0);
    params->FetchFloat(0x520682C6u, &p.m_unknown30, 0);

    // The positions and boxes: scaled, then turned by the orientation.
    auto scaleAndTurn = [&](Maths::Vec4* v) {
        for (int i = 0; i < 3; ++i) {
            v[i] = {static_cast<float>(static_cast<double>(v[i].x) * scale),
                    static_cast<float>(static_cast<double>(v[i].y) * scale),
                    static_cast<float>(static_cast<double>(v[i].z) * scale),
                    static_cast<float>(static_cast<double>(v[i].w) * scale)};
        }
        for (int i = 0; i < 3; ++i) {
            Maths::Vec4 turned;
            v[i] = orient.Xform(turned, v[i]);
        }
    };
    auto vector = [&](uint32_t name, Maths::Vec4* out) {
        params->FetchVec(name, reinterpret_cast<Scripts::VectorValue*>(out), 0);
    };
    vector(params->HasBareFlag(0x64F194EDu) ? 0x3D5EA6A4u : 0x7F261953u, &p.m_atPoint[0]);
    vector(0x004B51A2u, &p.m_atPoint[1]);
    vector(0x5854AB9Eu, &p.m_atPoint[2]);
    scaleAndTurn(p.m_atPoint);
    vector(0x9656F775u, &p.m_box[0]);
    vector(0xC3CD20A2u, &p.m_box[1]);
    vector(0x829FE7DDu, &p.m_box[2]);
    scaleAndTurn(p.m_box);
    p.m_flag153 = params->HasBareFlag(0x454962EFu);
    params->FetchFloat(0x59463C93u, &p.m_unknown30, 0);

    // The colours (rgb from three arrays; the alphas at most 128).
    uint32_t value = 0;
    Scripts::CArray* array;
    static const uint32_t kColours[3] = {0xDAB75B6Cu, 0x0444769Au, 0x5A3728E7u};
    for (int i = 0; i < 3; ++i)
        if (params->FetchList(kColours[i], &array, 0))
            GetRGB(array, p.m_shade[i], &value);
    static const uint32_t kAlphas[3] = {0x4D83DB4Cu, 0x4ABA1FF1u, 0x5CF83ACAu};
    for (int i = 0; i < 3; ++i) {
        if (params->FetchIntValue(kAlphas[i], reinterpret_cast<int*>(&value), 0)) {
            uint8_t alpha = static_cast<uint8_t>(value);
            p.m_shade[i][3] = alpha > 0x80 ? 0x80 : alpha;
        }
    }
    params->ReadHashValue(0xDD6BB3D5u, &p.m_unknown134, 0);
    params->FetchIntValue(0x9F677D4Eu, &p.m_unknown13C, 0);
    params->FetchIntValue(0x13D0BE1Du, &p.m_unknown138, 0);
    params->ReadHashValue(0x7D99F28Du, &p.m_unknown140, 0);
    params->FetchIntValue(0x4EEDFAE7u, &p.m_unknown144, 0);
    params->FetchIntValue(0xD7E4AB5Du, &p.m_unknown148, 0);
    params->FetchIntValue(0xE4279BA2u, &p.m_unknown14C, 0);
    int byteValue = 0;
    params->FetchIntValue(0x9D5923D8u, &byteValue, 0);
    p.m_unknown150 = static_cast<uint8_t>(byteValue);

    // The rotation: X, Y, then Z.
    p.m_orientationTurn.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    p.m_orientationTurn.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    p.m_orientationTurn.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    p.m_orientationTurn.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    Maths::Vec4 angles = {0.0f, 0.0f, 0.0f, 1.0f};
    params->FetchVec(0x9D2D0915u, reinterpret_cast<Scripts::VectorValue*>(&angles), 0);
    Maths::Mat4 rotation;
    MatrixMultiply(&p.m_orientationTurn, &p.m_orientationTurn, &Maths::MakePitchMatrix(rotation, angles.x));
    MatrixMultiply(&p.m_orientationTurn, &p.m_orientationTurn, &Maths::MakeYawMatrix(rotation, angles.y));
    MatrixMultiply(&p.m_orientationTurn, &p.m_orientationTurn, &Maths::MakeRollMatrix(rotation, angles.z));

    p.m_localPositions = params->HasBareFlag(0xB7B7240Du);
    if (p.m_localPositions)
        for (int i = 0; i < 3; ++i)
            p.m_offsetPos[i] = p.m_atPoint[i];
    p.m_orientWithObject = params->HasBareFlag(0xA2487CC3u);
    p.m_flag158 = params->HasBareFlag(0xA0E585B0u);
    m_effectHandle = g_particleHub->SpawnParticle(
        &p);

    // The model it follows (0x5E2A8D9F: an object's; no such object - a crash, as the original).
    uint32_t model = 0;
    if (params->ReadHashValue(0x5E2A8D9Fu, &model, 0) && model)
        m_meshInstance = static_cast<CGameEntity*>(FindObject(model))->FetchModule(0x286A8D26);
    if (p.m_emitScript) {
        Scripts::CStruct* scriptParams = nullptr;
        params->ReadSubStruct(0x7031F10Cu, &scriptParams, 0);
        m_entity->StartScriptTask(p.m_emitScript, 0, false, false, scriptParams);
    }
    uint32_t value54 = 0xE3714DC1;
    params->ReadHashValue(0x7E4813A7u, &value54, 0);
    reinterpret_cast<uint32_t*>(m_effectHandle)[0x54 / 4] = value54;
    if (p.m_flag158)
        reinterpret_cast<Engine3D::CSparkle*>(m_effectHandle)->ChangeColour(reinterpret_cast<const uint8_t*>(static_cast<void*>(nullptr)));
}

// 0x0042AD90
void CEffectsEmitterModule::TickFrame()
{
    // (suspended: nothing - unless a network game runs and it has a lifetime)
    if (m_suspend && static_cast<CDormancyModule*>(m_suspend)->IsFar()) {
        void* net = static_cast<void*>(g_onlineHub);
        if (!static_cast<OnlinePlay::Hub*>(net)->InNetGame() || m_lifespan == 0)
            return;
    }
    if (m_lifespan != 0) {
        uint32_t age = static_cast<uint32_t>(FrameClock::GameClockElapsed() + (g_clockOffset - m_startTime));
        if (age > m_lifespan) {
            void** vtable = *reinterpret_cast<void***>(m_entity);
            reinterpret_cast<void(__thiscall*)(CGameEntity*)>(vtable[6])(m_entity); // (FlagAsDead)
            return;
        }
    }
    if (m_meshInstance)
        SetGroundValue(m_effectHandle, *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(m_meshInstance) + 0x22C));
    if (!Params(m_effectHandle)[0x152])
        return;
    for (int i = 0; i < 3; ++i) {
        const float* local = reinterpret_cast<const float*>(Params(m_effectHandle) + 0xF4 + i * 0x10);
        const Maths::Vec4& at = m_entity->m_atPoint;
        Maths::Vec4 pos = {local[0] + at.x, local[1] + at.y, local[2] + at.z, local[3] + at.w};
        reinterpret_cast<Engine3D::CSparkle*>(m_effectHandle)->ChangePlace(i, &pos);
        if (Params(m_effectHandle)[0x154]) {
            Maths::Mat4 orientation;
            orientation = m_entity->m_basisFrame;
            orientation.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
            reinterpret_cast<Engine3D::CSparkle*>(m_effectHandle)->SpinBy(i, &orientation);
        }
    }
}

// 0x0042AEE0
int CEffectsEmitterModule::InvokeMethodByName(uint32_t name, Scripts::CStruct*, Scripts::CScript*)
{
    if (name == kEmitting) {
        void** vtable = *reinterpret_cast<void***>(m_effectHandle);
        return reinterpret_cast<bool(__thiscall*)(void*)>(vtable[7])(m_effectHandle) ? 1 : 0;
    }
    if (name == kGroundTest) {
        // A line from just above the particle system's position to 4800 below it: the face hit's vertex value,
        // doubled (1 without a hit).
        float value = 1.0f;
        Engine3D::CProbeRay feeler;
        feeler.MakeNew();
        const float* at = reinterpret_cast<const float*>(Params(m_effectHandle) + 0x94);
        feeler.m_line.start = {at[0], static_cast<float>(at[1] + 1.0f), at[2], at[3]};
        feeler.m_line.end = {at[0], static_cast<float>(static_cast<double>(at[1]) - 4800.0), at[2], at[3]};
        if (feeler.GetHitShape(true, false))
            value = static_cast<float>(feeler.GetHitVertexValue() * 2.0);
        if (m_effectHandle)
            SetGroundValue(m_effectHandle, value);
        return 1;
    }
    return 2;
}

// 0x0042B040
bool CEffectsEmitterModule::GetParticleParams(Scripts::CStruct*, Scripts::CScript* script)
{
    Scripts::CStruct* result = Scripts::SpawnParamBag();
    if (result)
        result = result->PrepareAll();
    // (the name, two floats and the colours read straight from the system's +4; the rest through the getter, once
    // for each value)
    const ParticleParams* own = reinterpret_cast<const ParticleParams*>(static_cast<uint8_t*>(m_effectHandle) + 4);
    result->PutNameKey(0xA1DC81F9u, own->m_label);
    result->PushFloat(0x35B65BD4u, own->m_unknown28);
    result->PushFloat(0xC218CF77u, own->m_unknown2C);
    Scripts::CArray* colours[3];
    for (Scripts::CArray*& array : colours) {
        array = Scripts::AllocScriptArray();
        if (array)
            array = array->PrepareAll();
    }
    uint8_t colour[3][4];
    std::memcpy(colour, own->m_shade, sizeof(colour));
    for (Scripts::CArray* array : colours)
        array->ConfigureSizeKind(4, Scripts::QBVAL_INTEGER);
    for (int i = 0; i < 3; ++i)
        for (uint32_t j = 0; j < 4; ++j)
            colours[i]->SetElement(j, colour[i][j]);
    result->AppendArrayValue(0x7E6BCBE6u, colours[0]);
    result->AppendArrayValue(0x2A6B6FE2u, colours[1]);
    result->AppendArrayValue(0x0481113Du, colours[2]);
    auto get = [this] { return reinterpret_cast<const ParticleParams*>(Params(m_effectHandle)); };
    auto addVector = [&](uint32_t name, const Maths::Vec4& v) {
        result->PutVec3(name, Scripts::VectorValue{v.x, v.y, v.z, v.w});
    };
    addVector(0x7B5A25F8u, get()->m_atPoint[0]);
    addVector(0xA5A9080Eu, get()->m_atPoint[1]);
    addVector(0xFBDA5673u, get()->m_atPoint[2]);
    result->PushFloat(0x3BEE67C8u, get()->m_extent[0]);
    result->PushFloat(0x8E39913Du, get()->m_extent[1]);
    result->PushFloat(0x3F243A3Cu, get()->m_extent[2]);
    addVector(0x9656F775u, get()->m_box[0]);
    addVector(0xC3CD20A2u, get()->m_box[1]);
    addVector(0x829FE7DDu, get()->m_box[2]);
    script->m_args->PutSubStruct(0xB91EE381u, result);
    for (Scripts::CArray* array : colours) {
        if (array) {
            array->Clear();
            Scripts::DeleteArray(array);
        }
    }
    if (result) {
        result->Clear();
        Scripts::DeleteStruct(result);
    }
    return true;
}

// (no addresses: the code folded at 0x004AAD40 / 0x004076F0 - particle_component.h)
const uint8_t* __fastcall ParticleSystemParams(void* particle) { return static_cast<const uint8_t*>(particle) + 4; }
void __fastcall SetParticleValue34(void* particle, float value)
{
    *reinterpret_cast<float*>(static_cast<uint8_t*>(particle) + 0x34) = value;
}

} // namespace Obj

using GameObjects::CEffectsEmitterModule;

namespace GameObjects {
namespace {

static VtableSlot kVtable_slots[] = {&CEffectsEmitterModule::Teardown,
         &CEffectsEmitterModule::TickFrame,
         &CEffectsEmitterModule::ConfigureFromData,
         &CEffectsEmitterModule::Seal,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         Stubs::IgnoreArguments2,
         &GameObjects::CObjectModule::HoldOff,
         &CEffectsEmitterModule::Conceal,
         Stubs::DoNothing,
         Stubs::IgnoreArgument,
         &GameObjects::CObjectModule::DispatchScriptMethod,
         &CEffectsEmitterModule::InvokeMethodByName};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);

} // namespace
} // namespace Obj
