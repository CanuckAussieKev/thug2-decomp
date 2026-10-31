// The particle component's particle systems and their manager (Nx): see particles.h. From the disassembly of
// 0x00495260-0x004968AF.

#include "game/gfx/particles.h"
#include "game/gfx/pc_particles.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/core/code_address.h"
#include "game/gfx/particle_maths.h"
#include "game/core/game_vtables.h"
#include "game/core/stubs.h"
#include "game/obj/movie_manager.h"

namespace Engine3D {

namespace {

void** const& kParticleVtable = Vt::kEngineParticles;
extern void** const kManagerVtable;
constexpr float kByteToUnit = 1.0f / 255.0f;   // 0x00645D70 (the bit pattern 0x3B808081)
constexpr uint32_t kObjectIdParam = 0x40C698AF; // (the emit script's parameter: the object's id)

bool IsGameHalted() { return GameObjects::g_gamePaused; }

// The system's virtual functions.
CSparkle* Delete(CSparkle* particle, unsigned int flags)
{
    return reinterpret_cast<CSparkle*(__thiscall*)(CSparkle*, unsigned int)>(particle->vtable[0])(particle, flags);
}
void Initialise(CSparkle* particle) { reinterpret_cast<void(__thiscall*)(CSparkle*)>(particle->vtable[1])(particle); }
void ColourChanged(CSparkle* particle)
{
    reinterpret_cast<void(__thiscall*)(CSparkle*)>(particle->vtable[6])(particle);
}

Scripts::CStruct* NewStructure()
{
    Scripts::CStruct* structure = Scripts::NewStruct();
    return structure ? structure->Init() : nullptr;
}

// The engine's helpers (not yet rewritten; register arguments).
// 0x00495130 / 0x004951B0 (ECX a, EDX b, ESI out; returns out): each component b's where it's below / above a's.
Maths::Vector* CallMinOrMax(uint32_t code, const Maths::Vector* a, const Maths::Vector* b, Maths::Vector* destination)
{
    Maths::Vector* result;
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
Maths::Vector* MinOf(const Maths::Vector* a, const Maths::Vector* b, Maths::Vector* out)
{
    return CallMinOrMax(reinterpret_cast<uint32_t>(CodeAddress(&MinVectorStub)), a, b, out);
}
Maths::Vector* MaxOf(const Maths::Vector* a, const Maths::Vector* b, Maths::Vector* out)
{
    return CallMinOrMax(reinterpret_cast<uint32_t>(CodeAddress(&MaxVectorStub)), a, b, out);
}
// 0x00494F10 (thiscall on `out`, the 14 words of arguments popped by the caller): per component, the extreme of the
// quadratic through `start` (at 0), `mid` (at `midTime`) and `end` (at `life`); w 0. Returns out.
Maths::Vector* CurveExtremes(Maths::Vector* destination, const Maths::Vector& start, const Maths::Vector& mid, const Maths::Vector& end,
                           float midTime, float life)
{
    uint32_t args[14];
    std::memcpy(args, &start, 16);
    std::memcpy(args + 4, &mid, 16);
    std::memcpy(args + 8, &end, 16);
    std::memcpy(args + 12, &midTime, 4);
    std::memcpy(args + 13, &life, 4);
    uint32_t* words = args;
    void* code = CodeAddress(&QuadraticLowestStub);
    Maths::Vector* result;
    __asm {
        sub esp, 0x38
        mov esi, words
        mov edi, esp
        mov ecx, 14
        rep movsd
        mov ecx, destination
        mov eax, code
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
CSparkleConfig* CSparkleConfig::Construct()
{
    m_unknown04 = 0xDEDFC057;
    m_flag151 = true;
    m_localPositions = false;
    m_orientWithObject = false;
    for (int i = 0; i < 3; ++i) {
        m_size[i] = 1.0f;
        m_radius[i] = 0.0f;
        m_pos[i] = Maths::Vector{0.0f, 0.0f, 0.0f, 0.0f};
        m_box[i] = Maths::Vector{20.0f, 20.0f, 20.0f, 0.0f};
        m_localPos[i] = Maths::Vector{0.0f, 0.0f, 0.0f, 0.0f};
        for (int k = 0; k < 4; ++k)
            m_shade[i][k] = 0x80;
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
    m_entityKey = 0;
    m_unknown144 = 400;
    m_unknown148 = 401;
    return this;
}

// 0x00495C80
CSparkleConfig* CSparkleConfig::Assign(const CSparkleConfig& other)
{
    std::memcpy(this, &other, offsetof(CSparkleConfig, unknown159));
    return this;
}

// --- the system ---

// 0x00496360
CSparkle* CSparkle::Construct()
{
    vtable = kParticleVtable;
    m_params.Construct();
    new (&m_extentBox) Maths::BBox();
    return this;
}

// 0x00496380
void CSparkle::Init(const CSparkleConfig* params)
{
    m_params.Assign(*params);
    UpdateBounds();
    Initialise(this);
}

// 0x00495260. The corners are (position, size) less / plus (half the box, half the radius), the size and half the
// radius; the original's x87 code rounds some of the sums to floats and keeps others at 53 bits - followed term by term.
void CSparkle::UpdateBounds()
{
    const CSparkleConfig& p = m_params;
    float hb[3][3], hr[3], s[3];
    for (int i = 0; i < 3; ++i) {
        hb[i][0] = p.m_box[i].x * 0.5f;
        hb[i][1] = p.m_box[i].y * 0.5f;
        hb[i][2] = p.m_box[i].z * 0.5f;
        hr[i] = p.m_radius[i] * 0.5f;
        s[i] = p.m_size[i];
    }
    Maths::Vector lo[3], hi[3];
    for (int i = 0; i < 3; ++i) {
        const Maths::Vector& at = p.m_pos[i];
        lo[i].x = Round(Round(Round(static_cast<double>(at.x) - hb[i][0]) - s[i]) - hr[i]);
        lo[i].y = Round(Round(Round(static_cast<double>(at.y) - hb[i][1]) - s[i]) - hr[i]);
        lo[i].z = Round(Round(Round(static_cast<double>(at.z) - hb[i][2]) - s[i]) - hr[i]);
        lo[i].w = Round(Round(Round(static_cast<double>(s[i]) - hr[i]) - s[i]) - hr[i]);
    }
    {
        const Maths::Vector& at = p.m_pos[0];
        hi[0].x = Round((static_cast<double>(Round(static_cast<double>(hb[0][0]) + at.x)) + s[0]) + hr[0]);
        hi[0].y = Round(((static_cast<double>(at.y) + hb[0][1]) + s[0]) + hr[0]);
        hi[0].z = Round(static_cast<double>(Round(s[0] + static_cast<double>(Round(static_cast<double>(at.z) +
                                                                                    hb[0][2])))) + hr[0]);
        hi[0].w = Round(static_cast<double>(Round(s[0] + static_cast<double>(Round(static_cast<double>(hr[0]) +
                                                                                    s[0])))) + hr[0]);
    }
    for (int i = 1; i < 3; ++i) {
        const Maths::Vector& at = p.m_pos[i];
        hi[i].x = Round(Round(Round(static_cast<double>(hb[i][0]) + at.x) + s[i]) + hr[i]);
        hi[i].y = Round(Round(Round(static_cast<double>(at.y) + hb[i][1]) + s[i]) + hr[i]);
        hi[i].z = Round((s[i] + static_cast<double>(Round(static_cast<double>(at.z) + hb[i][2]))) + hr[i]);
        hi[i].w = Round(static_cast<double>(Round(s[i] + (static_cast<double>(hr[i]) + s[i]))) + hr[i]);
    }
    Maths::Vector low, high, curve;
    low = *MinOf(&lo[0], &lo[2], &curve);
    high = *MaxOf(&hi[0], &hi[2], &curve);
    if (p.m_flag151) { // (the paths go through the mid: their extremes on the way)
        float life = p.m_unknown2C;
        float midTime = Round(static_cast<double>(life) * p.m_unknown30 * 0.01f);
        Maths::Vector extreme;
        CurveExtremes(&extreme, lo[0], lo[1], lo[2], midTime, life);
        low = *MinOf(&low, &extreme, &curve);
        CurveExtremes(&extreme, hi[0], hi[1], hi[2], midTime, life);
        high = *MaxOf(&high, &extreme, &curve);
    }
    m_extentBox.min = low;
    m_extentBox.max = high;
    // the sphere about the box
    float halfX = Round((static_cast<double>(high.x) - low.x) * 0.5f);
    double halfY = (static_cast<double>(high.y) - low.y) * 0.5f;
    double halfZ = static_cast<double>(Round(static_cast<double>(high.z) - low.z)) * 0.5f;
    m_boundBall.w = low.w; // (overwritten below, as the original)
    m_boundBall.z = Round(halfZ + low.z);
    m_boundBall.x = Round(static_cast<double>(halfX) + low.x);
    m_boundBall.y = Round(halfY + low.y);
    m_boundBall.w = Round(std::sqrt((static_cast<double>(halfX) * halfX + halfZ * halfZ) + halfY * halfY));
}

// 0x00496030
void CSparkle::SetPosition(int which, const Maths::Vector* pos) { m_params.m_pos[which] = *pos; }

// 0x00496160
void CSparkle::SetBox(int which, const Maths::Vector* box) { m_params.m_box[which] = *box; }

// 0x00496060
void CSparkle::Rotate(int which, const Maths::Matrix* matrix)
{
    const Maths::Vector start = m_params.m_pos[0];
    Maths::Vector& pos = m_params.m_pos[which];
    pos.x -= start.x;
    pos.y -= start.y;
    pos.z -= start.z;
    pos.w -= start.w;
    Maths::Vector turned;
    pos = matrix->Transform(turned, pos);
    Maths::Vector& box = m_params.m_box[which];
    box = matrix->Transform(turned, box);
    pos.x = start.x + pos.x;
    pos.y = start.y + pos.y;
    pos.z = start.z + pos.z;
    pos.w = start.w + pos.w;
    m_params.m_orientationTurn = *matrix;
}

// 0x00496190
void CSparkle::ChangeColour(const uint8_t* rgba)
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
void CSparkle::SetColourScale(const float* rgba)
{
    if (m_params.m_flag158) {
        for (int i = 0; i < 4; ++i)
            m_params.m_colourScale[i] = rgba[i];
        ColourChanged(this);
    }
}

// 0x004963B0
void CSparkle::SetEmitScript(uint32_t script, uint32_t objectId)
{
    if (m_params.m_emitScript)
        Scripts::StopScriptsByTag(m_params.m_emitScript);
    m_params.m_emitScript = script;
    m_params.m_entityKey = objectId;
    Scripts::CStruct* params = NewStructure();
    params->PutNameKey(kObjectIdParam, m_params.m_entityKey); // (not checked for null, as the original)
    Scripts::SpawnScript(m_params.m_emitScript, params, 0, nullptr, 0, 0, false, false, false, false);
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

void CSparkle::Render(bool advance) { reinterpret_cast<CPcParticleSystem*>(this)->EngineRender(advance); }
void CSparkle::Update() { reinterpret_cast<CPcParticleSystem*>(this)->EngineUpdate(); }
void CSparkle::Release() { reinterpret_cast<CPcParticleSystem*>(this)->EngineRelease(); }

// --- the manager ---

// 0x00496770
CParticleManager* CParticleManager::Construct()
{
    vtable = kManagerVtable;
    m_systems.size = 0;
    auto* head = static_cast<GameObjects::PointerListNode*>(MemSys::New(sizeof(GameObjects::PointerListNode)));
    if (head) {
        head->next = head;
        head->prev = head;
    }
    m_systems.head = head;
    for (float& c : m_shade)
        c = 1.0f;
    return this;
}

// 0x004967D0
void CParticleManager::DestroyBody()
{
    vtable = kManagerVtable;
    GameObjects::PointerListNode* result;
    for (int pass = 0; pass < 2; ++pass) {
        GameObjects::PointerListNode* head = m_systems.head;
        m_systems.Erase(&result, head ? head->next : nullptr, head);
    }
    MemSys::Free(m_systems.head);
}

// 0x00496860
CParticleManager* CParticleManager::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Free(this);
    return this;
}

// 0x00496510
CSparkle* CParticleManager::SpawnParticle(const CSparkleConfig* params)
{
    CSparkle* particle = reinterpret_cast<CSparkle*(__thiscall*)(CParticleManager*)>(vtable[1])(this);
    if (particle) {
        particle->Init(params);
        if (particle->m_params.m_flag158) {
            for (int i = 0; i < 4; ++i)
                particle->m_params.m_colourScale[i] = m_shade[i];
            particle->SetColourScale(m_shade);
        }
        m_systems.Insert(m_systems.head, reinterpret_cast<void* const*>(&particle));
    }
    return particle;
}

// 0x00496590
void CParticleManager::DestroyParticle(CSparkle* particle)
{
    if (particle->m_params.m_emitScript) {
        Scripts::StopScriptsByTag(particle->m_params.m_emitScript);
        particle->m_params.m_emitScript = 0;
    }
    GameObjects::PointerListNode* node = m_systems.head ? m_systems.head->next : nullptr;
    if (node == m_systems.head)
        return;
    while (static_cast<CSparkle*>(node->data)->m_params.m_name != particle->m_params.m_name) {
        node = node->next;
        if (node == m_systems.head)
            return;
    }
    auto* found = static_cast<CSparkle*>(node->data);
    found->Release();
    if (found)
        Delete(found, 1);
    if (node != m_systems.head) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        MemSys::Free(node);
        --m_systems.size;
    }
}

// 0x00496640
void CParticleManager::DrawParticles()
{
    if (!m_systems.size)
        return;
    for (GameObjects::PointerListNode* node = m_systems.head ? m_systems.head->next : nullptr; node != m_systems.head;
         node = node->next)
        static_cast<CSparkle*>(node->data)->Render(!IsGameHalted());
}

// 0x00496680
void CParticleManager::TickParticles()
{
    for (GameObjects::PointerListNode* node = m_systems.head ? m_systems.head->next : nullptr; node != m_systems.head;
         node = node->next)
        static_cast<CSparkle*>(node->data)->Update();
}

// 0x004966B0
void CParticleManager::ChangeColour(const float* rgba)
{
    for (GameObjects::PointerListNode* node = m_systems.head ? m_systems.head->next : nullptr; node != m_systems.head;
         node = node->next)
        static_cast<CSparkle*>(node->data)->SetColourScale(rgba);
    for (int i = 0; i < 4; ++i)
        m_shade[i] = rgba[i];
}

// 0x00496700
void CParticleManager::DestroyAllParticles()
{
    for (GameObjects::PointerListNode* node = m_systems.head ? m_systems.head->next : nullptr; node != m_systems.head;
         node = node->next) {
        auto* particle = static_cast<CSparkle*>(node->data);
        particle->Release();
        if (particle)
            Delete(particle, 1);
    }
    GameObjects::PointerListNode* result;
    GameObjects::PointerListNode* head = m_systems.head;
    m_systems.Erase(&result, head ? head->next : nullptr, head);
}

namespace {
static VtableSlot kManagerVtable_slots[] = {&Engine3D::CParticleManager::Destroy, Stubs::ReturnZero};
void** const kManagerVtable = reinterpret_cast<void**>(kManagerVtable_slots);
} // namespace

} // namespace Nx

namespace GameObjects {

// 0x004964A0
void PointerList::Insert(PointerListNode* position, void* const* value)
{
    auto* node = static_cast<PointerListNode*>(MemSys::New(sizeof(PointerListNode)));
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

namespace Vt {
static VtableSlot kEngineParticles_slots[] = {&Engine3D::CPcParticleSystem::EngineDestroy, Stubs::DoNothing, Stubs::DoNothing,
                Stubs::IgnoreArgument, Stubs::IgnoreArgument, Stubs::DoNothing, Stubs::DoNothing, Stubs::ReturnTrue};
void** const kEngineParticles = reinterpret_cast<void**>(kEngineParticles_slots);
} // namespace Vt
