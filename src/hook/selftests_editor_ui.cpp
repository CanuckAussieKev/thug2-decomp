// Self-tests for the level editor's UI layer (src/game/front/level_editor_ui*.cpp, not original): the layout maths,
// the input-routing decision, and the fonts compressed into the DLL (decompressed and baked by ImGui, headless).

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "game/core/crc.h"
#include "game/front/level_editor.h"
#include "game/front/level_editor_nodes.h"
#include "game/front/level_editor_ui.h"
#include "game/front/level_editor_ui_fonts.h"
#include "game/front/level_editor_ui_layout.h"
#include "game/front/level_editor_ui_strip.h"
#include "game/front/level_editor_ui_theme.h"
#include "game/front/level_editor_ux8.h"
#include "hook/selftest.h"
#include "imgui.h"

namespace {

namespace L = Front::LevelEditor::UiLayout;
namespace LE = Front::LevelEditor;

bool Near(float a, float b, float tolerance = 0.01f) { return std::fabs(a - b) <= tolerance; }

// --- the layout -------------------------------------------------------------------------------------------------

void TestEditorUiLayout()
{
    // The scale: 1 at 1080p (Blender's / Unreal's 100 %), quarter steps of the smaller ratio, 0.875 to 3.
    Check(L::ScaleFor(1920, 1080) == 1.0f && L::ScaleFor(0, 0) == 1.0f, "editor UI: 1080p is 1");
    Check(L::ScaleFor(1280, 720) == 0.875f && L::ScaleFor(800, 600) == 0.875f && L::ScaleFor(1600, 900) == 0.875f,
          "editor UI: below 1080p 0.875 (Inter at 12 px)");
    Check(L::ScaleFor(2560, 1440) == 1.25f, "editor UI: 1440p is 1.25");
    Check(L::ScaleFor(3840, 2160) == 2.0f && L::ScaleFor(5120, 2880) == 2.5f && L::ScaleFor(7680, 4320) == 3.0f,
          "editor UI: 4K is 2, 5K 2.5, at most 3");
    Check(L::ScaleFor(3840, 1080) == 1.0f && L::ScaleFor(2000, 1200) == 1.0f,
          "editor UI: the smaller ratio, rounded down to a quarter");    L::FontSizes sizes = L::FontSizesFor(1.0f);
    Check(sizes.ui == 14.0f && sizes.small == 12.0f && sizes.mono == 13.0f, "editor UI: Inter 14 / 12, mono 13");
    sizes = L::FontSizesFor(1.5f);
    Check(sizes.ui == 21.0f && sizes.small == 18.0f && sizes.mono == 20.0f, "editor UI: font sizes scale (rounded)");

    // 1280 x 720 at scale 1: menu bar 22 over the tool strip 28, status 24, Outliner 280, Inspector 320, the viewport
    // between.
    L::Layout layout = L::Compute(1280.0f, 720.0f, 1.0f, true, true);
    Check(layout.outlinerShown && layout.inspectorShown, "editor UI: both panels at 1280x720");
    Check(Near(layout.toolbar.height, 50.0f) && Near(layout.status.height, 24.0f) && Near(layout.status.y, 696.0f),
          "editor UI: toolbar and status bar");
    Check(Near(layout.menu.y, 0.0f) && Near(layout.menu.height, 22.0f) && Near(layout.strip.y, 22.0f) &&
              Near(layout.strip.height, 28.0f),
          "editor UI: the menu bar over the tool strip");
    Check(Near(layout.outliner.width, 280.0f) && Near(layout.inspector.width, 320.0f) &&
              Near(layout.inspector.x, 960.0f),
          "editor UI: the side panels' widths");
    Check(Near(layout.viewport.x, 280.0f) && Near(layout.viewport.width, 680.0f) && Near(layout.viewport.y, 50.0f) &&
              Near(layout.viewport.height, 646.0f),
          "editor UI: the viewport fills the middle");
    Check(Near(layout.outliner.height, 646.0f) && Near(layout.outliner.y, 50.0f), "editor UI: panels between bars");
    // The parts tile the screen: their areas add up to it.
    float area = layout.toolbar.width * layout.toolbar.height + layout.status.width * layout.status.height +
                 layout.outliner.width * layout.outliner.height + layout.inspector.width * layout.inspector.height +
                 layout.viewport.width * layout.viewport.height;
    Check(Near(area, 1280.0f * 720.0f, 1.0f), "editor UI: the parts tile the window");
    // 4K at scale 3: the same proportions.
    layout = L::Compute(3840.0f, 2160.0f, 3.0f, true, true);
    Check(Near(layout.outliner.width, 840.0f) && Near(layout.inspector.width, 960.0f) &&
              Near(layout.toolbar.height, 150.0f) && Near(layout.viewport.width, 2040.0f),
          "editor UI: 4K at scale 3");
    // Narrow: the panels shrink so the viewport keeps 40%, then the Inspector closes, then the Outliner.
    layout = L::Compute(1000.0f, 720.0f, 1.0f, true, true);
    Check(layout.viewport.width >= 400.0f - 0.5f && layout.outliner.width >= 180.0f && layout.inspector.width >= 180.0f,
          "editor UI: narrow - panels shrink, the viewport keeps 40%");
    layout = L::Compute(640.0f, 480.0f, 1.0f, true, true);
    Check(layout.outlinerShown && !layout.inspectorShown && Near(layout.outliner.width, 180.0f),
          "editor UI: very narrow - the Inspector closes first");
    layout = L::Compute(250.0f, 480.0f, 1.0f, true, true);
    Check(!layout.outlinerShown && !layout.inspectorShown && Near(layout.viewport.width, 250.0f),
          "editor UI: too narrow for any panel");
    // Closed panels take no room.
    layout = L::Compute(1280.0f, 720.0f, 1.0f, false, true);
    Check(!layout.outlinerShown && Near(layout.viewport.x, 0.0f) && Near(layout.viewport.width, 960.0f),
          "editor UI: a closed Outliner takes no room");
    // On a panel or not.
    layout = L::Compute(1280.0f, 720.0f, 1.0f, true, true);
    Check(L::OnPanel(layout, 10.0f, 10.0f) && L::OnPanel(layout, 100.0f, 300.0f) && L::OnPanel(layout, 1000.0f, 300.0f) &&
              L::OnPanel(layout, 640.0f, 700.0f),
          "editor UI: toolbar, Outliner, Inspector, status bar are panels");
    Check(!L::OnPanel(layout, 640.0f, 360.0f) && !L::OnPanel(layout, 281.0f, 51.0f) && !L::OnPanel(layout, 959.0f, 693.0f),
          "editor UI: the viewport isn't");
    Check(L::OnPanel(layout, 279.9f, 300.0f) && !L::OnPanel(layout, 280.0f, 300.0f), "editor UI: the Outliner's edge");
    layout = L::Compute(1280.0f, 720.0f, 1.0f, false, false);
    Check(!L::OnPanel(layout, 100.0f, 300.0f), "editor UI: a closed panel isn't");
}
SELFTEST(Early, TestEditorUiLayout);

// --- the input routing ------------------------------------------------------------------------------------------

void TestEditorUiRouting()
{
    // Every combination, against the rules written out.
    int wrong = 0;
    for (int bits = 0; bits < 16; ++bits) {
        L::RouteInput in = {(bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0, (bits & 8) != 0};
        L::Route r = L::Decide(in);
        bool uiOwns = !in.editorCapture && (in.uiWantsMouse || in.overPanel);
        bool expectEditor = in.editorCapture || !uiOwns;
        bool expectUi = !in.editorCapture;
        if (r.mouseToEditor != expectEditor || r.mouseToUi != expectUi || r.keysToEditor != !in.uiWantsKeyboard)
            ++wrong;
        if (r.mouseToEditor && r.mouseToUi && (in.overPanel || in.uiWantsMouse))
            ++wrong; // (never both over a panel)
        if (!r.mouseToEditor && !r.mouseToUi)
            ++wrong; // (someone always has it)
    }
    Check(wrong == 0, "editor UI: routing over every combination");
    // The cases by name.
    L::Route r = L::Decide({false, false, false, false});
    Check(r.mouseToEditor && r.mouseToUi && r.keysToEditor, "editor UI: the viewport - the editor picks and flies");
    r = L::Decide({false, false, true, false});
    Check(!r.mouseToEditor && r.mouseToUi && r.keysToEditor, "editor UI: over a panel - the UI; keys stay the editor's");
    r = L::Decide({true, false, false, false});
    Check(!r.mouseToEditor && r.mouseToUi, "editor UI: ImGui holding the mouse (a slider dragged off its panel)");
    r = L::Decide({false, true, false, false});
    Check(r.mouseToEditor && !r.keysToEditor, "editor UI: typing in a field - the keys are the UI's");
    r = L::Decide({true, false, true, true});
    Check(r.mouseToEditor && !r.mouseToUi, "editor UI: a drag from the viewport keeps the mouse over a panel");
}
SELFTEST(Early, TestEditorUiRouting);

// --- the fonts --------------------------------------------------------------------------------------------------

void TestEditorUiFonts()
{
    namespace F = Front::LevelEditor::UiFonts;
    Check(F::Inter().size > 100000 && F::InterSemiBold().size > 100000 && F::Mono().size > 50000,
          "editor UI: the three fonts are in the DLL");
    // Decompressed and baked by ImGui in a context of its own (the editor's isn't touched).
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2013, 0x2026, 0x2190, 0x2193, 0x25B2, 0x25C0, 0};
    ImFont* inter = io.Fonts->AddFontFromMemoryCompressedTTF(F::Inter().data, F::Inter().size, 14.0f, nullptr, ranges);
    ImFont* bold = io.Fonts->AddFontFromMemoryCompressedTTF(F::InterSemiBold().data, F::InterSemiBold().size, 15.0f,
                                                            nullptr, ranges);
    ImFont* mono = io.Fonts->AddFontFromMemoryCompressedTTF(F::Mono().data, F::Mono().size, 13.0f, nullptr, ranges);
    bool built = inter && bold && mono && io.Fonts->Build();
    Check(built, "editor UI: the fonts decompress and bake");
    if (built) {
        Check(inter->FindGlyphNoFallback('A') && inter->FindGlyphNoFallback(0x00B7) &&
                  inter->FindGlyphNoFallback(0x2026) && inter->FindGlyphNoFallback(0x00B0),
              "editor UI: Inter has A, the middle dot, the ellipsis and the degree sign");
        Check(inter->FindGlyphNoFallback(0x25BC) != nullptr, "editor UI: Inter has the strip's list arrow (U+25BC)");
        const ImFontGlyph* i = mono->FindGlyphNoFallback('i');
        const ImFontGlyph* w = mono->FindGlyphNoFallback('W');
        const ImFontGlyph* zero = mono->FindGlyphNoFallback('0');
        Check(i && w && zero && i->AdvanceX == w->AdvanceX && w->AdvanceX == zero->AdvanceX,
              "editor UI: JetBrains Mono's figures and letters line up (one advance)");
        const ImFontGlyph* ii = inter->FindGlyphNoFallback('i');
        const ImFontGlyph* iw = inter->FindGlyphNoFallback('W');
        Check(ii && iw && ii->AdvanceX < iw->AdvanceX, "editor UI: Inter is proportional");
        // A frame with no backend: a window and a table lay out.
        int width = 0, height = 0;
        unsigned char* pixels = nullptr;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        Check(pixels && width > 0 && height > 0, "editor UI: the font atlas has pixels");
        io.DisplaySize = ImVec2(1280.0f, 720.0f);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(280.0f, 600.0f));
        ImGui::Begin("Outliner");
        ImGui::PushFont(mono);
        ImGui::Text("x %10.1f", -1842.0f);
        ImGui::PopFont();
        ImGui::End();
        ImGui::Render();
        ImDrawData* data = ImGui::GetDrawData();
        Check(data && data->Valid && data->TotalVtxCount > 0, "editor UI: a frame draws headless");
    }
    ImGui::DestroyContext(context);
    ImGui::SetCurrentContext(previous);
}
SELFTEST(Early, TestEditorUiFonts);

