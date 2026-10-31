// The particle component's particle systems and their manager (Nx): see particle_sets.h. From the disassembly of
// 0x00495260-0x004968AF.

#include "engine/gfx/particle_sets.h"
#include "engine/gfx/pc_particles.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/gfx/particle_maths.h"
#include "engine/core/game_vtables.h"
#include "engine/core/stubs.h"
#include "engine/objects/movie_manager.h"

namespace Engine3D {

namespace {

void** const& kParticleVtable = Vt::kEngineParticles;
extern void** const kManagerVtable;
constexpr float kByteToUnit = 1.0f / 255.0f;   // 0x00645D70 (the bit pattern 0x3B808081)
constexpr uint32_t kObjectIdParam = 0x40C698AF; // (the emit script's parameter: the object's id)

bool IsGameHalted() { return GameObjects::g_simulationHalted; }

// The system's virtual functions.
CSparkle* EraseOne(CSparkle* particle, unsigned int flags)
{
    return reinterpret_cast<CSparkle*(__thiscall*)(CSparkle*, unsigned int)>(particle->vtable[0])(particle, flags);
}
void Initialise(CSparkle* particle) { reinterpret_cast<void(__thiscall*)(CSparkle*)>(particle->vtable[1])(particle); }
void ColourChanged(CSparkle* particle)
{
    reinterpret_cast<void(__thiscall*)(CSparkle*)>(particle->vtable[6])(particle);
}

Scripts::CStruct* SpawnParamTable()
{
    Scripts::CStruct* structure = Scripts::SpawnParamBag();
    return structure ? structure->PrepareAll() : nullptr;
}

// The engine's helpers (not yet rewritten; register arguments).
// 0x00495130 / 0x004951B0 (ECX a, EDX b, ESI out; returns out): each component b's where it's below / above a's.
Maths::Vec4* CallMinOrMax(uint32_t code, const Maths::Vec4* a, const Maths::Vec4* b, Maths::Vec4* destination)
{
    Maths::Vec4* result;
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
Maths::Vec4* MinOf(const Maths::Vec4* a, const Maths::Vec4* b, Maths::Vec4* out)
{
    return CallMinOrMax(reinterpret_cast<uint32_t>(CodeAddress(&MinVectorStub)), a, b, out);
}
Maths::Vec4* MaxOf(const Maths::Vec4* a, const Maths::Vec4* b, Maths::Vec4* out)
{
    return CallMinOrMax(reinterpret_cast<uint32_t>(CodeAddress(&MaxVectorStub)), a, b, out);
}
// 0x00494F10 (thiscall on `out`, the 14 words of arguments popped by the caller): per component, the extreme of the
// quadratic through `start` (at 0), `mid` (at `midTime`) and `end` (at `life`); w 0. Returns out.
Maths::Vec4* CurveExtremes(Maths::Vec4* destination, const Maths::Vec4& start, const Maths::Vec4& mid, const Maths::Vec4& end,
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
    Maths::Vec4* result;
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
CSparkleConfig* CSparkleConfig::MakeNew()
{
    m_unknown04 = 0xDEDFC057;
    m_flag151 = true;
    m_localPositions = false;
    m_orientWithObject = false;
    for (int i = 0; i < 3; ++i) {
        m_extent[i] = 1.0f;
        m_radius[i] = 0.0f;
        m_atPoint[i] = Maths::Vec4{0.0f, 0.0f, 0.0f, 0.0f};
        m_box[i] = Maths::Vec4{20.0f, 20.0f, 20.0f, 0.0f};
        m_offsetPos[i] = Maths::Vec4{0.0f, 0.0f, 0.0f, 0.0f};
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
CSparkle* CSparkle::MakeNew()
{
    vtable = kParticleVtable;
    m_args.MakeNew();
    new (&m_extentBox) Maths::BBox();
    return this;
}

// 0x00496380
void CSparkle::PrepareAll(const CSparkleConfig* params)
{
    m_args.Assign(*params);
    UpdateBounds();
    Initialise(this);
}

// 0x00495260. The corners are (position, size) less / plus (half the box, half the radius), the size and half the
// radius; the original's x87 code rounds some of the sums to floats and keeps others at 53 bits - followed term by term.
void CSparkle::UpdateBounds()
{
    const CSparkleConfig& p = m_args;
    float hb[3][3], hr[3], s[3];
    for (int i = 0; i < 3; ++i) {
        hb[i][0] = p.m_box[i].x * 0.5f;
        hb[i][1] = p.m_box[i].y * 0.5f;
        hb[i][2] = p.m_box[i].z * 0.5f;
        hr[i] = p.m_radius[i] * 0.5f;
        s[i] = p.m_extent[i];
    }
    Maths::Vec4 lo[3], hi[3];
    for (int i = 0; i < 3; ++i) {
        const Maths::Vec4& at = p.m_atPoint[i];
        lo[i].x = Round(Round(Round(static_cast<double>(at.x) - hb[i][0]) - s[i]) - hr[i]);
        lo[i].y = Round(Round(Round(static_cast<double>(at.y) - hb[i][1]) - s[i]) - hr[i]);
        lo[i].z = Round(Round(Round(static_cast<double>(at.z) - hb[i][2]) - s[i]) - hr[i]);
        lo[i].w = Round(Round(Round(static_cast<double>(s[i]) - hr[i]) - s[i]) - hr[i]);
    }
    {
        const Maths::Vec4& at = p.m_atPoint[0];
        hi[0].x = Round((static_cast<double>(Round(static_cast<double>(hb[0][0]) + at.x)) + s[0]) + hr[0]);
        hi[0].y = Round(((static_cast<double>(at.y) + hb[0][1]) + s[0]) + hr[0]);
        hi[0].z = Round(static_cast<double>(Round(s[0] + static_cast<double>(Round(static_cast<double>(at.z) +
                                                                                    hb[0][2])))) + hr[0]);
        hi[0].w = Round(static_cast<double>(Round(s[0] + static_cast<double>(Round(static_cast<double>(hr[0]) +
                                                                                    s[0])))) + hr[0]);
    }
    for (int i = 1; i < 3; ++i) {
        const Maths::Vec4& at = p.m_atPoint[i];
        hi[i].x = Round(Round(Round(static_cast<double>(hb[i][0]) + at.x) + s[i]) + hr[i]);
        hi[i].y = Round(Round(Round(static_cast<double>(at.y) + hb[i][1]) + s[i]) + hr[i]);
        hi[i].z = Round((s[i] + static_cast<double>(Round(static_cast<double>(at.z) + hb[i][2]))) + hr[i]);
        hi[i].w = Round(static_cast<double>(Round(s[i] + (static_cast<double>(hr[i]) + s[i]))) + hr[i]);
    }
    Maths::Vec4 low, high, curve;
    low = *MinOf(&lo[0], &lo[2], &curve);
    high = *MaxOf(&hi[0], &hi[2], &curve);
    if (p.m_flag151) { // (the paths go through the mid: their extremes on the way)
        float life = p.m_unknown2C;
        float midTime = Round(static_cast<double>(life) * p.m_unknown30 * 0.01f);
        Maths::Vec4 extreme;
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
void CSparkle::ChangePlace(int which, const Maths::Vec4* pos) { m_args.m_atPoint[which] = *pos; }

// 0x00496160
void CSparkle::SetBox(int which, const Maths::Vec4* box) { m_args.m_box[which] = *box; }

// 0x00496060
void CSparkle::SpinBy(int which, const Maths::Mat4* matrix)
{
    const Maths::Vec4 start = m_args.m_atPoint[0];
    Maths::Vec4& pos = m_args.m_atPoint[which];
    pos.x -= start.x;
    pos.y -= start.y;
    pos.z -= start.z;
    pos.w -= start.w;
    Maths::Vec4 turned;
    pos = matrix->Xform(turned, pos);
    Maths::Vec4& box = m_args.m_box[which];
    box = matrix->Xform(turned, box);
    pos.x = start.x + pos.x;
    pos.y = start.y + pos.y;
    pos.z = start.z + pos.z;
    pos.w = start.w + pos.w;
    m_args.m_orientationTurn = *matrix;
}

// 0x00496190
void CSparkle::ChangeColour(const uint8_t* rgba)
{
    if (!rgba) {
        ColourChanged(this);
        return;
    }
    if (m_args.m_flag158) {
        for (int i = 0; i < 4; ++i)
            m_args.m_colourScale[i] = static_cast<float>(rgba[i]) * kByteToUnit;
        ColourChanged(this);
    }
}

// 0x00496210
void CSparkle::SetColourScale(const float* rgba)
{
    if (m_args.m_flag158) {
        for (int i = 0; i < 4; ++i)
            m_args.m_colourScale[i] = rgba[i];
        ColourChanged(this);
    }
}

// 0x004963B0
void CSparkle::SetEmitScript(uint32_t script, uint32_t objectId)
{
    if (m_args.m_emitScript)
        Scripts::StopScriptsByTag(m_args.m_emitScript);
    m_args.m_emitScript = script;
    m_args.m_entityKey = objectId;
    Scripts::CStruct* params = SpawnParamTable();
    params->PutNameKey(kObjectIdParam, m_args.m_entityKey); // (not checked for null, as the original)
    Scripts::SpawnScript(m_args.m_emitScript, params, 0, nullptr, 0, 0, false, false, false, false);
    if (params) {
        params->Clear();
        Scripts::DeleteStruct(params);
    }
}

void CSparkle::Render(bool advance) { reinterpret_cast<CPcParticleSystem*>(this)->EngineRender(advance); }
void CSparkle::TickFrame() { reinterpret_cast<CPcParticleSystem*>(this)->EngineUpdate(); }
void CSparkle::Release() { reinterpret_cast<CPcParticleSystem*>(this)->EngineRelease(); }

// --- the manager ---

// 0x00496770
CEffectsDirector* CEffectsDirector::MakeNew()
{
    vtable = kManagerVtable;
    m_systems.size = 0;
    auto* head = static_cast<GameObjects::PointerListNode*>(MemSys::Alloc(sizeof(GameObjects::PointerListNode)));
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
void CEffectsDirector::DestroyBody()
{
    vtable = kManagerVtable;
    GameObjects::PointerListNode* result;
    for (int pass = 0; pass < 2; ++pass) {
        GameObjects::PointerListNode* head = m_systems.head;
        m_systems.Erase(&result, head ? head->next : nullptr, head);
    }
    MemSys::Dealloc(m_systems.head);
}

// 0x00496860
CEffectsDirector* CEffectsDirector::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x00496510
CSparkle* CEffectsDirector::SpawnParticle(const CSparkleConfig* params)
{
    CSparkle* particle = reinterpret_cast<CSparkle*(__thiscall*)(CEffectsDirector*)>(vtable[1])(this);
    if (particle) {
        particle->PrepareAll(params);
        if (particle->m_args.m_flag158) {
            for (int i = 0; i < 4; ++i)
                particle->m_args.m_colourScale[i] = m_shade[i];
            particle->SetColourScale(m_shade);
        }
        m_systems.Splice(m_systems.head, reinterpret_cast<void* const*>(&particle));
    }
    return particle;
}

// 0x00496590
void CEffectsDirector::RetireParticle(CSparkle* particle)
{
    if (particle->m_args.m_emitScript) {
        Scripts::StopScriptsByTag(particle->m_args.m_emitScript);
        particle->m_args.m_emitScript = 0;
    }
    GameObjects::PointerListNode* node = m_systems.head ? m_systems.head->next : nullptr;
    if (node == m_systems.head)
        return;
    while (static_cast<CSparkle*>(node->data)->m_args.m_label != particle->m_args.m_label) {
        node = node->next;
        if (node == m_systems.head)
            return;
    }
    auto* found = static_cast<CSparkle*>(node->data);
    found->Release();
    if (found)
        EraseOne(found, 1);
    if (node != m_systems.head) {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        MemSys::Dealloc(node);
        --m_systems.size;
    }
}

// 0x00496640
void CEffectsDirector::DrawParticles()
{
    if (!m_systems.size)
        return;
    for (GameObjects::PointerListNode* node = m_systems.head ? m_systems.head->next : nullptr; node != m_systems.head;
         node = node->next)
        static_cast<CSparkle*>(node->data)->Render(!IsGameHalted());
}

// 0x00496680
void CEffectsDirector::TickParticles()
{
    for (GameObjects::PointerListNode* node = m_systems.head ? m_systems.head->next : nullptr; node != m_systems.head;
         node = node->next)
        static_cast<CSparkle*>(node->data)->TickFrame();
}

// 0x004966B0
void CEffectsDirector::ChangeColour(const float* rgba)
{
    for (GameObjects::PointerListNode* node = m_systems.head ? m_systems.head->next : nullptr; node != m_systems.head;
         node = node->next)
        static_cast<CSparkle*>(node->data)->SetColourScale(rgba);
    for (int i = 0; i < 4; ++i)
        m_shade[i] = rgba[i];
}

// 0x00496700
void CEffectsDirector::KillEveryParticle()
{
    for (GameObjects::PointerListNode* node = m_systems.head ? m_systems.head->next : nullptr; node != m_systems.head;
         node = node->next) {
        auto* particle = static_cast<CSparkle*>(node->data);
        particle->Release();
        if (particle)
            EraseOne(particle, 1);
    }
    GameObjects::PointerListNode* result;
    GameObjects::PointerListNode* head = m_systems.head;
    m_systems.Erase(&result, head ? head->next : nullptr, head);
}

namespace {
static VtableSlot kManagerVtable_slots[] = {&Engine3D::CEffectsDirector::Teardown, Stubs::ReturnZero};
void** const kManagerVtable = reinterpret_cast<void**>(kManagerVtable_slots);
} // namespace

} // namespace Nx

namespace GameObjects {

// 0x004964A0
void PointerList::Splice(PointerListNode* position, void* const* value)
{
    auto* node = static_cast<PointerListNode*>(MemSys::Alloc(sizeof(PointerListNode)));
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
