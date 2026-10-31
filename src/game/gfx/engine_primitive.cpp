// The engine's primitive (Engine3D::CPrimitive) and its lists: see engine_primitive.h. From the disassembly of
// 0x0049C390-0x0049C906.

#include <cstdint>

#include "game/gfx/engine_primitive.h"

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/gfx/pc_primitive.h"
#include "game/core/code_address.h"

namespace Engine3D {

namespace {

extern void** const kVtable;
constexpr uint32_t kDefaultBlendMode = 0x54628ED7;
constexpr uint32_t kNewFlag = 0x100;

uint32_t g_primitiveStates;
CPrimitive* g_drawnPrimitives;
CPrimitive* g_newPrimitives;

CPrimitive*& NewPrimitives() { return g_newPrimitives; }
CPrimitive*& DrawnPrimitives() { return g_drawnPrimitives; }
uint32_t& PrimitiveStates() { return g_primitiveStates; }

void** Vtable(CPrimitive* primitive) { return primitive->m_vtable; }

} // namespace

// 0x0049C660
CPrimitive* CPrimitive::Construct(int own)
{
    unknown1C = 0;
    m_ready = false;
    m_following = nullptr;
    unknown14 = 0;
    m_flags = 0;
    m_vtable = reinterpret_cast<void**>(kVtable);
    m_blendMode = kDefaultBlendMode;
    m_ownsVertices = own;
    if (own == 1)
        LinkPrimitive(this);
    return this;
}

// 0x0049C6A0
void CPrimitive::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(kVtable);
    if (m_ownsVertices == 1)
        UnlinkPrimitive(this);
}

// 0x0049C790
CPrimitive* CPrimitive::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Free(this);
    return this;
}

// 0x0049C6C0
void CPrimitive::Ready()
{
    reinterpret_cast<void(__thiscall*)(CPrimitive*)>(Vtable(this)[4])(this);
    m_ready = true;
}

// 0x0049C6D0
void CPrimitive::RenderIfReady()
{
    if (m_ready)
        reinterpret_cast<void(__thiscall*)(CPrimitive*)>(Vtable(this)[5])(this);
}

// 0x0049C6E0
void CPrimitive::DrawIfReady(const float* positions, const uint32_t* colours, const float* uvs, const float* normals)
{
    if (m_ready)
        reinterpret_cast<void(__thiscall*)(CPrimitive*, const float*, const uint32_t*, const float*, const float*)>(
            Vtable(this)[6])(this, positions, colours, uvs, normals);
}

// --- the virtuals' bodies (0x0049C390-0x0049C640) ---

void CPrimitive::SetTexture(const uint8_t*, int, int) { DebugOut::Printf("STUB: plat_set_texture\n"); }
void CPrimitive::ChangeColour(uint32_t) { DebugOut::Printf("STUB: plat_set_base_color\n"); }
void CPrimitive::Init(uint32_t, int, int) { DebugOut::Printf("STUB: plat_begin\n"); }
void CPrimitive::End() { DebugOut::Printf("STUB: plat_end\n"); }
void CPrimitive::Render() { DebugOut::Printf("STUB: plat_draw\n"); }
void CPrimitive::Draw(const float*, const uint32_t*, const float*, const float*) { DebugOut::Printf("STUB: plat_render\n"); }
void CPrimitive::SetPosition(int, float, float, float) { DebugOut::Printf("STUB: plat_set_pos\n"); }
void CPrimitive::SetX(int, float) { DebugOut::Printf("STUB: plat_set_pos_x\n"); }
void CPrimitive::SetY(int, float) { DebugOut::Printf("STUB: plat_set_pos_y\n"); }
void CPrimitive::SetZ(int, float) { DebugOut::Printf("STUB: plat_set_pos_z\n"); }
void CPrimitive::SetNormal(int, float, float, float) { DebugOut::Printf("STUB: plat_set_normal\n"); }
void CPrimitive::SetNormalX(int, float) { DebugOut::Printf("STUB: plat_set_normal_x\n"); }
void CPrimitive::SetNormalY(int, float) { DebugOut::Printf("STUB: plat_set_normal_y\n"); }
void CPrimitive::SetNormalZ(int, float) { DebugOut::Printf("STUB: plat_set_normal_z\n"); }
void CPrimitive::SetVertexColour(int, uint32_t) { DebugOut::Printf("STUB: plat_set_color\n"); }
void CPrimitive::SetUV(int, float, float) { DebugOut::Printf("STUB: plat_set_uv\n"); }
void CPrimitive::ChooseU(int, float) { DebugOut::Printf("STUB: plat_set_u\n"); }
void CPrimitive::ChooseV(int, float) { DebugOut::Printf("STUB: plat_set_v\n"); }
float CPrimitive::FetchPosX(int)
{
    DebugOut::Printf("STUB: plat_get_pos_x\n");
    return 0.0f;
}
float CPrimitive::FetchPosY(int)
{
    DebugOut::Printf("STUB: plat_get_pos_y\n");
    return 0.0f;
}
float CPrimitive::FetchPosZ(int)
{
    DebugOut::Printf("STUB: plat_get_pos_z\n");
    return 0.0f;
}
float CPrimitive::GetNormalX(int)
{
    DebugOut::Printf("STUB: plat_get_normal_x\n");
    return 0.0f;
}
float CPrimitive::GetNormalY(int)
{
    DebugOut::Printf("STUB: plat_get_normal_y\n");
    return 0.0f;
}
float CPrimitive::GetNormalZ(int)
{
    DebugOut::Printf("STUB: plat_get_normal_z\n");
    return 0.0f;
}
uint32_t CPrimitive::GetVertexColour(int)
{
    DebugOut::Printf("STUB: plat_get_color\n");
    return 0;
}
float CPrimitive::GetU(int)
{
    DebugOut::Printf("STUB: plat_get_u\n");
    return 0.0f;
}
float CPrimitive::GetV(int)
{
    DebugOut::Printf("STUB: plat_get_v\n");
    return 0.0f;
}
void CPrimitive::Translate(float, float, float) { DebugOut::Printf("STUB: plat_add_pos\n"); }
void CPrimitive::TranslateX(float) { DebugOut::Printf("STUB: plat_add_pos_x\n"); }
void CPrimitive::TranslateY(float) { DebugOut::Printf("STUB: plat_add_pos_y\n"); }
void CPrimitive::TranslateZ(float) { DebugOut::Printf("STUB: plat_add_pos_z\n"); }
void CPrimitive::ScrollUV(float, float) { DebugOut::Printf("STUB: plat_add_uv\n"); }
void CPrimitive::ScrollU(float) { DebugOut::Printf("STUB: plat_add_u\n"); }
void CPrimitive::ScrollV(float) { DebugOut::Printf("STUB: plat_add_v\n"); }
int CPrimitive::Compare(const CPrimitive*)
{
    DebugOut::Printf("STUB: plat_sort\n");
    return 0;
}

