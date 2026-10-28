#pragma once

#include <cstddef>
#include <cstdint>

// The engine's primitive (Nx; the platform-independent base of the PC primitive drawer in pc_primitive.h): a run of
// vertices set through its virtuals - here only "STUB" messages; the PC class does the work. Primitives that own their
// vertices go on a list of new ones (0x00700F00), which RenderPrimitives sorts into the drawing list (0x00700EFC).
namespace Nx {

// (0x20 bytes of the PC class's 0x158; vtable 0x00649878.)
struct CPrimitive {
    void** m_vtable;
    CPrimitive* mp_next;  // +0x04: in the new or the drawing list
    uint32_t m_flags;     // +0x08: 0x100 on the list of new ones (the PC class's: 1 made, 2 set up, 4 ready)
    uint32_t m_blendMode; // +0x0C: a checksum (0x54628ED7 to start)
    int m_ownsVertices;   // +0x10: 1: listed (and the vertices its own)
    uint8_t unknown14;    // +0x14
    bool m_ready;         // +0x15: readied (Ready): drawn by RenderPrimitives
    uint8_t unknown16[6];
    uint32_t unknown1C;   // +0x1C

    // 0x0049C660: the fields cleared, the vtable, blend mode 0x54628ED7, `own` kept - 1: on the list of new ones.
    // Returns this.
    CPrimitive* Construct(int own);
    // 0x0049C6A0: the vtable; off its list when listed.
    void DestroyBody();
    // 0x0049C790 (vtable slot 0): destroyed, and freed with flags & 1. Returns this.
    CPrimitive* Destroy(unsigned int flags);
    // 0x0049C6C0: the platform's end (vtable slot 4), then readied.
    void Ready();
    // 0x0049C6D0: drawn (vtable slot 5) when readied.
    void RenderIfReady();
    // 0x0049C6E0: drawn from arrays (vtable slot 6) when readied.
    void DrawIfReady(const float* positions, const uint32_t* colours, const float* uvs, const float* normals);

    // The virtuals' bodies (vtable slots 1 - 35, in CPcPrimitive's order): each prints "STUB: plat_..." - the getters
    // return 0.
    void SetTexture(const uint8_t* texture, int unused1, int unused2);  // 0x0049C390
    void SetColour(uint32_t rgba);                                       // 0x0049C3A0
    void Init(uint32_t layoutFlags, int primitiveType, int count);      // 0x0049C3B0
    void End();                                                          // 0x0049C3C0
    void Render();                                                       // 0x0049C3D0
    void Draw(const float* positions, const uint32_t* colours, const float* uvs, const float* normals); // 0x0049C3E0
    void SetPosition(int index, float x, float y, float z);             // 0x0049C3F0
    void SetX(int index, float x);                                       // 0x0049C400
    void SetY(int index, float y);                                       // 0x0049C410
    void SetZ(int index, float z);                                       // 0x0049C420
    void SetNormal(int index, float x, float y, float z);               // 0x0049C430
    void SetNormalX(int index, float x);                                 // 0x0049C440
    void SetNormalY(int index, float y);                                 // 0x0049C450
    void SetNormalZ(int index, float z);                                 // 0x0049C460
    void SetVertexColour(int index, uint32_t rgba);                      // 0x0049C470
    void SetUV(int index, float u, float v);                             // 0x0049C480
    void SetU(int index, float u);                                       // 0x0049C490
    void SetV(int index, float v);                                       // 0x0049C4A0
    float GetX(int index);                                               // 0x0049C4B0
    float GetY(int index);                                               // 0x0049C4D0
    float GetZ(int index);                                               // 0x0049C4F0
    float GetNormalX(int index);                                         // 0x0049C510
    float GetNormalY(int index);                                         // 0x0049C530
    float GetNormalZ(int index);                                         // 0x0049C550
    uint32_t GetVertexColour(int index);                                 // 0x0049C570
    float GetU(int index);                                               // 0x0049C590
    float GetV(int index);                                               // 0x0049C5B0
    void Translate(float x, float y, float z);                          // 0x0049C5D0
    void TranslateX(float x);                                            // 0x0049C5E0
    void TranslateY(float y);                                            // 0x0049C5F0
    void TranslateZ(float z);                                            // 0x0049C600
    void ScrollUV(float u, float v);                                     // 0x0049C610
    void ScrollU(float u);                                               // 0x0049C620
    void ScrollV(float v);                                               // 0x0049C630
    int Compare(const CPrimitive* other);                                // 0x0049C640
};
static_assert(offsetof(CPrimitive, m_flags) == 0x08 && offsetof(CPrimitive, m_ownsVertices) == 0x10 &&
                  offsetof(CPrimitive, m_ready) == 0x15 && offsetof(CPrimitive, unknown1C) == 0x1C &&
                  sizeof(CPrimitive) == 0x20,
              "layout must match the original");

// 0x0049C6F0 (thiscall; linker-folded: a jump through vtable slot 35 - the primitives' Compare, a texture's slot 35
// for the texture code - its arguments left to the callee): a naked jump.
void CallVirtual35Stub();
// 0x0049C7D0 (cdecl): `primitive` put first on the list of new ones (flag 0x100).
void __cdecl LinkPrimitive(CPrimitive* primitive);
// 0x0049C7F0 (cdecl): `primitive` taken off its list (the new ones with flag 0x100, else the drawing list). Not
// there and not first, the original goes past the end of the list (crashes) - kept.
void __cdecl UnlinkPrimitive(CPrimitive* primitive);
// 0x0049C840 (cdecl): every new primitive moved into the drawing list in order (inserted before the first it
// Compares below 0 with), then each readied one drawn.
void __cdecl RenderPrimitives();
// 0x0049C700 (cdecl): a primitive render state kept in 0x00700EF8 (1: bits 0-2 - the depth test -, 2: bits 3-7, 4:
// bits 8-9) and passed on to the PC's 0x004C5E40.
void __cdecl SetPrimitiveState(int state, int value);

} // namespace Nx