// --- the menu bar, the tool strip and the status bar: nothing overlaps (level_editor_ui_strip.h) -----------------

void TestEditorUiStripFit()
{
    using S = Front::LevelEditor::UiStrip::Slot;
    namespace U = Front::LevelEditor::UiStrip;
    // Room for all: left from the left, right against the right end, no overflow.
    std::vector<S> slots = {{50, 10, false, 5}, {30, 10, false, 2}, {40, 10, true, 1}, {20, 10, true, 4}};
    U::Plan plan = U::Fit(slots, 0.0f, 400.0f, 16.0f, 8.0f);
    Check(!plan.overflow && plan.shown[0] && plan.shown[1] && plan.shown[2] && plan.shown[3],
          "editor strip: everything fits");
    Check(Near(plan.x[0], 0.0f) && Near(plan.x[1], 60.0f) && Near(plan.x[3], 380.0f) && Near(plan.x[2], 330.0f),
          "editor strip: left from the left, right against the end");
    // 160 wide: 50 + 10 + 30 + 10 + 40 + 10 + 20 = 170 doesn't fit - the least important (priority 1) goes, and the
    // overflow button takes its room at the end.
    plan = U::Fit(slots, 0.0f, 160.0f, 16.0f, 8.0f);
    Check(plan.overflow && plan.shown[0] && plan.shown[3] && !plan.shown[2] && Near(plan.overflowX, 144.0f),
          "editor strip: the least important go into the overflow");
    Check(plan.x[3] + 20.0f <= plan.overflowX - 8.0f + 0.01f && plan.x[0] + 50.0f <= plan.x[3] - 10.0f + 0.01f,
          "editor strip: nothing overlaps the overflow button");
    // Too narrow for anything but the overflow button.
    plan = U::Fit(slots, 0.0f, 30.0f, 16.0f, 8.0f);
    Check(plan.overflow && !plan.shown[0] && !plan.shown[1] && !plan.shown[2] && !plan.shown[3],
          "editor strip: all in the overflow");
}
SELFTEST(Early, TestEditorUiStripFit);

