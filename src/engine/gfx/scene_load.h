#pragma once

#include <cstddef>
#include <cstdint>
#include "engine/memory/slab_bank.h"
#include <gsl/pointers>
#include <array>
#include "base/maths/mat4.h"

// The PC engine's scene and mesh loaders (Nx engine's platform calls, cdecl): a scene's geometry from a file
// or from memory - its materials (with the texture dictionary's textures), its sectors (each with a PC geom), the
// mesh list's bounds and its hierarchy - and the meshes (a model's scene and its texture dictionary) made from them.
namespace render {


class SubMesh;
class TextureSet;
struct PcScene;
struct VertexArena;


// 0x004B3490: a scene from geometry in memory: after a 12-byte header, the materials (with `textures`' (+0x0C)
// textures), the sector count, each sector (a PC geom of its own; kept when it loads), the hierarchy (count and
// nodes). The scene's hash table has 2^n entries for twice the sectors (n 1-12). `with_super_sectors` / `flag_8d` go
// to the scene's +0x8C / +0x8D, `mesh_flag` to its mesh list's +0x40.
// The scene's `size` bytes are read through scene_format::MemoryStream (scene_format.h): a damaged record ends it.
gsl::owner<PcScene*> load_scene_from_memory(const uint8_t* data, size_t size, const TextureSet* textures, bool with_super_sectors,
                                bool flag_8d, bool mesh_flag);
// 0x004B36D0: the same from a file (opened "rb"; null if there's none) - the header's three words read and not
// used; the sectors' vertices packed into `arena` when there is one (not for billboards).
gsl::owner<PcScene*> load_scene_from_file(const char* name, const TextureSet* textures, bool with_super_sectors, bool flag_8d,
                              bool mesh_flag, VertexArena* arena);
// 0x004B3A20: a mesh (0xC8 bytes) from a scene file: the scene named by the file's name checksum (+0x84) with its
// texture dictionary (+0x88), its sectors set up (0x0049C910) and the 2D clip turned off.
gsl::owner<SubMesh*> load_mesh_file(const char* name, TextureSet* textures);
// 0x004B3AD0: a mesh from geometry in memory, the scene named by `checksum` as a decimal number and a line feed; the skin data, when
// there is any, loaded into the mesh first (0x004B7EB0). `size` / `skin_size`: their bytes (the original's `size`
// went unused).
gsl::owner<SubMesh*> load_mesh_from_memory(uint32_t checksum, const uint8_t* data, size_t size, const void* skin, size_t skin_size,
                              TextureSet* textures);

} // namespace render