// --- the lists ---

// 0x0049C6F0
__declspec(naked) void CallVirtual35Stub()
{
    __asm {
        mov eax, [ecx]
        jmp dword ptr [eax + 0x8C]
    }
}

// 0x0049C7D0
void __cdecl LinkPrimitive(CPrimitive* primitive)
{
    primitive->m_following = NewPrimitives();
    NewPrimitives() = primitive;
    primitive->m_flags |= kNewFlag;
}

// 0x0049C7F0
void __cdecl UnlinkPrimitive(CPrimitive* primitive)
{
    CPrimitive** list = (primitive->m_flags & kNewFlag) ? &NewPrimitives() : &DrawnPrimitives();
    CPrimitive* before = *list;
    if (before == primitive) {
        *list = before->m_following;
        return;
    }
    while (before && before->m_following && before->m_following != primitive)
        before = before->m_following;
    before->m_following = before->m_following->m_following; // (not found: through null, as the original)
}

// 0x0049C840
void __cdecl RenderPrimitives()
{
    while (CPrimitive* primitive = NewPrimitives()) {
        UnlinkPrimitive(primitive);
        primitive->m_flags &= ~kNewFlag;
        CPrimitive** at = &DrawnPrimitives();
        if (*at) {
            for (;;) {
                // (the primitive's Compare - vtable slot 35 - with the one there)
                int order = reinterpret_cast<int(__thiscall*)(CPrimitive*, CPrimitive*)>(Vtable(primitive)[35])(
                    primitive, *at);
                if (order < 0)
                    break;
                at = &(*at)->m_following;
                if (!*at)
                    break;
            }
        }
        primitive->m_following = *at;
        *at = primitive;
    }
    for (CPrimitive* primitive = DrawnPrimitives(); primitive; primitive = primitive->m_following) {
        if (primitive->m_ready)
            primitive->RenderIfReady();
    }
}

// 0x0049C700
void __cdecl SetPrimitiveState(int state, int value)
{
    uint32_t& states = PrimitiveStates();
    switch (state) {
    case 1:
        states = (states & ~0x7u) | (static_cast<uint32_t>(value) & 0x7u);
        break;
    case 2:
        states = (states & ~0xF8u) | ((static_cast<uint32_t>(value) << 3) & 0xF8u);
        break;
    case 4:
        states = (states & ~0x300u) | ((static_cast<uint32_t>(value) << 8) & 0x300u);
        break;
    default:
        break;
    }
    SetPrimitiveDepthTest(state, value);
}

namespace {
static VtableSlot kVtable_slots[] = {&Engine3D::CPrimitive::Destroy, &Engine3D::CPrimitive::SetTexture, &Engine3D::CPrimitive::ChangeColour,
                &Engine3D::CPrimitive::Init, &Engine3D::CPrimitive::End, &Engine3D::CPrimitive::Render, &Engine3D::CPrimitive::Draw,
                &Engine3D::CPrimitive::SetPosition, &Engine3D::CPrimitive::SetX, &Engine3D::CPrimitive::SetY, &Engine3D::CPrimitive::SetZ,
                &Engine3D::CPrimitive::SetNormal, &Engine3D::CPrimitive::SetNormalX, &Engine3D::CPrimitive::SetNormalY,
                &Engine3D::CPrimitive::SetNormalZ, &Engine3D::CPrimitive::SetVertexColour, &Engine3D::CPrimitive::SetUV,
                &Engine3D::CPrimitive::ChooseU, &Engine3D::CPrimitive::ChooseV, &Engine3D::CPrimitive::FetchPosX, &Engine3D::CPrimitive::FetchPosY,
                &Engine3D::CPrimitive::FetchPosZ, &Engine3D::CPrimitive::GetNormalX, &Engine3D::CPrimitive::GetNormalY,
                &Engine3D::CPrimitive::GetNormalZ, &Engine3D::CPrimitive::GetVertexColour, &Engine3D::CPrimitive::GetU,
                &Engine3D::CPrimitive::GetV, &Engine3D::CPrimitive::Translate, &Engine3D::CPrimitive::TranslateX,
                &Engine3D::CPrimitive::TranslateY, &Engine3D::CPrimitive::TranslateZ, &Engine3D::CPrimitive::ScrollUV,
                &Engine3D::CPrimitive::ScrollU, &Engine3D::CPrimitive::ScrollV, &Engine3D::CPrimitive::Compare};
void** const kVtable = reinterpret_cast<void**>(kVtable_slots);
} // namespace

} // namespace Nx
