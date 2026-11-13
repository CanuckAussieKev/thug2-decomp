// NOT ORIGINAL - a texture set switch's reload on the PC (texture_set_reload.h).

#include "render_pc/texture_set_reload.h"

#include <cstdint>
#include <utility>
#include <vector>

#include <gsl/pointers>

#include "engine/gfx/engine_texture.h"
#include "engine/gfx/texture_dict.h"
#include "engine/lists/key_map.h"
#include "render_pc/bitmap_texture.h"
#include "render_pc/pc_grass.h"
#include "render_pc/pc_texture.h"

namespace render {

namespace {

constexpr uint32_t kTextureEdited = 1; // PcTexture's flag: its image is a TextureEdit's

// The two images' contents exchanged - everything but the checksum (the same), the list they're on and what only
// render targets have.
void swap_image(TextureImage& a, TextureImage& b)
{
    std::swap(a.width, b.width);
    std::swap(a.height, b.height);
    std::swap(a.image_width_, b.image_width_);
    std::swap(a.image_height_, b.image_height_);
    std::swap(a.levels, b.levels);
    std::swap(a.depth, b.depth);
    std::swap(a.unknowne_, b.unknowne_);
    std::swap(a.unknownf_, b.unknownf_);
    std::swap(a.texture, b.texture);
    std::swap(a.palette, b.palette);
}

// A table's textures (iterated from the start).
std::vector<BitmapTexture*> textures_of(lists::KeyMap* table)
{
    std::vector<BitmapTexture*> out;
    if (table == nullptr) {
        return out;
    }
    table->set_iter_bucket(-1);
    table->set_iter_item(nullptr);
    while (table->step_iterator() && (table->iter_item() != nullptr)) {
        if (auto* texture = static_cast<BitmapTexture*>(table->iter_item()->value)) {
            out.push_back(texture);
        }
    }
    return out;
}

bool swappable(const BitmapTexture* texture)
{
    if ((texture->flag_bits() & kTextureEdited) != 0U) {
        return false; // (being edited: the edit holds its image and pixels)
    }
    const TextureRef* image = image_of(texture);
    return image != nullptr && !image->render_target_ && image->surface == nullptr && image->texture != nullptr;
}

} // namespace

int reload_dictionary(TextureSet* dictionary, const char* file)
{
    if (dictionary == nullptr || file == nullptr || dictionary->textures() == nullptr) {
        return -1;
    }
    gsl::owner<PcTexDict*> fresh = load_tex_dict_file(file);
    if (fresh == nullptr) {
        return -1;
    }
    const std::vector<BitmapTexture*> read = textures_of(fresh->textures());
    int swapped = 0;
    for (BitmapTexture* from : read) {
        BitmapTexture* to = dictionary->fetch_skin(from->checksum());
        if (to == nullptr || !swappable(to) || !swappable(from)) {
            continue;
        }
        TextureRef* live = image_of(to);
        TextureRef* old = image_of(from);
        swap_image(*live, *old);
        grass_texture_swapped(old->texture, *live); // (the grass's copy of a sky texture's record)
        ++swapped;
    }
    // (the old images go with it: each TextureRef's destructor releases its device texture and unsets it where set)
    delete fresh;
    return read.empty() ? -1 : swapped;
}

} // namespace render
