// The particle component (Obj::CParticleComponent, particle_component.h): its life and its script member functions.
// The Nx particle systems themselves have no source yet: their methods are called as the game's.

#include "game/obj/particle_component.h"

#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/gfx/particles.h"
#include "game/mth/matrix.h"
#include "game/mth/vector.h"
#include "game/net/gamenet_manager.h"
#include "game/nx/feeler.h"
#include "game/obj/suspend_component.h"
#include "game/script/cscript.h"
#include "game/script/names.h"
#include "game/script/struct.h"
#include "game/sys/game_clock.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Obj {

namespace {

void** const kVtable = reinterpret_cast<void**>(0x00646340);
constexpr uint32_t kSuspend = 0xCE0CA665; // the suspend component

// The particle system's methods (Nx, no source yet; thiscall).
template <typename... A>
void Particle(uint32_t address, void* particle, A... args)
{
    GameFunction<void(__thiscall*)(void*, A...)>(address)(particle, args...);
}
void Refresh(void* particle) { Particle(0x00494E20, particle); }
// The unnamed float (required). The original reads it into its own `params` argument's slot: not given, the
// value is the pointer's bits.
float UnnamedFloat(Script::CStruct* params)
{
    float value;
    std::memcpy(&value, &params, 4);
    params->GetFloat(0u, &value, 1);
    return value;
}

// Its parameters (particle_component.h).
const uint8_t* Params(void* particle) { return ParticleSystemParams(particle); }
// 0x00494EE0: its +0x38 set (the model's +0x22C, or the ground test's value), then its vtable slot 6.
void SetGroundValue(void* particle, float value) { Particle(0x00494EE0, particle, value); }

// The parameters a particle system is made from (Nx::CParticleParams, gfx/particles.h; constructed by 0x00496240,
// given to the particle manager's 0x00496510, kept as the system's +4).
using ParticleParams = Nx::CParticleParams;

// D3DXMatrixMultiply (the library's, 0x005BFA8C): out = a * b.
void MatrixMultiply(Mth::Matrix* out, const Mth::Matrix* a, const Mth::Matrix* b)
{
    GameFunction<void*(__stdcall*)(Mth::Matrix*, const Mth::Matrix*, const Mth::Matrix*)>(0x005BFA8C)(out, a, b);
}

// An array's first three integers' low bytes into `rgb`; `last` the third.
void GetRGB(Script::CArray* array, uint8_t* rgb, uint32_t* last)
{
    for (uint32_t i = 0; i < 3; ++i) {
        *last = array->GetElement(i);
        rgb[i] = static_cast<uint8_t>(*last);
    }
}

constexpr uint32_t kEmitting = 0x2299A9F2;   // (its name unknown): the particle system's vtable slot 7
constexpr uint32_t kGroundTest = 0x96896315; // (its name unknown)

// The unnamed array's first four integers' low bytes (r, g, b, a).
struct Colour {
    uint8_t rgba[4];
};
Colour ColourOf(Script::CArray* array)
{
    Colour colour;
    for (uint32_t i = 0; i < 4; ++i)
        colour.rgba[i] = static_cast<uint8_t>(array->GetElement(i));
    return colour;
}

} // namespace

// --- life ---

// 0x0042A000
CBaseComponent* __cdecl CParticleComponent::Create()
{
    void* memory = Mem::Calloc(sizeof(CParticleComponent));
    return memory ? static_cast<CParticleComponent*>(memory)->Construct() : nullptr;
}

// 0x00429940
void __cdecl CParticleComponent::RegisterMemberFunctions()
{
    Script::RegisterMemberFunctionTable(reinterpret_cast<void*>(kType), &GameVariable<uint32_t>(0x00683D30), 0x18);
}

// 0x00429B20
CParticleComponent* CParticleComponent::Construct()
{
    CBaseComponent::Construct();
    m_vtable = kVtable;
    m_type = kType;
    m_emitTarget = 0;
    mp_particle = nullptr;
    mp_model = nullptr;
    m_lifetime = 0;
    m_startTime = Tmr::GameClockElapsed() + GameVariable<int>(0x00696D60);
    return this;
}

// 0x00429B90
void CParticleComponent::DestroyBody()
{
    m_vtable = kVtable;
    if (mp_particle)
        GameFunction<void(__thiscall*)(void*, void*)>(0x00496590)(GameVariable<void*>(0x006FA7C4), mp_particle);
    CBaseComponent::DestroyBody();
}

