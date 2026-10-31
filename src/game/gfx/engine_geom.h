#pragma once

#include <cstddef>
#include <cstdint>

namespace Maths {
class BBox;
class Mat4;
class Vec4;
}

// The engine's geom (Engine3D::CGeom - the platform-independent base of the PC geom, CPcGeom in geom.h, its first 0x14
// bytes): what a geom keeps for itself - how it was made, its collision faces, a copy of its vertex colours, whether it
// is multicoloured - and the base's platform calls, all stubs ("STUB: Plat...", vtable 0x00648EC8) that CPcGeom
// replaces. Its public calls are the dispatchers below (one jump to a slot each, linker-folded: other engine classes
// call the same addresses for their own slots).
namespace Engine3D {

class CGeom {
public:
    // 0x0048DD60: construct - nothing kept. Returns this.
    CGeom* MakeNew();
    // 0x0048E130: the destructor's body - the colours' copy freed (not cleared).
    void DestroyBody();
    // 0x0048E220 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CGeom* Teardown(unsigned int flags);

    // 0x0048DD80: a copy (virtual [2]) - made with `instance` 1, else 2; given this one's collision faces (when it has
    // some), no colours (the low byte of `instance` tested, its whole word passed on). The copy (or null).
    CGeom* Replicate(int instance, void* scene);
    // 0x0048DDC0: a copy for a model (virtual [1]) - made with `instance` 1, else 2; no colours. The copy (or null).
    CGeom* CloneForModel(int instance, void* model);
    // 0x0048DDF0: made an instance (1) of a mesh for a model, multicoloured or not (a jump to virtual [3] - the
    // callee's arguments: `multicolour` is its third).
    bool InitInstance(void* mesh, void* model, int multicolour);
    // 0x0048DE10: the colour (virtual [7]) into `out`. Returns out.
    uint32_t* FetchTint(uint32_t* out);
    // 0x0048DE90 (not a Ghidra function): the bounding sphere (virtual [17]) into `out`. Returns out.
    Maths::Vec4* FetchBoundSphere(Maths::Vec4* out);
    // 0x0048DF10 (folded with other classes' setters of the word at +8): the collision faces.
    void ChooseTriSource(void* faces);
    // 0x0048E150: the vertex colours (virtual [36] of them) copied to `out` - from a copy kept (virtual [38], made
    // the first time with MemSys::AllocBlock on the front end's heap).
    void FetchTints(uint32_t* out);

    // The base's platform calls (virtual [3] - [47]; each prints its "STUB: ..." and does nothing):
    bool PlatInitInstance(void* mesh, void* model, int multicolour);                 // 0x0048DA90 [3]: true
    uint32_t PlatGetChecksum();                                                      // 0x0048DAB0 [5]: 0
    void PlatSetColor(uint32_t rgba);                                                // 0x0048DAC0 [6]
    uint32_t* PlatGetColor(uint32_t* out);                                           // 0x0048E040 [7]: 0
    void PlatClearColor();                                                           // 0x0048DAD0 [8]
    bool PlatSetMaterialColor(uint32_t checksum, int pass, uint32_t rgba);           // 0x0048DAE0 [9]: false
    uint32_t* PlatGetMaterialColor(uint32_t* out, uint32_t checksum, int pass);      // 0x0048E070 [10]: 0
    void PlatSetVisibility(uint32_t mask);                                           // 0x0048DB00 [11]
    uint32_t PlatGetVisibility();                                                    // 0x0048DB10 [12]: 0
    void PlatSetActive(bool active);                                                 // 0x0048DB20 [13]
    bool PlatIsActive();                                                             // 0x0048DB30 [14]: false
    Maths::BBox* PlatGetBoundingBox();                                                 // 0x0048DB40 [15]: a static empty box
    void PlatSetBoundingSphere(const Maths::Vec4* sphere);                           // 0x0048DC30 [16]
    Maths::Vec4* PlatGetBoundingSphere(Maths::Vec4* out);                            // 0x0048E0A0 [17]: 0, radius 10000
    void PlatSetWorldPosition(const Maths::Vec4* position);                          // 0x0048DBB0 [18]
    Maths::Vec4* PlatGetWorldPosition();                                             // 0x0048E0D0 [19]: a static vector
    void PlatSetOrientation(const Maths::Mat4* orientation);                         // 0x0048DBC0 [20]
    Maths::Mat4* PlatGetOrientation();                                               // 0x0048E1C0 [21]: a static matrix
    void PlatRotateY(int turns);                                                     // 0x0048DBD0 [22]
    void PlatSetTransform(const Maths::Mat4* transform);                             // 0x0048DBE0 [23]
    Maths::Mat4* PlatGetTransform();                                                 // 0x0048E1F0 [24]: a static matrix
    void PlatSetScale(const Maths::Vec4* scale);                                     // 0x0048DBF0 [25]
    Maths::Vec4* PlatGetScale(Maths::Vec4* out);                                     // 0x0048E100 [26]: 0
    void PlatSetModelLights(void* lights);                                           // 0x0048DC00 [27]
    bool PlatRenderSetup(const Maths::Mat4* transform, const Maths::Mat4* bones, int count, int a, int b); // 0x0048DC10 [28]: true
    bool PlatHidePolys(uint32_t mask);                                               // 0x0048DC40 [31]: true
    uint32_t PlatGetNumRenderVerts();                                                // 0x0048DC60 [36]: 0
    void PlatGetRenderVerts(Maths::Vec4* out);                                       // 0x0048DC70 [37] (not a Ghidra function)
    uint32_t PlatGetNumRenderPolys();                                                // 0x0048DC80 [33]: 0
    uint32_t PlatGetNumRenderBasePolys();                                            // 0x0048DC90 [34]: 0 (not a Ghidra function)
    void PlatGetRenderColors(uint32_t* out);                                         // 0x0048DCA0 [38] (not a Ghidra function)
    void PlatSetRenderVerts(const Maths::Vec4* in);                                  // 0x0048DCB0 [39]
    void PlatSetRenderColors(const uint32_t* in);                                    // 0x0048DCC0 [40]
    void PlatSetUVWibbleParams(float uVel, float uAmp, float uFreq, float uPhase, float vVel, float vAmp, float vFreq,
                               float vPhase);                                        // 0x0048DCE0 [42]
    void PlatUseExplicitUVWibble(int on);                                            // 0x0048DCF0 [43]
    bool PlatSetMaterialUVWibbleOffsets(uint32_t checksum, int pass, float u, float v); // 0x0048DD10 [44]: false
    void PlatSetUVWibbleOffsets(float u, float v);                                   // 0x0048DD00 [45]
    bool PlatSetUVMatrix(uint32_t checksum, int pass, const Maths::Mat4* matrix);    // 0x0048DD30 [46]: false
    // ([29] / [30] are Stubs::IgnoreArguments2, [41] Stubs::IgnoreArguments3, [47] Stubs::ReturnTrue2 - folded.)

