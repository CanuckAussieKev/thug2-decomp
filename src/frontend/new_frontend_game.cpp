// NOT ORIGINAL - the game's screens as the new front end reads them (ours): see new_frontend_game.h.

#include "frontend/new_frontend_game.h"

#include <cstdint>
#include <cstdio>
#include <array>

#include "engine/core/checksums.h"
#include "frontend/front_end.h"
#include "editor/level_editor.h"
#include "frontend/screen_element.h"
#include "frontend/screen_element_manager.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/net_manager.h"
#include "engine/objects/signal.h"
#include "engine/objects/entity.h"
#include "engine/objects/tracker.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/skate/game_mode.h"
#include "game/skate/skate_module.h"
#include "engine/pad_input/device.h"
#include "engine/core/raw_view.h"
#include "engine/core/checked_index.h"

namespace frontend::new_frontend::game_screens {

namespace {

uint32_t name_hash(const char* name) { return checksums::hash_from_text(name); }

constexpr uint32_t kLoadMainMenu = 0xE92ECAFE; // load_mainmenu
constexpr uint32_t kMenuEvent = 0x36B2EE74;    // the pad events' target and source (as input_logic launches them)
constexpr uint32_t kDeviceNum = 0xC9428A08;    // "device_num"
constexpr uint32_t kController = 0xB30D9965;   // "controller"

modules::Skate* skate_module() { return modules::g_skate_module; }
uint32_t current_load_state() { return skate_module() ? modules::g_skate_module->level_ : 0; }

uint32_t id_of(UiWidget* element) { return static_cast<objects::Entity*>(element)->ident_; }

bool get_paused(modules::FrontEnd* front_end) { return front_end && front_end->is_halted_; }

} // namespace

const char* anchor_name(Anchor anchor)
{
    switch (anchor) {
    case Anchor::kNone:
        return "none";
    case Anchor::kMainMenu:
        return "main menu";
    case Anchor::kCareerOptions:
        return "career options";
    case Anchor::kCreateModes:
        return "create-a-modes";
    case Anchor::kSkateShop:
        return "skate shop menu";
    case Anchor::kFreeze:
        return "pause menu";
    }
    return "?";
}

std::string active_ruleset_label()
{
    goals::MatchRules* mode = goals::g_game_mode;
    if (!mode)
        return {};
    const uint32_t name = mode->get_name_key();
    static const auto kNames = std::to_array<const char*>({"singlesession", "freeskate", "career",       "classic",   "creategoals",
                                         "net",           "netlobby",  "freeskate_2p", "parkeditor"});
    for (const char* known : kNames)
        if (name_hash(known) == name)
            return known;
    std::array<char, 16> hex{};
    std::snprintf(hex.data(), sizeof(hex), "0x%08X", name);
    return hex.data();
}

bool net_session()
{
    if (net::Hub* net = net::Hub::sole())
        if (net->is_online())
            return true;
    if (void* game_net = online::g_online_hub)
        if (static_cast<online::Hub*>(game_net)->in_net_game())
            return true;
    return active_ruleset_label().starts_with("net");
}

UiWidget* get_find(uint32_t id)
{
    if (!UiTree::sole() || !id)
        return nullptr;
    ElementPtr holder;
    fetch_part(&holder, id);
    return holder.get_pull(); // (the manager keeps the element; the holder's reference only for this lookup)
}

Anchor at_hand(modules::FrontEnd* front_end)
{
    const uint32_t level = current_load_state();
    if (!level || net_session() || level_editor::live_now())
        return Anchor::kNone;
    if (level == kLoadMainMenu) {
        if (get_find(name_hash("career_options_vmenu")))
            return Anchor::kCareerOptions;
        if (get_find(name_hash("menu_cas")))
            return Anchor::kCreateModes;
        if (get_find(name_hash("ss_play_level")) && active_ruleset_label() == "singlesession")
            return Anchor::kSkateShop;
        if (get_find(name_hash("main_vmenu")))
            return Anchor::kMainMenu;
        return Anchor::kNone;
    }
    if (!get_paused(front_end) || !get_find(name_hash("pause_vmenu")) || !get_find(name_hash("menu_continue")))
        return Anchor::kNone;
    const std::string mode = active_ruleset_label();
    if (mode != "singlesession" && mode != "freeskate" && mode != "career" && mode != "classic")
        return Anchor::kNone;
    return Anchor::kFreeze;
}

void launch_pad(modules::FrontEnd* front_end, uint32_t type, UiWidget* element)
{
    if (!element)
        return;
    script::Struct params;
    int device = 0;
    if (front_end && core::at(front_end->pad_listeners_, 0) && core::at(front_end->pad_listeners_, 0)->dev_)
        device = core::at(front_end->pad_listeners_, 0)->dev_->port_no_;
    params.put_int(kDeviceNum, device);
    params.put_int(kController, 0);
    objects::get_tracker()->launch_event(type, id_of(element), kMenuEvent, &params, false, -1.0f);
    params.get_clear();
}

} // namespace frontend::new_frontend::game_screens