// 0x0042A060
CParticleComponent* CParticleComponent::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Manager::Get()->Free(this);
    return this;
}

// 0x00429BF0
void CParticleComponent::Finalize() { mp_suspend = m_object->GetComponent(kSuspend); }

// 0x00429960
void CParticleComponent::Hide(bool hide) { Particle(0x00494E70, mp_particle, hide); }

// --- script member functions ---

// 0x00429970
bool CParticleComponent::SetEmitRate(Script::CStruct* params, Script::CScript*)
{
    float value = UnnamedFloat(params);
    if (mp_particle)
        Particle(0x00494E90, mp_particle, value);
    return true;
}

// 0x004299A0
bool CParticleComponent::SetLifetime(Script::CStruct* params, Script::CScript*)
{
    float value = UnnamedFloat(params);
    if (mp_particle) {
        Particle(0x00494EA0, mp_particle, value);
        Refresh(mp_particle);
    }
    return true;
}

// 0x004299E0
bool CParticleComponent::SetFloat19AF74D1(Script::CStruct* params, Script::CScript*)
{
    float value = UnnamedFloat(params);
    if (mp_particle) {
        Particle(0x00494EF0, mp_particle, value);
        Refresh(mp_particle);
    }
    return true;
}

// 0x00429A20
bool CParticleComponent::SetMidPointPct(Script::CStruct* params, Script::CScript*)
{
    float value = UnnamedFloat(params);
    if (mp_particle) {
        SetParticleValue34(mp_particle, value);
        Refresh(mp_particle);
    }
    return true;
}

namespace {
bool SetRadius(CParticleComponent* component, Script::CStruct* params, int which)
{
    float value = UnnamedFloat(params);
    if (component->mp_particle) {
        Particle(0x00494EB0, component->mp_particle, which, value);
        Refresh(component->mp_particle);
    }
    return true;
}
bool SetPos(CParticleComponent* component, Script::CStruct* params, int which)
{
    Mth::Vector pos;
    params->GetVector(0u, reinterpret_cast<Script::VectorValue*>(&pos), 1);
    if (component->mp_particle) {
        Particle(0x00496030, component->mp_particle, which, &pos);
        Refresh(component->mp_particle);
    }
    return true;
}
bool SetBox(CParticleComponent* component, Script::CStruct* params, int which)
{
    Mth::Vector box;
    params->GetVector(0u, reinterpret_cast<Script::VectorValue*>(&box), 1);
    if (component->mp_particle)
        Particle(0x00496160, component->mp_particle, which, &box);
    return true;
}
bool SetColour(CParticleComponent* component, Script::CStruct* params, int which)
{
    Script::CArray* array;
    params->GetArray(0u, &array, 1);
    if (component->mp_particle) {
        Colour colour = ColourOf(array);
        Particle(0x00494EC0, component->mp_particle, which, &colour);
    }
    return true;
}
} // namespace

bool CParticleComponent::SetStartRadius(Script::CStruct* params, Script::CScript*) { return SetRadius(this, params, 0); } // 0x00429A60
bool CParticleComponent::SetMidRadius(Script::CStruct* params, Script::CScript*) { return SetRadius(this, params, 1); }   // 0x00429AA0
bool CParticleComponent::SetEndRadius(Script::CStruct* params, Script::CScript*) { return SetRadius(this, params, 2); }   // 0x00429AE0
bool CParticleComponent::SetStartPos(Script::CStruct* params, Script::CScript*) { return SetPos(this, params, 0); }       // 0x00429C10
bool CParticleComponent::SetMidPos(Script::CStruct* params, Script::CScript*) { return SetPos(this, params, 1); }         // 0x00429C50
bool CParticleComponent::SetEndPos(Script::CStruct* params, Script::CScript*) { return SetPos(this, params, 2); }         // 0x00429C90
bool CParticleComponent::SetStartBox(Script::CStruct* params, Script::CScript*) { return SetBox(this, params, 0); }       // 0x00429CD0
bool CParticleComponent::SetMidBox(Script::CStruct* params, Script::CScript*) { return SetBox(this, params, 1); }         // 0x00429D10
bool CParticleComponent::SetEndBox(Script::CStruct* params, Script::CScript*) { return SetBox(this, params, 2); }         // 0x00429D50
bool CParticleComponent::SetStartColor(Script::CStruct* params, Script::CScript*) { return SetColour(this, params, 0); } // 0x00429DD0
bool CParticleComponent::SetMidColor(Script::CStruct* params, Script::CScript*) { return SetColour(this, params, 1); }   // 0x00429E50
bool CParticleComponent::SetEndColor(Script::CStruct* params, Script::CScript*) { return SetColour(this, params, 2); }   // 0x00429ED0

