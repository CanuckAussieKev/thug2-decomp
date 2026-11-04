// NOT ORIGINAL - the memory card screens' state their files share: the card, the out-of-space flag, the parameters and type kept for the next step and the loaded custom skater flag (save_cards_state.h): made on first use, never destroyed.

#include "game/save_cards/save_cards_state.h"

#include "base/core/no_destroy.h"

namespace save_cards {

SaveCardsState& save_cards_state()
{
    static core::NoDestroy<SaveCardsState> state;
    return state.get();
}

} // namespace save_cards
