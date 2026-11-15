#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

// NOT ORIGINAL - a replay's skater (devtools/demo.h): the recorded skater is lent the player's place for the playback
// and the player's own is given back after it, exactly. One loan a playback, however often its level loads (the demo
// viewer's seek back starts it again by the main menu: lent once, never a second time - a second "before" would be the
// demo's). The game's side is `Side` (devtools/demo.cpp: the profile roster and the skate module; thug2_viewer_check's
// a fake), with:
//   uint32_t current();                         the current skater's name
//   bool exists(uint32_t name);                 a skater by that name
//   void make_current(uint32_t name);
//   Kept keep(uint32_t name);                   a full copy of that skater's profile
//   void put_back(uint32_t name, Kept& kept);   the profile made the copy again
//   std::vector<uint8_t> profile(uint32_t name), details(uint32_t name);      its bytes (the demo's PROF / DETL)
//   void set_profile(uint32_t name, std::span<const uint8_t>), set_details(...);
//   Options options(); void set_options(const Options&);   the player's options that change a run
namespace devtools::skater_loan {

struct Options {
    bool auto_kick = false;
    bool spin_taps = false;
    float stat_override = 0.0F;
    bool operator==(const Options&) const = default;
};

// What a replay wants: its skater (0: the current one), its profile and info as recorded (empty: as they are), its
// options (none: as they are).
struct Wanted {
    uint32_t skater = 0;
    std::span<const uint8_t> profile;
    std::span<const uint8_t> details;
    std::optional<Options> options;
};

// What lending or giving back changed.
struct Changes {
    bool skater = false;  // another skater made current
    bool profile = false; // a profile's appearance, tricks or info written
    bool options = false;
    [[nodiscard]] bool any() const { return skater || profile || options; }
};

template <typename Side>
class Loan {
public:
    using Kept = decltype(std::declval<Side&>().keep(0U));

    [[nodiscard]] bool active() const { return active_; }

    // The replay's skater made the player's: the first call of a playback notes what the player had (the current
    // skater, the options; a copy of the profile about to be written, when one is - a replay of the player's own skater
    // as it is changes and keeps nothing); later calls of the same loan only make sure the replay's is still there.
    Changes lend(Side& side, const Wanted& wanted)
    {
        if (!active_) {
            active_ = true;
            player_ = side.current();
            player_profile_ = side.profile(player_);
            player_details_ = side.details(player_);
            options_ = side.options();
            lent_ = wanted.skater != 0 && side.exists(wanted.skater) ? wanted.skater : player_;
        }
        Changes changes;
        if (side.current() != lent_) {
            side.make_current(lent_);
            changes.skater = true;
        }
        const bool profile = !wanted.profile.empty() && !same(side.profile(lent_), wanted.profile);
        const bool details = !wanted.details.empty() && !same(side.details(lent_), wanted.details);
        if ((profile || details) && !kept_) {
            kept_.emplace(side.keep(lent_));
        }
        if (profile) {
            side.set_profile(lent_, wanted.profile);
        }
        if (details) {
            side.set_details(lent_, wanted.details);
        }
        changes.profile = profile || details;
        if (wanted.options && side.options() != *wanted.options) {
            side.set_options(*wanted.options);
            changes.options = true;
        }
        return changes;
    }

    // The player's own given back (nothing if no loan): the lent profile made its copy again, the player's skater
    // current, the options. `identical`: the player's profile and info byte for byte as before the loan.
    Changes give_back(Side& side, bool* identical)
    {
        Changes changes;
        if (!active_) {
            *identical = true;
            return changes;
        }
        active_ = false;
        if (kept_) {
            side.put_back(lent_, *kept_);
            changes.profile = true;
        }
        kept_.reset();
        if (side.current() != player_) {
            side.make_current(player_);
            changes.skater = true;
        }
        if (side.options() != options_) {
            side.set_options(options_);
            changes.options = true;
        }
        *identical = side.current() == player_ && side.profile(player_) == player_profile_ &&
                     side.details(player_) == player_details_ && side.options() == options_;
        return changes;
    }

private:
    static bool same(const std::vector<uint8_t>& have, std::span<const uint8_t> want)
    {
        return have.size() == want.size() && std::equal(have.begin(), have.end(), want.begin());
    }

    bool active_ = false;
    uint32_t player_ = 0;
    uint32_t lent_ = 0;
    std::vector<uint8_t> player_profile_;
    std::vector<uint8_t> player_details_;
    Options options_;
    std::optional<Kept> kept_;
};

} // namespace devtools::skater_loan
