// The engine's primitive (Nx::CPrimitive) and its lists: see engine_primitive.h. From the disassembly of
// 0x0049C390-0x0049C906.

#include "game/gfx/engine_primitive.h"

#include "game/core/dbg.h"
#include "game/core/mem.h"
#include "game/gfx/pc_primitive.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Nx {

namespace {

constexpr uint32_t kVtable = 0x00649878;
constexpr uint32_t kDefaultBlendMode = 0x54628ED7;
constexpr uint32_t kNewFlag = 0x100;

CPrimitive*& NewPrimitives() { return GameVariable<CPrimitive*>(0x00700F00); }
CPrimitive*& DrawnPrimitives() { return GameVariable<CPrimitive*>(0x00700EFC); }
uint32_t& PrimitiveStates() { return GameVariable<uint32_t>(0x00700EF8); }

void** Vtable(CPrimitive* primitive) { return primitive->m_vtable; }

} // namespace

// 0x0049C660
CPrimitive* CPrimitive::Construct(int own)
{
    unknown1C = 0;
    m_ready = false;
    mp_next = nullptr;
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
        Mem::Free(this);
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

void CPrimitive::SetTexture(const uint8_t*, int, int) { Dbg::Printf("STUB: plat_set_texture\n"); }
void CPrimitive::SetColour(uint32_t) { Dbg::Printf("STUB: plat_set_base_color\n"); }
void CPrimitive::Init(uint32_t, int, int) { Dbg::Printf("STUB: plat_begin\n"); }
void CPrimitive::End() { Dbg::Printf("STUB: plat_end\n"); }
void CPrimitive::Render() { Dbg::Printf("STUB: plat_draw\n"); }
void CPrimitive::Draw(const float*, const uint32_t*, const float*, const float*) { Dbg::Printf("STUB: plat_render\n"); }
void CPrimitive::SetPosition(int, float, float, float) { Dbg::Printf("STUB: plat_set_pos\n"); }
void CPrimitive::SetX(int, float) { Dbg::Printf("STUB: plat_set_pos_x\n"); }
void CPrimitive::SetY(int, float) { Dbg::Printf("STUB: plat_set_pos_y\n"); }
void CPrimitive::SetZ(int, float) { Dbg::Printf("STUB: plat_set_pos_z\n"); }
void CPrimitive::SetNormal(int, float, float, float) { Dbg::Printf("STUB: plat_set_normal\n"); }
void CPrimitive::SetNormalX(int, float) { Dbg::Printf("STUB: plat_set_normal_x\n"); }
void CPrimitive::SetNormalY(int, float) { Dbg::Printf("STUB: plat_set_normal_y\n"); }
void CPrimitive::SetNormalZ(int, float) { Dbg::Printf("STUB: plat_set_normal_z\n"); }
void CPrimitive::SetVertexColour(int, uint32_t) { Dbg::Printf("STUB: plat_set_color\n"); }
void CPrimitive::SetUV(int, float, float) { Dbg::Printf("STUB: plat_set_uv\n"); }
void CPrimitive::SetU(int, float) { Dbg::Printf("STUB: plat_set_u\n"); }
void CPrimitive::SetV(int, float) { Dbg::Printf("STUB: plat_set_v\n"); }
float CPrimitive::GetX(int)
{
    Dbg::Printf("STUB: plat_get_pos_x\n");
    return 0.0f;
}
float CPrimitive::GetY(int)
{
    Dbg::Printf("STUB: plat_get_pos_y\n");
    return 0.0f;
}
float CPrimitive::GetZ(int)
{
    Dbg::Printf("STUB: plat_get_pos_z\n");
    return 0.0f;
}
float CPrimitive::GetNormalX(int)
{
    Dbg::Printf("STUB: plat_get_normal_x\n");
    return 0.0f;
}
float CPrimitive::GetNormalY(int)
{
    Dbg::Printf("STUB: plat_get_normal_y\n");
    return 0.0f;
}
float CPrimitive::GetNormalZ(int)
{
    Dbg::Printf("STUB: plat_get_normal_z\n");
    return 0.0f;
}
uint32_t CPrimitive::GetVertexColour(int)
{
    Dbg::Printf("STUB: plat_get_color\n");
    return 0;
}
float CPrimitive::GetU(int)
{
    Dbg::Printf("STUB: plat_get_u\n");
    return 0.0f;
}
float CPrimitive::GetV(int)
{
    Dbg::Printf("STUB: plat_get_v\n");
    return 0.0f;
}
void CPrimitive::Translate(float, float, float) { Dbg::Printf("STUB: plat_add_pos\n"); }
void CPrimitive::TranslateX(float) { Dbg::Printf("STUB: plat_add_pos_x\n"); }
void CPrimitive::TranslateY(float) { Dbg::Printf("STUB: plat_add_pos_y\n"); }
void CPrimitive::TranslateZ(float) { Dbg::Printf("STUB: plat_add_pos_z\n"); }
void CPrimitive::ScrollUV(float, float) { Dbg::Printf("STUB: plat_add_uv\n"); }
void CPrimitive::ScrollU(float) { Dbg::Printf("STUB: plat_add_u\n"); }
void CPrimitive::ScrollV(float) { Dbg::Printf("STUB: plat_add_v\n"); }
int CPrimitive::Compare(const CPrimitive*)
{
    Dbg::Printf("STUB: plat_sort\n");
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
    primitive->mp_next = NewPrimitives();
    NewPrimitives() = primitive;
    primitive->m_flags |= kNewFlag;
}

// 0x0049C7F0
void __cdecl UnlinkPrimitive(CPrimitive* primitive)
{
    CPrimitive** list = (primitive->m_flags & kNewFlag) ? &NewPrimitives() : &DrawnPrimitives();
    CPrimitive* before = *list;
    if (before == primitive) {
        *list = before->mp_next;
        return;
    }
    while (before && before->mp_next && before->mp_next != primitive)
        before = before->mp_next;
    before->mp_next = before->mp_next->mp_next; // (not found: through null, as the original)
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
                at = &(*at)->mp_next;
                if (!*at)
                    break;
            }
        }
        primitive->mp_next = *at;
        *at = primitive;
    }
    for (CPrimitive* primitive = DrawnPrimitives(); primitive; primitive = primitive->mp_next) {
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

} // namespace Nx

