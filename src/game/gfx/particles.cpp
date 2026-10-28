// The particle component's particle systems and their manager (Nx): see particles.h. From the disassembly of
// 0x00495260-0x004968AF.

#include "game/gfx/particles.h"

#include <cmath>
#include <cstring>
#include <new>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

void** const kParticleVtable = reinterpret_cast<void**>(0x00649380);
void** const kManagerVtable = reinterpret_cast<void**>(0x006493A0);
constexpr float kByteToUnit = 1.0f / 255.0f;   // 0x00645D70 (the bit pattern 0x3B808081)
constexpr uint32_t kObjectIdParam = 0x40C698AF; // (the emit script's parameter: the object's id)

bool GamePaused() { return GameVariable<bool>(0x006FA7D4); }

// The system's virtual functions.
CParticle* Delete(CParticle* particle, unsigned int flags)
{
    return reinterpret_cast<CParticle*(__thiscall*)(CParticle*, unsigned int)>(particle->vtable[0])(particle, flags);
}
void Initialise(CParticle* particle) { reinterpret_cast<void(__thiscall*)(CParticle*)>(particle->vtable[1])(particle); }
void ColourChanged(CParticle* particle)
{
    reinterpret_cast<void(__thiscall*)(CParticle*)>(particle->vtable[6])(particle);
}

Script::CStruct* NewStructure()
{
    Script::CStruct* structure = Script::NewStruct();
    return structure ? structure->Init() : nullptr;
}

// The engine's helpers (not yet rewritten; register arguments).
// 0x00495130 / 0x004951B0 (ECX a, EDX b, ESI out; returns out): each component b's where it's below / above a's.
Mth::Vector* CallMinOrMax(uint32_t code, const Mth::Vector* a, const Mth::Vector* b, Mth::Vector* destination)
{
    Mth::Vector* result;
    __asm {
        mov ecx, a
        mov edx, b
        mov esi, destination
        mov eax, code
        call eax
        mov result, eax
    }
    return result;
}
Mth::Vector* MinOf(const Mth::Vector* a, const Mth::Vector* b, Mth::Vector* out)
{
    return CallMinOrMax(0x00495130, a, b, out);
}
Mth::Vector* MaxOf(const Mth::Vector* a, const Mth::Vector* b, Mth::Vector* out)
{
    return CallMinOrMax(0x004951B0, a, b, out);
}
// 0x00494F10 (thiscall on `out`, the 14 words of arguments popped by the caller): per component, the extreme of the
// quadratic through `start` (at 0), `mid` (at `midTime`) and `end` (at `life`); w 0. Returns out.
Mth::Vector* CurveExtremes(Mth::Vector* destination, const Mth::Vector& start, const Mth::Vector& mid, const Mth::Vector& end,
                           float midTime, float life)
{
    uint32_t args[14];
    std::memcpy(args, &start, 16);
    std::memcpy(args + 4, &mid, 16);
    std::memcpy(args + 8, &end, 16);
    std::memcpy(args + 12, &midTime, 4);
    std::memcpy(args + 13, &life, 4);
    uint32_t* words = args;
    Mth::Vector* result;
    __asm {
        sub esp, 0x38
        mov esi, words
        mov edi, esp
        mov ecx, 14
        rep movsd
        mov ecx, destination
        mov eax, 0x00494F10
        call eax
        add esp, 0x38
        mov result, eax
    }
    return result;
}

float Round(double value) { return static_cast<float>(value); }

} // namespace

// --- the parameters ---

// 0x00496240
CParticleParams* CParticleParams::Construct()
{
    m_unknown04 = 0xDEDFC057;
    m_flag151 = true;
    m_localPositions = false;
    m_orientWithObject = false;
    for (int i = 0; i < 3; ++i) {
        m_size[i] = 1.0f;
        m_radius[i] = 0.0f;
        m_pos[i] = Mth::Vector{0.0f, 0.0f, 0.0f, 0.0f};
        m_box[i] = Mth::Vector{20.0f, 20.0f, 20.0f, 0.0f};
        m_localPos[i] = Mth::Vector{0.0f, 0.0f, 0.0f, 0.0f};
        for (int k = 0; k < 4; ++k)
            m_colour[i][k] = 0x80;
    }
    m_flag153 = false;
    m_unknown140 = 0;
    m_unknown138 = 0;
    m_unknown30 = 50.0f;
    m_unknown24 = 2;
    m_unknown28 = 300.0f;
    m_unknown2C = 4.0f;
    m_unknown34 = 1.0f;
    m_unknown124 = 50.0f;
    m_unknown134 = 0xA86285A1;
    m_unknown13C = 1;
    for (float& scale : m_colourScale)
        scale = 1.0f;
    m_unknown14C = 0;
    unknown155[0] = 0;
    unknown155[1] = 0;
    m_flag158 = false;
    m_objectId = 0;
    m_unknown144 = 400;
    m_unknown148 = 401;
    return this;
}

