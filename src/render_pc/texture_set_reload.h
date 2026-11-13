#pragma once

// NOT ORIGINAL - a texture set switch's reload on the PC (engine/gfx/texture_sets.h): a loaded dictionary's textures read
// again from its file - which the data overlay now serves from the new set - and swapped in place.
namespace render {

class TextureSet;

// The file read into a dictionary of its own (load_tex_dict_file: the same loader, surface maps and all); then, for each
// of its textures the live dictionary has under the same checksum, the two TextureRefs' images swapped - sizes, levels,
// format bytes and device texture: the live TextureRef (where every material, splat and projection points) now holds
// the new device texture, the read one the old. Left as they are: a texture being edited (a TextureEdit holds its
// image), a render target. The read dictionary is then deleted - each TextureRef's destructor releases the old device
// texture (on Vulkan its image retired once the frames using it are done, its ray-tracing slot freed) and unsets it
// from the stages. Returns the textures swapped; -1 when the file gave no textures.
int reload_dictionary(TextureSet* dictionary, const char* file);

} // namespace render
