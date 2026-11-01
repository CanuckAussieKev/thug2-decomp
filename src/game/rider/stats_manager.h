#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/maths/vec4.h"
#include "engine/objects/entity.h"
#include "engine/core/owner.h"

namespace script {
class Script;
class Struct;
} // namespace script

// The stats manager component (StatsManager, type 0x4FF8DAC6, vtable 0x0064C294, 0xAA0 bytes; made by 0x0051CB90):
// the story mode's stat challenges - up to 70 goals (the global array 0x4E46CC28, per difficulty) a skater meets by
// what it does: a combo's score or multiplier, air height and distance, a jump's rise or drop, tricks and gaps
// counted, a manual or grind held long enough. Each frame the skater's state and score are watched; a goal met is
// announced (script 0x55E516C8) and, when the stats are announced (0x0051CBF0), raises a stat in the profile.
//
// Vtable: 0 Destroy, 1 Update, 2 configure_from_data, 3 the empty 0x005480A0, 5 0x004E3F90 (shared), 11 0x0042BB20
// (member function entries by table: the StatsManager_ functions, the table at 0x00684180), 12 0x0051ADC0 (shared).
namespace objects {

class RiderPoseState;
class RiderBoardPhysics;

class AttributeTracker final : public ObjectModule {
public:
    static constexpr uint32_t kType = 0x4FF8DAC6;
    static constexpr int kGoals = 70;

    // A stat challenge (0x24 bytes from +0x1C).
    struct Goal {
        uint32_t kind_;     // +0x00 (0x0FFD7BBE of its structure) what's checked
        uint32_t attr_key_; // +0x04 (0xC2455018) the stat it raises
        int aim_point_;     // +0x08 (0xE288A7CB, by difficulty)
        bool met_;          // +0x0C met (until announced or reset)
        uint8_t pad0d_[3];
        int cur_standing_;  // +0x10 0 open, 1 done, 2 / 3 locked until the am / pro goals unlock
        const char* label_; // +0x14 (0xC4745838)
        const char* text_;  // +0x18 (0xCD021293, by difficulty)
        uint32_t trick_;    // +0x1C (0xD8A9D3CA, by difficulty)
        uint32_t value20_;  // +0x20 (0x3C2FB496, by difficulty)
    };

    // 0x0051CB90 (registered with the composite object manager): a new one (0 if out of memory).
    static memory::owner<ObjectModule*> fabricate();
    // 0x0051C440 (not a Ghidra function; registered with Create): its script member functions (the table at
    // 0x00684180) registered.
    static void publish_method_table();
    // 0x0051C5A0: its type; no skater, the positions and the last air combo's
    // score cleared.
    AttributeTracker();
    // 0x0051C460: the destructor body (the base's).
    ~AttributeTracker() override;
    int invoke_method(script::MemberFunctionEntry* entry, script::Struct* params, script::Script* script) override
    {
        return dispatch_script_method(entry, params, script); // (slot 11: 0x0042BB20, shared)
    }

    // 0x0051C470 / 0x0051C480: goal `index`'s status; set (1 also marks that one was done, +0x9F6).
    int get_goal_status(int index);
    void set_goal_status(int index, int status);

    // 0x0051C670 (vtable slot 2): everything reset; the skater (the object's id in the object manager) and its
    // state and physics control components; the difficulty; the goals read from the global array 0x4E46CC28 (`params`
    // isn't read).
    void configure_from_data(script::Struct* params) override;
    // 0x0051C950: whether the goals are checked: a skater, the game mode's flag 0x1DED1EA4, activated, the skate
    // module not in mode 0xE92ECAFE, and the first profile's name 0x0A7BE964.
    bool is_operative();
    // 0x0051C9D0: +0xA10 = `a` unless `b` is +0xA14 (checked). `c` isn't used.
    void set10(int a, int b, int c);
    // 0x0051CA00: back to before the combo (script 0xD44EA68F): goals met but not done unmarked, the air and jump
    // phases and the two counters cleared, the starts back to the ends.
    void get_reset();
    // 0x0051CAC0: +0xA58 = `value` (checked).
    void set_value58(float value);
    // 0x0051CAE0: counts 0xBA15FC7F (+0xA80) and 0xB9AE26D2 (+0xA84) (checked).
    void total_of(uint32_t name);
    // 0x0051CBF0: the goals met are done: announced (scripts 0x750BCF49, 0x55E516C8 with the stat's name), their
    // stats raised in the first profile (with +0x9F5) and given to the skater; the first time, 0xEF3A4EE5.
    void announce_met_goals();
    // 0x0051CE80: goal `index` met (script 0x55E516C8 with its name, target, text and index).
    void meet_goal(int index);
    // 0x0051CF90 (vtable slot 1): the score and multiplier noted, the air and jump watched, every open goal checked.
    void tick_frame() override;
    // 0x0051D6A0: a balance trick of `trickType` held `time` seconds: the manual / grind time goals checked.
    void check_balance_time(uint32_t trick_type, float time, int unused);