// 0x00495C80
CParticleParams* CParticleParams::Assign(const CParticleParams& other)
{
    std::memcpy(this, &other, offsetof(CParticleParams, unknown159));
    return this;
}

// --- the system ---

// 0x00496360
CParticle* CParticle::Construct()
{
    vtable = kParticleVtable;
    m_params.Construct();
    new (&m_bbox) Mth::BBox();
    return this;
}

// 0x00496380
void CParticle::Init(const CParticleParams* params)
{
    m_params.Assign(*params);
    UpdateBounds();
    Initialise(this);
}

// 0x00495260. The corners are (position, size) less / plus (half the box, half the radius), the size and half the
// radius; the original's x87 code rounds some of the sums to floats and keeps others at 53 bits - followed term by term.
void CParticle::UpdateBounds()
{
    const CParticleParams& p = m_params;
    float hb[3][3], hr[3], s[3];
    for (int i = 0; i < 3; ++i) {
        hb[i][0] = p.m_box[i].x * 0.5f;
        hb[i][1] = p.m_box[i].y * 0.5f;
        hb[i][2] = p.m_box[i].z * 0.5f;
        hr[i] = p.m_radius[i] * 0.5f;
        s[i] = p.m_size[i];
    }
    Mth::Vector lo[3], hi[3];
    for (int i = 0; i < 3; ++i) {
        const Mth::Vector& at = p.m_pos[i];
        lo[i].x = Round(Round(Round(static_cast<double>(at.x) - hb[i][0]) - s[i]) - hr[i]);
        lo[i].y = Round(Round(Round(static_cast<double>(at.y) - hb[i][1]) - s[i]) - hr[i]);
        lo[i].z = Round(Round(Round(static_cast<double>(at.z) - hb[i][2]) - s[i]) - hr[i]);
        lo[i].w = Round(Round(Round(static_cast<double>(s[i]) - hr[i]) - s[i]) - hr[i]);
    }
    {
        const Mth::Vector& at = p.m_pos[0];
        hi[0].x = Round((static_cast<double>(Round(static_cast<double>(hb[0][0]) + at.x)) + s[0]) + hr[0]);
        hi[0].y = Round(((static_cast<double>(at.y) + hb[0][1]) + s[0]) + hr[0]);
        hi[0].z = Round(static_cast<double>(Round(s[0] + static_cast<double>(Round(static_cast<double>(at.z) +
                                                                                    hb[0][2])))) + hr[0]);
        hi[0].w = Round(static_cast<double>(Round(s[0] + static_cast<double>(Round(static_cast<double>(hr[0]) +
                                                                                    s[0])))) + hr[0]);
    }
    for (int i = 1; i < 3; ++i) {
        const Mth::Vector& at = p.m_pos[i];
        hi[i].x = Round(Round(Round(static_cast<double>(hb[i][0]) + at.x) + s[i]) + hr[i]);
        hi[i].y = Round(Round(Round(static_cast<double>(at.y) + hb[i][1]) + s[i]) + hr[i]);
        hi[i].z = Round((s[i] + static_cast<double>(Round(static_cast<double>(at.z) + hb[i][2]))) + hr[i]);
        hi[i].w = Round(static_cast<double>(Round(s[i] + (static_cast<double>(hr[i]) + s[i]))) + hr[i]);
    }
    Mth::Vector low, high, curve;
    low = *MinOf(&lo[0], &lo[2], &curve);
    high = *MaxOf(&hi[0], &hi[2], &curve);
    if (p.m_flag151) { // (the paths go through the mid: their extremes on the way)
        float life = p.m_unknown2C;
        float midTime = Round(static_cast<double>(life) * p.m_unknown30 * 0.01f);
        Mth::Vector extreme;
        CurveExtremes(&extreme, lo[0], lo[1], lo[2], midTime, life);
        low = *MinOf(&low, &extreme, &curve);
        CurveExtremes(&extreme, hi[0], hi[1], hi[2], midTime, life);
        high = *MaxOf(&high, &extreme, &curve);
    }
    m_bbox.min = low;
    m_bbox.max = high;
    // the sphere about the box
    float halfX = Round((static_cast<double>(high.x) - low.x) * 0.5f);
    double halfY = (static_cast<double>(high.y) - low.y) * 0.5f;
    double halfZ = static_cast<double>(Round(static_cast<double>(high.z) - low.z)) * 0.5f;
    m_sphere.w = low.w; // (overwritten below, as the original)
    m_sphere.z = Round(halfZ + low.z);
    m_sphere.x = Round(static_cast<double>(halfX) + low.x);
    m_sphere.y = Round(halfY + low.y);
    m_sphere.w = Round(std::sqrt((static_cast<double>(halfX) * halfX + halfZ * halfZ) + halfY * halfY));
}

