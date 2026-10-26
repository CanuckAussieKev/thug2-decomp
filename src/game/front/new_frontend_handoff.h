#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Front {
class CScreenElement;
}
namespace Mdl {
struct FrontEnd;
}

// NOT ORIGINAL - the new front end's hand-offs to the original screens (ours; see new_frontend.h): which original
// screen is up under ours (an "anchor"), and plans - a few steps run one a frame, each the original's own: an item of
// an original menu chosen (its pad_choose, as InputLogic launches pad events - the item found by its id or by its
// pad_choose handler's script and parameters), an original menu's Back (pad_back), a wait for an anchor, a script.
// Nothing here changes the original's scripts: a plan does what the player would press through.
namespace Front::NewFrontend::Handoff {

// The original screens the new front end stands for (drawn over them).
enum class Anchor {
    None,
    MainMenu,      // the front end's main menu (main_vmenu: the overlay's roundbar menu or the game's wheel)
    CareerOptions, // STORY MODE / CLASSIC MODE OPTIONS (career_options_vmenu)
    CreateModes,   // CREATE-A-MODES (its menu_cas item)
    SkateShop,     // free skate's skate shop menu after the skater select (ss_play_level, single session)
    Pause,         // a level's pause menu (pause_vmenu with its CONTINUE: free skate, story, classic)
};
const char* AnchorName(Anchor anchor);

// The anchor up now (`frontEnd` for the pause state; any of it may be null).
Anchor Current(Mdl::FrontEnd* frontEnd);
// The game mode's name ("singlesession", "freeskate", "career", "classic", "net"...; "" before there is one).
std::string GameModeName();
// A network game, online, or a network game mode: the new front end leaves those to the original screens.
bool NetSession();

// An element of the screen tree (by id; null if none).
CScreenElement* Find(uint32_t id);

// What a Choose step looks for: an element by id, or the first element (depth first from the root) with a pad_choose
// handler whose script is `script` (0: any) and whose parameters have these checksums / integers.
struct Match {
    uint32_t id = 0;
    uint32_t script = 0;
    std::vector<std::pair<uint32_t, uint32_t>> checksums;
    std::vector<std::pair<uint32_t, int>> integers;
};
CScreenElement* Find(const Match& match);
bool Exists(const Match& match);

struct Step {
    enum class Kind {
        Choose,       // `match` chosen (pad_choose), once it has been there kSettleSeconds
        PadBack,      // the anchor's menu gets pad_back
        WaitAnchor,   // until `anchor` is up
        WaitUnpaused, // until the game isn't paused
        Run,          // `script` run now (`checksums`: its parameters, name = value)
        Spawn,        // `script` spawned (the same parameters)
    } kind = Kind::Choose;
    Match match;
    Anchor anchor = Anchor::None;
    std::string script;
    std::vector<std::pair<uint32_t, uint32_t>> checksums;
};
// How long a Choose waits for its item to settle, and how long any step waits at most.
constexpr float kSettleSeconds = 0.35f;
constexpr float kStepSeconds = 6.0f;

struct Plan {
    std::string what;        // (for the log)
    std::vector<Step> steps;
    bool endsOurs = false;   // done: ours over the anchor then up (else the original's screens: ours steps aside)
    // The first step's item is on the menu the player was looking at (up long since - nothing to settle): chosen at
    // once, so its own pad_choose sound is the press's sound, played as the player presses (the pause menu's items:
    // ours plays none of its own for them - the user's 2026-10-26 report of Quit to menu's sound played twice).
    bool firstAtOnce = false;
};

// The plan under way.
class Runner {
public:
    void Start(Plan plan);
    bool Running() const { return m_running; }
    const Plan& Current() const { return m_plan; }
    // One frame: the step's work. Returns true the frame the plan ends; `failed` then says whether it gave up.
    bool Update(Mdl::FrontEnd* frontEnd, float dt, bool* failed);
    void Cancel() { m_running = false; }

private:
    Plan m_plan;
    size_t m_step = 0;
    float m_time = 0.0f, m_seen = 0.0f;
    bool m_running = false;
};

// Helpers for plans.
Step ChooseId(const char* id);
Step ChooseBy(const char* script, std::vector<std::pair<const char*, const char*>> checksums,
              std::vector<std::pair<const char*, int>> integers = {});
Step PadBack(Anchor anchor);
Step WaitFor(Anchor anchor);
Step RunScript(const char* script, std::vector<std::pair<const char*, const char*>> checksums = {});
Step SpawnScript(const char* script, std::vector<std::pair<const char*, const char*>> checksums = {});
Step WaitUnpaused();

// pad_choose / pad_back launched at an element (as InputLogic launches them: device_num, controller).
void LaunchPad(Mdl::FrontEnd* frontEnd, uint32_t type, CScreenElement* element);

} // namespace Front::NewFrontend::Handoff
