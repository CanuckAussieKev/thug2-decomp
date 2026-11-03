// Script commands (CFunctions) and helpers 0x005AA930-0x005AAEEF: profiles and the career's menu numbers (see
// profile_cfuncs.h).

#include "game/skate/profile_cfuncs.h"

#include <cstdint>
#include <array>

#include "engine/core/checked_index.h"
#include "engine/core/manager.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/career.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "game/park_editing/goal_editor.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/model.h"
#include "engine/gfx/model_appearance.h"
#include <gsl/pointers>
#include "engine/core/format.h"

namespace commands {

std::array<SwapSlot, kSwapSlotCount>& swap_table()
{
    static std::array<SwapSlot, kSwapSlotCount> swap_table{};
    return swap_table;
}

namespace {
constexpr int kTextBufferBytes = 100;
constexpr uint32_t kAllowRandom = 0xF1E3CD22U;         // allow_random
constexpr uint32_t kMesh = 0x1E90C5A9U;                // mesh
constexpr uint32_t kGoalsCompletedKey = 0xC661FE79U;   // (text unknown) the goals-completed text's parameter
constexpr uint32_t kGoalsCompletedKey2 = 0xA43DC969U;  // (text unknown) the empty text's parameter
constexpr uint32_t kLevelSteps = 0x1D8F1A7EU;          // (text unknown) the level steps list
constexpr uint32_t kGoalsFlag = 0x38DBE1D0U;           // goals
constexpr uint32_t kGoalFlag = 0x032314D1U;            // goal
constexpr uint32_t kMedalsFlag = 0x56907F8BU;          // medals
constexpr uint32_t kMedalFlag = 0x23BBA846U;           // medal
constexpr uint32_t kGoalsForNextLevel = 0x407BFD24U;   // cfuncs_str_goalsfornextlevel
constexpr uint32_t kMedalsForNextLevel = 0x4F3E243EU;  // cfuncs_str_medalsfornextlevel
constexpr uint32_t kRequirementsKey = 0xAF2305A6U;     // (text unknown) the requirements text's parameter
constexpr uint32_t kName = 0xA1DC81F9U;                // name
constexpr uint32_t kAppearanceStructure = 0xEFC8944CU; // appearance_structure
constexpr uint32_t kPlayer = 0x67E6859AU;              // player
constexpr uint32_t kNoFaceTexture = 0x3AF36E6DU;       // NoFaceTexture

using script::Script;
using script::Struct;

constexpr int kSwapSlots = 5;

objects::RiderStoryProgress* get_career() { return modules::Skate::sole()->career().get(); }

// The skate module's profile `params`' skater names (0x00564F80).
objects::RiderIdentity* skate_profile(Struct* params) { return modules::Skate::fetch_account(params); }

const char* const kNoText = "---"; // "---"

} // namespace

// 0x005AA930
void swap_mapping(uint32_t a, uint32_t b)
{
    auto& table = swap_table();
    int slot_a = -1;
    int slot_b = -1;
    for (int i = 0; i < kSwapSlots; ++i) {
        if (!core::at(table, i).used)
            continue;
        if (core::at(table, i).key == a)
            slot_a = i;
        else if (core::at(table, i).key == b)
            slot_b = i;
    }
    if (slot_a == -1 || slot_b == -1) {
        for (int i = 0; i < kSwapSlots; ++i) {
            if (core::at(table, i).used)
                continue;
            if (slot_a == -1) {
                slot_a = i;
                core::at(table, i).key = a;
                core::at(table, i).value = a;
            } else if (slot_b == -1) {
                slot_b = i;
                core::at(table, i).key = b;
                core::at(table, i).value = b;
            } else {
                continue;
            }
            core::at(table, i).used = true;
        }
    }
    // (with the table full, a slot of -1: the entry before the table, as in the original)
    SwapSlot& first = core::at(table, slot_a);
    SwapSlot& second = core::at(table, slot_b);
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
    for (uint32_t i = 0; i < list->extent(); ++i) {
        auto* entry = list->struct_at(i);
        int allow_random = 0;
        entry->fetch_int_value(kAllowRandom, &allow_random, 0); // allow_random
        if (!allow_random)
            continue;
        const char* mesh = nullptr;
        if (!entry->fetch_text(kMesh, &mesh, 0)) // mesh
            continue;
        // a model made (0x0048BFA0), the mesh added to it (0x004935E0), the model deleted (0x0048C3C0)
        gsl::owner<render::RenderModel*> model = render::spawn_model();
        model->add_file_geom(mesh, 0, true, 0, false, false, true, false);
        delete model;
    }
}

// 0x005AAA80
bool cfunc_add_goal_editor_nodes_to_level_node_array(Struct* /*params*/, Script* /*script*/)
{
    // the goal editor's nodes added (0x004E8F50, no source yet)
    park_editing::add_goal_editor_nodes(nullptr, nullptr, memory::Hub::get_pull()->bottom_up_heap());
    return true;
}

// 0x005AAB50
bool cfunc_current_skater_is_pro(Struct* params, Script* /*script*/)
{
    objects::RiderIdentity* profile = skate_profile(params);
    return profile && profile->is_pro();
}

// 0x005AAB80
bool cfunc_get_goals_completed(Struct* params, Script* script)
{
    int level = 0;
    params->fetch_int_value(0U, &level, 0);
    const int goals = get_career()->count_goals(level);
    std::array<char, kTextBufferBytes> text{};
    text::format_to(text, "%d/9", goals);
    script->args().get()->append_word(kGoalsCompletedKey, script::QbValueKind::kString, text.data());    // (name unknown)
    script->args().get()->append_word(kGoalsCompletedKey2, script::QbValueKind::kString, kNoText); // (name unknown)
    return true;
}

// 0x005AAC10
bool cfunc_get_next_level_requirements(Struct* /*params*/, Script* script)
{
    std::array<char, kTextBufferBytes> text{};
    text::format_to(text, "%s", kNoText);
    objects::RiderStoryProgress* career = get_career();
    const int goals = career->count_total_goals();
    const int medals = career->count_levels_with_goals();
    script::Array* steps = script::fetch_list(kLevelSteps); // (name unknown) the level steps
    for (uint32_t i = 0; i < steps->extent(); ++i) {
        auto* step = steps->struct_at(i);
        int goals_needed = 0;
        int medals_needed = 0;
        if (step->has_bare_flag(kGoalsFlag) || step->has_bare_flag(kGoalFlag)) // goals / goal
            step->fetch_int_value(0U, &goals_needed, 0);
        if (step->has_bare_flag(kMedalsFlag) || step->has_bare_flag(kMedalFlag)) // (names unknown) medals
            step->fetch_int_value(0U, &medals_needed, 0);
        if (goals < goals_needed) {
            text::format_data_to(text, script::fetch_text(kGoalsForNextLevel),
                                 goals_needed - goals); // cfuncs_str_goalsfornextlevel (the text is the format)
            break;
        }
        if (medals < medals_needed) {
            text::format_data_to(text, script::fetch_text(kMedalsForNextLevel),
                                 medals_needed - medals); // cfuncs_str_medalsfornextlevel (the text is the format)
            break;
        }
    }
    script->args().get()->append_word(kRequirementsKey, script::QbValueKind::kString, text.data()); // (name unknown)
    return true;
}

// 0x005AAD70
bool cfunc_set_current_skater_profile(Struct* params, Script* /*script*/)
{
    int index = 0;
    params->fetch_int_value(0U, &index, 0);
    objects::profile_roster()->set_current_index(index);
    return true;
}

// 0x005AADA0
bool cfunc_add_skater_profile(Struct* params, Script* /*script*/)
{
    return objects::profile_roster()->add_template(params);
}

// 0x005AADC0
bool cfunc_add_temporary_profile(Struct* params, Script* /*script*/)
{
    uint32_t name = 0;
    params->read_hash_value(kName, &name, 1); // name (asserted: used as it is)
    return objects::profile_roster()->add_extra_profile(name);
}

// 0x005AADF0
bool cfunc_remember_temporary_appearance(Struct* params, Script* /*script*/)
{
    uint32_t name = 0;
    params->read_hash_value(kName, &name, 1); // name
    gfx::ModelLook appearance;                      // (destroyed at the end)
    objects::ProfileRoster* profiles = objects::profile_roster();
    gfx::ModelLook* source = nullptr;
    Struct* structure = nullptr;
    if (params->read_sub_struct(kAppearanceStructure, &structure, 0)) { // appearance_structure
        appearance.read_in(structure, true);
        source = &appearance;
    } else {
        int player = 0;                                   // (asserted: used as it is)
        params->fetch_int_value(kPlayer, &player, 1); // player
        source = profiles->fetch_account(player)->appearance();
    }
    gfx::ModelLook* temporary = profiles->get_extra_profile(name)->appearance();
    if (params->has_bare_flag(kNoFaceTexture)) { // NoFaceTexture: only the appearance's structure
        auto* own = &temporary->structure();
        own->get_clear();
        own->append_structure_if_any(&source->structure());
    } else {
        temporary->copy_from(source); // the appearance copied
    }
    return true;
}

} // namespace commands
