// NOT ORIGINAL - the level editor's UI layout and input routing (ours). See level_editor_ui_layout.h.

#include "game/front/level_editor_ui_layout.h"
#include "game/front/level_editor_ui.h"

#include <algorithm>
#include <cmath>

namespace Front::LevelEditor::UiLayout {

float ScaleFor(int windowWidth, int windowHeight)
{
    if (windowWidth <= 0 || windowHeight <= 0)
        return 1.0f;
    double scale = std::min(windowWidth / 1280.0, windowHeight / 720.0);
    scale = std::floor(scale * 4.0) / 4.0;
    return static_cast<float>(std::max(1.0, std::min(3.0, scale)));
}

FontSizes FontSizesFor(float scale)
{
    auto px = [scale](float base) { return std::round(base * scale); };
    return {px(14.0f), px(12.0f), px(13.0f), px(15.0f)};
}

Layout Compute(float width, float height, float scale, bool outlinerOpen, bool inspectorOpen)
{
    Layout layout = {};
    float toolbar = std::round(36.0f * scale), status = std::round(26.0f * scale);
    toolbar = std::min(toolbar, height * 0.25f);
    status = std::min(status, height * 0.25f);
    float middleTop = toolbar, middleHeight = std::max(0.0f, height - toolbar - status);
    layout.toolbar = {0.0f, 0.0f, width, toolbar};
    layout.status = {0.0f, height - status, width, status};
    // The side panels: 280 / 320 at scale 1, narrowed to 180 each so the viewport keeps 40% of the width.
    float left = outlinerOpen ? 280.0f * scale : 0.0f, right = inspectorOpen ? 320.0f * scale : 0.0f;
    float room = width * 0.6f;
    if (left + right > room) {
        float minimum = 180.0f * scale;
        float shrink = (left + right - room) / std::max(1.0f, left + right);
        left = left > 0.0f ? std::max(minimum, left * (1.0f - shrink)) : 0.0f;
        right = right > 0.0f ? std::max(minimum, right * (1.0f - shrink)) : 0.0f;
        // (still too wide: the Inspector closes first, then the Outliner)
        if (left + right > room && right > 0.0f)
            right = 0.0f;
        if (left > room)
            left = 0.0f;
    }
    left = std::round(left);
    right = std::round(right);
    layout.outlinerShown = left > 0.0f;
    layout.inspectorShown = right > 0.0f;
    layout.outliner = {0.0f, middleTop, left, middleHeight};
    layout.inspector = {width - right, middleTop, right, middleHeight};
    layout.viewport = {left, middleTop, std::max(0.0f, width - left - right), middleHeight};
    return layout;
}

bool OnPanel(const Layout& layout, float x, float y)
{
    return layout.toolbar.Contains(x, y) || layout.status.Contains(x, y) ||
           (layout.outlinerShown && layout.outliner.Contains(x, y)) ||
           (layout.inspectorShown && layout.inspector.Contains(x, y));
}

Route Decide(const RouteInput& input)
{
    Route route = {};
    if (input.editorCapture) {
        route.mouseToEditor = true;
    } else if (input.uiWantsMouse || input.overPanel) {
        route.mouseToUi = true;
    } else {
        route.mouseToUi = true;
        route.mouseToEditor = true;
    }
    route.keysToEditor = !input.uiWantsKeyboard;
    return route;
}

} // namespace Front::LevelEditor::UiLayout

// The Outliner's markers (level_editor_ui.h) - pure, beside the layout's other pure parts.
namespace Front::LevelEditor::Ui {

namespace {
struct MarkInfo {
    char letter;
    uint32_t colour;
    const char* name;
};
const MarkInfo kMarks[kMarkCount] = {
    {'M', 0xFFFFC040u, "moved (turned / scaled) by the editor - Home puts it back"},
    {'+', 0xFF60E060u, "added by the editor"},
    {'D', 0xFFFF6060u, "hidden in the level (Delete) - saved; Home / Ctrl+Z brings it back"},
    {'H', 0xFF9090A0u, "hidden while editing only (H / Isolate - Alt+H shows all; never saved)"},
    {'L', 0xFF60B0FFu, "locked: clicks pass it (L, the Inspector's Unlock)"},
    {'C', 0xFFFF40E0u, "its collision is switched off (the skater passes through)"},
};
} // namespace

char MarkLetter(int bit) { return bit >= 0 && bit < kMarkCount ? kMarks[bit].letter : '?'; }
uint32_t MarkColour(int bit) { return bit >= 0 && bit < kMarkCount ? kMarks[bit].colour : 0xFFFFFFFFu; }
const char* MarkName(int bit) { return bit >= 0 && bit < kMarkCount ? kMarks[bit].name : ""; }

bool MarkShown(uint8_t marks, bool changedOnly) { return !changedOnly || (marks & ((1u << kMarkCount) - 1)) != 0; }

std::string MarkLetters(uint8_t marks)
{
    std::string letters;
    for (int bit = 0; bit < kMarkCount; ++bit) {
        if (marks & (1u << bit))
            letters.push_back(kMarks[bit].letter);
    }
    return letters;
}

} // namespace Front::LevelEditor::Ui
