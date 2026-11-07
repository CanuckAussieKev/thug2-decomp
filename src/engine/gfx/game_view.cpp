// NOT ORIGINAL - the game view (game_view.h): what the UI asks for and what the renderer last drew.

#include "engine/gfx/game_view.h"

#include <cstdint>

#include "base/core/no_destroy.h"

namespace render::game_view {

namespace {

constexpr int kSmallestSide = 16; // a smaller picture is none

struct GameViewState {
    Request asked;
    bool have_request{};
    Drawn drawn;
    bool on{};
};

GameViewState& game_view_state()
{
    static core::NoDestroy<GameViewState> state;
    return state.get();
}

} // namespace

void request(const Request& request)
{
    GameViewState& state = game_view_state();
    state.have_request = request.width >= kSmallestSide && request.height >= kSmallestSide;
    state.asked = request;
}

uint64_t texture() { return game_view_state().on ? game_view_state().drawn.texture : 0; }

Size drawn_size() { return game_view_state().on ? game_view_state().drawn.size : Size{}; }

bool take_request(Request* request)
{
    GameViewState& state = game_view_state();
    const bool asked = state.have_request;
    *request = state.asked;
    state.have_request = false;
    return asked;
}

void note_drawn(const Drawn& drawn)
{
    game_view_state().drawn = drawn;
    game_view_state().on = true;
}

void note_window()
{
    game_view_state().drawn = {};
    game_view_state().on = false;
}

bool on() { return game_view_state().on; }

Size size() { return game_view_state().drawn.size; }

bool window_to_picture(float window_x, float window_y, PicturePoint* point)
{
    const Drawn& drawn = game_view_state().drawn;
    if (!game_view_state().on || drawn.shown.width <= 0.0F || drawn.shown.height <= 0.0F) {
        return false;
    }
    point->x = static_cast<double>(window_x - drawn.shown.x) * drawn.size.width / drawn.shown.width;
    point->y = static_cast<double>(window_y - drawn.shown.y) * drawn.size.height / drawn.shown.height;
    return true;
}

LayoutMap layout_map() { return game_view_state().drawn.layout; }

} // namespace render::game_view
