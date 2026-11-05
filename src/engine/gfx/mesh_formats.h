#pragma once

#include <cstdint>

// The materials' blend modes and the meshes' vertex formats as the scene data stores them (a pass's blend word, a
// mesh's flexible vertex format): what the renderer draws them with (render_d3d9/render_state.h) and what the level
// editor reads of a material or a mesh.
namespace render {

// choose_blend_mode's modes the pixel shaders and the fixed-function states know (with kBlendModeAlpha, 5, and
// kBlendModePrimitive below): the Fix modes take the pass colour's alpha, the others the texture's; 0xC and 0xD blend
// with the pass before.
constexpr int kBlendModeFixBlend = 6;
constexpr int kBlendModeModulate = 7;
constexpr int kBlendModeFixModulate = 8;
constexpr int kBlendModeBrighten = 9;
constexpr int kBlendModeFixBrighten = 0xA;
constexpr int kBlendModeVertexTexAlpha = 0xB;
constexpr int kBlendModeLerpPrevious = 0xC;
constexpr int kBlendModeLerpFromPrevious = 0xD;
constexpr int kBlendModeMultiplyColour = 0xF;
constexpr int kBlendModeAlpha = 5;        // choose_blend_mode: the alpha blend the 2D drawing uses
constexpr int kBlendModePrimitive = 0x11; // choose_blend_mode: what the primitives' state is set up with
constexpr int kBlendModeMask = 0xFFFFFF;  // a pass's blend mode in its blend word (the top byte: its fixed alpha, flags)

// The flexible vertex format's bits the renderer and the editor read (D3DFVF_*).
constexpr uint32_t kFvfXyz = 0x002U;
constexpr uint32_t kFvfNormal = 0x010U;
constexpr uint32_t kFvfDiffuse = 0x040U;
constexpr uint32_t kFvfSpecular = 0x080U;
constexpr uint32_t kFvfTex1 = 0x100U; // one to four sets of texture coordinates
constexpr uint32_t kFvfTex2 = 0x200U;
constexpr uint32_t kFvfTex3 = 0x300U;
constexpr uint32_t kFvfTex4 = 0x400U;

} // namespace render
