#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/objects/entity.h"
#include "game/rider/manual_balance.h"
#include <gsl/pointers>
#include <array>

namespace script {
class Script;
class Struct;
} // namespace script

// The skater's balance trick component (SkaterBalanceTrick, type 0x589E5FB3, vtable 0x0064BA30, 0x188 bytes; made by
// 0x004F9170): the balance of the trick being balanced - a manual (or nose manual), a grind (or slide), a lip trick or
// a skitch, one Balance each (manual_balance.h) - started by DoBalanceTrick, tweaked by AdjustBalance, its meter switched on
// and off; the physics values of its type (GrindParams / ManualParams / SkitchParams / LipParams, or the structure
// DoBalanceTrick was given). From the disassembly of 0x004F8CD0-0x004F9960.
//
// Vtable: 0 Destroy, 2 configure_from_data, 3 Finalize, 12 invoke_method_by_name; the rest the base's / shared (1 the empty
// 0x0051B970, 4 / 9 0x005480A0, 5 0x004E3F90, 6 0x0048DCD0, 7 the base's Suspend, 8 / 10 0x005BAB90, 11 0x0042BB20).
namespace objects {

class AnimBlenderComponent;

class RiderTeeterMoves final : public ObjectModule {
public:
    RiderTeeterMoves(const RiderTeeterMoves&) = delete;
    RiderTeeterMoves& operator=(const RiderTeeterMoves&) = delete;
    static constexpr uint32_t kType = 0x589E5FB3;
    // The balance trick types (trick_).
    static constexpr uint32_t kManual = 0xEF24413B, kNoseManual = 0x0AC90769, kGrind = 0x255ED86F, kSlide = 0x8D10119D,
                              kSkitch = 0x3506CE64, kLip = 0xA549B57B;

    // 0x004F9170 (registered by register_component_types): a new one (0 if out of memory).
    static gsl::owner<ObjectModule*> fabricate();
    // 0x004F8CD0 (registered with the type): the member function table 0x00683238 (in_balance_move).
    static void publish_method_table();
    // 0x004F8F80: the four balances made, no physics structure, no anim component.
    RiderTeeterMoves();
    // 0x004F8CF0: the destructor body (the balances', last first, then the base's). (0x004F9010 the deleting one)
    ~RiderTeeterMoves() override;
    int invoke_method(const script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_script_method(entry, params, script); // (slot 11: 0x0042BB20, shared)
    }
    // 0x004F9040 (vtable slot 2): each balance given the skater. `params` isn't read.
    void configure_from_data(script::Struct* params) override;
    // 0x004F9080 (vtable slot 3): the anim blender component (0x7149B753).
    void seal() override;
    // 0x004F9420 (vtable slot 12): SwitchOffBalanceMeter, SwitchOnBalanceMeter, StartBalanceTrick, AdjustBalance,
    // SetWobbleDetails, StopBalanceTrick, DoBalanceTrick; 2 for other names.
    int invoke_method_by_name(uint32_t name, script::Struct* params, script::Script* script) override;
    // 0x004F8D70 (the member function in_balance_move, from the table; not a Ghidra function): whether one was
    // started (StartBalanceTrick). Its parameters aren't read.
    int in_balance_move(script::Struct* params, script::Script* script);

    // 0x004F8D80 / 0x004F8DB0: every balance's best time cleared / kept.
    void clear_best_times();
    void keep_best_times();
    // 0x004F8DE0: every balance reset (manual, grind, skitch, lip); no trick, not started.
    void get_reset();
    // 0x004F8EE0: every balance stopped (manual, grind, lip, skitch); no trick, not started.
    void halt();
    // 0x004F8E20: the current balance's two buttons into `buttons` (`*count` 2), or `*count` 0 without one.
    void get_buttons(int* count, uint32_t* buttons) const;
    // 0x004F8F20: the current balance's lean (0 without one; the original also leaves the balance in EAX).
    [[nodiscard]] float get_lean() const;
    // 0x004F90A0: the physics value `name` scaled for the skater, from DoBalanceTrick's structure if there is one,
    // else the type's structure (0 for other types).
    float get_physics_value(uint32_t name);
    // 0x004F91D0: DoBalanceTrick's structure freed.
    void clear_params();
    // 0x004F9370: `params` copied into DoBalanceTrick's structure (made when there's none).
    void change_args(script::Struct* params);
    // 0x004F9200 (not a Ghidra function): the wobble from `wobble` (WobbleAmpA / B, WobbleK1 / 2, SpazFactor - each a
    // structure scaled for the skater) given to the anim blender; the skater's script made first if it has none.
    void set_wobble(script::Struct* wobble);

    // The balance of the current type (null for none).
    Balance* at_hand();
    [[nodiscard]] const Balance* at_hand() const;

    uint32_t trick_{}; // +0x18 the balance trick being balanced (0: none)
    bool started_{};   // +0x1C StartBalanceTrick
    std::array<uint8_t, 3> pad1d_{};
    gsl::owner<script::Struct*> call_args_{nullptr}; // +0x20 DoBalanceTrick's BalanceParams (a copy)
    Balance manual_;                         // +0x24 manual / nose manual
    Balance grind_;                          // +0x7C grind / slide
    Balance lip_;                            // +0xD4
    Balance skitch_;                         // +0x12C
    AnimBlenderComponent* anim_blender_ptr_{nullptr}; // +0x184

private:
    // at_hand's choice, for both constnesses
    template <typename Self>
    static auto* balance_of(Self* self);
};

} // namespace objects

namespace objects {
// The script member table's records (their code is filled in by engine/script/member_function_code.cpp).
extern std::array<script::MemberFunctionEntry, 2> g_balance_trick_member_functions;
} // namespace objects