// 0x00429D90
bool CParticleComponent::SetEmitTarget(Script::CStruct* params, Script::CScript*)
{
    params->GetChecksum(0u, &m_emitTarget, 1);
    if (mp_particle)
        Particle(0x004963B0, mp_particle, m_emitTarget, m_object->m_id);
    return true;
}

// 0x00429F50
bool CParticleComponent::SetFlag5446AF14(Script::CStruct* params, Script::CScript*)
{
    int value = 0;
    params->GetInteger(0u, &value, 0);
    if (mp_particle)
        static_cast<uint8_t*>(mp_particle)[0x15B] = value == 1;
    return true;
}

// 0x00429F90
bool CParticleComponent::SetColor55835CF2(Script::CStruct* params, Script::CScript*)
{
    Script::CArray* array;
    params->GetArray(0u, &array, 1);
    if (mp_particle) {
        Colour colour = ColourOf(array);
        Particle(0x00496190, mp_particle, &colour);
    }
    return true;
}

// 0x0042A090
void CParticleComponent::InitFromStructure(Script::CStruct* params)
{
    ParticleParams p;
    GameFunction<void(__thiscall*)(ParticleParams*)>(0x00496240)(&p);
    float scale = 1.0f;
    p.m_emitScript = 0;
    if (params->GetChecksum(0x092276DBu, &m_emitTarget, 0)) {
        p.m_emitScript = m_emitTarget;
        p.m_objectId = m_object->m_id;
    }
    params->GetInteger(0x5A235299u, reinterpret_cast<int*>(&m_lifetime), 0);
    params->GetChecksum(0xA1DC81F9u, &p.m_name, 1);
    params->GetChecksum("GroupID", &p.m_groupId, 0);
    params->GetChecksum(0x7321A8D6u, &p.m_unknown04, 0);
    p.m_flag151 = params->ContainsFlag(0xA7A5BFFBu);
    p.m_flag157 = params->ContainsFlag(0x5446AF14u);
    params->GetFloat(0x13B9DA7Bu, &scale, 0);
    params->GetFloat(0x3BEE67C8u, &p.m_size[0], 0);
    params->GetFloat(0x8E39913Du, &p.m_size[1], 0);
    params->GetFloat(0x3F243A3Cu, &p.m_size[2], 0);

    // The orientation: the identity, or turned to face 0x0C4C809E (normalised).
    Mth::Matrix orient;
    orient.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    orient.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    orient.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    orient.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (params->ContainsFlag(0x7045843Au)) {
        Mth::Vector at;
        params->GetVector(0x0C4C809Eu, reinterpret_cast<Script::VectorValue*>(&at), 1);
        at.Normalize();
        orient.row[2] = at;
        const Mth::Vector up = {0.0f, 1.0f, 0.0f, 1.0f};
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
        const Mth::Vector& r = orient.row[0];
        orient.row[1].w = 0.0f;
        orient.row[1].x = static_cast<float>(static_cast<double>(at.y) * r.z - static_cast<double>(at.z) * r.y);
        orient.row[1].y = static_cast<float>(static_cast<double>(at.z) * r.x - static_cast<double>(at.x) * r.z);
        orient.row[1].z = static_cast<float>(static_cast<double>(at.x) * r.y - static_cast<double>(at.y) * r.x);
    }
    for (float& size : p.m_size)
        size = static_cast<float>(static_cast<double>(size) * scale);
    params->GetFloat(0x05400A25u, &p.m_radius[0], 0);
    params->GetFloat(0xFF7D9AA9u, &p.m_radius[1], 0);
    params->GetFloat(0x5539FCEEu, &p.m_radius[2], 0);
    for (float& radius : p.m_radius)
        radius = static_cast<float>(static_cast<double>(radius) * scale);
    params->GetInteger(0x9E52A20Fu, &p.m_unknown24, 0);
    params->GetFloat(0x35B65BD4u, &p.m_unknown28, 0);
    params->GetFloat(0xC218CF77u, &p.m_unknown2C, 0);
    params->GetFloat(0x520682C6u, &p.m_unknown30, 0);

    // The positions and boxes: scaled, then turned by the orientation.
    auto scaleAndTurn = [&](Mth::Vector* v) {
        for (int i = 0; i < 3; ++i) {
            v[i] = {static_cast<float>(static_cast<double>(v[i].x) * scale),
                    static_cast<float>(static_cast<double>(v[i].y) * scale),
                    static_cast<float>(static_cast<double>(v[i].z) * scale),
                    static_cast<float>(static_cast<double>(v[i].w) * scale)};
        }
        for (int i = 0; i < 3; ++i) {
            Mth::Vector turned;
            v[i] = orient.Transform(turned, v[i]);
        }
    };
    auto vector = [&](uint32_t name, Mth::Vector* out) {
        params->GetVector(name, reinterpret_cast<Script::VectorValue*>(out), 0);
    };
    vector(params->ContainsFlag(0x64F194EDu) ? 0x3D5EA6A4u : 0x7F261953u, &p.m_pos[0]);
    vector(0x004B51A2u, &p.m_pos[1]);
    vector(0x5854AB9Eu, &p.m_pos[2]);
    scaleAndTurn(p.m_pos);
    vector(0x9656F775u, &p.m_box[0]);
    vector(0xC3CD20A2u, &p.m_box[1]);
    vector(0x829FE7DDu, &p.m_box[2]);
    scaleAndTurn(p.m_box);
    p.m_flag153 = params->ContainsFlag(0x454962EFu);
    params->GetFloat(0x59463C93u, &p.m_unknown30, 0);

    // The colours (rgb from three arrays; the alphas at most 128).
    uint32_t value = 0;
    Script::CArray* array;
    static const uint32_t kColours[3] = {0xDAB75B6Cu, 0x0444769Au, 0x5A3728E7u};
    for (int i = 0; i < 3; ++i)
        if (params->GetArray(kColours[i], &array, 0))
            GetRGB(array, p.m_colour[i], &value);
    static const uint32_t kAlphas[3] = {0x4D83DB4Cu, 0x4ABA1FF1u, 0x5CF83ACAu};
    for (int i = 0; i < 3; ++i) {
        if (params->GetInteger(kAlphas[i], reinterpret_cast<int*>(&value), 0)) {
            uint8_t alpha = static_cast<uint8_t>(value);
            p.m_colour[i][3] = alpha > 0x80 ? 0x80 : alpha;
        }
    }
    params->GetChecksum(0xDD6BB3D5u, &p.m_unknown134, 0);
    params->GetInteger(0x9F677D4Eu, &p.m_unknown13C, 0);
    params->GetInteger(0x13D0BE1Du, &p.m_unknown138, 0);
    params->GetChecksum(0x7D99F28Du, &p.m_unknown140, 0);
    params->GetInteger(0x4EEDFAE7u, &p.m_unknown144, 0);
    params->GetInteger(0xD7E4AB5Du, &p.m_unknown148, 0);
    params->GetInteger(0xE4279BA2u, &p.m_unknown14C, 0);
    int byteValue = 0;
    params->GetInteger(0x9D5923D8u, &byteValue, 0);
    p.m_unknown150 = static_cast<uint8_t>(byteValue);

    // The rotation: X, Y, then Z.
    p.m_rotation.row[0] = {1.0f, 0.0f, 0.0f, 0.0f};
    p.m_rotation.row[1] = {0.0f, 1.0f, 0.0f, 0.0f};
    p.m_rotation.row[2] = {0.0f, 0.0f, 1.0f, 0.0f};
    p.m_rotation.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
    Mth::Vector angles = {0.0f, 0.0f, 0.0f, 1.0f};
    params->GetVector(0x9D2D0915u, reinterpret_cast<Script::VectorValue*>(&angles), 0);
    Mth::Matrix rotation;
    MatrixMultiply(&p.m_rotation, &p.m_rotation, &Mth::CreateRotateXMatrix(rotation, angles.x));
    MatrixMultiply(&p.m_rotation, &p.m_rotation, &Mth::CreateRotateYMatrix(rotation, angles.y));
    MatrixMultiply(&p.m_rotation, &p.m_rotation, &Mth::CreateRotateZMatrix(rotation, angles.z));

    p.m_localPositions = params->ContainsFlag(0xB7B7240Du);
    if (p.m_localPositions)
        for (int i = 0; i < 3; ++i)
            p.m_localPos[i] = p.m_pos[i];
    p.m_orientWithObject = params->ContainsFlag(0xA2487CC3u);
    p.m_flag158 = params->ContainsFlag(0xA0E585B0u);
    mp_particle = GameFunction<void*(__thiscall*)(void*, ParticleParams*)>(0x00496510)(
        GameVariable<void*>(0x006FA7C4), &p);

    // The model it follows (0x5E2A8D9F: an object's; no such object - a crash, as the original).
    uint32_t model = 0;
    if (params->GetChecksum(0x5E2A8D9Fu, &model, 0) && model)
        mp_model = static_cast<CCompositeObject*>(FindObject(model))->GetComponent(0x286A8D26);
    if (p.m_emitScript) {
        Script::CStruct* scriptParams = nullptr;
        params->GetStructure(0x7031F10Cu, &scriptParams, 0);
        m_object->SpawnAndRunScript(p.m_emitScript, 0, false, false, scriptParams);
    }
    uint32_t value54 = 0xE3714DC1;
    params->GetChecksum(0x7E4813A7u, &value54, 0);
    reinterpret_cast<uint32_t*>(mp_particle)[0x54 / 4] = value54;
    if (p.m_flag158)
        Particle(0x00496190, mp_particle, static_cast<void*>(nullptr));
}

