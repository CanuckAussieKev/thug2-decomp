// The engine's texture dictionary and the loaded dictionaries (see texture_dict.h).

#include "engine/gfx/texture_dict.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <array>
#include <iterator>
#include <string_view>
#include <span>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_texture.h"
#include "engine/gfx/pc_texture.h"
#include "engine/lists/key_map.h"
#include "engine/sys/startup_init.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"

namespace render {

namespace {
// NOT ORIGINAL grouping - texture_dict.cpp's state (the original's globals). Made on first use, never destroyed
// (texture_dict_state()).
struct TextureDictState {
    std::array<int, 8> key_multipliers_{{2, 3, 4, 6, 12, 8, 24, -1}};
    // The loaded dictionaries, by checksum (a static hash table).
    lists::KeyMap loaded_tex_dicts_{lists::KeyMap::Unconstructed{}}; // (the exe makes it with KeyMap(4) at start-up)
};

TextureDictState& texture_dict_state()
{
    static core::NoDestroy<TextureDictState> state;
    return state.get();
}

} // namespace

// The game's texture dictionaries: the textures every level shares, and the particles'.
TextureSet* g_game_texture_dict;
TextureSet* g_particle_texture_dict;

namespace {

lists::KeyMap& loaded_tex_dicts() { return texture_dict_state().loaded_tex_dicts_; }

// The multipliers find_texture tries the name's CRC with (0x0067E520: 2, 3, 4, 6, 12, 8, 24, then -1).
const std::array<int, 8>& key_multipliers() { return texture_dict_state().key_multipliers_; }

// The CRC of what follows a name's last slash or backslash.
uint32_t filename_key(const char* name)
{
    const std::string_view full(name);
    const size_t slash = full.find_last_of("\\/");
    const size_t start = slash == std::string_view::npos ? 0 : slash + 1;
    return checksums::hash_from_text(std::next(name, static_cast<std::ptrdiff_t>(start)));
}

// (0x0048C1B0: an empty call - compiled-out debug code - each unload makes before the platform's)
void debug_before_unload() {}

// A new texture: kept on flushes or not, and added under its checksum.
BitmapTexture* insert_texture(TextureSet* dict, BitmapTexture* texture, bool permanent)
{
    if (texture) {
        texture->pin_resident(permanent);
        const uint32_t key = texture->checksum_;
        dict->textures_->store_entry(key, texture);
    }
    return texture;
}

// A new table of 32 buckets (none without memory).
gsl::owner<lists::KeyMap*> new_texture_table()
{
    void* memory = memory::alloc(sizeof(lists::KeyMap));
    return memory ? memory::construct_in<lists::KeyMap>(memory, 5) : nullptr;
}

} // namespace

// 0x0049F830
TextureSet::TextureSet(uint32_t checksum, bool make_table)
    : checksum_(checksum), share_count_(1), textures_(make_table ? new_texture_table() : nullptr)
{
}

// 0x0049F8F0
TextureSet::TextureSet(const char* name, bool make_table)
    : checksum_(checksums::hash_from_text(name)), share_count_(1), textures_(make_table ? new_texture_table() : nullptr)
{
}

// 0x0049FC70
TextureSet::~TextureSet()
{
    drop_textures(true);
    if (lists::KeyMap* table = textures_) {
        if (table->buckets_) {
            table->empty_table();
            memory::dealloc(table->buckets_);
            table->buckets_ = nullptr;
        }
        memory::dealloc(table);
    }
}

// 0x0049F540
BitmapTexture* TextureSet::fetch_skin(uint32_t checksum)
{
    if (!textures_)
        return nullptr;
    return static_cast<BitmapTexture*>(textures_->fetch_entry(checksum, true));
}

// 0x0049F560
BitmapTexture* TextureSet::get_texture_by_name(const char* name)
{
    const uint32_t checksum = filename_key(name);
    if (!textures_)
        return nullptr;
    return static_cast<BitmapTexture*>(textures_->fetch_entry(checksum, true));
}

// 0x0049F660 (the original copies the name into 512 bytes unbounded)
BitmapTexture* TextureSet::find_texture(const char* name)
{
    auto find = [this](uint32_t checksum) -> BitmapTexture* {
        for (const int multiplier : key_multipliers()) {
            if (multiplier <= 0)
                break;
            const uint32_t key = static_cast<uint32_t>(multiplier) * checksum;
            BitmapTexture* texture = textures_ ? static_cast<BitmapTexture*>(textures_->fetch_entry(key)) : nullptr;
            if (texture)
                return texture;
        }
        return nullptr;
    };
    if (BitmapTexture* texture = find(checksums::hash_from_text(name)))
        return texture;
    std::array<char, 0x200> other{};
    text::format_to(other, "%s", name);
    const size_t length = std::strlen(other.data());
    if (length > 4 && length - 4 + 14 <= sizeof(other)) // "_auto32m0.png" and its 0 in place of the last four
        std::memcpy(std::span(other).subspan(length - 4).data(), "_auto32m0.png", 14);
    return find(checksums::hash_from_text(other.data()));
}

// 0x0049F9B0
BitmapTexture* TextureSet::ingest_skin(const char* name, uint32_t a1, uint32_t a2, bool permanent)
{
    memory::push_mem_profile(name);
    auto* texture = load_texture_file(name, a1, a2);
    insert_texture(this, texture, permanent);
    memory::pop_mem_profile();
    return texture;
}

// 0x0049FA20
BitmapTexture* TextureSet::read_texture_buffer(const uint8_t* data, uint32_t size, uint32_t checksum, uint32_t a3,
                                               uint32_t a4, bool permanent, uint32_t a6)
{
    memory::push_mem_profile("texture from buffer"); // "texture from buf"
    auto* texture = load_texture_data(data, size, checksum, a3, a4, a6);
    insert_texture(this, texture, permanent);
    memory::pop_mem_profile();
    return texture;
}

// 0x0049FAA0
BitmapTexture* TextureSet::create_texture(uint32_t checksum, uint32_t width, uint32_t height, uint32_t bits_per_pixel,
                                          uint32_t a4, uint32_t a5, bool permanent)
{
    memory::push_mem_profile("created texture"); // "created texture"
    auto* texture = make_texture(checksum, width, height, bits_per_pixel, a4, a5);
    insert_texture(this, texture, permanent);
    memory::pop_mem_profile();
    return texture;
}

// 0x0049F500
bool TextureSet::unload_texture(BitmapTexture* texture)
{
    const uint32_t key = texture->checksum_;
    debug_before_unload();
    if (!destroy_texture(texture))
        return false;
    textures_->erase_entry(key);
    return true;
}

// 0x0049F5C0 (the next texture found before this one goes)
void TextureSet::drop_textures(bool all)
{
    lists::KeyMap* table = textures_;
    if (!table)
        return;
    table->iter_bucket_ = -1;
    table->iter_item_ = nullptr;
    if (!table->step_iterator() || !table->iter_item_)
        return;
    auto* texture = static_cast<BitmapTexture*>(table->iter_item_->value);
    while (texture) {
        BitmapTexture* next = nullptr;
        if (textures_->step_iterator() && textures_->iter_item_)
            next = static_cast<BitmapTexture*>(textures_->iter_item_->value);
        if (all || !(texture->flag_bits_ & kTexturePermanent)) {
            const uint32_t key = texture->checksum_;
            gsl::owner<BitmapTexture*> owned = memory::owned_element(texture); // (the dictionary's)
            delete owned;
            textures_->erase_entry(key);
        }
        texture = next;
    }
}

// 0x0049F800
bool TextureSet::swap_texture(const char* name, BitmapTexture* source)
{
    BitmapTexture* texture = find_texture(name);
    if (!texture)
        return false;
    return texture->copy_from(source);
}

// 0x0049FB20 (no texture loaded: the original reads through null)
bool TextureSet::replace_texture_from_file(const char* name, const char* file)
{
    BitmapTexture* texture = find_texture(name);
    if (!texture)
        return false;
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap_);
    memory::push_mem_profile(file);
    auto* loaded = load_texture_file(file, 0u, 0u);
    insert_texture(this, loaded, true);
    memory::pop_mem_profile();
    const bool replaced = texture->copy_from(loaded);
    const uint32_t key = loaded->checksum_;
    debug_before_unload();
    if (destroy_texture(loaded))
        textures_->erase_entry(key);
    memory::Hub::get_pull()->leave_heap_scope();
    return replaced;
}

