// Self-tests for the level editor's UI layer (src/game/front/level_editor_ui*.cpp, not original): the layout maths,
// the input-routing decision, and the fonts compressed into the DLL (decompressed and baked by ImGui, headless).

#include <cmath>
#include <cstdio>

#include "game/front/level_editor_ui_fonts.h"
#include "game/front/level_editor_ui_layout.h"
#include "hook/selftest.h"
#include "imgui.h"

namespace {

namespace L = Front::LevelEditor::UiLayout;

bool Near(float a, float b, float tolerance = 0.01f) { return std::fabs(a - b) <= tolerance; }

// --- the layout -------------------------------------------------------------------------------------------------

void TestEditorUiLayout()
{
    // The scale: 1 up to 1280 x 720, the smaller of the two ratios in quarter steps, at most 3.
    Check(L::ScaleFor(1280, 720) == 1.0f && L::ScaleFor(800, 600) == 1.0f && L::ScaleFor(0, 0) == 1.0f,
          "editor UI: scale 1 at 1280x720 and below");
    Check(L::ScaleFor(1920, 1080) == 1.5f, "editor UI: 1080p is 1.5");
    Check(L::ScaleFor(2560, 1440) == 2.0f, "editor UI: 1440p is 2");
    Check(L::ScaleFor(3840, 2160) == 3.0f && L::ScaleFor(7680, 4320) == 3.0f, "editor UI: 4K is 3, the most");
    Check(L::ScaleFor(3840, 1080) == 1.5f, "editor UI: the smaller ratio (an ultra-wide screen)");
    Check(L::ScaleFor(1600, 900) == 1.25f, "editor UI: quarter steps");
    L::FontSizes sizes = L::FontSizesFor(1.0f);
    Check(sizes.ui == 14.0f && sizes.small == 12.0f && sizes.mono == 13.0f, "editor UI: Inter 14 / 12, mono 13");
    sizes = L::FontSizesFor(1.5f);
    Check(sizes.ui == 21.0f && sizes.small == 18.0f && sizes.mono == 20.0f, "editor UI: font sizes scale (rounded)");

    // 1280 x 720: toolbar 36, status 26, Outliner 280, Inspector 320, the viewport between.
    L::Layout layout = L::Compute(1280.0f, 720.0f, 1.0f, true, true);
    Check(layout.outlinerShown && layout.inspectorShown, "editor UI: both panels at 1280x720");
    Check(Near(layout.toolbar.height, 36.0f) && Near(layout.status.height, 26.0f) && Near(layout.status.y, 694.0f),
          "editor UI: toolbar and status bar");
    Check(Near(layout.outliner.width, 280.0f) && Near(layout.inspector.width, 320.0f) &&
              Near(layout.inspector.x, 960.0f),
          "editor UI: the side panels' widths");
    Check(Near(layout.viewport.x, 280.0f) && Near(layout.viewport.width, 680.0f) && Near(layout.viewport.y, 36.0f) &&
              Near(layout.viewport.height, 658.0f),
          "editor UI: the viewport fills the middle");
    Check(Near(layout.outliner.height, 658.0f) && Near(layout.outliner.y, 36.0f), "editor UI: panels between bars");
    // The parts tile the screen: their areas add up to it.
    float area = layout.toolbar.width * layout.toolbar.height + layout.status.width * layout.status.height +
                 layout.outliner.width * layout.outliner.height + layout.inspector.width * layout.inspector.height +
                 layout.viewport.width * layout.viewport.height;
    Check(Near(area, 1280.0f * 720.0f, 1.0f), "editor UI: the parts tile the window");
    // 4K at scale 3: the same proportions.
    layout = L::Compute(3840.0f, 2160.0f, 3.0f, true, true);
    Check(Near(layout.outliner.width, 840.0f) && Near(layout.inspector.width, 960.0f) &&
              Near(layout.toolbar.height, 108.0f) && Near(layout.viewport.width, 2040.0f),
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
    Check(!L::OnPanel(layout, 640.0f, 360.0f) && !L::OnPanel(layout, 281.0f, 37.0f) && !L::OnPanel(layout, 959.0f, 693.0f),
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

} // namespace
