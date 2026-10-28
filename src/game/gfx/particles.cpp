// The particle component's particle systems and their manager (Nx): see particles.h. From the disassembly of
// 0x00495260-0x004968AF.

#include "game/gfx/particles.h"

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

// 0x00495260 (not yet rewritten here)
void CParticle::UpdateBounds() { GameFunction<void(__thiscall*)(CParticle*)>(0x00495260)(this); }

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