// 0x0049FBE0
BitmapTexture* TextureSet::copy_texture(uint32_t checksum, BitmapTexture* source)
{
    auto* texture = duplicate_texture(checksum, source);
    if (texture) {
        texture->checksum_ = checksum;
        textures_->store_entry(checksum, texture);
    }
    return texture;
}

// 0x0049FC20
BitmapTexture* TextureSet::copy_texture_with_overlay(uint32_t checksum, BitmapTexture* source, BitmapTexture* overlay,
                                                     uint32_t argument)
{
    auto* texture = duplicate_texture(checksum, source);
    if (texture) {
        texture->checksum_ = checksum;
        textures_->store_entry(checksum, texture);
        texture->get_overlay(overlay, static_cast<int>(argument));
    }
    return texture;
}

// --- the loaded dictionaries --------------------------------------------------------------------------------------

// 0x0049F010
TextureSet* find_tex_dict(uint32_t checksum)
{
    return static_cast<TextureSet*>(loaded_tex_dicts().fetch_entry(checksum));
}

// 0x0049F0C0
TextureSet* load_tex_dict(const char* path, int /*level*/, uint32_t offset, uint32_t /*a3*/, uint32_t /*a4*/)
{
    std::array<char, 0x80> file{};
    text::format_to(file, "%s.%s", path, platform_suffix());
    memory::push_mem_profile(path);
    const uint32_t key = checksums::hash_from_text(path) + offset;
    TextureSet* dict = find_tex_dict(key);
    if (dict) {
        ++dict->share_count_;
    } else {
        // (the original passes level, offset, a3 and a4 too: the PC's reads the name alone)
        gsl::owner<TextureSet*> loaded = load_tex_dict_file(file.data()); // (the loaded table's)
        dict = loaded;
        dict->checksum_ = key;
        loaded_tex_dicts().store_entry(key, dict);
    }
    memory::pop_mem_profile();
    return dict;
}