    void** m_vtable;
    int m_cloneKind;             // +0x04: 1 an instance / a copy made with `instance`, 2 a copy without
    void* mp_collTriData;        // +0x08: the collision faces
    uint32_t* mp_renderColours;  // +0x0C: the vertex colours' copy (FetchTints)
    uint8_t m_multicolour;       // +0x10 (InitInstance's third argument's low byte)
};
static_assert(offsetof(CGeom, mp_renderColours) == 0xC && offsetof(CGeom, m_multicolour) == 0x10,
              "layout must match the original");

// The dispatchers: `MOV EAX,[ECX]; JMP [EAX + 4 * slot]` - the callee's arguments, whatever the class (the geom's
// slots named; the same addresses serve other engine classes: e.g. 0x0048D060 is the font's string size and the
// geom's WipeTint, 0x0048DE80 a texture's slot 15). Naked, registered unchecked.
void VirtualSlot7Jump();   // 0x0048E4B0
void VirtualSlot8Jump();   // 0x0048D060 (the geom's WipeTint, the font's ReadStringParam)
void VirtualSlot9Jump();   // 0x0048DE30 ChangeMaterialTint
void VirtualSlot10Jump();  // 0x0048E4C0
void VirtualSlot11Jump();  // 0x0048DE40 ToggleShown
void VirtualSlot12Jump();  // 0x0048DE50 FetchShown
void VirtualSlot13Jump();  // 0x0048DE60 ChangeLive
void VirtualSlot14Jump();  // 0x0048DE70 IsOperative
void VirtualSlot15Jump();  // 0x0048DE80 FetchBounds
void VirtualSlot16Jump();  // 0x0048DEB0 ChangeBoundSphere
void VirtualSlot18Jump();  // 0x0048DEC0 PlaceAtAbsolutePoint
void VirtualSlot19Jump();  // 0x0048DED0 GetAbsolutePoint
void VirtualSlot20Jump();  // 0x0048DEE0 ChangeFacing
void VirtualSlot22Jump();  // 0x0048DEF0 RotateY
void VirtualSlot25Jump();  // 0x0048DF00 ChangeScale
void VirtualSlot27Jump();  // 0x0048DF20 SetModelLights
void VirtualSlot28Jump();  // 0x0048DF30 SetBoneMatrices
void VirtualSlot29Jump();  // 0x0048DF40 CopyBones
void VirtualSlot30Jump();  // 0x0048DF50 SetBones
void VirtualSlot31Jump();  // 0x0048DF60 BreakStrips
void VirtualSlot36Jump();  // 0x0048DF70 FetchVertexTally
void VirtualSlot37Jump();  // 0x0048DF80 GetVertices
void VirtualSlot39Jump();  // 0x0048DF90 SetVertices
void VirtualSlot40Jump();  // 0x0048DFA0 ChangeColors
void VirtualSlot42Jump();  // 0x0048DFB0 SetUVWibbleParams
void VirtualSlot43Jump();  // 0x0048DFC0 EnableExplicitUVWibble
void VirtualSlot44Jump();  // 0x0048DFE0 SetMaterialUVWibbleOffsets
void VirtualSlot45Jump();  // 0x0048DFD0 SetUVWibbleOffsets
void VirtualSlot46Jump();  // 0x0048DFF0 ChooseUvMatrix
void VirtualSlot47Jump();  // 0x0048E000 CreateUvAnimParams
void VirtualSlot48Jump();  // 0x0048E010 ResetTextureAnims
void VirtualSlot49Jump();  // 0x0048E020
void VirtualSlot50Jump();  // 0x0048E030

} // namespace Nx
