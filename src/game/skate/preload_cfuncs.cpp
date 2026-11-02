// Script commands preloading what the level's nodes and the pedestrians use (see preload_cfuncs.h).

#include "game/skate/preload_cfuncs.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <array>
#include <span>

#include "engine/assets/resource_registry.h"
#include "engine/core/checksums.h"
#include "engine/core/mem.h"
#include "engine/core/str.h"
#include "engine/core/text_store.h"
#include "engine/maths/random.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/script/cscript.h"
#include "engine/script/nodes.h"
#include "engine/script/param_struct.h"
#include "game/skate/profile_cfuncs.h"
#include "game/skate/skate_module.h"
#include "platform/code_address.h"
#include "game/skate/skate_module_calls.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/model.h"
#include "engine/gfx/model_appearance.h"
#include "engine/gfx/model_builder.h"
#include "engine/gfx/model_utils.h"
#include "game/skate/cas_options.h"
#include "engine/objects/model_component.h"
#include "engine/core/down_cast.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>

namespace commands {

namespace {

using namespace skate_calls;

constexpr uint32_t kClass = 0x12B4E660;           // Class
constexpr uint32_t kType = 0x7321A8D6;            // type
constexpr uint32_t kVehicle = 0xE47F1B79;         // vehicle
constexpr uint32_t kGameObject = 0xEF59C100;      // GameObject
constexpr uint32_t kPedestrian = 0xA0DFAC98;      // Pedestrian
constexpr uint32_t kModel = 0x286A8D26;           // model
constexpr uint32_t kSecondModel = 0x545F8172;     // (name unknown) a pedestrian's second model
constexpr uint32_t kProfile = 0x7EA855F0;         // profile
constexpr uint32_t kAllowReplaceTex = 0x6C37FDC7; // AllowReplaceTex
constexpr uint32_t kNoCollision = 0x0BF29BC0;     // nocollision
constexpr uint32_t kNone = 0x806FFF30;            // none
constexpr uint32_t kNoRandom = 0xF6F8E158;        // no_random
constexpr uint32_t kNoPedestrians = 0x2CAA92ED;   // (name unknown) a global integer: no pedestrians preloaded
constexpr uint32_t kPreloadModelFromAppearance = 0x53E82A70;                   // preload_model_from_appearance
constexpr uint32_t kPart = 0xB6F08F39;                                         // part
constexpr uint32_t kDescId = 0x4BB2084E;                                       // desc_id
constexpr uint32_t kPickPermitted = 0x355F9467;                                // allowed_to_pick
constexpr uint32_t kAllowRandom = 0xF1E3CD22;                                  // allow_random
constexpr uint32_t kAlreadySelected = 0x92BDDFD9;                              // already_selected
constexpr uint32_t kShuffleIndex = 0x4B833E64;                                 // random_index
constexpr uint32_t kLevelSpecific = 0x3598BF7D;                                // level_specific
constexpr uint32_t kExcludeFromLevels = 0x94D12F97;                            // exclude_from_levels
constexpr uint32_t kCasArtist = 0x2A648514;                                    // cas_artist
constexpr uint32_t kNum = 0x23BC5091;                                          // num
constexpr uint32_t kLight = 0x94E5A308, kTan = 0x85AAF0D8, kDark = 0xE4834204; // light, tan, dark
constexpr uint32_t kCarType = 0x5C0312EB;                                      // OBJECT_TYPE_CAR
constexpr uint32_t kObjReplaceTexture = 0x83F9BE15;                            // Obj_ReplaceTexture
// The capture-the-flag nodes' types (not preloaded in a game of one player).
constexpr auto kFlagTypes = std::to_array<uint32_t>({
    0xBEBB41F0, 0x836284A5, 0xC3EBE05E, 0x4F8FF239,  // Flag_Red(_Base), Flag_Green(_Base)
    0x8CCA938A, 0x041E5BCF, 0xC2AF1EB1, 0xED3AD7FE}); // Flag_Blue(_Base), Flag_Yellow(_Base)
// The pedestrians' CAS part lists preloaded (unless no_random).
constexpr auto kPedPartLists = std::to_array<uint32_t>({0xF56A8CFB, 0x26D93E89, 0xAF8139ED, 0xE5DC27BA,  // ped_m_head, torso, legs,
                                      0x9FAD7C38, 0x24DE6A73, 0xC546C92E, 0x02159819}); // accessories; ped_f_...

// Nx (not rewritten yet): a new model (0x0048BFA0, cdecl) and its deletion (0x0048C3C0); a model loaded from a file
// (0x004935E0, eight arguments); its geom count (0x00492E40) and a geom's texture dictionary (0x004946C0).
gsl::owner<render::RenderModel*> spawn_render_model(int)
{
    return render::spawn_model();
} // (0x0048BFA0 jumps to 0x004B29C0; its argument isn't used)
void delete_model(gsl::owner<render::RenderModel*> model) { delete model; }
void ingest_mesh(render::RenderModel* model, const char* file, int a, int b, int c, uint32_t d, int e, int f, int g)
{
    model->add_file_geom(file, static_cast<uint32_t>(a), b != 0, c, d != 0, e != 0, f != 0, g != 0);
}
// 0x004879D0 (cdecl): the model file's path for `name` with `extension` (a String, returned).
text::Chars model_path(const char* name, const char* extension) { return gfx::make_model_path(name, extension); }
// 0x0048BFE0 (cdecl): a model file's base name (without its extension) into `base`.
void get_base_name(const char* name, char* base) { render::engine::split_skin_name(name, base); }

// 0x00565BB0: the skate module's: whether a node isn't for this game (its own 0x68910AC6 test).
bool skip_node(script::Struct* node) { return modules::Skate::should_be_absent(node) != 0; }

// 0x005AAAA0: every part of a global CAS part list with allow_random
// preloaded (its mesh kept in memory).
void preload_part_list(uint32_t list) { commands::preload_meshes(list); }

// The CAS option lists (no source yet): 0x00485CF0 the option `descriptor_id` of a slot's global list; 0x00485D70 the
// options of a slot's list matching a filter (0: any of them allowed to pick) into `out`, their count in `count`.
script::Struct* customise_choice(uint32_t part, uint32_t descriptor_id, bool assert)
{
    return objects::find_cas_option(part, descriptor_id, assert);
}
void matching_options(uint32_t part, uint32_t filter, std::span<script::Struct*> out, int* count)
{
    objects::get_matching_cas_options(part, filter, out, count);
}

script::Struct* get_element(script::Array* array, uint32_t index)
{
    return array->struct_at(index);
}
// A pedestrian part chosen at random: allowed, not to be picked again, its place among the random choices.
void get_choose(script::Struct* option, int index)
{
    option->put_int(kAllowRandom, 1);
    option->put_int(kPickPermitted, 0);
    option->put_int(kShuffleIndex, index);
}

} // namespace

// 0x005AAFA0: PreloadModels - every vehicle and game object node's model (not "none"; a flag's only in a game of
// more than one player) loaded and dropped, keeping its data: AllowReplaceTex passed on; unless nocollision, its
// .col file too; a vehicle's .qb too.
bool cfunc_preload_models(script::Struct*, script::Script*)
{
    memory::push_mem_profile("PreLoadModels");
    level_nodes::start_node_walk(0);
    for (script::Struct* node = level_nodes::get_next_node(); node; node = level_nodes::get_next_node()) {
        if (skip_node(node))
            continue;
        uint32_t node_class = 0, type = 0;
        node->read_hash_value(kClass, &node_class, 0);
        node->read_hash_value(kType, &type, 0);
        if (!is_multiplayer()) {
            bool flag = false;
            for (const uint32_t flag_type : kFlagTypes)
                flag = flag || type == flag_type;
            if (flag)
                continue;
        }
        if (node_class != kVehicle && node_class != kGameObject)
            continue;
        const char* model = nullptr;
        if (!node->fetch_text(kModel, &model, 0))
            continue;
        node->fetch_text(kModel, &model, 1);
        if (_stricmp("none", model) == 0)
            continue;
        text::Chars path;
        path = model_path(model, ".mdl");
        gsl::owner<render::RenderModel*> nx_model = spawn_render_model(1);
        ingest_mesh(nx_model, path.c_str(), 0, 1, 0, node->has_bare_flag(kAllowReplaceTex), 0, 1, 0);
        if (!node->has_bare_flag(kNoCollision)) {
            const char* file = path.c_str();
            std::array<char, 0x100> name{};
            std::memcpy(name.data(), file, std::strlen(file) + 1); // (no bound: as the original)
            text::to_lower_text(name.data());
            if (std::strstr(name.data(), ".mdl")) {
                get_base_name(file, name.data());
                text::append_unbounded(name.data(), ".col");
                assets::ResourceRegistry* assets = assets::asset_manager();
                const bool permanent = assets->get_permanent_by_default();
                assets->fetch_or_load_resource(name.data(), 8, 0, checksums::hash_from_text(file), false, false, permanent,
                                               nullptr, nullptr);
            }
        }
        if (node_class == kVehicle) {
            std::array<char, 0x100> script{};
            get_base_name(path.c_str(), script.data());
            text::append_unbounded(script.data(), ".qb");
            assets::ResourceRegistry* assets = assets::asset_manager();
            const bool permanent = assets->get_permanent_by_default();
            assets->fetch_or_load_resource(script.data(), 0xA, 0, 0, false, false, permanent, nullptr, nullptr);
        }
        delete_model(nx_model);
    }
    level_nodes::end_node_walk();
    memory::pop_mem_profile();
    return true;
}

// 0x005AB300: PreloadPedestrians [no_random] - not in a network game, nor with the global 0x2CAA92ED set (false):
// the pedestrians' CAS part lists (unless no_random), every pedestrian node's .skin and second (.mdl) models, and the
// model its profile's appearance builds (preload_model_from_appearance) loaded and dropped.
bool cfunc_preload_pedestrians(script::Struct* params, script::Script*)
{
    if (online::g_online_hub->in_net_game())
        return false;
    if (script::fetch_int_value(kNoPedestrians))
        return false;
    memory::push_mem_profile("Random Ped Parts");
    if (!params->has_bare_flag(kNoRandom)) {
        for (const uint32_t list : kPedPartLists)
            preload_part_list(list);
    }
    memory::pop_mem_profile();
    memory::push_mem_profile("Unique Ped Parts");
    level_nodes::start_node_walk(0);
    for (script::Struct* node = level_nodes::get_next_node(); node; node = level_nodes::get_next_node()) {
        uint32_t node_class = 0;
        node->read_hash_value(kClass, &node_class, 0);
        if (node_class != kPedestrian)
            continue;
        const char* model = nullptr;
        if (node->fetch_text(kModel, &model, 0) && checksums::hash_from_text(model) != kNone) {
            text::Chars path;
            path = model_path(model, ".skin");
            gsl::owner<render::RenderModel*> nx_model = spawn_render_model(1);
            ingest_mesh(nx_model, path.c_str(), 0, 1, 8, 0, 0, 1, 0);
            delete_model(nx_model);
        }
        if (node->fetch_text(kSecondModel, &model, 0)) {
            text::Chars path;
            path = model_path(model, ".mdl");
            gsl::owner<render::RenderModel*> nx_model = spawn_render_model(1);
            ingest_mesh(nx_model, path.c_str(), 0, 1, 8, 0, 0, 1, 0);
            delete_model(nx_model);
        }
        uint32_t profile_name = 0;
        if (!node->read_hash_value(kProfile, &profile_name, 0))
            continue;
        script::Struct* profile = script::read_sub_struct(profile_name);
        if (!profile)
            continue;
        gsl::owner<render::RenderModel*> nx_model = spawn_render_model(1);
        // The pedestrian's appearance and a model builder (destroyed at the end of the loop: the builder first).
        gfx::ModelLook appearance;
        appearance.read_in(profile, false);
        gfx::ModelAssembler builder(true, 8);
        builder.get_build(&appearance, nx_model, nullptr, kPreloadModelFromAppearance);
        delete_model(nx_model);
    }
    level_nodes::end_node_walk();
    memory::pop_mem_profile();
    return true;
}

// 0x005AB690: PreselectRandomPedestrians part=<slot> [num=<n>] - the slot's CAS options marked for the random
// pedestrians: those allowed here (not no_random; level_specific to this level, or not excluded from it) are
// candidates; the pedestrian nodes' own parts are taken; then one option of any kind (failing that one light, one
// tan and one dark) and the rest at random among the candidates, n (the candidates) in all, numbered by
// random_index. (No candidate left while choosing: the original loops for ever - kept.)
bool cfunc_preselect_random_pedestrians(script::Struct* params, script::Script*)
{
    uint32_t part = 0; // (uninitialised in the original when missing)
    params->read_hash_value(kPart, &part, 1);
    script::Array* list = script::fetch_list(part);
    const uint32_t size = list->extent_;
    const uint32_t level = modules::Skate::sole()->pending_level_; // (the level the parts are chosen for)
    for (uint32_t i = 0; i < size; ++i) {
        script::Struct* option = get_element(list, i);
        option->erase_module(kPickPermitted);
        option->put_int(kAllowRandom, 0);
        option->put_int(kAlreadySelected, 0);
        option->put_int(kShuffleIndex, -1);
    }
    int candidates = 0;
    for (uint32_t i = 0; i < size; ++i) {
        script::Struct* option = get_element(list, i);
        script::Struct* levels = nullptr;
        bool candidate = false;
        if (option->has_entry_named(kNoRandom)) {
            if (option->read_sub_struct(kLevelSpecific, &levels, 0))
                script::dump_to_log(option);
            candidate = false;
        } else if (option->read_sub_struct(kLevelSpecific, &levels, 0)) {
            candidate = levels->has_entry_named(level);
        } else if (option->read_sub_struct(kExcludeFromLevels, &levels, 0)) {
            candidate = !levels->has_entry_named(level);
        } else {
            candidate = true;
        }
        if (candidate) {
            ++candidates;
            option->put_int(kPickPermitted, 1);
        } else {
            option->put_int(kPickPermitted, 0);
            script::fetch_int_value(kCasArtist); // (read and not used)
        }
    }
    level_nodes::start_node_walk(0);
    for (script::Struct* node = level_nodes::get_next_node(); node; node = level_nodes::get_next_node()) {
        uint32_t node_class = 0;
        node->read_hash_value(kClass, &node_class, 0);
        if (node_class != kPedestrian)
            continue;
        uint32_t profile_name = 0;
        if (!node->read_hash_value(kProfile, &profile_name, 0))
            continue;
        script::Struct* profile = script::read_sub_struct(profile_name);
        if (!profile)
            continue;
        script::Struct* slot = nullptr;
        if (!profile->read_sub_struct(part, &slot, 0))
            continue;
        uint32_t descriptor_id = 0;
        if (!slot->read_hash_value(kDescId, &descriptor_id, 0))
            continue;
        script::Struct* option = customise_choice(part, descriptor_id, false);
        if (!option || option->has_entry_named(kNoRandom))
            continue;
        script::fetch_int_value(kCasArtist); // (read and not used)
        option->put_int(kAllowRandom, 1);
        option->put_int(kPickPermitted, 1);
        option->put_int(kAlreadySelected, 1);
    }
    level_nodes::end_node_walk();
    int wanted = candidates;
    params->fetch_int_value(kNum, &wanted, 0);
    if (wanted > candidates)
        wanted = candidates;
    std::array<script::Struct*, 64> matching{};
    int count = 0;
    matching_options(part, 0, matching, &count);
    if (count) {
        get_choose(core::at(matching, maths::rnd(count)), wanted - 1);
    } else {
        matching_options(part, kLight, matching, &count);
        get_choose(core::at(matching, maths::rnd(count)), wanted - 1);
        --wanted;
        matching_options(part, kTan, matching, &count);
        get_choose(core::at(matching, maths::rnd(count)), wanted - 1);
        --wanted;
        matching_options(part, kDark, matching, &count);
        get_choose(core::at(matching, maths::rnd(count)), wanted - 1);
    }
    --wanted;
    for (int n = 0; n < wanted; ++n) {
        int allowed = 0;
        script::Struct* option = nullptr;
        for (bool again = true; again; again = !allowed) {
            option = get_element(list, static_cast<uint32_t>(maths::rnd(static_cast<int>(size))));
            option->fetch_int_value(kPickPermitted, &allowed, 1);
        }
        get_choose(option, n);
    }
    return true;
}

// 0x005ABB20: ReplaceCarTextures ... - every car's model component whose geoms use a texture dictionary not met yet
// (in this walk) runs Obj_ReplaceTexture with the parameters.
bool cfunc_replace_car_textures(script::Struct* params, script::Script* script)
{
    auto* objects = objects::g_composite_registry.get();
    // (the original keeps 32 on its stack without a bound: more would overwrite its frame; here the rest aren't kept)
    std::array<const render::TextureSet*, 32> seen{};
    int seen_count = 0;
    for (objects::ObjectModule* component = objects->find_first_module_of_kind(kModel); component;
         component = component->next_of_type_) {
        if (component->entity_->kind_tag_ != kCarType)
            continue;
        render::RenderModel* model = core::down_cast<objects::MeshInstanceModule>(component)->mesh_instance_.get();
        bool added = false;
        for (int i = 0; i < model->fetch_geom_total(); ++i) {
            const render::TextureSet* textures = model->get_geom_textures(i);
            bool found = false;
            for (int k = 0; k < seen_count; ++k) {
                if (core::at(seen, k) == textures)
                    found = true;
            }
            if (!found && seen_count < 32) {
                core::at(seen, seen_count++) = textures;
                added = true;
            }
        }
        if (added)
            component->invoke_method_by_name(kObjReplaceTexture, params, script);
    }
    return true;
}

} // namespace commands