// 0x0049F050
TextureSet* load_tex_dict_from_memory(uint32_t checksum, const uint8_t* data, uint32_t size, uint32_t /*a3*/,
                                      uint32_t offset, uint32_t /*a5*/, uint32_t /*a6*/)
{
    memory::push_mem_profile("TEX_data_buffer"); // "TEX_data_buffer"
    const uint32_t key = checksum + offset;
    TextureSet* dict = find_tex_dict(key);
    if (!dict) {
        // (the original passes size, a3, offset, a5 and a6 too: the PC's reads the checksum and data alone - and,
        // not original, the size as its reader's bound)
        gsl::owner<TextureSet*> loaded = load_tex_dict_memory(key, data, size); // (the loaded table's)
        dict = loaded;
        dict->checksum_ = key;
        loaded_tex_dicts().store_entry(key, dict);
    }
    memory::pop_mem_profile();
    return dict;
}

// 0x0049F180 (the skin flag is pushed as a whole word in the original - stack noise above its byte)
TextureSet* load_model_textures(const char* name, uint32_t offset, uint32_t a4)
{
    std::array<char, 0x200> base{};
    const bool skin = engine::split_skin_name(name, base.data());
    std::array<char, 0x200> file{};
    text::format_to(file, "%s.tex", base.data());
    return load_tex_dict(file.data(), 0, offset, skin, a4);
}

// 0x0049EFD0
bool unload_tex_dict(TextureSet* dict)
{
    if (--dict->share_count_ != 0)
        return false;
    const uint32_t key = dict->checksum_;
    loaded_tex_dicts().erase_entry(key);
    gsl::owner<TextureSet*> owned = memory::owned_element(dict); // (the loaded table's)
    delete owned; // (through its vtable)
    return true;
}

// 0x0049EF90 (the original sprintfs unbounded)
void tex_dict_file_name(char* out, const char* name)
{
    text::format_to(std::span(out, 256), "%s.%s", name, platform_suffix());
}

// 0x0049EFB0
void drop_particle_textures(bool all) { g_particle_texture_dict->drop_textures(all); }

} // namespace render

namespace render {

namespace {

// A table (2 buckets) nothing uses but its static initialiser and exit function.
lists::KeyMap& spare_tex_dict_table()
{
    static lists::KeyMap spare_tex_dict_table{lists::KeyMap::Unconstructed{}};
    return spare_tex_dict_table;
}

// The loaded dictionary named `name` (one more use of it), or a new one, made and listed.
TextureSet* shared_tex_dict(const char* name)
{
    const uint32_t checksum = checksums::hash_from_text(name);
    if (TextureSet* dict = find_tex_dict(checksum)) {
        ++dict->share_count_;
        return dict;
    }
    gsl::owner<PcTexDict*> dict = create_tex_dict(checksum);
    loaded_tex_dicts().store_entry(checksum, dict);
    TextureSet* const listed = dict; // (the loaded table's)
    return listed;
}

} // namespace

// 0x00640BF0 (a static initialiser): the loaded dictionaries' table, 16 buckets.
void construct_loaded_tex_dicts() { new (&texture_dict_state().loaded_tex_dicts_) lists::KeyMap(4); }

// 0x00640C10, 0x00640C60 (static initialisers): the dictionaries of the sprites and the particles.
void set_up_game_texture_dict() { g_game_texture_dict = shared_tex_dict("sprite"); }
void set_up_particle_texture_dict() { g_particle_texture_dict = shared_tex_dict("particle"); }

// 0x00640CB0 (a static initialiser)
void construct_spare_tex_dict_table() { new (&spare_tex_dict_table()) lists::KeyMap(1); }

} // namespace render