    // --- script member functions (the table at 0x00684180) ---
    bool re_init(script::Struct* params, script::Script* script);          // 0x0051C4A0 StatsManager_ReInit
    bool reset_goals(script::Struct* params, script::Script* script);      // 0x0051CB20 StatsManager_Reset
    bool activate_goals(script::Struct* params, script::Script* script);   // 0x0051C4C0 (script 0x7EC5F3E0)
    bool deactivate_goals(script::Struct* params, script::Script* script); // 0x0051C4F0 (script 0x3CB06807)
    bool get_goal_status_member(script::Struct* params,
                                script::Script* script);                   // 0x0051CB30 StatsManager_GetGoalStatus
    bool unlock_am_goals(script::Struct* params, script::Script* script);  // 0x0051C520 (status 2 -> 0)
    bool unlock_pro_goals(script::Struct* params, script::Script* script); // 0x0051C550 (status 3 -> 0)
    bool get_last_air_combo_score(script::Struct* params, script::Script* script);   // 0x0051CB70
    bool clear_last_air_combo_score(script::Struct* params, script::Script* script); // 0x0051C580

    bool first_update_; // +0x18 the next update only clears the phases
    uint8_t pad19[3];
    Goal goals_[kGoals]; // +0x1C
    bool activated_;     // +0x9F4
    bool raise_stats_;   // +0x9F5 (the global integer 0xB8688A8A is 1)
    bool goal_done_;     // +0x9F6 a goal's status was set to 1
    uint8_t pad9f7_;
    int points_held_;                    // +0x9F8 the combo's points times its multiplier
    int score_before_combo_;             // +0x9FC
    int path_vertices_;                  // +0xA00 the score over the multiplier
    int last_points_;                    // +0xA04
    float multiplier_;                   // +0xA08
    float last_multiplier_;              // +0xA0C
    int value10_;                        // +0xA10
    int value14_;                        // +0xA14
    int difficulty_;                     // +0xA18
    uint32_t cur_phase_;                 // +0xA1C this frame's (0x26A7CADF, 0xB39B4F1B, 0x439F4704)
    uint32_t last_state_;                // +0xA20
    maths::Vec4 air_start_;              // +0xA24 where the skater left the ground (state +0x30)
    maths::Vec4 air_end_;                // +0xA34 where it landed
    int air_start_points_;               // +0xA44
    float air_start_multiplier_;         // +0xA48
    int air_best_;                       // +0xA4C the best combo score made in the air
    int air_phase_;                      // +0xA50 0, 1 in the air, 2 landed
    float air_height_;                   // +0xA54 the most above the start
    float value58_;                      // +0xA58 (cleared after each update)
    maths::Vec4 jump_start_;             // +0xA5C
    maths::Vec4 jump_end_;               // +0xA6C
    int jump_phase_;                     // +0xA7C 0, 1 in the air, 2 landed
    int count_a80_;                      // +0xA80 (0xBA15FC7F)
    int count_a84_;                      // +0xA84 (0xB9AE26D2)
    int air_score_;                      // +0xA88
    int last_air_combo_score_;           // +0xA8C
    GameEntity* skater_;                 // +0xA90
    RiderPoseState* skater_state_;       // +0xA94
    RiderBoardPhysics* physics_control_; // +0xA98
    uint32_t unknowna9c_;
};

} // namespace objects
