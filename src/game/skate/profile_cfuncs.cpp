// Script commands (CFunctions) and helpers 0x005AA930-0x005AAEEF: profiles and the career's menu numbers (see
// profile_cfuncs.h).

#include "game/skate/profile_cfuncs.h"

#include <cstdint>
#include <cstdio>
#include <array>

#include "engine/core/manager.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "game/park_editing/goal_editor.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/model.h"
#include "engine/gfx/model_appearance.h"
#include "engine/core/raw_view.h"
#include <gsl/pointers>

namespace commands {

std::array<SwapSlot, 5> g_swap_table;

namespace {

using script::Script;
using script::Struct;

constexpr int kSwapSlots = 5;
SwapSlot* swap_table() { return g_swap_table.data(); }

objects::ProfileRoster* get_profiles() { return objects::g_profile_roster.get(); }
objects::RiderStoryProgress* get_career() { return modules::Skate::sole()->career_.get(); }
Struct* result_params(Script* script) { return script->args_.get(); }

// The skate module's profile `params`' skater names (0x00564F80).
objects::RiderIdentity* skate_profile(Struct* params) { return modules::Skate::sole()->fetch_account(params); }

const char* const kNoText = "---"; // "---"

} // namespace

// 0x005AA930
void swap_mapping(uint32_t a, uint32_t b)
{
    SwapSlot* table = swap_table();
    int slot_a = -1;
    int slot_b = -1;
    for (int i = 0; i < kSwapSlots; ++i) {
        if (!table[i].used)
            continue;
        if (table[i].key == a)
            slot_a = i;
        else if (table[i].key == b)
            slot_b = i;
    }
    if (slot_a == -1 || slot_b == -1) {
        for (int i = 0; i < kSwapSlots; ++i) {
            if (table[i].used)
                continue;
            if (slot_a == -1) {
                slot_a = i;
                table[i].key = a;
                table[i].value = a;
            } else if (slot_b == -1) {
                slot_b = i;
                table[i].key = b;
                table[i].value = b;
            } else {
                continue;
            }
            table[i].used = true;
        }
    }
    // (with the table full, a slot of -1: the entry before the table, as in the original)
    SwapSlot& first = table[slot_a];
    SwapSlot& second = table[slot_b];
    const uint32_t first_value = first.value;
    const uint32_t second_value = second.value;
    if (first.key == second_value)
        first.used = false;
    else
        first.value = second_value;
    if (second.key == first_value)
        second.used = false;
    else
        second.value = first_value;
}

// 0x005AAAA0
void preload_meshes(uint32_t array_name)
{
    script::Array* list = script::fetch_list(array_name);
    for (uint32_t i = 0; i < list->extent_; ++i) {
        auto* entry = list->struct_at(i);
        int allow_random = 0;
        entry->fetch_int_value(0xF1E3CD22u, &allow_random, 0); // allow_random
        if (!allow_random)
            continue;
        const char* mesh = nullptr;
        if (!entry->fetch_text(0x1E90C5A9u, &mesh, 0)) // mesh
            continue;
        // a model made (0x0048BFA0), the mesh added to it (0x004935E0), the model deleted (0x0048C3C0)
        gsl::owner<render::RenderModel*> model = render::spawn_model();
        model->add_file_geom(mesh, 0, true, 0, false, false, true, false);
        delete model;
    }
}

// 0x005AAA80
bool cfunc_add_goal_editor_nodes_to_level_node_array(Struct*, Script*)
{
    // the goal editor's nodes added (0x004E8F50, no source yet)
    park_editing::add_goal_editor_nodes(nullptr, nullptr, memory::Hub::get_pull()->bottom_up_heap_);
    return true;
}

// 0x005AAB50
bool cfunc_current_skater_is_pro(Struct* params, Script*)
{
    objects::RiderIdentity* profile = skate_profile(params);
    return profile && profile->is_pro();
}

// 0x005AAB80
bool cfunc_get_goals_completed(Struct* params, Script* script)
{
    int level = 0;
    params->fetch_int_value(0u, &level, 0);
    const int goals = get_career()->count_goals(level);
    std::array<char, 100> text{};
    std::snprintf(text.data(), sizeof(text), "%d/9", goals);
    result_params(script)->append_word(0xC661FE79u, script::kQbvalString, text.data());    // (name unknown)
    result_params(script)->append_word(0xA43DC969u, script::kQbvalString, kNoText); // (name unknown)
    return true;
}

// 0x005AAC10
bool cfunc_get_next_level_requirements(Struct*, Script* script)
{
    std::array<char, 100> text{};
    std::snprintf(text.data(), sizeof(text), "%s", kNoText);
    objects::RiderStoryProgress* career = get_career();
    const int goals = career->count_total_goals();
    const int medals = career->count_levels_with_goals();
    script::Array* steps = script::fetch_list(0x1D8F1A7Eu); // (name unknown) the level steps
    for (uint32_t i = 0; i < steps->extent_; ++i) {
        auto* step = steps->struct_at(i);
        int goals_needed = 0;
        int medals_needed = 0;
        if (step->has_bare_flag(0x38DBE1D0u) || step->has_bare_flag(0x032314D1u)) // goals / goal
            step->fetch_int_value(0u, &goals_needed, 0);
        if (step->has_bare_flag(0x56907F8Bu) || step->has_bare_flag(0x23BBA846u)) // (names unknown) medals
            step->fetch_int_value(0u, &medals_needed, 0);
        if (goals < goals_needed) {
            std::snprintf(text.data(), sizeof(text), script::fetch_text(0x407BFD24u),
                          goals_needed - goals); // cfuncs_str_goalsfornextlevel
            break;
        }
        if (medals < medals_needed) {
            std::snprintf(text.data(), sizeof(text), script::fetch_text(0x4F3E243Eu),
                          medals_needed - medals); // cfuncs_str_medalsfornextlevel
            break;
        }
    }
    result_params(script)->append_word(0xAF2305A6u, script::kQbvalString, text.data()); // (name unknown)
    return true;
}

// 0x005AAD70
bool cfunc_set_current_skater_profile(Struct* params, Script*)
{
    int index = 0;
    params->fetch_int_value(0u, &index, 0);
    get_profiles()->set_current_index(index);
    return true;
}

// 0x005AADA0
bool cfunc_add_skater_profile(Struct* params, Script*) { return get_profiles()->add_template(params); }

// 0x005AADC0
bool cfunc_add_temporary_profile(Struct* params, Script*)
{
    uint32_t name = 0;
    params->read_hash_value(0xA1DC81F9u, &name, 1); // name (asserted: used as it is)
    return get_profiles()->add_extra_profile(name);
}

// 0x005AADF0
bool cfunc_remember_temporary_appearance(Struct* params, Script*)
{
    uint32_t name = 0;
    params->read_hash_value(0xA1DC81F9u, &name, 1); // name
    gfx::ModelLook appearance;                      // (destroyed at the end)
    objects::ProfileRoster* profiles = get_profiles();
    gfx::ModelLook* source = nullptr;
    Struct* structure = nullptr;
    if (params->read_sub_struct(0xEFC8944Cu, &structure, 0)) { // appearance_structure
        appearance.read_in(structure, true);
        source = &appearance;
    } else {
        int player = 0;                                   // (asserted: used as it is)
        params->fetch_int_value(0x67E6859Au, &player, 1); // player
        source = profiles->fetch_account(player)->appearance();
    }
    gfx::ModelLook* temporary = profiles->get_extra_profile(name)->appearance();
    if (params->has_bare_flag(0x3AF36E6Du)) { // NoFaceTexture: only the appearance's structure
        auto* own = &temporary->structure_;
        own->get_clear();
        own->append_structure_if_any(&source->structure_);
    } else {
        temporary->copy_from(source); // the appearance copied
    }
    return true;
}

} // namespace commands