// 0x00496030
void CParticle::SetPosition(int which, const Mth::Vector* pos) { m_params.m_pos[which] = *pos; }

// 0x00496160
void CParticle::SetBox(int which, const Mth::Vector* box) { m_params.m_box[which] = *box; }

// 0x00496060
void CParticle::Rotate(int which, const Mth::Matrix* matrix)
{
    const Mth::Vector start = m_params.m_pos[0];
    Mth::Vector& pos = m_params.m_pos[which];
    pos.x -= start.x;
    pos.y -= start.y;
    pos.z -= start.z;
    pos.w -= start.w;
    Mth::Vector turned;
    pos = matrix->Transform(turned, pos);
    Mth::Vector& box = m_params.m_box[which];
    box = matrix->Transform(turned, box);
    pos.x = start.x + pos.x;
    pos.y = start.y + pos.y;
    pos.z = start.z + pos.z;
    pos.w = start.w + pos.w;
    m_params.m_rotation = *matrix;
}

// 0x00496190
void CParticle::SetColour(const uint8_t* rgba)
{
    if (!rgba) {
        ColourChanged(this);
        return;
    }
    if (m_params.m_flag158) {
        for (int i = 0; i < 4; ++i)
            m_params.m_colourScale[i] = static_cast<float>(rgba[i]) * kByteToUnit;
        ColourChanged(this);
    }
}

// 0x00496210
void CParticle::SetColourScale(const float* rgba)
{
    if (m_params.m_flag158) {
        for (int i = 0; i < 4; ++i)
            m_params.m_colourScale[i] = rgba[i];
        ColourChanged(this);
    }
}

// 0x004963B0
void CParticle::SetEmitScript(uint32_t script, uint32_t objectId)
{
    if (m_params.m_emitScript)
        Script::KillSpawnedScriptsWithId(m_params.m_emitScript);
    m_params.m_emitScript = script;
    m_params.m_objectId = objectId;
    Script::CStruct* params = NewStructure();
    params->AddChecksum(kObjectIdParam, m_params.m_objectId); // (not checked for null, as the original)
    Script::SpawnScript(m_params.m_emitScript, params, 0, nullptr, 0, 0, false, false, false, false);
    if (params) {
        params->Clear();
        Script::DeleteStruct(params);
    }
}

void CParticle::Render(bool advance) { GameFunction<void(__thiscall*)(CParticle*, bool)>(0x00494E00)(this, advance); }
void CParticle::Update() { GameFunction<void(__thiscall*)(CParticle*)>(0x00494E20)(this); }
void CParticle::Release() { GameFunction<void(__thiscall*)(CParticle*)>(0x00494E40)(this); }

// --- the manager ---

// 0x00496770
CParticleManager* CParticleManager::Construct()
{
    vtable = kManagerVtable;
    m_systems.size = 0;
    auto* head = static_cast<Obj::PointerListNode*>(Mem::New(sizeof(Obj::PointerListNode)));
    if (head) {
        head->next = head;
        head->prev = head;
    }
    m_systems.head = head;
    for (float& c : m_colour)
        c = 1.0f;
    return this;
}

// 0x004967D0
void CParticleManager::DestroyBody()
{
    vtable = kManagerVtable;
    Obj::PointerListNode* result;
    for (int pass = 0; pass < 2; ++pass) {
        Obj::PointerListNode* head = m_systems.head;
        m_systems.Erase(&result, head ? head->next : nullptr, head);
    }
    Mem::Free(m_systems.head);
}

// 0x00496860
CParticleManager* CParticleManager::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x00496510
CParticle* CParticleManager::CreateParticle(const CParticleParams* params)
{
    CParticle* particle = reinterpret_cast<CParticle*(__thiscall*)(CParticleManager*)>(vtable[1])(this);
    if (particle) {
        particle->Init(params);
        if (particle->m_params.m_flag158) {
            for (int i = 0; i < 4; ++i)
                particle->m_params.m_colourScale[i] = m_colour[i];
            particle->SetColourScale(m_colour);
        }
        m_systems.Insert(m_systems.head, reinterpret_cast<void* const*>(&particle));
    }
    return particle;
}

