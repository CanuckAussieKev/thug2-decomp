#pragma once

#include <array>
#include <cstdint>
#include "game/save_cards/memcard.h"
#include "game/save_cards/memcard_cfuncs.h"

// NOT ORIGINAL - the memory card screens' state their files share: the card, the out-of-space flag, the parameters and
// type kept for the next step and the loaded custom skater flag (the original's globals).
namespace save_cards {

// The memory card screens' state their files share: the card, the out-of-space flag, the parameters and type kept for
// the next step and the loaded custom skater flag. Made on first use, never destroyed.
class SaveCardsState {
public:
    [[nodiscard]] Hub* card_hub() const { return the_card_; }
    void set_card_hub(Hub* value) { the_card_ = value; }
    bool& out_of_space() { return out_of_space_; }
    script::Struct*& kept_params() { return kept_params_; }
    [[nodiscard]] uint32_t kept_type() const { return kept_type_; }
    void set_kept_type(uint32_t value) { kept_type_ = value; }
    bool& loaded_custom_skater() { return loaded_custom_skater_; }

private:
    // (memcard.cpp)
    Hub* the_card_{};

    // (memcard_cfuncs.cpp)
    bool out_of_space_{};
    script::Struct* kept_params_{};
    uint32_t kept_type_{};
    bool loaded_custom_skater_{};
};

// The state (made on first use, never destroyed).
SaveCardsState& save_cards_state();

} // namespace save_cards
