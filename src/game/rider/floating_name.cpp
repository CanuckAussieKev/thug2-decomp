// The skater's floating name component (GameObjects::RiderOverheadTag): see floating_name.h.

#include "game/rider/floating_name.h"

#include <cstdint>
#include <cstdio>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "frontend/screen_element_manager.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/player_info.h"
#include "engine/objects/smtptr.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "game/rider/rider.h"
#include "game/skate/game_mode.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "game/rider/vehicle_sound.h"
#include "engine/objects/camera_lookaround.h"

namespace objects {

namespace {

constexpr uint32_t kSkaterFloatingName = 0x125044E2;
constexpr uint32_t kId = 0x40C698AF, kText = 0xC4745838, kPos3d = 0x4B491900;
constexpr uint32_t kCreateObjectLabel = 0x6A060CF0, kDestroyObjectLabel = 0x2575B406;

// skater_name_0 ... skater_name_7
constexpr uint32_t kLabelIds[8] = {0xE797A186, 0x90909110, 0x0999C0AA, 0x7E9EF03C,
                                   0xE0FA659F, 0x97FD5509, 0x0EF404B3, 0x79F33425};

constexpr float kLocalHeight = 96.0f;  // above this machine's player's skater
constexpr float kOtherHeight = 120.0f; // above the others'

// The network manager (0x007CCE14): a network game on, its show_names preference on, the player with an object id,
// this machine's player (fetch_viewed_player: its +0x1CC).
online::Hub* net_session_slot() { return online::g_online_hub; }
bool in_net_game() { return net_session_slot()->in_net_game(); }
bool show_names() { return net_session_slot()->wants_name_tags(); }
online::PlayerRecord* find_player_slot_by_obj(uint32_t id)
{
    return net_session_slot()->find_player_slot_by_obj(static_cast<uint16_t>(id));
}
online::PlayerRecord* find_own_player_record() { return net_session_slot()->fetch_viewed_player(); }
// The game mode (0x007CE470)'s 0x005535F0: a team game. Not rewritten yet.
bool is_squad_match() { return goals::g_game_mode->is_squad_match() != 0; }

// `name` run with a structure holding only id = `label_id`.
void run_with_label_id(uint32_t name, uint32_t label_id)
{
    script::Struct* params = script::spawn_param_bag();
    if (params)
        new (params) script::Struct;
    params->put_name_key(kId, label_id);
    script::launch_script(name, params, nullptr, false, nullptr);
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

} // namespace

// 0x00511810
memory::owner<ObjectModule*> RiderOverheadTag::fabricate() { return new RiderOverheadTag; }

// (the constructor: inlined in fabricate in the original)
RiderOverheadTag::RiderOverheadTag() { kind_tag_ = kSkaterFloatingName; }

// 0x00511AA0 (0x00511B80 the deleting one)
RiderOverheadTag::~RiderOverheadTag() { run_with_label_id(kDestroyObjectLabel, label_id_); }

// 0x00511780
void RiderOverheadTag::configure_from_data(script::Struct* /*params*/)
{
    uint32_t id = entity_->ident_;
    if (id < 8)
        label_id_ = kLabelIds[id];
}

// 0x00511870
void RiderOverheadTag::tick_frame()
{
    if (!in_net_game() || !show_names()) {
        hold_off(true);
        return;
    }
    frontend::UiWidget* label = nullptr;
    {
        frontend::ElementPtr found;
        frontend::UiTree::sole()->fetch_part(&found, label_id_, 0);
        label = found.get_pull();
    }
    online::PlayerRecord* player = find_player_slot_by_obj(entity_->ident_);
    auto* skater = static_cast<Rider*>(entity_);
    if (!skater->get_flag_2c_4() || (entity_->composite_flags_ & kCompositeFlagHidden)) {
        if (label)
            run_with_label_id(kDestroyObjectLabel, label_id_);
        return;
    }
    float height = find_own_player_record() == player ? kLocalHeight : kOtherHeight;
    script::Struct* params = script::spawn_param_bag();
    if (params)
        new (params) script::Struct;
    params->put_name_key(kId, label_id_);
    // The text's colour: the player's team's in a team game, else the skater's own.
    uint32_t colour = is_squad_match() ? static_cast<uint32_t>(player->team_) : entity_->ident_;
    char text[64];
    std::snprintf(text, sizeof(text), "\\c%d%s", static_cast<int>(colour + 2), skater->fetch_shown_name());
    params->append_word(kText, text);
    const maths::Vec4& pos = entity_->at_point_;
    params->put_vec3(kPos3d, pos.x, height + pos.y, pos.z);
    if (label) // (Entity's virtual change_traits: whatever object has the id)
        label->change_traits(params);
    else
        script::launch_script(kCreateObjectLabel, params, nullptr, false, nullptr);
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
}

} // namespace objects
