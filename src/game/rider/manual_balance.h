#pragma once

#include <cstddef>
#include <cstdint>

// The balance of a balance trick (0x00579850-0x0057A177): a manual, a grind, a lip trick or a skitch - the skater's
// balance trick component keeps one each (0x004F8D00-). The skater leans (lean_, degrees) and the lean speeds up
// (lean_speed_); the player's two buttons push it back. Each start repeats the last lean harder (Repeat_Multiplier,
// Lean_Repeat_Multiplier); a grind on the same rail adds time (Same_Grind_Add_Time), a new one takes it off; past
// Lean_Bail_Angle the skater falls (an event). The skater's balance meter shows it.
namespace objects {

class GameEntity;
class ObjectModule;

class Balance {
public:
    // 0x00579950: the vtable, Reset's state (without its script). Returns this.
    Balance* make_new();
    // 0x00579850: the destructor body (only the vtable). 0x005799A0: the scalar deleting destructor (vtable
    // slot 0).
    void destroy_body();
    Balance* teardown(unsigned int flags);
    // 0x00579860: the skater and its components SkaterBalanceTrick, SkaterScore, input.
    void prepare_all(GameEntity* skater);
    // 0x005798A0: reset_balance_indicator (if the script integer 0x5D07E2BE is set); the lean, its speed, the
    // times cleared; not on a rail (-1).
    void get_reset();
    // 0x00579A00: started with the two buttons that push the lean left / right and the trick (+0x4C), and two
    // flags: on a grind or slide, time added or taken off by the rail (and a robot rail's nudge on a rail run
    // shorter than half a second); the last lean speed repeated harder (at least Repeat_Min; a new manual: random
    // side, a nose manual's to the back); the lean repeated (Lean_Repeat_Multiplier, plus the cheese); kept within
    // Lean_Bail_Angle; the cheese, the start time.
    void kickoff(uint32_t left_button, uint32_t right_button, uint32_t trick, bool flipped, bool flip);
    // 0x00579CC0: one frame (while started): the cheese wears off; the score's trick time (0x00564270); the time
    // goes on; the lean speeds up (its physics: 0x06851748, 0xA9FEF5AB, 0xB6A634F3); the buttons push it back
    // (Lean_Push - not before the "no input" time 0x07DA1621 has passed or a button was let go); with no button,
    // a random shake; past Lean_Bail_Angle the skater's bail event; the balance meter (0x7149B753) and the score's
    // display (0x00563370 / 0x00563390) shown.
    void tick_frame();
    // 0x005799D0: the score's displays hidden, the buttons forgotten.
    void halt();
    // 0x00579920: the longest time so far kept (best_time_), the time cleared. 0x005798F0: the longest time cleared.
    void keep_best_time();
    void clear_best_time();
    // 0x00579900 / 0x00579910: the score display hidden or not. 0x00579940: the balance meter shown.
    void hide_display();
    void show_display();
    void show_meter();

    void** vtable_;
    GameEntity* skater_;    // +0x04
    uint8_t* balance_trick_;       // +0x08 SkaterBalanceTrick (+0x18 the balance trick's type)
    uint8_t* points_held_;              // +0x0C SkaterScore (+0x18 the score)
    uint8_t* input_;              // +0x10 input (+0x20 its control data)
    uint8_t unknown14_[0x10];
    float clock_;                  // +0x24
    float on_rail_seconds_;              // +0x28
    float best_time_;              // +0x2C
    float lean_;                  // +0x30
    float lean_speed_;             // +0x34
    float cheese_;                // +0x38
    bool pushed_;                 // +0x3C a button was let go, or the "no input" time passed
    uint8_t pad3d_[3];
    uint32_t start_time_;          // +0x40 the game clock at the start
    uint32_t left_button_;         // +0x44
    uint32_t right_press_;        // +0x48
    uint32_t trick_;              // +0x4C
    int rail_;                    // +0x50 the rail of the last grind (-1: none)
    bool show_meter_;              // +0x54
    bool hide_display_;            // +0x55
    bool mirrored_;                // +0x56
    bool flip_;                   // +0x57
};
static_assert(offsetof(Balance, clock_) == 0x24 && offsetof(Balance, cheese_) == 0x38 &&
                  offsetof(Balance, start_time_) == 0x40 && offsetof(Balance, rail_) == 0x50 &&
                  offsetof(Balance, flip_) == 0x57 && sizeof(Balance) == 0x58,
              "layout must match the original");

} // namespace Obj