// 0x00496590
void CParticleManager::DestroyParticle(CParticle* particle)
{
    if (particle->m_params.m_emitScript) {
        Script::KillSpawnedScriptsWithId(particle->m_params.m_emitScript);
        particle->m_params.m_emitScript = 0;
    }
    Obj::PointerListNode* node = m_systems.head ? m_systems.head->next : nullptr;
    if (node == m_systems.head)
        return;
    while (static_cast<CParticle*>(node->data)->m_params.m_name != particle->m_params.m_name) {
        node = node->next;
        if (node == m_systems.head)
            return;
    }
    auto* found = static_cast<CParticle*>(node->data);
    found->Release();
    if (found)
        Delete(found, 1);
    if (node != m_systems.head) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        Mem::Free(node);
        --m_systems.size;
    }
}

// 0x00496640
void CParticleManager::RenderParticles()
{
    if (!m_systems.size)
        return;
    for (Obj::PointerListNode* node = m_systems.head ? m_systems.head->next : nullptr; node != m_systems.head;
         node = node->next)
        static_cast<CParticle*>(node->data)->Render(!GamePaused());
}

// 0x00496680
void CParticleManager::UpdateParticles()
{
    for (Obj::PointerListNode* node = m_systems.head ? m_systems.head->next : nullptr; node != m_systems.head;
         node = node->next)
        static_cast<CParticle*>(node->data)->Update();
}

// 0x004966B0
void CParticleManager::SetColour(const float* rgba)
{
    for (Obj::PointerListNode* node = m_systems.head ? m_systems.head->next : nullptr; node != m_systems.head;
         node = node->next)
        static_cast<CParticle*>(node->data)->SetColourScale(rgba);
    for (int i = 0; i < 4; ++i)
        m_colour[i] = rgba[i];
}

// 0x00496700
void CParticleManager::DestroyAllParticles()
{
    for (Obj::PointerListNode* node = m_systems.head ? m_systems.head->next : nullptr; node != m_systems.head;
         node = node->next) {
        auto* particle = static_cast<CParticle*>(node->data);
        particle->Release();
        if (particle)
            Delete(particle, 1);
    }
    Obj::PointerListNode* result;
    Obj::PointerListNode* head = m_systems.head;
    m_systems.Erase(&result, head ? head->next : nullptr, head);
}

} // namespace Nx

namespace Obj {

// 0x004964A0
void PointerList::Insert(PointerListNode* position, void* const* value)
{
    auto* node = static_cast<PointerListNode*>(Mem::New(sizeof(PointerListNode)));
    if (node) {
        node->data = *value;
        node->prev = position->prev;
        node->next = position;
    }
    ++size;
    position->prev = node;
    node->prev->next = node; // (with no node, the original reads through null here)
}

} // namespace Obj

REPLACE(0x00495260, &Nx::CParticle::UpdateBounds);
REPLACE(0x00496240, &Nx::CParticleParams::Construct);
REPLACE(0x00495C80, &Nx::CParticleParams::Assign);
REPLACE(0x00496360, &Nx::CParticle::Construct);
REPLACE(0x00496380, &Nx::CParticle::Init);
REPLACE(0x00496030, &Nx::CParticle::SetPosition);
REPLACE(0x00496160, &Nx::CParticle::SetBox);
REPLACE(0x00496060, &Nx::CParticle::Rotate);
REPLACE(0x00496190, &Nx::CParticle::SetColour);
REPLACE(0x00496210, &Nx::CParticle::SetColourScale);
REPLACE(0x004963B0, &Nx::CParticle::SetEmitScript);
REPLACE(0x00496770, &Nx::CParticleManager::Construct);
REPLACE(0x004967D0, &Nx::CParticleManager::DestroyBody);
REPLACE(0x00496860, &Nx::CParticleManager::Destroy);
REPLACE(0x00496510, &Nx::CParticleManager::CreateParticle);
REPLACE(0x00496590, &Nx::CParticleManager::DestroyParticle);
REPLACE(0x00496640, &Nx::CParticleManager::RenderParticles);
REPLACE(0x00496680, &Nx::CParticleManager::UpdateParticles);
REPLACE(0x004966B0, &Nx::CParticleManager::SetColour);
REPLACE(0x00496700, &Nx::CParticleManager::DestroyAllParticles);
REPLACE(0x004964A0, &Obj::PointerList::Insert);