// 0x0042AD90
void CParticleComponent::Update()
{
    // (suspended: nothing - unless a network game runs and it has a lifetime)
    if (mp_suspend && static_cast<CSuspendComponent*>(mp_suspend)->IsFar()) {
        void* net = GameVariable<void*>(0x006FA7D0);
        if (!static_cast<GameNet::Manager*>(net)->InNetGame() || m_lifetime == 0)
            return;
    }
    if (m_lifetime != 0) {
        uint32_t age = static_cast<uint32_t>(Tmr::GameClockElapsed() + (GameVariable<int>(0x00696D60) - m_startTime));
        if (age > m_lifetime) {
            void** vtable = *reinterpret_cast<void***>(m_object);
            reinterpret_cast<void(__thiscall*)(CCompositeObject*)>(vtable[6])(m_object); // (MarkAsDead)
            return;
        }
    }
    if (mp_model)
        SetGroundValue(mp_particle, *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(mp_model) + 0x22C));
    if (!Params(mp_particle)[0x152])
        return;
    for (int i = 0; i < 3; ++i) {
        const float* local = reinterpret_cast<const float*>(Params(mp_particle) + 0xF4 + i * 0x10);
        const Mth::Vector& at = m_object->m_pos;
        Mth::Vector pos = {local[0] + at.x, local[1] + at.y, local[2] + at.z, local[3] + at.w};
        Particle(0x00496030, mp_particle, i, &pos);
        if (Params(mp_particle)[0x154]) {
            Mth::Matrix orientation;
            orientation = m_object->m_matrix;
            orientation.row[3] = {0.0f, 0.0f, 0.0f, 1.0f};
            Particle(0x00496060, mp_particle, i, &orientation);
        }
    }
}