// The three bars laid out headless with the editor's fonts and style at 1280 x 720, 1920 x 1080 and 2560 x 1440, at
// the automatic scale and at 100 / 150 / 200 %: no two controls overlap, every one is inside the window and its own
// bar, and whatever doesn't fit is in the overflow menu.
void TestEditorUiBarsFit()
{
    namespace U = Front::LevelEditor::UiStrip;
    namespace T = Front::LevelEditor::UiTheme;
    namespace Ui = Front::LevelEditor::Ui;
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    const float accent[3] = {0.94f, 0.48f, 0.24f};
    // A crowded state: everything the strip can show, long names, a long hint row (the screenshot's), a long status.
    U::State state;
    state.selection = true;
    state.tool = 1;
    state.snap = true;
    state.grid = 100.0f;
    state.grids = {1, 5, 10, 25, 50, 100};
    state.pickFilter = 5;
    state.level = "Shipyard Rooftops and Warehouse";
    state.unsaved = true;
    state.budget = true;
    state.budgetFill = 0.83f;
    state.budgetArgb = 0xFFE0A030u;
    state.errors = 12;
    state.warnings = 34;
    state.hidden = 128;
    state.simulating = true;
    state.keyOf = [](const char*) { return std::string("Ctrl+Shift+F"); };
    const std::vector<std::pair<std::string, std::string>> hints = {
        {"LMB", "drag: move"}, {"W E R", "tool (move)"}, {"RMB+WASD", "fly"}, {"Up Down Left Right", "nudge"},
        {"L", "whole chain / itself"}, {"Insert", "insert a point"}, {"End", "draw on"}, {"G", "drop"},
        {"Ctrl+D", "copy"}, {"Ctrl+Alt+D", "linked copy"}, {"Alt+R", "rail along an edge"}, {"F2", "arrange"},
        {"F", "frame"}, {"Shift+F", "follow"}, {"Q", "pie"}, {"Delete", "delete / hide"}};
    const std::string right = "Unsaved  \xC2\xB7  1234 changes  \xC2\xB7  567 moved  \xC2\xB7  -3952, 38, -516  \xC2\xB7  "
                              "speed 1500  \xC2\xB7  Ortho top, orbiting";
    Ui::Model model = {};
    model.mode = "SELECTED";
    struct Size {
        int w, h;
    };
    const Size sizes[] = {{1280, 720}, {1920, 1080}, {2560, 1440}};
    int bad = 0, cases = 0, overflowed = 0;
    for (const Size& size : sizes) {
        float autoScale = L::ScaleFor(size.w, size.h);
        for (float scale : {autoScale, 1.0f, 1.5f, 2.0f}) {
            T::Fonts fonts = T::BuildFonts(scale);
            T::ApplyStyle(scale, accent);
            Front::LevelEditor::Ux8::Fonts ux8;
            ux8.ui = fonts.ui;
            ux8.small = fonts.small;
            ux8.mono = fonts.mono;
            ux8.scale = scale;
            io.DisplaySize = ImVec2(static_cast<float>(size.w), static_cast<float>(size.h));
            io.DeltaTime = 1.0f / 60.0f;
            L::Layout layout = L::Compute(io.DisplaySize.x, io.DisplaySize.y, scale, true, true);
            std::vector<U::Box> menu, strip, status;
            float barHeights[3] = {};
            for (int frame = 0; frame < 2; ++frame) {
                ImGui::NewFrame();
                ImGui::PushFont(fonts.ui);
                const ImGuiWindowFlags panel = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;
                // (as level_editor_ui.cpp's Toolbar and StatusBar place them)
                U::SetRecorder(frame ? &menu : nullptr);
                ImGui::SetNextWindowPos(ImVec2(layout.menu.x, layout.menu.y));
                ImGui::SetNextWindowSize(ImVec2(layout.menu.width, layout.menu.height));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
                ImGui::Begin("##menubar", nullptr, panel | ImGuiWindowFlags_MenuBar);
                ImGui::PopStyleVar();
                barHeights[0] = ImGui::GetWindowHeight();
                std::vector<Ui::Request> requests;
                if (ImGui::BeginMenuBar()) {
                    Ui::MenuBar(model, ux8, &requests);
                    ImGui::EndMenuBar();
                }
                ImGui::End();
                U::SetRecorder(frame ? &strip : nullptr);
                ImGui::SetNextWindowPos(ImVec2(layout.strip.x, layout.strip.y));
                ImGui::SetNextWindowSize(ImVec2(layout.strip.width, layout.strip.height));
                ImGui::Begin("##toolstrip", nullptr, panel);
                barHeights[1] = ImGui::GetWindowHeight();
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * scale, 3.0f * scale));
                float padX = 8.0f * scale, padY = std::max(0.0f, (layout.strip.height - ImGui::GetFrameHeight()) * 0.5f);
                U::Output out;
                U::Draw(state, layout.strip.x + padX, layout.strip.y + padY, layout.strip.width - 2.0f * padX, scale,
                        &out);
                ImGui::PopStyleVar();
                ImGui::End();
                U::SetRecorder(frame ? &status : nullptr);
                ImGui::SetNextWindowPos(ImVec2(layout.status.x, layout.status.y));
                ImGui::SetNextWindowSize(ImVec2(layout.status.width, layout.status.height));
                ImGui::Begin("##status", nullptr, panel);
                barHeights[2] = ImGui::GetWindowHeight();
                ImGui::PushFont(fonts.small);
                float y = layout.status.y + std::max(0.0f, (layout.status.height - ImGui::GetTextLineHeight()) * 0.5f);
                ImGui::PopFont();
                U::StatusRow("", hints, right, layout.status.x + 8.0f * scale, y, layout.status.width - 16.0f * scale,
                             ux8);
                ImGui::End();
                U::SetRecorder(nullptr);
                ImGui::PopFont();
                ImGui::Render();
            }
            ++cases;
            char where[96];
            std::snprintf(where, sizeof(where), "%dx%d at %.0f%%", size.w, size.h, scale * 100.0f);
            auto checkRow = [&](const std::vector<U::Box>& boxes, const L::Rect& band, const char* row, size_t least) {
                bool ok = boxes.size() >= least;
                for (size_t a = 0; a < boxes.size(); ++a) {
                    const U::Box& p = boxes[a];
                    // (inside the window and within its bar's height)
                    if (p.x0 < -0.5f || p.x1 > band.x + band.width + 0.5f || p.y0 < band.y - 0.5f ||
                        p.y1 > band.y + band.height + 0.5f) {
                        ok = false;
                        char text[192];
                        std::snprintf(text, sizeof(text), "editor bars: %s %s \"%s\" outside (%.0f..%.0f, %.0f..%.0f)",
                                      where, row, p.what.c_str(), p.x0, p.x1, p.y0, p.y1);
                        Check(false, text);
                    }
                    for (size_t b = a + 1; b < boxes.size(); ++b) {
                        const U::Box& q = boxes[b];
                        if (std::min(p.x1, q.x1) - std::max(p.x0, q.x0) > 0.5f &&
                            std::min(p.y1, q.y1) - std::max(p.y0, q.y0) > 0.5f) {
                            ok = false;
                            char text[192];
                            std::snprintf(text, sizeof(text), "editor bars: %s %s \"%s\" overlaps \"%s\"", where, row,
                                          p.what.c_str(), q.what.c_str());
                            Check(false, text);
                        }
                    }
                }
                return ok;
            };
            // The windows themselves keep their bars' heights (ImGui's minimum window size once stretched the menu bar's
            // window down over the strip).
            const float wanted[3] = {layout.menu.height, layout.strip.height, layout.status.height};
            bool heights = true;
            for (int i = 0; i < 3; ++i) {
                if (std::abs(barHeights[i] - wanted[i]) > 0.5f) {
                    heights = false;
                    char text[128];
                    std::snprintf(text, sizeof(text), "editor bars: %s bar %d is %.0f high, not %.0f", where, i,
                                  barHeights[i], wanted[i]);
                    Check(false, text);
                }
            }
            bool ok = heights && checkRow(menu, layout.menu, "menu", 5) && checkRow(strip, layout.strip, "strip", 3) &&
                      checkRow(status, layout.status, "status", 2);
            // The core: Move and Problems always on the strip (the least important go first).
            bool move = false, problems = false, more = false;
            for (const U::Box& box : strip) {
                move = move || box.what == "Move";
                problems = problems || box.what == "Problems";
                more = more || box.what == "More";
            }
            overflowed += more ? 1 : 0;
            if (!move || !problems) {
                char text[128];
                std::snprintf(text, sizeof(text), "editor bars: %s - the core controls shown", where);
                Check(false, text);
                ok = false;
            }
            bad += ok ? 0 : 1;
        }
    }
    Check(cases == 12 && bad == 0, "editor bars: menu bar, tool strip and status bar fit without overlaps (12 cases)");
    Check(overflowed > 0 && overflowed < cases, "editor bars: the overflow menu is used when crowded, not always");
    ImGui::DestroyContext(context);
    ImGui::SetCurrentContext(previous);
}
SELFTEST(Early, TestEditorUiBarsFit);

