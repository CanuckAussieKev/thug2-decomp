#pragma once

#include <cstdint>
#include <string>
#include <vector>

// NOT ORIGINAL - following a skater's gaps and combos frame by frame (ours, from the level editor's play testing:
// editor/level_editor_playtest.h, which records them on its trails; the new front end keeps a session's best combo and
// gaps landed with them too). Pure: the caller reads the gap checklist and the score each frame and feeds them in.
// (The namespace is the editor's, where they were written - kept when they moved here.)
namespace frontend::level_editor::play_test {

// The gap checklist as read this frame: each gap's name, pending landings and times landed.
struct GapRead {
    std::string name;
    int pending = 0, count = 0, score = 0;
};
// What changed since the last read: gaps got (pending went up), landed (count went up), lost (pending back to 0 with
// no landing). The first read only learns the list.
struct GapChange {
    enum class Kind : uint8_t { kGot, kLanded, kLost } kind;
    std::string name;
    int score = 0;
};
struct GapTracker {
private:
    bool known = false;
    std::vector<GapRead> last;

public:
    std::vector<GapChange> tick_frame(const std::vector<GapRead>& now);
    void get_forget() { known = false, last.clear(); }
};

// A combo's end: its points (pending, as the score reads them) dropped to nothing - landed, unless the skater bails
// within kSettleFrames of it (or was bailing then).
constexpr int kSettleFrames = 12;
struct ComboEnd {
    int points = 0;
    bool landed = false;
};
struct ComboTracker {
    // The fields other code reads and sets.
    [[nodiscard]] int ending() const { return ending_; }

private:
    int pending = 0;                                               // the points pending last frame
    int ending_ = 0;                                                // a combo that just ended: its points (0: none) ...
    int settle = 0;                                                // ... and the frames left to see a bail

public:
    bool tick_frame(int pending_now, bool bailing, ComboEnd* end); // true when `end` is a combo decided this frame
};

} // namespace frontend::level_editor::play_test
