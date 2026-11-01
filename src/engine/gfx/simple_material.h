#pragma once

#include <cstdint>

// A one-pass material made on the fly (Nx engine, PC) and its pixel shader - for things drawn with a texture by
// checksum and a blend mode by name; and two chunk-skipping helpers of the scene loaders.
namespace render {

struct MaterialRef;

struct SimpleMaterial {
    MaterialRef* material; // (made by make_new 0x004D1A90)
    void* shader;

    // 0x004D9B80: one pass with the texture `texture_checksum` (from the texture dictionary 0x00700F20, if there),
    // blend mode `blend_checksum` (blend_mode_from_name) with `fixedAlpha`, a random sort key; its shader.
    SimpleMaterial(uint32_t unused0, uint32_t unused1, uint32_t texture_checksum, uint32_t blend_checksum,
                   int fixed_alpha, uint32_t unused5, uint32_t unused6);
    // 0x004D9CD0: the destructor's body: the material destroyed and freed.
    ~SimpleMaterial();
    SimpleMaterial(const SimpleMaterial&) = delete;
    SimpleMaterial& operator=(const SimpleMaterial&) = delete;
};

// 0x004D9CF0: a blend mode's number from its name's checksum: diffuse / none 0, add 1, fixadd 2, subtract / sub 3,
// fixsubtract / fixsub 4, blend 5, fixblend 6, modulate 7, fixmodulate 8, brighten 9, fixbrighten 10; others 0.
uint32_t blend_mode_from_name(uint32_t checksum);

// 0x004DA6D0: skip a chunk in a file (its id, then its size, then that many bytes). Returns 0.
int skip_file_chunk(void* file);
// 0x004DA710: skip a chunk in memory (*data moves past the id, the size and the contents). Returns 0.
int skip_memory_chunk(uint8_t** data);

} // namespace render