// --- the node layer: families and picking (level_editor_nodes.h) -------------------------------------------------

void TestEditorNodeFamilies()
{
    namespace N = Front::LevelEditor::Nodes;
    auto crc = [](const char* name) { return Crc::GenerateCRCFromString(name); };
    struct Case {
        const char* cls;
        const char* type;
        N::Family family;
    };
    // Classes and types as the THUG2 levels and the ports have them (alc.qb, BA, TR...).
    static const Case kCases[] = {
        {"Restart", "Generic", N::Family::Spawn},         {"Restart", "Multiplayer", N::Family::Spawn},
        {"restart", "", N::Family::Spawn},                {"GenericNode", "Crown", N::Family::Crown},
        {"GenericNode", "Generic", N::Family::Other},     {"GameObject", "Flag_Red", N::Family::Flag},
        {"GameObject", "Flag_Blue_Base", N::Family::Flag}, {"GameObject", "Flag_Yellow", N::Family::Flag},
        {"GameObject", "Ghost", N::Family::Object},       {"RailNode", "Metal", N::Family::Rail},
        {"ClimbingNode", "Ladder", N::Family::Ladder},    {"Waypoint", "", N::Family::Path},
        {"LevelObject", "", N::Family::Object},           {"Pedestrian", "Ped_From_Profile", N::Family::Object},
        {"Vehicle", "Smart_Car", N::Family::Object},      {"LevelGeometry", "", N::Family::Scenery},
        {"LevelLight", "", N::Family::Light},             {"EmitterObject", "BoundingBox", N::Family::Sound},
        {"ParticleObject", "", N::Family::Particle},      {"ProximNode", "Camera", N::Family::Trigger},
        {"CAS_SubMesh", "", N::Family::Other},            {"", "", N::Family::Other},
    };
    int wrong = 0;
    for (const Case& c : kCases) {
        if (N::Classify(c.cls[0] ? crc(c.cls) : 0, c.type[0] ? crc(c.type) : 0) != c.family) {
            ++wrong;
            char text[96];
            std::snprintf(text, sizeof(text), "editor nodes: %s %s classified wrong", c.cls, c.type);
            Check(false, text);
        }
    }
    Check(wrong == 0, "editor nodes: classes and types to families");
    Check(N::ShapeOf(N::Family::Rail) == N::Shape::Chain && N::ShapeOf(N::Family::Path) == N::Shape::Chain &&
              N::ShapeOf(N::Family::Ladder) == N::Shape::Chain && N::ShapeOf(N::Family::Spawn) == N::Shape::Point &&
              N::ShapeOf(N::Family::Crown) == N::Shape::Point && N::ShapeOf(N::Family::Trigger) == N::Shape::Volume &&
              N::ShapeOf(N::Family::Light) == N::Shape::Volume && N::ShapeOf(N::Family::Scenery) == N::Shape::None,
          "editor nodes: the families' shapes");
    Check(std::string(N::FamilyName(N::Family::Spawn)) == "Spawns" && std::string(N::FamilyName(N::Family::Other)) == "Other",
          "editor nodes: family names");
    Check(N::Bit(N::Family::Spawn) == 1 && N::Bit(N::Family::Other) == 1u << 12 && N::kAllFamilies == 0x1FFF,
          "editor nodes: the chips' bits");
    Check(N::Online(N::Family::Spawn) && N::Online(N::Family::Crown) && N::Online(N::Family::Flag) &&
              !N::Online(N::Family::Rail) && !N::Online(N::Family::Light),
          "editor nodes: spawns, crowns and flags matter online");
    // Restart types, team colours, spawn colours.
    Check(N::RestartTypeBit(crc("Multiplayer")) == N::kMultiplayer && N::RestartTypeBit(crc("MultiPlayer")) == N::kMultiplayer &&
              N::RestartTypeBit(crc("Horse")) == N::kHorse && N::RestartTypeBit(crc("CTF_2")) == N::kCtf &&
              N::RestartTypeBit(crc("Player1")) == N::kPlayer1 && N::RestartTypeBit(crc("Skate")) == 0,
          "editor nodes: restart types");
    uint32_t red = N::TeamColour("TRG_CTF_Red"), blue = N::TeamColour("TRG_CTF_Restart_Blue");
    Check(red && blue && red != blue && N::TeamColour("Flag_Green_Base") && N::TeamColour("TRG_Team_Yellow01") &&
              N::TeamColour("trg_ctf_red") == red,
          "editor nodes: team colours from names and types");
    Check(!N::TeamColour("TRG_Reduce01") && !N::TeamColour("TRG_Restart01") && !N::TeamColour(nullptr) &&
              !N::TeamColour("Bluebird"),
          "editor nodes: no team colour without one");
    Check(N::SpawnColour(N::kCtf, red) == red && N::SpawnColour(N::kTeam | N::kMultiplayer, blue) == blue,
          "editor nodes: a team restart in its team's colour");
    Check(N::SpawnColour(N::kMultiplayer | N::kHorse, red) != red &&
              N::SpawnColour(N::kMultiplayer | N::kHorse, 0) == N::SpawnColour(N::kMultiplayer, 0) &&
              N::SpawnColour(N::kPlayer1, 0) != N::SpawnColour(N::kMultiplayer, 0) &&
              N::SpawnColour(N::kHorse, 0) != N::SpawnColour(0, 0),
          "editor nodes: spawn colours by type");
}
SELFTEST(Early, TestEditorNodeFamilies);