REPLACE(0x0049C660, &Nx::CPrimitive::Construct);
REPLACE(0x0049C6A0, &Nx::CPrimitive::DestroyBody);
REPLACE(0x0049C790, &Nx::CPrimitive::Destroy);
REPLACE(0x0049C6C0, &Nx::CPrimitive::Ready);
REPLACE(0x0049C6D0, &Nx::CPrimitive::RenderIfReady);
REPLACE(0x0049C6E0, &Nx::CPrimitive::DrawIfReady);
REPLACE_UNCHECKED(0x0049C6F0, Nx::CallVirtual35Stub);
REPLACE(0x0049C7D0, Nx::LinkPrimitive);
REPLACE(0x0049C7F0, Nx::UnlinkPrimitive);
REPLACE(0x0049C840, Nx::RenderPrimitives);
REPLACE(0x0049C700, Nx::SetPrimitiveState);
REPLACE(0x0049C390, &Nx::CPrimitive::SetTexture);
REPLACE(0x0049C3A0, &Nx::CPrimitive::SetColour);
REPLACE(0x0049C3B0, &Nx::CPrimitive::Init);
REPLACE(0x0049C3C0, &Nx::CPrimitive::End);
REPLACE(0x0049C3D0, &Nx::CPrimitive::Render);
REPLACE(0x0049C3E0, &Nx::CPrimitive::Draw);
REPLACE(0x0049C3F0, &Nx::CPrimitive::SetPosition);
REPLACE(0x0049C400, &Nx::CPrimitive::SetX);
REPLACE(0x0049C410, &Nx::CPrimitive::SetY);
REPLACE(0x0049C420, &Nx::CPrimitive::SetZ);
REPLACE(0x0049C430, &Nx::CPrimitive::SetNormal);
REPLACE(0x0049C440, &Nx::CPrimitive::SetNormalX);
REPLACE(0x0049C450, &Nx::CPrimitive::SetNormalY);
REPLACE(0x0049C460, &Nx::CPrimitive::SetNormalZ);
REPLACE(0x0049C470, &Nx::CPrimitive::SetVertexColour);
REPLACE(0x0049C480, &Nx::CPrimitive::SetUV);
REPLACE(0x0049C490, &Nx::CPrimitive::SetU);
REPLACE(0x0049C4A0, &Nx::CPrimitive::SetV);
REPLACE(0x0049C4B0, &Nx::CPrimitive::GetX);
REPLACE(0x0049C4D0, &Nx::CPrimitive::GetY);
REPLACE(0x0049C4F0, &Nx::CPrimitive::GetZ);
REPLACE(0x0049C510, &Nx::CPrimitive::GetNormalX);
REPLACE(0x0049C530, &Nx::CPrimitive::GetNormalY);
REPLACE(0x0049C550, &Nx::CPrimitive::GetNormalZ);
REPLACE(0x0049C570, &Nx::CPrimitive::GetVertexColour);
REPLACE(0x0049C590, &Nx::CPrimitive::GetU);
REPLACE(0x0049C5B0, &Nx::CPrimitive::GetV);
REPLACE(0x0049C5D0, &Nx::CPrimitive::Translate);
REPLACE(0x0049C5E0, &Nx::CPrimitive::TranslateX);
REPLACE(0x0049C5F0, &Nx::CPrimitive::TranslateY);
REPLACE(0x0049C600, &Nx::CPrimitive::TranslateZ);
REPLACE(0x0049C610, &Nx::CPrimitive::ScrollUV);
REPLACE(0x0049C620, &Nx::CPrimitive::ScrollU);
REPLACE(0x0049C630, &Nx::CPrimitive::ScrollV);
REPLACE(0x0049C640, &Nx::CPrimitive::Compare);
