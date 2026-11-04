#include "engine/gfx/world_scene.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/texture_dict.h"

#include <cstddef>
#include <cstdint>
#include <array>

#include "base/core/checksums.h"
#include "base/core/dbg.h"
#include "base/core/str.h"
#include "editor/level_editor_layout.h"
#include "engine/script/params.h"
#include <gsl/pointers>
#include "engine/memory/mem.h"
#include "base/core/format.h"

namespace render {

constexpr std::size_t kTexturePathSize = 128;
constexpr std::size_t kLoadDescriptionBytes = 8;

namespace {

constexpr uint32_t kScene = 0x26861025;              // scene
constexpr uint32_t kIsDictionary = 0x3D559E7D;       // is_dictionary
constexpr uint32_t kIsSky = 0xB5BD28D8;              // is_sky
constexpr uint32_t kIsNet = 0x417149C8;              // is_net
constexpr uint32_t kNoSuperSectors = 0x8943DBD8;     // no_supersectors
constexpr uint32_t kEnableSuperSectors = 0x9E6EA452; // add_super_sectors
constexpr uint32_t kTexDict = 0xDC8116B2;            // tex_dict

// 0x0049F0C0 (cdecl): a texture dictionary from its file; the 1 marks a level's.
void* read_texture_dictionary(const char* path, int level, int unknown2, int unknown3, int unknown4)
{
    return load_tex_dict(path, level, static_cast<uint32_t>(unknown2), static_cast<uint32_t>(unknown3),
                         static_cast<uint32_t>(unknown4));
}

// 0x0048CCB0 (cdecl): the engine's scene load (the geometry file, then the scene registered under its name and
// put in the scene list); returns the scene.
void* load_scene_file(const char* name, void* tex_dict, bool with_super_sectors, bool is_sky, bool is_dictionary,
                      bool is_net)
{
    return load_scene(name, static_cast<TextureSet*>(tex_dict), with_super_sectors, is_sky, is_dictionary,
                      static_cast<int>(is_net));
}

} // namespace

// 0x00445130
bool cfunc_load_scene(script::Struct* params, script::Script* /*script*/)
{
    const bool is_dictionary = params->has_bare_flag(kIsDictionary);
    bool is_sky = params->has_bare_flag(kIsSky);
    const bool is_net = params->has_bare_flag(kIsNet);
    bool with_super_sectors = !params->has_bare_flag(kNoSuperSectors);
    const char* name = nullptr;
    params->fetch_text(kScene, &name, 1);
    debug::printf("Loading Scene %s ...", name);

    // (the original sprintf's into the 128 bytes unchecked; the empty string is a platform suffix)
    std::array<char, kTexturePathSize> texture_path{};
    text::format_to(texture_path, "levels\\%s\\%s%s.tex", name, name, "");
    void* tex_dict = read_texture_dictionary(texture_path.data(), 1, 0, 0, 0);

    // A structure describing the load is built here and never used (left from an older interface).
    alignas(4) std::array<uint8_t, kLoadDescriptionBytes> memory{};
    gsl::owner<script::Struct*> description{memory::construct_in<script::Struct>(memory.data())};
    description->put_name_key(checksums::Name{kTexDict}, checksums::hash_from_text(texture_path.data()));
    if (text::find_caseless({.haystack = name, .needle = "_sky"}) || is_sky) {
        is_sky = true;
        with_super_sectors = false;
        description->put_name_key(checksums::Name{0U}, kIsSky);
    }
    if (is_dictionary) {
        with_super_sectors = false;
        description->put_name_key(checksums::Name{0U}, kIsDictionary);
    } else if (with_super_sectors) {
        description->put_name_key(checksums::Name{0U}, kEnableSuperSectors);
    }
    if (is_net) {
        description->put_name_key(checksums::Name{0U}, kIsNet);
    }

    load_scene_file(name, tex_dict, with_super_sectors, is_sky, is_dictionary, is_net);
    debug::printf("... done\n");
    description->get_clear();

    // Not original - the level editor's saved layouts (level_editor_layout.h): put back once the level is up.
    frontend::level_editor::layout::on_scene_loaded(name, is_net);
    return true;
}

} // namespace render
