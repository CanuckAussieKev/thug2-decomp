// Script commands of the engine, 0x00445000-0x00445B30: terrain sounds, models, scenes and their collision,
// animations and other assets, the system configuration and the sun's lens flare (see cfuncs_scene.h).

#include "engine/script/cfuncs_scene.h"

#include <cstdint>

#include "engine/assets/resource_registry.h"
#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/text_store.h"
#include "engine/file/async.h"
#include "engine/gfx/lens_flare.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/sound/surface_kind.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/model.h"
#include "engine/gfx/model_utils.h"
#include "engine/gfx/texture_dict.h"
#include "engine/sys/game_config.h"
#include "engine/core/owner.h"

namespace commands {

using script::Script;
using script::Struct;

int g_config_ntsc{1};

namespace {

constexpr uint32_t kTerrain = 0x3789AC4E;               // "Terrain"
constexpr uint32_t kName = 0xA1DC81F9;                  // "name"
constexpr uint32_t kAllowReplaceTex = 0x6C37FDC7;       // "AllowReplaceTex"
constexpr uint32_t kScene = 0x26861025;                 // "scene"
constexpr uint32_t kIsNet = 0x417149C8;                 // "is_net"
constexpr uint32_t kAdd = 0x02E58C18;                   // "Add"
constexpr uint32_t kSpecKey = 0x4D20D794;               // "descChecksum"
constexpr uint32_t kTargetKey = 0x91CC92C8;             // "refChecksum"
constexpr uint32_t kParent = 0xC2719FB0;                // "parent"
constexpr uint32_t kAsync = 0x90E07C79;                 // "async"
constexpr uint32_t kDesc = 0xF44A53AB;                  // "desc"
constexpr uint32_t kAssertOnMissingAssets = 0x25DC7904; // "AssertOnMissingAssets"
constexpr uint32_t kUsePip = 0x23028E29;                // "use_pip"
constexpr uint32_t kLanguage = 0x2B248E4A;              // "language"
constexpr uint32_t kEnglish = 0xD37CFDFF;               // "English"
constexpr uint32_t kFrench = 0x508A31A1;                // "French"
constexpr uint32_t kGerman = 0x5DCBBF5E;                // "German"
constexpr uint32_t kItalian = 0xA8469630;               // "Italian"
constexpr uint32_t kSpanish = 0xCBE70ACB;               // "Spanish"
constexpr uint32_t kCd = 0xBA297025;                    // "CD"
constexpr uint32_t kNotCd = 0x2B6D7B32;                 // "NotCD"
constexpr uint32_t kGotExtraMemory = 0x38D23FD4;        // "GotExtraMemory"
constexpr uint32_t kNoExtraMemory = 0xAF11B50F;         // "NoExtraMemory"
constexpr uint32_t kPal = 0x6CAD3928;                   // "Pal"
constexpr uint32_t kNtsc = 0x86137A88;                  // "NTSC"
constexpr uint32_t kFramerate = 0xEE2BC65F;             // "framerate"
constexpr uint32_t kTheta = 0xF4E775F6;                 // "theta"
constexpr uint32_t kPhi = 0xCD0576EE;                   // "phi"
constexpr uint32_t kRadius = 0xC48391A5;                // "radius"
constexpr uint32_t kSize = 0x083FDB95;                  // "size"
constexpr uint32_t kTexture = 0x7D99F28D;               // "texture"
constexpr uint32_t kCoronaTexture = 0xEB1CCC76;         // "corona_texture"
constexpr uint32_t kLensFlareData = 0x04CBD07F;         // "lens_flare_data"

// The system configuration (Config): the language (2 English, 3 French, 5 German - see str.cpp), running from the CD,
// the extra memory of a development kit, the TV standard (0 PAL, 1 NTSC) and the frame rate.
int& config_language() { return game_config::g_language; }
bool& config_cd() { return game_config::g_from_cd; }
bool& config_got_extra_memory() { return g_got_extra_memory; }
int& config_ntsc() { return g_config_ntsc; }
int& config_framerate() { return game_config::g_frame_rate; }
bool& config_bootstrap() { return game_config::g_config_bootstrap; }
// The configuration's names (cdecl): the hardware, the display type for a frame rate, the language, the territory.
const char* platform_label_text() { return game_config::get_platform_label(); }
const char* video_mode_label_text(int) { return game_config::get_screen_mode_label(); } // (the frame rate isn't read)
const char* locale_text() { return game_config::get_locale_label(); }
const char* region_label_text() { return game_config::get_region_label(); }

// The engine's scenes (Nx, cdecl): by name (0x0048C750: the name's checksum looked up, 0x0048C700), the first in the
// scene table, every one unloaded, one unloaded (0x0048C690; 1: its collision too), reloaded, geometry added to one
// (0x0048CDD0: the scene's name and the added file's). A scene's texture dictionary is at +0x88.
void* look_up_scene(const char* name) { return render::get_scene_by_name(name); }
void* first_scene() { return render::get_main_scene(); }
void unload_all_scenes() { render::unload_all_scenes(); }
void unload_scene(void* scene, int flag)
{
    render::engine::unload_scene(static_cast<render::WorldScene*>(scene), flag);
}
void quick_reload_scene(const char* name) { render::reload_scene(name); }
void add_to_scene(const char* scene, const char* add) { render::add_scene(scene, add); }
void unload_tex_dict(void* textures) { render::unload_tex_dict(static_cast<render::TextureSet*>(textures)); }
// A scene's collision (thiscall): loaded from the scene's collision file (0x0049DF70), or added from another one
// (0x0049DFD0).
void load_scene_collision(void* scene, const char* name, bool is_net)
{
    static_cast<render::WorldScene*>(scene)->load_collision_file(name, is_net);
}
void add_scene_collision(void* scene, const char* add) { static_cast<render::WorldScene*>(scene)->load_add_scene(add); }

// Models (Nx): one made (0x0048BFA0, a jump to 0x004B29C0), its model file loaded (0x004935E0, thiscall), destroyed
// again (0x0048C3C0).
memory::owner<render::RenderModel*> spawn_model(int) { return render::spawn_model(); } // (0x0048BFA0, a jump to 0x004B29C0)
void load_model_file(render::RenderModel* model, const char* file, bool allow_replace_tex)
{
    model->add_file_geom(file, 0, true, 0, allow_replace_tex, false, true, false);
}
void destroy_model(render::RenderModel* model) { delete model; }

} // namespace

// 0x00445000
bool __cdecl cfunc_load_terrain_sounds(Struct* params, Script*)
{
    uint32_t terrain;
    if (!params->read_hash_value(kTerrain, &terrain, 0))
        return false;
    surface_kind::load_terrain_sounds(terrain);
    return true;
}

// 0x00445030
bool __cdecl cfunc_preload_model(Struct* params, Script*)
{
    const char* name;
    params->fetch_text(kName, &name, true);
    render::RenderModel* model = spawn_model(1);
    bool allow_replace_tex = params->has_bare_flag(kAllowReplaceTex);
    text::Chars file;
    {
        // <name>.mdl (0x004879D0 returns a String: made in `storage`)
        alignas(text::Chars) uint8_t storage[sizeof(text::Chars)];
        auto* path = gfx::make_model_path(reinterpret_cast<text::Chars*>(storage), name, ".mdl");
        file = *path;
        path->~Chars();
    }
    load_model_file(model, file.c_str(), allow_replace_tex);
    destroy_model(model);
    return true;
}

// 0x00445120
bool __cdecl cfunc_unload_all_level_geometry(Struct*, Script*)
{
    unload_all_scenes();
    return true;
}

// 0x00445320 (the scene isn't checked: a name that isn't loaded crashes, as in the original)
bool __cdecl cfunc_load_collision(Struct* params, Script*)
{
    const char* name = nullptr;
    params->fetch_text(kScene, &name, false);
    if (name) {
        bool is_net = params->has_bare_flag(kIsNet);
        load_scene_collision(look_up_scene(name), name, is_net);
    }
    return true;
}

// 0x00445380
bool __cdecl cfunc_add_collision(Struct* params, Script*)
{
    const char* scene = nullptr;
    const char* add = nullptr;
    params->fetch_text(kScene, &scene, false);
    params->fetch_text(kAdd, &add, false);
    if (scene && add)
        add_scene_collision(look_up_scene(scene), add);
    return true;
}

// 0x004453F0
bool __cdecl cfunc_quick_reload(Struct* params, Script*)
{
    const char* name = nullptr;
    params->fetch_text(kScene, &name, false);
    if (name)
        quick_reload_scene(name);
    return true;
}

// 0x00445430
bool __cdecl cfunc_unload_anim(Struct* params, Script*)
{
    assets::ResourceRegistry* assets = assets::asset_manager();
    const char* name = nullptr;
    params->fetch_text(kName, &name, true);
    uint32_t desc = 0;
    uint32_t reference = 0;
    params->read_hash_value(kSpecKey, &desc, 1);
    if (!params->read_hash_value(kTargetKey, &reference, 0))
        reference = static_cast<uint32_t>(assets->unknown18_); // the reference checksum (SetReferenceChecksum)
    uint32_t checksum = desc ? reference + desc : checksums::hash_from_text(name);
    if (assets::Resource* asset = assets->obtain_loaded_item(checksum, 0))
        assets->discard_loaded_item(asset);
    return true;
}

// 0x004454E0 (the script's params are passed on as the load's extra value)
bool __cdecl cfunc_load_asset(Struct* params, Script*)
{
    assets::ResourceRegistry* assets = assets::asset_manager();
    uint32_t parent = 0;
    const char* name;
    if (!params->fetch_text(kName, &name, false))
        params->fetch_text(0u, &name, true);
    params->read_hash_value(kParent, &parent, false);
    bool async = params->has_bare_flag(kAsync);
    uint32_t desc = 0;
    params->read_hash_value(kDesc, &desc, false);
    if (!assets->fetch_or_load_resource(name, 0, desc, parent, async, false, assets->get_permanent_by_default(),
                                        nullptr, static_cast<int>(reinterpret_cast<uintptr_t>(params))))
        script::fetch_int_value(kAssertOnMissingAssets); // (an assert on it compiled out)
    return true;
}

// 0x004455A0
bool __cdecl cfunc_load_anim(Struct* params, Script*)
{
    assets::ResourceRegistry* assets = assets::asset_manager();
    const char* name;
    uint32_t desc = 0;
    params->fetch_text(kName, &name, false);
    params->read_hash_value(kSpecKey, &desc, true);
    bool async = params->has_bare_flag(kAsync);
    bool use_pip = params->has_bare_flag(kUsePip);
    return assets->load_anim(name, desc, static_cast<uint32_t>(assets->unknown18_), async, use_pip);
}

// 0x00445640
bool __cdecl cfunc_load_skeleton(Struct* params, Script*)
{
    assets::ResourceRegistry* assets = assets::asset_manager();
    const char* name;
    uint32_t checksum;
    params->fetch_text(kName, &name, true);
    params->read_hash_value(0u, &checksum, 1);
    assets->fetch_or_load_resource(name, 0, checksum, 0, false, false, assets->get_permanent_by_default(), nullptr, 0);
    return true;
}

// 0x004456A0
bool __cdecl cfunc_set_reference_checksum(Struct* params, Script*)
{
    assets::ResourceRegistry* assets = assets::asset_manager();
    uint32_t checksum = 0; // (uninitialised in the original when neither is given; the getter asserts)
    if (!params->read_hash_value(0u, &checksum, 0))
        params->fetch_int_value(0u, reinterpret_cast<int*>(&checksum), true);
    assets->set_unknown18(static_cast<int>(checksum));
    return true;
}

// 0x004456F0
bool __cdecl cfunc_set_default_permanent(Struct* params, Script*)
{
    assets::ResourceRegistry* assets = assets::asset_manager();
    int permanent = 0; // (uninitialised in the original without one; the getter asserts)
    params->fetch_int_value(0u, &permanent, true);
    assets->set_permanent_by_default(permanent != 0);
    return true;
}

// 0x00445720
bool __cdecl cfunc_add_scene(Struct* params, Script*)
{
    const char* scene = nullptr;
    const char* add = nullptr;
    params->fetch_text(kScene, &scene, false);
    params->fetch_text(kAdd, &add, false);
    if (scene && add) {
        memory::Hub* manager = memory::Hub::sole();
        manager->enter_heap_scope(manager->debug_heap_);
        add_to_scene(scene, add);
        memory::Hub::sole()->leave_heap_scope();
    }
    return true;
}

// 0x004457B0
bool __cdecl cfunc_unload_scene(Struct* params, Script*)
{
    const char* name = nullptr;
    params->fetch_text(kScene, &name, false);
    bool found = false;
    if (name) {
        void* scene = look_up_scene(name);
        if (scene)
            found = true;
        else
            scene = first_scene();
        if (scene) {
            void* textures = *reinterpret_cast<void**>(static_cast<uint8_t*>(scene) + 0x88);
            unload_scene(scene, 1);
            if (textures)
                unload_tex_dict(textures);
        }
    }
    return found;
}

// 0x00445820 (Italian picks the German setting and Spanish the French one)
bool __cdecl cfunc_set_config(Struct* params, Script*)
{
    uint32_t language = 0;
    params->read_hash_value(kLanguage, &language, 0);
    switch (language) {
    case kItalian:
    case kGerman:
        config_language() = 5;
        break;
    case kFrench:
    case kSpanish:
        config_language() = 3;
        break;
    case kEnglish:
        config_language() = 2;
        break;
    default:
        break;
    }
    if (params->has_bare_flag(kCd)) {
        if (!config_cd())
            file::async_init(); // (passed "DATA", unused)
        config_cd() = true;
    }
    if (params->has_bare_flag(kNotCd)) {
        // (with the CD set, a compiled-out call: 0x005480A0)
        config_cd() = false;
    }
    if (params->has_bare_flag(kGotExtraMemory))
        config_got_extra_memory() = true;
    if (params->has_bare_flag(kNoExtraMemory))
        config_got_extra_memory() = false;
    if (params->has_bare_flag(kPal))
        config_ntsc() = 0;
    if (params->has_bare_flag(kNtsc))
        config_ntsc() = 1;
    int framerate = 60;
    if (params->fetch_int_value(kFramerate, &framerate, 0))
        config_framerate() = framerate;
    return true;
}

// 0x00445980
bool __cdecl cfunc_print_config(Struct*, Script*)
{
    debug::printf("\n");
    debug::printf("System Configuration:\n\n");
    debug::printf("Hardware       = %s\n", platform_label_text());
    // (the original passes no number: the %d reads the frame rate it passed to the name's function, still on the
    // stack)
    int framerate = config_framerate();
    debug::printf("Display Type   = %s, %d fps\n", video_mode_label_text(framerate), framerate);
    debug::printf("Language       = %s\n", locale_text());
    debug::printf("Territory      = %s\n", region_label_text());
    debug::printf("CD             = %s\n", config_cd() ? "True" : "False");
    debug::printf("GotExtraMemory = %s\n", config_got_extra_memory() ? "True" : "False");
    debug::printf("\n");
    return true;
}

// 0x00445A30
bool __cdecl cfunc_bootstrap(Struct*, Script*) { return config_bootstrap(); }

// 0x00445A40
bool __cdecl cfunc_init_sun(Struct* params, Script*)
{
    float theta = 0.0f;
    float phi = 0.0f;
    float radius = 0.0f;
    float size = 0.0f;
    uint32_t texture = 0;
    uint32_t corona_texture = 0;
    uint32_t lens_flare_data = 0;
    params->fetch_float(kTheta, &theta, 1);
    params->fetch_float(kPhi, &phi, 1);
    params->fetch_float(kRadius, &radius, 1);
    params->fetch_float(kSize, &size, 1);
    params->read_hash_value(kTexture, &texture, 1);
    params->read_hash_value(kCoronaTexture, &corona_texture, 1);
    params->read_hash_value(kLensFlareData, &lens_flare_data, 1);
    sun::prepare_all(theta, phi, radius, size, texture, corona_texture, lens_flare_data);
    return true;
}

// 0x00445B30
bool __cdecl cfunc_update_lens_flare(Struct* params, Script* script)
{
    sun::update_flare(params, script);
    return true;
}

} // namespace commands