void TestEditorNodePicking()
{
    namespace N = Front::LevelEditor::Nodes;
    std::vector<N::Pickable> p;
    Check(N::PickNearest(p, 10.0f, 10.0f, 8.0f) == -1, "editor picking: nothing to pick");
    p.push_back({1, 100.0f, 100.0f, 100.0f, 100.0f, false, 500.0f});   // a point
    p.push_back({2, 50.0f, 200.0f, 150.0f, 200.0f, true, 300.0f});     // a segment across y 200
    p.push_back({3, 106.0f, 100.0f, 106.0f, 100.0f, false, 200.0f});   // a nearer point 6 px right of 1
    Check(N::PickNearest(p, 100.0f, 100.0f, 8.0f) == 1, "editor picking: the point under the cursor");
    Check(N::PickNearest(p, 106.0f, 101.0f, 8.0f) == 3, "editor picking: the nearer of two points");
    Check(N::PickNearest(p, 103.0f, 100.0f, 8.0f) == 3, "editor picking: equal distances go to the nearer to the camera");
    Check(N::PickNearest(p, 120.0f, 205.0f, 8.0f) == 2, "editor picking: a segment within reach");
    Check(N::PickNearest(p, 120.0f, 209.0f, 8.0f) == -1, "editor picking: a segment out of reach");
    Check(N::PickNearest(p, 160.0f, 200.0f, 8.0f) == -1, "editor picking: past a segment's end");
    Check(N::PickNearest(p, 300.0f, 300.0f, 8.0f) == -1, "editor picking: nothing near");
    // A point within reach beats a segment that's closer to the cursor.
    p.push_back({4, 120.0f, 206.0f, 120.0f, 206.0f, false, 900.0f});
    Check(N::PickNearest(p, 120.0f, 201.0f, 8.0f) == 4, "editor picking: a point beats a segment");
    // With the projection: a node projected to the pixels it's picked at.
    LE::Frame frame = LE::FrameFromYawPitch(0.3f, -0.2f);
    LE::Lens lens = LE::LensFor(72.0f, 1280.0f / 720.0f);
    const uint32_t viewport[4] = {0, 0, 1280, 720};
    LE::V3 camera = {100.0f, 200.0f, -300.0f};
    LE::V3 node = camera + frame.back * -900.0f + frame.right * 120.0f + frame.up * 40.0f;
    float nx, ny, px, py;
    bool projected = LE::Project(camera, frame, lens, node, 2.0f, &nx, &ny);
    LE::NdcToPixel(viewport, nx, ny, &px, &py);
    std::vector<N::Pickable> one = {{7, px, py, px, py, false, 900.0f}};
    Check(projected && px > 640.0f && py < 360.0f && N::PickNearest(one, px + 3.0f, py - 4.0f, 8.0f) == 7,
          "editor picking: a projected node picked where it's drawn");
}
SELFTEST(Early, TestEditorNodePicking);

} // namespace
