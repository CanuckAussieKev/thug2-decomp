#pragma once

#include <cstdint>

// The PC grass (Nx engine): a sector mesh whose material asks for grass (pass 0's flag 0x8000000: animated, else
// layered by the material's +0x18 layer count and +0x14 height) gets it when the sector loads (CPcSector's loaders).
namespace Nx {

struct CPcGeom;
struct sMaterial;
struct sMesh;

// 0x004D08A0: an animated grass material: two passes (the textures 0x00782350 / 0x00782370, the second bump
// mapped), sorted late (1501), its first pass's texture cycling through 17 frames (0x00782390) at 30 a second.
void __cdecl SetUpAnimatedGrassMaterial(sMaterial* material);
// 0x004D0B60: animated grass on the mesh: the sky's texture 0x96D0BF08 copied in as the second texture, a second
// texture coordinate set made room for (the vertex buffer widened) and filled from the positions (x and z, at two
// scales), the material set up, flags 0x402, the pixel shader 0x007CA924. False without the texture.
bool __cdecl MakeAnimatedGrass(CPcGeom* geom, sMesh* mesh);
// 0x004D0D30: the mesh's grass: animated, or up to five layers - copies of it (on the bottom-up heap), each lifted a
// further step, with its "Grass-Grass_Layer<n>" material (sorted after the ground) and texture coordinates from the
// positions, added to the geom; the mesh then flagged 0x80. False without a material or layers.
bool __cdecl MakeGrass(CPcGeom* geom, sMesh* mesh);

} // namespace Nx
