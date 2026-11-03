// The skater's flip-and-rotate component (GameObjects::RiderSpinAndFlip): see flip_and_rotate.h.

#include "game/rider/flip_and_rotate.h"

#include <cstdint>
#include <gsl/pointers>
#include <array>

#include "engine/net/gamenet_manager.h"
#include "engine/net/net_app.h"
#include "engine/net/net_client.h"
#include "engine/net/player_info.h"
#include "engine/objects/anim_blender.h"
#include "engine/script/param_struct.h"
#include "game/rider/thaw_bmx.h"
#include "platform/game_clock.h"
#include "game/rider/core_physics.h"
#include "engine/objects/control_pad.h"
#include "game/rider/skitch.h"
#include "engine/objects/car_physics.h"
#include "engine/objects/node_components.h"
#include "engine/objects/entity.h"
#include "engine/objects/camera_lookaround.h"
#include "game/rider/skater_state.h"
#include "engine/objects/model_component.h"
#include "game/rider/rider.h"
#include "engine/core/down_cast.h"
#include "engine/net/net_game_state.h"
#include "wire/skater_messages.h"

namespace objects {

namespace {
constexpr unsigned kByteMask = 0xFF;

constexpr uint32_t kSkaterFlipAndRotate = 0x488AF07A;
constexpr uint32_t kSkaterCorePhysics = 0x5BD63B29, kModel = 0x286A8D26, kAnimBlender = 0x7149B753,
                   kSkaterState = 0x43686585;

// Script member functions
constexpr uint32_t kBoardIsRotated = 0x79EE5CCF, kBoardRotateAfter = 0x63E93F69, kPlayAnim = 0x0B1E7291,
                   kRotateNoFlip = 0x587FBDD7, kFlip = 0x65011BAA, kFlipIfNeeded = 0x72289A28 /* (name unknown) */,
                   kFlipAndRotate = 0xD9DE0C65, kIsFlipAfterSet = 0xA6243DAA, kRotateAfter = 0xACAC1BAD,
                   kFlipAfter = 0xEBA11B20, kBoardRotate = 0xE0F3A644;
constexpr uint32_t kNormal = 0xDE7A971B;


// The network manager (0x007CCE14) and its players: the player with an object id, whether a player is this
// machine's, a player's skater's index (its +0x2BC) and the client app of an index.
online::PlayerRecord* find_player_slot_by_obj(uint32_t id)
{
    return (online::online_hub()->find_player_slot_by_obj(static_cast<uint16_t>(id)));
}
bool is_own_player(online::PlayerRecord* player) { return player->is_own_player() != 0; }
int player_skater_index(online::PlayerRecord* player) { return static_cast<int>(player->get_skater_view_slot()); }
net::Guest* client_app(int index) { return core::down_cast<net::Guest>(online::online_hub()->fetch_patron(index)); }
// A client app's +0x08 (passed to the animation blender and to apply_board_turned, which doesn't use it).
uint32_t app_value8(net::Guest* app) { return app->clock(); }

// The core physics component's (not rewritten yet): the skater turned around (0x004FE120) and whether the flip is
// needed (0x004FC440).
void turn_around(skater::SkaterPhysicsModule* core_physics) { core_physics->turn_around(); }
bool flip_needed(skater::SkaterPhysicsModule* core_physics) { return (core_physics->is_sides_swapped() & kByteMask) != 0; }

// The animation blender's 0x00419C10: its animations mirrored or not (`flip`: the state component's byte as it is).
void flip_anims(ObjectModule* blender, uint32_t object_id, uint32_t flip, uint32_t a, int b)
{
    core::down_cast<AnimBlenderComponent>(blender)->any_channel(object_id, flip, a, static_cast<uint32_t>(b));
}

// The skater's index (+0x2BC of the skater object).
int get_skater_index(GameEntity* skater) { return core::down_cast<Rider>(skater)->view_slot(); }

} // namespace

// 0x00511470
gsl::owner<ObjectModule*> RiderSpinAndFlip::fabricate() { return new RiderSpinAndFlip; }

// (the constructor: inlined in fabricate in the original)
RiderSpinAndFlip::RiderSpinAndFlip() { set_kind_tag(kSkaterFlipAndRotate); }

// 0x00511300 (0x00511320 the deleting one)
RiderSpinAndFlip::~RiderSpinAndFlip() = default;

// 0x00511350
void RiderSpinAndFlip::seal()
{
    core_physics_ = core::down_cast<skater::SkaterPhysicsModule>(entity()->fetch_module(kSkaterCorePhysics));
    mesh_set_ = core::down_cast<MeshInstanceModule>(entity()->fetch_module(kModel));
    anim_blender_ = entity()->fetch_module(kAnimBlender);
}

// 0x00511390
bool RiderSpinAndFlip::apply_board_turned(uint32_t object_id, bool rotated, uint32_t /*unused*/, bool send)
{
    if (send) {
        online::PlayerRecord* player = find_player_slot_by_obj(object_id);
        if (player && is_own_player(player)) {
            net::NetMessageInfo message;
            net::Guest* app = client_app(player_skater_index(player));
            std::array<uint8_t, 2> data = {{static_cast<uint8_t>(object_id), static_cast<uint8_t>(rotated)}};
            message.set_ident(online::kMsgBoardRotated);
            message.set_byte_count(2);
            message.set_content(data.data());
            app->send_to_host(&message);
        }
    }
    const bool was = deck_turned_;
    deck_turned_ = rotated;
    return was;
}

// 0x00511310
void RiderSpinAndFlip::clear_after_flags()
{
    flip_after_ = false;
    rotate_after_ = false;
}

// 0x00511420
void RiderSpinAndFlip::apply_flip_to_anims()
{
    net::Guest* app = client_app(get_skater_index(entity()));
    auto* state = core::down_cast<objects::RiderPoseState>(entity()->fetch_module(kSkaterState));
    flip_anims(anim_blender_, entity()->ident(), state->flag(PoseFlag::kFlipped).on, app_value8(app), 1);
}

// 0x005114F0
void RiderSpinAndFlip::get_flip()
{
    auto* state = core::down_cast<objects::RiderPoseState>(entity()->fetch_module(kSkaterState));
    uint8_t& flipped = state->flag(PoseFlag::kFlipped).on;
    const uint8_t flip = flipped == 0 ? 1 : 0;
    if (flipped != flip) { // (always)
        flipped = flip;
        state->flag(PoseFlag::kFlipped).changed_at = frame_clock::game_clock_elapsed() + clock_offset();
    }
    // NOT ORIGINAL - THAW's BMX ([features] thaw_bmx, on the bike): THAW's Flip turns the flag only - the animations
    // take it when they're played (game/rider/thaw_bmx.h flip_flag_only / play_flip). Original behaviour: every running
    // animation mirrored at once.
    if (skater::thaw_bmx::flip_flag_only(core_physics_))
        return;
    apply_flip_to_anims();
}

// 0x00511570
void RiderSpinAndFlip::do_after_anim()
{
    if (flip_after_) {
        get_flip();
        flip_after_ = false;
    }
    if (rotate_after_) {
        turn_around(core_physics_);
        rotate_after_ = false;
    }
    if (board_rotate_after_) {
        // (the player isn't checked for null here)
        net::Guest* app = client_app(player_skater_index(find_player_slot_by_obj(entity()->ident())));
        apply_board_turned(entity()->ident(), !deck_turned_, app_value8(app), true);
        board_rotate_after_ = false;
    }
    mesh_set_->set_flip(false);
}

// 0x00511600
int RiderSpinAndFlip::invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* /*script*/)
{
    switch (name) {
    case kBoardIsRotated:
        return deck_turned_ ? 1 : 0;
    case kBoardRotateAfter:
        board_rotate_after_ = true;
        return 1;
    case kPlayAnim:
        do_after_anim();
        return 2;
    case kRotateNoFlip:
        turn_around(core_physics_);
        return 1;
    case kFlip:
        get_flip();
        return 1;
    case kFlipIfNeeded:
        if (flip_needed(core_physics_))
            get_flip();
        return 1;
    case kFlipAndRotate:
        turn_around(core_physics_);
        get_flip();
        return 1;
    case kIsFlipAfterSet:
        return flip_after_ ? 1 : 0;
    case kRotateAfter:
        rotate_after_ = true;
        return 1;
    case kFlipAfter:
        flip_after_ = true;
        return 1;
    case kBoardRotate: {
        // (the player isn't checked for null here)
        net::Guest* app = client_app(player_skater_index(find_player_slot_by_obj(entity()->ident())));
        const bool rotated = params->has_bare_flag(kNormal) ? false : !deck_turned_;
        apply_board_turned(entity()->ident(), rotated, app_value8(app), true);
        return 1;
    }
    default:
        return 2;
    }
}

} // namespace objects