// 0x0042AEE0
int CParticleComponent::CallMemberFunction(uint32_t name, Script::CStruct*, Script::CScript*)
{
    if (name == kEmitting) {
        void** vtable = *reinterpret_cast<void***>(mp_particle);
        return reinterpret_cast<bool(__thiscall*)(void*)>(vtable[7])(mp_particle) ? 1 : 0;
    }
    if (name == kGroundTest) {
        // A line from just above the particle system's position to 4800 below it: the face hit's vertex value,
        // doubled (1 without a hit).
        float value = 1.0f;
        Nx::CFeeler feeler;
        feeler.Construct();
        const float* at = reinterpret_cast<const float*>(Params(mp_particle) + 0x94);
        feeler.m_line.start = {at[0], static_cast<float>(at[1] + 1.0f), at[2], at[3]};
        feeler.m_line.end = {at[0], static_cast<float>(static_cast<double>(at[1]) - 4800.0), at[2], at[3]};
        if (feeler.GetCollision(true, false))
            value = static_cast<float>(feeler.GetHitVertexValue() * 2.0);
        if (mp_particle)
            SetGroundValue(mp_particle, value);
        return 1;
    }
    return 2;
}

// 0x0042B040
bool CParticleComponent::GetParticleParams(Script::CStruct*, Script::CScript* script)
{
    Script::CStruct* result = Script::NewStruct();
    if (result)
        result = result->Init();
    // (the name, two floats and the colours read straight from the system's +4; the rest through the getter, once
    // for each value)
    const ParticleParams* own = reinterpret_cast<const ParticleParams*>(static_cast<uint8_t*>(mp_particle) + 4);
    result->AddChecksum(0xA1DC81F9u, own->m_name);
    result->AddFloat(0x35B65BD4u, own->m_unknown28);
    result->AddFloat(0xC218CF77u, own->m_unknown2C);
    Script::CArray* colours[3];
    for (Script::CArray*& array : colours) {
        array = Script::NewArray();
        if (array)
            array = array->Init();
    }
    uint8_t colour[3][4];
    std::memcpy(colour, own->m_colour, sizeof(colour));
    for (Script::CArray* array : colours)
        array->SetSizeAndType(4, Script::ESYMBOLTYPE_INTEGER);
    for (int i = 0; i < 3; ++i)
        for (uint32_t j = 0; j < 4; ++j)
            colours[i]->SetElement(j, colour[i][j]);
    result->AddArray(0x7E6BCBE6u, colours[0]);
    result->AddArray(0x2A6B6FE2u, colours[1]);
    result->AddArray(0x0481113Du, colours[2]);
    auto get = [this] { return reinterpret_cast<const ParticleParams*>(Params(mp_particle)); };
    auto addVector = [&](uint32_t name, const Mth::Vector& v) {
        result->AddVector(name, Script::VectorValue{v.x, v.y, v.z, v.w});
    };
    addVector(0x7B5A25F8u, get()->m_pos[0]);
    addVector(0xA5A9080Eu, get()->m_pos[1]);
    addVector(0xFBDA5673u, get()->m_pos[2]);
    result->AddFloat(0x3BEE67C8u, get()->m_size[0]);
    result->AddFloat(0x8E39913Du, get()->m_size[1]);
    result->AddFloat(0x3F243A3Cu, get()->m_size[2]);
    addVector(0x9656F775u, get()->m_box[0]);
    addVector(0xC3CD20A2u, get()->m_box[1]);
    addVector(0x829FE7DDu, get()->m_box[2]);
    script->m_params->AddStructure(0xB91EE381u, result);
    for (Script::CArray* array : colours) {
        if (array) {
            array->Clear();
            Script::DeleteArray(array);
        }
    }
    if (result) {
        result->Clear();
        Script::DeleteStruct(result);
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

using Obj::CParticleComponent;

REPLACE(0x0042A000, CParticleComponent::Create);
REPLACE(0x00429940, CParticleComponent::RegisterMemberFunctions);
REPLACE(0x00429B20, &CParticleComponent::Construct);
REPLACE(0x00429B90, &CParticleComponent::DestroyBody);
REPLACE(0x0042A060, &CParticleComponent::Destroy);
REPLACE(0x00429BF0, &CParticleComponent::Finalize);
REPLACE(0x00429960, &CParticleComponent::Hide);
REPLACE(0x00429970, &CParticleComponent::SetEmitRate);
REPLACE(0x004299A0, &CParticleComponent::SetLifetime);
REPLACE(0x004299E0, &CParticleComponent::SetFloat19AF74D1);
REPLACE(0x00429A20, &CParticleComponent::SetMidPointPct);
REPLACE(0x00429A60, &CParticleComponent::SetStartRadius);
REPLACE(0x00429AA0, &CParticleComponent::SetMidRadius);
REPLACE(0x00429AE0, &CParticleComponent::SetEndRadius);
REPLACE(0x00429C10, &CParticleComponent::SetStartPos);
REPLACE(0x00429C50, &CParticleComponent::SetMidPos);
REPLACE(0x00429C90, &CParticleComponent::SetEndPos);
REPLACE(0x00429CD0, &CParticleComponent::SetStartBox);
REPLACE(0x00429D10, &CParticleComponent::SetMidBox);
REPLACE(0x00429D50, &CParticleComponent::SetEndBox);
REPLACE(0x00429D90, &CParticleComponent::SetEmitTarget);
REPLACE(0x00429DD0, &CParticleComponent::SetStartColor);
REPLACE(0x00429E50, &CParticleComponent::SetMidColor);
REPLACE(0x00429ED0, &CParticleComponent::SetEndColor);
REPLACE(0x00429F50, &CParticleComponent::SetFlag5446AF14);
REPLACE(0x00429F90, &CParticleComponent::SetColor55835CF2);
REPLACE(0x0042A090, &CParticleComponent::InitFromStructure);
REPLACE(0x0042AD90, &CParticleComponent::Update);
REPLACE(0x0042AEE0, &CParticleComponent::CallMemberFunction);
REPLACE(0x0042B040, &CParticleComponent::GetParticleParams);
