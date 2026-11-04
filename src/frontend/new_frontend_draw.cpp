// NOT ORIGINAL - the new front end's drawing (ours): see new_frontend_draw.h. The numbers are the design's
// (docs/design/frontend/HMain.dc.html, HLevels.dc.html - their CSS), in design pixels at 1280 x 720.

#include "frontend/new_frontend_draw.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <array>
#include <vector>

#include "frontend/new_frontend_mirror.h"
#include "frontend/new_frontend_model.h"
#include "frontend/new_frontend_options.h"
#include "frontend/new_frontend_text.h"
#include "platform/run_log.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "engine/util/unique_handle.h"
#include "engine/core/ints.h"
#include "engine/core/checked_index.h"
#include "engine/core/format.h"
#include "engine/core/no_destroy.h"
#include "engine/maths/rect.h"
#include "engine/maths/vec2.h"
#include "engine/maths/trig.h"
#include "engine/maths/maths.h"

namespace frontend::new_frontend::paint {

namespace {

constexpr float kBodyLineHeight = 1.5F; // and of the body text
constexpr float kBodyTextSize = 14.0F;
constexpr float kChannelMax = 255.0F;
constexpr float kColumnLeft = 48.0F;    // the left edge of the menu column, in design pixels
constexpr float kColumnRight = 1232.0F; // its right edge
constexpr float kColumnSpan = 1184.0F;  // its width (kColumnRight - kColumnLeft)
constexpr float kDegreesPerHalfTurn = 180.0F;
constexpr float kHalf = 0.5F;
constexpr float kHeadingTextSize = 30.0F;
constexpr float kHintsBottom = 688.0F; // the baseline of the hint bar
constexpr float kKeyTextSize = 20.0F;
constexpr float kLabelTextSize = 13.0F;
constexpr float kNameTextSize = 28.0F;
constexpr float kNoteTextSize = 12.0F;      // text sizes in design pixels
constexpr float kPanelHintsBottom = 692.0F; // the baseline of the hint bar under a panel
constexpr float kRowRight = 1240.0F;        // where a row ends and its hit box with it
constexpr float kRowTiltDegrees = 3.0F;     // the tilt a row starts its entrance at
constexpr float kStatTextSize = 16.0F;
constexpr float kStripCenterX = kDesignWidth / 2;          // the centre of the design canvas (the skater strip centres the selection on it)
constexpr float kTabLineHeight = 1.05F;
constexpr float kTextLineHeight = 1.3F; // the line height (times the text size) of the small text
constexpr float kTitleTextSize = 72.0F;
constexpr float kTypedTextSize = 48.0F;
constexpr float kVisibleThreshold = 0.001F; // an ease or fade below this counts as not started (or gone)

// --- layout numbers, in design pixels unless the name says otherwise; grouped by the screen or element they place ---

// Bits and bytes: shifts and masks that take a colour channel or a font file's big-endian field apart.
constexpr int kBitsPerByte = 8;
constexpr int kBitsPerTwoBytes = 16;
constexpr int kBitsPerThreeBytes = 24;
constexpr int kBitsPerWord = 32;
constexpr int kByteMask = 0xFF;
constexpr long kMinFontFileBytes = 12; // a font file smaller than its own header is not one
constexpr long kMaxFontFileMegabytes = 64;
constexpr long kBytesPerKilobyte = 1024;

// Glyph, arrow and art quads: the indices of a quad, how many copies the scrim draws, the shade and arrow edges.
constexpr int kGlyphQuadIndices = 6; // two triangles
constexpr float kArrowTrailingGap = 0.45F; // of the arrow's size, between it and the text after it
constexpr uint32_t kOpaqueWhite = 0xFFFFFFFF;
constexpr uint32_t kOpaqueBlack = 0xFF000000;
constexpr float kShadeAlpha = 0.88F;  // the art's foot shade at its darkest
constexpr float kShadeLeadIn = 32.0F; // the shade begins this far left of the right panel
constexpr int kScrimCopyCount = 9;    // the scrim averages this many offset copies of the art
constexpr int kScrimCopyIndices = 12; // two quads of six indices
constexpr int kScrimCopyVertices = 8; // two quads of four vertices
constexpr std::size_t kPad2BufferSize = 16;

// The back arrow and its hit box in the header, and the gap before the title after it.
constexpr float kBackHitPad = 6.0F;
constexpr float kBackLabelGap = 16.0F;

// Row entrances and the numbered rows: the slide a row makes as it comes in; the main menu's rows and the level
// list's (and the dialog's, which uses them) differ in how far their text and cut sit from the number.
constexpr float kRowSlideDistance = 24.0F;
constexpr float kRowNumberWidth = 26.0F;
constexpr float kRowCutLeadIn = 6.0F; // how far the cut starts left of the text
constexpr float kMainRowLineHeight = 0.92F;
constexpr float kMainRowPadTop = 2.0F;
constexpr float kMainRowNumberGap = 18.0F;
constexpr float kMainRowCutPad = 12.0F;
constexpr float kMainRowCutTail = 26.0F;
constexpr float kMainRowCutTop = 0.18F; // as a fraction of the line
constexpr float kMainRowCutBottom = 0.92F;
constexpr float kMainRowTopInset = 10.0F;
constexpr float kMainRowBottomInset = 18.0F;
constexpr float kLevelRowNumberGap = 14.0F;
constexpr float kLevelRowCutPad = 10.0F;
constexpr float kLevelRowCutTail = 22.0F;
constexpr float kLevelRowCutTop = 0.16F;
constexpr float kLevelRowCutBottom = 0.94F;
constexpr float kLevelRowTopInset = 8.0F;
constexpr float kLevelRowBottomInset = 14.0F;
constexpr float kMainListTopGap = 36.0F; // the main menu's first row under the header's rule
constexpr float kMainRowStep = 2.0F;     // the gap between two main menu rows
constexpr float kTabListGap = 18.0F;     // the level list under the last row of tabs

// The big name: the smallest it shrinks to for a word that is too wide, the clip box that is never the limit, and the
// half thickness of the cut line while it splits.
constexpr float kBigNameMinScale = 0.66F;
constexpr float kClipUnbounded = 1e6F;
constexpr float kSliceRuleHalfThickness = 2.0F;

// The pause screens: how far the game shows through, the clock text, the side titles' lift.
constexpr float kPauseGroundDim = 0.55F;
constexpr float kEditorGroundDim = 0.15F; // an editor's view shows through more
constexpr float kKeyboardGroundDim = 0.7F;
constexpr std::size_t kClockBufferSize = 32;
constexpr int kSecondsPerHour = 3600;
constexpr int kSecondsPerMinute = 60;
constexpr float kSideTitleLift = 10.0F;

// Tabs: the gap between rows of tabs and between tabs, the underline under the selected one, the hit box's side padding.
constexpr float kTabRowGap = 4.0F;
constexpr float kTabUnderlineTop = 5.0F;
constexpr float kTabUnderlineBottom = 6.0F;
constexpr float kTabHitPad = 4.0F;
constexpr float kTabGap = 16.0F;

// The slanted button (Skate, Open): its padding, the cut's inset, the gap before its arrow; its height is the heading
// text's line plus kButtonVerticalPad; the records start kButtonRecordsGap above it.
constexpr float kButtonCutInset = 10.0F;
constexpr float kButtonPadding = 22.0F;
constexpr float kButtonArrowGap = 10.0F;
constexpr float kButtonLineHeight = 1.15F;
constexpr float kButtonVerticalPad = 12.0F;
constexpr float kButtonRecordsGap = 6.0F;

// The options screen: the row's padding, the value's gap from the label, the right panel's stack of gaps (the choices
// control, the notes under it, the help text, the kicker above the name).
constexpr float kOptionRowPad = 6.0F;
constexpr float kOptionValueGap = 40.0F;
constexpr float kPanelStackGap = 14.0F;
constexpr float kHelpLineBox = 21.0F; // the help text's line: its size times its line height
constexpr float kHelpGap = 10.0F;
constexpr float kChoiceCutInset = 6.0F;
constexpr float kChoicePadX = 14.0F;
constexpr float kChoicePadY = 8.0F;
constexpr float kChoiceGap = 6.0F;
constexpr float kNoteLineBox = 15.6F; // the note's line: its size times its line height
constexpr float kNoteGap = 8.0F;
constexpr float kKickerGap = 8.0F;
constexpr float kScreenNoteTop = 36.0F; // the note along the top, level with the header
constexpr int kHitChoiceStride = 16;    // a choice's hit id is its row times this plus the choice's number

// Lists: the hit box reaches past the column, the clip around the rows and the rows kept in view.
constexpr float kRowHitOverhang = 20.0F;
constexpr float kListClipAbove = 2.0F;
constexpr float kListClipBelow = 8.0F;
constexpr float kScrollMargin = 2.0F; // the rows left in view past the selection
constexpr float kRowFitSlack = 10.0F;
constexpr float kLabelValueGap = 24.0F;
constexpr float kListHintsGap = 12.0F;
constexpr float kTagRaise = 0.12F; // a row's tag sits this fraction of the text size above its baseline

// The level select: the coloured edge, when the launch counts as done, the variant label's and records' text buffers,
// the kicker's gap, the wipe's skew.
constexpr float kColumnEdgeWidth = 4.0F;
constexpr float kLaunchGrowDone = 0.999F;
constexpr std::size_t kVariantLabelBuffer = 64;
constexpr std::size_t kRecordsBuffer = 64;
constexpr float kLevelKickerGap = 10.0F;
constexpr float kWipeSkewSides = 2.0F; // the skew is added on both sides of the window
constexpr float kWipePhases = 2.0F;    // the band comes in over the first half and goes out over the second

// The dialog: the dimmed screen, its band and the yellow edge, the title and the text under it, the hints' lift.
constexpr float kParagraphGap = 6.0F;
constexpr float kDialogDim = 0.62F;
constexpr float kDialogBandAlpha = 0.94F;
constexpr float kDialogEdgeHeight = 4.0F;
constexpr float kDialogTitleDrop = 56.0F;
constexpr float kDialogTitleLineHeight = 0.9F;
constexpr float kDialogTextGap = 14.0F;
constexpr int kDialogTextLines = 6;
constexpr float kDialogHintsLift = 18.0F;
constexpr float kHeaderTop = 36.0F;

// The skater select: the shades (top, panel, foot), the strip's clip, its cuts and hit boxes, the stat blocks.
constexpr float kSkaterTopShadeHeight = 170.0F;
constexpr float kSkaterTopShadeAlpha = 0.92F;
constexpr float kSkaterPanelFeather = 90.0F;
constexpr float kSkaterPanelAlpha = 0.86F;
constexpr float kSkaterFootShadeTop = 600.0F;
constexpr float kSkaterFootShadeAlpha = 0.9F;
constexpr float kStripClipAbove = 20.0F;
constexpr float kStripClipBelow = 60.0F;
constexpr float kStripCutLeadIn = 10.0F;
constexpr float kStripCutTop = 0.12F;
constexpr float kStripCutExtra = 26.0F;
constexpr float kStripCutBottom = 0.98F;
constexpr float kStripCutTopInset = 7.0F;
constexpr float kStripCutBottomInset = 10.0F;
constexpr float kStripTiltLift = 4.0F;
constexpr float kStripHitLeft = 10.0F;
constexpr float kStripHitRight = 16.0F;
constexpr float kStripHitVertical = 6.0F;
constexpr float kSkaterHintsLift = 22.0F;
constexpr int kStatBlockCount = 10;
constexpr float kStatBlockTop = 7.0F;
constexpr float kStatBlockGap = 4.0F;
constexpr float kStatBlockBottom = 19.0F;
constexpr float kStatBlockInset = 3.0F;

// The stats screen: the panel and its feather, the selected row's band and cut, the blocks, the hit padding, the
// points left and the text under the name.
constexpr float kStatsPanelAlpha = 0.88F;
constexpr float kStatsPanelFeather = 80.0F;
constexpr float kStatRowBandInset = 2.0F;
constexpr float kStatRowRightPad = 12.0F;
constexpr float kStatRowBandAlpha = 0.06F;
constexpr float kStatCutLeadIn = 12.0F;
constexpr float kStatCutTop = 0.18F;
constexpr float kStatCutExtra = 28.0F;
constexpr float kStatCutBottom = 0.82F;
constexpr float kStatCutInset = 6.0F;
constexpr int kStatsBlockCount = 10;
constexpr float kStatsBlockGap = 5.0F;
constexpr float kStatsBlockHeight = 14.0F;
constexpr float kStatsBlockInset = 3.0F;
constexpr float kStatBarHitPad = 6.0F;
constexpr float kStatsHintsGap = 16.0F;
constexpr float kPointsLineHeight = 0.95F;
constexpr float kPointsKickerGap = 4.0F;
constexpr float kPointsGap = 20.0F;
constexpr float kStatsTextTop = 96.0F;
constexpr int kStatsTextLines = 12;

// A colour menu's slider track: the value a track without a colour starts at, a full turn of hue, the fill and the
// marker when the row is not the selected one, the outline.
constexpr float kDefaultTrackValue = 40.0F;
constexpr float kDegreesPerTurn = 360.0F;
constexpr uint32_t kTrackFillIdle = 0xFF8C8C86;
constexpr uint32_t kMarkerIdle = 0xFFE8E8E0;
constexpr float kTrackOutlineAlpha = 0.6F;
constexpr float kTrackMinWidth = 40.0F; // a track narrower than this is not drawn

// The item lists: how many rows still count as few enough for the big type (the menu list, the pause list), the text
// under the name, the colour swatch and its numbers, the selected item's value.
constexpr int kListBigRows = 6;
constexpr int kPauseBigRows = 7;
constexpr int kPlainTextLines = 10;
constexpr std::size_t kSwatchNumbersBuffer = 48;
constexpr float kSwatchNumbersX = 76.0F;
constexpr float kSwatchNumbersDrop = 8.0F;
constexpr float kSwatchGap = 12.0F;
constexpr float kSelectedValueGap = 12.0F;
constexpr float kResumeHitPad = 6.0F;

// The on-screen keyboard: the typed text, its cursor bar and rule, the keys' rows and their hit ids.
constexpr float kTypedTextDrop = 12.0F;
constexpr float kCursorTop = 58.0F;
constexpr float kCursorBottom = 62.0F;
constexpr float kCursorWidth = 22.0F;
constexpr float kEntryRuleThickness = 2.0F;
constexpr float kKeyRowsDrop = 36.0F;
constexpr float kKeyHeight = 44.0F;
constexpr float kKeyGap = 8.0F;
constexpr float kKeyFillAlpha = 0.85F;
constexpr int kKeyHitStride = 64; // a key's hit id is its row times this plus its column

// The font file reader: an sfnt's header, its table records and the tags of the tables read (the tags are their four
// letters as a big-endian number), the head table's size and where it keeps the units per em.
constexpr std::size_t kSfntHeaderBytes = 12;
constexpr uint32_t kSfntVersionTrueType = 0x00010000;
constexpr uint32_t kTagTrue = 0x74727565; // true
constexpr uint32_t kTagOtto = 0x4F54544F; // OTTO
constexpr std::size_t kSfntTableRecordBytes = 16;
constexpr std::size_t kHeadTableBytes = 54;
constexpr uint32_t kTagHead = 0x68656164; // head
constexpr uint32_t kTagHhea = 0x68686561; // hhea
constexpr std::size_t kHeadUnitsPerEmOffset = 18;

// The font loader's limits (the ini's condense and bold), and the letterbox bars' threshold.
constexpr float kCondenseMin = 0.5F;
constexpr float kCondenseMax = 1.5F;
constexpr float kBoldMax = 0.1F;
constexpr float kLetterboxMargin = 0.5F; // the frame is offset by more than this: bars are drawn
// --- the colours (docs/design/frontend/README.md) ---
constexpr uint32_t kGround = 0xFF0A0A0A;
constexpr uint32_t kPaper = 0xFFF2EFE8;
constexpr uint32_t kMuted = 0xFFB9B5AC;
constexpr uint32_t kMuted2 = 0xFF8E8A82;
constexpr uint32_t kRule = 0xFF3A3A38;
constexpr uint32_t kArtGround = 0xFF1C1C1B;

ImU32 get_col(uint32_t argb, float alpha = 1.0F)
{
    const uint32_t alpha_byte =
        static_cast<uint32_t>(std::clamp(static_cast<float>((argb >> 24) & 0xFF) * alpha, 0.0F, 255.0F));
    return IM_COL32((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, alpha_byte);
}

uint32_t get_mix(uint32_t a, uint32_t b, float blend)
{
    blend = std::clamp(blend, 0.0F, 1.0F);
    uint32_t out = 0;
    for (int shift = 0; shift < kBitsPerWord; shift += kBitsPerByte) {
        auto x = static_cast<float>((a >> shift) & kByteMask);
        auto y = static_cast<float>((b >> shift) & kByteMask);
        out |= maths::round_non_negative<uint32_t>(x + ((y - x) * blend)) << shift;
    }
    return out;
}

// --- the fonts ---

// The sizes baked (CSS px at 1280 x 720): the display face's 120 (the big names: the level select's 112 drawn from
// it), 78 (the main menu), 40 (the levels), 30 (Skate); the mono face's 12 and 13; the body's 14.
constexpr auto kBakes = std::to_array<Bake>({
    {.face = Face::kDisplay, .css = 120.0F},
    {.face = Face::kDisplay, .css = kMainType},
    {.face = Face::kDisplay, .css = kLevelType},
    {.face = Face::kDisplay, .css = 28.0F},
    {.face = Face::kMono, .css = 12.0F},
    {.face = Face::kMono, .css = 13.0F},
    {.face = Face::kBody, .css = 14.0F},
    {.face = Face::kMono, .css = 20.0F},
    {.face = Face::kBody, .css = 48.0F}, // (the restyled keyboard's keys and the text typed)
});
constexpr int kBakeCount = static_cast<int>(sizeof(kBakes) / sizeof(core::at(kBakes, 0)));

struct FaceLayout {
    std::vector<uint8_t> file; // (kept: the atlas doesn't own it - several sizes share it)
    float ascent = 0.8F, descent = -0.2F;
    bool ok = false;
};
std::array<FaceLayout, 3>& stored_faces()
{
    static core::NoDestroy<std::array<FaceLayout, 3>> faces;
    return faces.get();
}

struct Baked {
    ImFont* font = nullptr;
    Face face = Face::kBody;
    float em_px_ = 0.0F; // the em in window px it was baked for
};
// NOT ORIGINAL grouping - new_frontend_draw.cpp's state (were globals). Made on first use, never destroyed
// (new_frontend_draw_state()).
struct NewFrontendDrawState {
    std::array<Baked, kBakeCount> baked_{};
    float scale_ = 0.0F;
    ImFontAtlas* atlas_ = nullptr; // (the atlas the fonts are in: another context's means none built here)
    float condense_ = 1.0F;
    float bold_ = 0.0F;
};

NewFrontendDrawState& new_frontend_draw_state()
{
    static core::NoDestroy<NewFrontendDrawState> state;
    return state.get();
}

// The display face's glyphs: upper case (the design sets it in capitals), figures, punctuation, the em dash. The
// mono and body faces: ASCII, the em dash, the middle dot, the apostrophe.
constexpr auto kDisplayRanges = std::to_array<ImWchar>({0x0020, 0x0060, 0x00C0, 0x00DE, 0x2014, 0x2014, 0x2019, 0x2019, 0});
constexpr auto kTextRanges = std::to_array<ImWchar>({0x0020, 0x007E, 0x00B7, 0x00B7, 0x00C0, 0x00FF, 0x2014, 0x2014, 0x2019, 0x2019, 0});

bool read_file(const std::string& path, std::vector<uint8_t>* out)
{
    const util::UniqueFile file = util::open_file(path.c_str(), "rb");
    if (!file) {
        return false;
    }
    std::fseek(file.get(), 0, SEEK_END);
    const long size = std::ftell(file.get());
    std::fseek(file.get(), 0, SEEK_SET);
    bool ok = size > kMinFontFileBytes && size < kMaxFontFileMegabytes * kBytesPerKilobyte * kBytesPerKilobyte;
    if (ok) {
        out->resize(static_cast<size_t>(size));
        ok = std::fread(out->data(), 1, out->size(), file.get()) == out->size();
    }
    return ok;
}

uint16_t be16(std::span<const uint8_t> bytes)
{
    return static_cast<uint16_t>(core::at(bytes, 0) << kBitsPerByte | core::at(bytes, 1));
}
uint32_t be32(std::span<const uint8_t> bytes)
{
    return static_cast<uint32_t>(core::at(bytes, 0)) << kBitsPerThreeBytes | core::at(bytes, 1) << kBitsPerTwoBytes |
           core::at(bytes, 2) << kBitsPerByte | core::at(bytes, 3);
}

const Baked* font_for(Face face, float css)
{
    const Baked* best = nullptr;
    for (const Baked& baked : new_frontend_draw_state().baked_) {
        if (!baked.font || baked.face != face) {
            continue;
        }
        // (the smallest bake at least as large, else the largest)
        const float baked_css = baked.em_px_ / std::max(new_frontend_draw_state().scale_, 0.01F);
        if (!best) {
            best = &baked;
            continue;
        }
        const float best_css = best->em_px_ / std::max(new_frontend_draw_state().scale_, 0.01F);
        const bool fits = baked_css >= css - 0.5F;
        const bool best_fits = best_css >= css - 0.5F;
        if ((fits && (!best_fits || baked_css < best_css)) || (!fits && !best_fits && baked_css > best_css)) {
            best = &baked;
        }
    }
    if (!best && face != Face::kBody) {
        return font_for(Face::kBody, css); // (a face that couldn't load: the body's)
    }
    if (!best && face != Face::kMono) {
        return font_for(Face::kMono, css);
    }
    return best;
}

const FaceLayout& data_of(Face face) { return core::at(stored_faces(), static_cast<int>(face)); }

// --- text ---

struct Style {
    Face face = Face::kBody;
    float css = 14.0F;     // its CSS size (design px)
    float tracking = 0.0F; // letter-spacing (design px)
    bool upper = false;
    uint32_t colour = kPaper;
    float alpha = 1.0F;
};

std::string get_upper(const char* text)
{
    std::string upper = text;
    for (char& character : upper) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }
    return upper;
}

// The scale from the baked font to the size asked, and the display face's width.
struct Gauges {
    const Baked* baked;
    float k;        // window px per baked px
    float x_scale_; // (the display face condensed)
    float bold_px_;
};
Gauges metrics_of(const Style& style, const TickCycle& frame)
{
    Gauges gauges = {.baked = font_for(style.face, style.css), .k = 1.0F, .x_scale_ = 1.0F, .bold_px_ = 0.0F};
    if (gauges.baked) {
        gauges.k = frame.current_s(style.css) / gauges.baked->em_px_;
    }
    if (style.face == Face::kDisplay && gauges.baked && gauges.baked->face == Face::kDisplay) {
        gauges.x_scale_ = new_frontend_draw_state().condense_;
        gauges.bold_px_ = new_frontend_draw_state().bold_ * frame.current_s(style.css);
    }
    return gauges;
}

// The text's width in window px.
float get_measure(const Style& style, const TickCycle& frame, const char* text)
{
    const Gauges gauges = metrics_of(style, frame);
    if (!gauges.baked) {
        return 0.0F;
    }
    const std::string shown = style.upper ? get_upper(text) : text;
    const char* cursor = shown.c_str();
    const char* end = std::next(cursor, static_cast<std::ptrdiff_t>(shown.size()));
    float width = 0.0F;
    int count = 0;
    while (cursor < end) {
        unsigned int codepoint = 0;
        cursor = std::next(cursor, ImTextCharFromUtf8(&codepoint, cursor, end));
        if (codepoint == 0U) {
            break;
        }
        const ImFontGlyph* glyph = gauges.baked->font->FindGlyph(static_cast<ImWchar>(codepoint));
        if (glyph) {
            width += glyph->AdvanceX * gauges.k * gauges.x_scale_;
        }
        ++count;
    }
    width += (frame.current_s(style.tracking) * static_cast<float>(count)) + gauges.bold_px_;
    return width;
}

// The text starting at window x `at.x` with its baseline at window y `at.y`.
void draw_text(ImDrawList* dl, const Style& style, const TickCycle& frame, maths::Vec2 at, const char* text)
{
    const float x = at.x;
    const float baseline = at.y;
    const Gauges gauges = metrics_of(style, frame);
    if (!gauges.baked) {
        return;
    }
    ImFont* font = gauges.baked->font;
    const std::string shown = style.upper ? get_upper(text) : text;
    const char* end = std::next(shown.c_str(), static_cast<std::ptrdiff_t>(shown.size()));
    const ImU32 colour = get_col(style.colour, style.alpha);
    const float top = baseline - (font->Ascent * gauges.k);
    for (int pass = 0; pass < (gauges.bold_px_ > 0.0F ? 2 : 1); ++pass) {
        float pen = x + ((pass != 0) ? gauges.bold_px_ : 0.0F);
        for (const char* cursor = shown.c_str(); cursor < end;) {
            unsigned int codepoint = 0;
            cursor = std::next(cursor, ImTextCharFromUtf8(&codepoint, cursor, end));
            if (codepoint == 0U) {
                break;
            }
            const ImFontGlyph* glyph = font->FindGlyph(static_cast<ImWchar>(codepoint));
            if (!glyph) {
                continue;
            }
            if (glyph->Visible) {
                const ImVec2 corner_min(pen + (glyph->X0 * gauges.k * gauges.x_scale_), top + (glyph->Y0 * gauges.k));
                const ImVec2 corner_max(pen + (glyph->X1 * gauges.k * gauges.x_scale_), top + (glyph->Y1 * gauges.k));
                dl->PrimReserve(kGlyphQuadIndices, 4);
                dl->PrimRectUV(corner_min, corner_max, ImVec2(glyph->U0, glyph->V0), ImVec2(glyph->U1, glyph->V1),
                               colour);
            }
            pen += (glyph->AdvanceX * gauges.k * gauges.x_scale_) + frame.current_s(style.tracking);
        }
    }
}

// The baseline of a line box at window y `top` for the style at `lineHeight` (a multiple of its size).
float datum(const Style& style, const TickCycle& frame, float top, float line_height)
{
    const Baked* baked = font_for(style.face, style.css);
    const FaceLayout& data = data_of(baked ? baked->face : style.face);
    return top + frame.current_s(baseline_in_line(data.ascent, data.descent, style.css, line_height));
}

// Words of `text` in lines no wider than `width` (window px).
std::vector<std::string> get_wrap(const Style& style, const TickCycle& frame, const std::string& text, float width)
{
    std::vector<std::string> lines;
    std::string line;
    size_t at = 0;
    while (at < text.size()) {
        const size_t space = text.find(' ', at);
        const std::string word = text.substr(at, space == std::string::npos ? std::string::npos : space - at);
        at = space == std::string::npos ? text.size() : space + 1;
        if (word.empty()) {
            continue;
        }
        std::string tried = line.empty() ? word : line;
        if (!line.empty()) {
            tried.append(" ").append(word);
        }
        if (!line.empty() && get_measure(style, frame, tried.c_str()) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = tried;
        }
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

// --- shapes ---

// A cut's slant: how far its top edge starts right of the box's left, and its bottom edge ends left of the box's right.
struct Slant {
    float top;
    float bottom;
};

// The design's clip-path: polygon(a 0, 100% 0, calc(100% - b) 100%, 0 100%) - a parallelogram leaning right.
void get_cut(ImDrawList* dl, const maths::Rect& box, Slant slant, ImU32 colour)
{
    std::array<ImVec2, 4> points = {
        {{box.x0 + slant.top, box.y0}, {box.x1, box.y0}, {box.x1 - slant.bottom, box.y1}, {box.x0, box.y1}}};
    dl->AddConvexPolyFilled(points.data(), 4, colour);
}

// The vertices added since `start` turned by `radians` about `pivot` and moved by `offset` - the row's
// translateX(24px) rotate(-3deg) about its left centre.
void xform(ImDrawList* dl, int start, maths::Vec2 pivot, float radians, maths::Vec2 offset)
{
    const float px = pivot.x;
    const float py = pivot.y;
    const float dx = offset.x;
    const float dy = offset.y;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    for (int i = start; i < dl->VtxBuffer.Size; ++i) {
        ImVec2& vertex_position = core::at(dl->VtxBuffer, i).pos;
        const float x = vertex_position.x - px;
        const float y = vertex_position.y - py;
        vertex_position.x = px + dx + (x * cosine) - (y * sine);
        vertex_position.y = py + dy + (x * sine) + (y * cosine);
    }
}

// An arrow (the design's "<-" / "->" in the mono face): a line with a head, `size` px tall, at the text's x-height -
// from window x `at.x`, on the baseline `at.y`.
void get_arrow(ImDrawList* dl, maths::Vec2 at, float size, bool left, ImU32 colour, float* advance)
{
    const float x = at.x;
    const float baseline = at.y;
    const float w = size * 0.9F;
    const float y = baseline - (size * 0.32F);
    const float thickness = std::max(1.0F, size * 0.09F);
    const float x0 = x;
    const float x1 = x + w;
    dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), colour, thickness);
    const float hx = left ? x0 : x1;
    const float dir = left ? 1.0F : -1.0F;
    const float head = size * 0.3F;
    dl->AddLine(ImVec2(hx, y), ImVec2(hx + (dir * head), y - head), colour, thickness);
    dl->AddLine(ImVec2(hx, y), ImVec2(hx + (dir * head), y + head), colour, thickness);
    *advance = w + (size * kArrowTrailingGap);
}

// How a piece of art shows: its pan (0 - 1 across the part the box doesn't cover) and its opacity.
struct ArtLook {
    float pan;
    float alpha;
};

void art_box(ImDrawList* dl, const Art& art, const maths::Rect& box, ArtLook look)
{
    const float x0 = box.x0;
    const float y0 = box.y0;
    const float x1 = box.x1;
    const float y1 = box.y1;
    const float alpha = look.alpha;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), get_col(kArtGround, alpha));
    if (art.texture == 0U) {
        return;
    }
    Uv uv = cover_uv({.width = art.width, .height = art.height}, {.width = x1 - x0, .height = y1 - y0}, look.pan);
    if (art.bottom_up_) {
        uv.v0 = 1.0F - uv.v0;
        uv.v1 = 1.0F - uv.v1;
    }
    dl->AddImage(static_cast<ImTextureID>(art.texture), ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(uv.u0, uv.v0),
                 ImVec2(uv.u1, uv.v1), get_col(kOpaqueWhite, alpha));
}

// The bottom of the art darkened so the words over it read (not in the mock-ups, which have flat grey there).
void get_shade(ImDrawList* dl, const maths::Rect& box)
{
    const float mid = box.y0 + ((box.y1 - box.y0) * 0.35F);
    dl->AddRectFilledMultiColor(ImVec2(box.x0, mid), ImVec2(box.x1, box.y1), get_col(kGround, 0.0F),
                                get_col(kGround, 0.0F), get_col(kGround, kShadeAlpha), get_col(kGround, kShadeAlpha));
}

// The list column's scrim (the user's change to the mock-ups, 2026-10-25: the art behind the whole screen, the column
// on a dark, softened copy of it rather than a flat panel): the art's column part drawn again a few times a little
// apart (averaged: a soft blur), darkened ~75 %, its right edge feathered over kScrimFeather. `art` may be empty (the
// pause menus: the game behind - only darkened).
void scrim(ImDrawList* dl, const TickCycle& frame, const Art& art, ArtLook look)
{
    const float alpha = look.alpha;
    if (alpha <= kVisibleThreshold) {
        return;
    }
    const float x1 = kColumn + kScrimFeather;
    if (art.texture != 0U) {
        Uv uv = cover_uv({.width = art.width, .height = art.height}, {.width = kDesignWidth, .height = kDesignHeight}, look.pan);
        if (art.bottom_up_) {
            uv.v0 = 1.0F - uv.v0;
            uv.v1 = 1.0F - uv.v1;
        }
        // (the column's share of the picture)
        const float share = x1 / 1280.0F;
        const float u1 = uv.u0 + ((uv.u1 - uv.u0) * share);
        const float u_edge = uv.u0 + ((uv.u1 - uv.u0) * (kColumn / 1280.0F));
        static constexpr auto kOffsets = std::to_array<std::array<float, 2>>(
            {{0, 0}, {9, 0}, {-9, 0}, {0, 9}, {0, -9}, {6, 6}, {-6, -6}, {6, -6}, {-6, 6}});
        dl->PushTextureID(static_cast<ImTextureID>(art.texture));
        dl->PushClipRect(ImVec2(frame.get_x(0.0F), frame.get_y(0.0F)),
                         ImVec2(frame.get_x(x1), frame.get_y(kDesignHeight)), true);
        for (int i = 0; i < kScrimCopyCount; ++i) {
            const float dx = frame.current_s(core::at(kOffsets, i).front());
            const float dy = frame.current_s(core::at(kOffsets, i).back());
            // (copy i at 1 / (i + 1): the result is the copies' average; across the feather it fades to the art)
            const ImU32 full = get_col(kOpaqueWhite, alpha / static_cast<float>(i + 1));
            const ImU32 none = get_col(kOpaqueWhite, 0.0F);
            const float ya = frame.get_y(0.0F) + dy;
            const float yb = frame.get_y(720.0F) + dy;
            const float xa = frame.get_x(0.0F) + dx;
            const float xm = frame.get_x(kColumn) + dx;
            const float xb = frame.get_x(x1) + dx;
            dl->PrimReserve(kScrimCopyIndices, kScrimCopyVertices);
            auto base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
            dl->PrimWriteVtx(ImVec2(xa, ya), ImVec2(uv.u0, uv.v0), full);
            dl->PrimWriteVtx(ImVec2(xm, ya), ImVec2(u_edge, uv.v0), full);
            dl->PrimWriteVtx(ImVec2(xm, yb), ImVec2(u_edge, uv.v1), full);
            dl->PrimWriteVtx(ImVec2(xa, yb), ImVec2(uv.u0, uv.v1), full);
            dl->PrimWriteVtx(ImVec2(xm, ya), ImVec2(u_edge, uv.v0), full);
            dl->PrimWriteVtx(ImVec2(xb, ya), ImVec2(u1, uv.v0), none);
            dl->PrimWriteVtx(ImVec2(xb, yb), ImVec2(u1, uv.v1), none);
            dl->PrimWriteVtx(ImVec2(xm, yb), ImVec2(u_edge, uv.v1), full);
            for (int quad = 0; quad < 2; ++quad) {
                auto base_index = static_cast<ImDrawIdx>(base + (quad * 4));
                dl->PrimWriteIdx(base_index);
                dl->PrimWriteIdx(static_cast<ImDrawIdx>(base_index + 1));
                dl->PrimWriteIdx(static_cast<ImDrawIdx>(base_index + 2));
                dl->PrimWriteIdx(base_index);
                dl->PrimWriteIdx(static_cast<ImDrawIdx>(base_index + 2));
                dl->PrimWriteIdx(static_cast<ImDrawIdx>(base_index + 3));
            }
        }
        dl->PopClipRect();
        dl->PopTextureID();
    }
    const float dark = 0.8F * alpha;
    dl->AddRectFilled(ImVec2(frame.get_x(0.0F), frame.get_y(0.0F)),
                      ImVec2(frame.get_x(kColumn), frame.get_y(kDesignHeight)), get_col(kGround, dark));
    dl->AddRectFilledMultiColor(ImVec2(frame.get_x(kColumn), frame.get_y(0.0F)),
                                ImVec2(frame.get_x(x1), frame.get_y(kDesignHeight)), get_col(kGround, dark),
                                get_col(kGround, 0.0F), get_col(kGround, 0.0F), get_col(kGround, dark));
}

// The whole screen's art (full-bleed), the words' shade at the foot, the column's scrim.
void backdrop(ImDrawList* dl, const TickCycle& frame, const Art& art, float pan)
{
    art_box(dl, art,
            {.x0 = frame.get_x(0.0F),
             .y0 = frame.get_y(0.0F),
             .x1 = frame.get_x(kDesignWidth),
             .y1 = frame.get_y(kDesignHeight)},
            {.pan = pan, .alpha = 1.0F});
    get_shade(dl, {.x0 = frame.get_x(kRight - kShadeLeadIn),
                   .y0 = frame.get_y(0.0F),
                   .x1 = frame.get_x(kDesignWidth),
                   .y1 = frame.get_y(kDesignHeight)});
    scrim(dl, frame, art, {.pan = pan, .alpha = 1.0F});
}

std::string pad2(int value)
{
    std::array<char, kPad2BufferSize> text{};
    text::format_to(text, "%02d", value);
    return text.data();
}

// --- one screen's parts ---

// A horizontal extent in design x: from `left` to `right`.
struct Band {
    float left;
    float right;
};

// A launch's slice through the big name: how far (0: none) and the cut's colour.
struct Slice {
    float amount;
    uint32_t colour;
};

// A colour and an opacity.
struct Ink {
    uint32_t colour{};
    float alpha = 1.0F;
};

struct Ctx {
    ImDrawList* dl;
    // (views of the caller's data, while the frame is drawn)
    const TickCycle* f;
    const Menu* menu;
    const Extras* extras;
    std::vector<Hit>* hits;
    void get_hit(Hit::Kind kind, int index, float x0, float y0, float x1, float y1) const
    {
        if (hits) {
            hits->push_back({.kind = kind, .index = index, .x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1});
        }
    }
};

Style mono12(uint32_t colour, bool tracked = true)
{
    Style style;
    style.face = Face::kMono;
    style.css = kNoteTextSize;
    style.tracking = tracked ? 1.0F : 0.0F;
    style.upper = true;
    style.colour = colour;
    return style;
}

// A header row whose top right is `top_right` (design; its left at 48): the texts, the rule under it. Returns the
// rule's y.
float get_header(Ctx& ctx, maths::Vec2 top_right, const char* left_text, bool back, const char* right_text)
{
    const float top = top_right.y;
    const float right = top_right.x;
    Style style = mono12(kMuted);
    const float line_h = 1.3F;
    const float base = datum(style, *ctx.f, ctx.f->get_y(top), line_h);
    float x = ctx.f->get_x(kColumnLeft);
    if (back) {
        const Style back_style = mono12(kMuted, false);
        float adv = 0.0F;
        get_arrow(ctx.dl, {.x = x, .y = base}, ctx.f->current_s(kNoteTextSize), true, get_col(kMuted), &adv);
        draw_text(ctx.dl, back_style, *ctx.f, {.x = x + adv, .y = base}, text_tools::kBack);
        const float w = adv + get_measure(back_style, *ctx.f, text_tools::kBack);
        ctx.get_hit(Hit::Kind::kBack, 0, kColumnLeft - kBackHitPad, top - kBackHitPad,
                    kColumnLeft + (w / ctx.f->scale()) + kBackHitPad, top + (kNoteTextSize * line_h) + kBackHitPad);
        x += w + ctx.f->current_s(kBackLabelGap);
        style.colour = kPaper;
    }
    draw_text(ctx.dl, style, *ctx.f, {.x = x, .y = base}, left_text);
    if (right_text && (*right_text != '\0')) {
        const Style right_style = mono12(kMuted);
        draw_text(ctx.dl, right_style, *ctx.f,
                  {.x = ctx.f->get_x(right) - get_measure(right_style, *ctx.f, right_text), .y = base}, right_text);
    }
    const float rule = top + (12.0F * line_h) + 12.0F;
    ctx.dl->AddRectFilled(ImVec2(ctx.f->get_x(kColumnLeft), ctx.f->get_y(rule)),
                          ImVec2(ctx.f->get_x(right), ctx.f->get_y(rule) + std::max(1.0F, ctx.f->current_s(1.0F))),
                          get_col(kRule));
    return rule + 1.0F;
}

// A list row with the kinetic selection: number, name, the cut behind the name when selected - all turned and moved
// by the selection's amount `e`. Design numbers: the row's top `top`, its line box `line_h`, the name's left `text_x_`,
// the number's style, the cut's extents and insets.
struct RowStyle {
    Style name;
    Style number;
    float line_height_{}; // (a multiple of the name's size)
    float row_pad_top_{}; // (above the line box)
    float row_height_{};
    float text_x_{};                 // the name's left
    float cut_left_{};               // the cut's left (the label span's left - 6)
    float cut_right_pad_{};          // past the name's right (span padding + 22 / 26)
    float cut_top_{}, cut_bottom_{}; // (fractions of the line box)
    float top_inset_{}, bottom_inset_{};
};

// A row's state: how far it is selected (0 - 1: the cut and the turn), its cut's colour, dimmed (not focusable).
struct RowLook {
    float amount;
    uint32_t cut;
    bool dim;
};

void get_row(Ctx& ctx, const RowStyle& rs, float top, const char* number, const char* label, RowLook look)
{
    const float amount = look.amount;
    const uint32_t cut_colour = look.cut;
    const bool dim = look.dim;
    ImDrawList* dl = ctx.dl;
    const int start = dl->VtxBuffer.Size;
    const float line_top = top + rs.row_pad_top_;
    const float line_h = rs.name.css * rs.line_height_;
    const float base = datum(rs.name, *ctx.f, ctx.f->get_y(line_top), rs.line_height_);
    const float width = get_measure(rs.name, *ctx.f, label);
    if (amount > kVisibleThreshold) {
        const float x0 = ctx.f->get_x(rs.cut_left_);
        float x1 = ctx.f->get_x(rs.text_x_) + width + ctx.f->current_s(rs.cut_right_pad_);
        x1 = x0 + ((x1 - x0) * amount); // (the cut snaps out from the left)
        get_cut(dl,
                {.x0 = x0,
                 .y0 = ctx.f->get_y(line_top + (line_h * rs.cut_top_)),
                 .x1 = x1,
                 .y1 = ctx.f->get_y(line_top + (line_h * rs.cut_bottom_))},
                {.top = ctx.f->current_s(rs.top_inset_), .bottom = ctx.f->current_s(rs.bottom_inset_)},
                get_col(cut_colour));
    }
    Style name = rs.name;
    name.colour = get_mix(dim ? kMuted2 : kPaper, kGround, amount);
    draw_text(dl, name, *ctx.f, {.x = ctx.f->get_x(rs.text_x_), .y = base}, label);
    const Style num = rs.number;
    draw_text(dl, num, *ctx.f, {.x = ctx.f->get_x(kColumnLeft), .y = base}, number);
    if (amount > kVisibleThreshold) {
        const float pivot_y = ctx.f->get_y(top + (rs.row_height_ * 0.5F));
        xform(dl, start, {.x = ctx.f->get_x(kColumnLeft), .y = pivot_y},
              -kRowTiltDegrees * maths::kPi / kDegreesPerHalfTurn * amount,
              {.x = ctx.f->current_s(kRowSlideDistance) * amount, .y = 0.0F});
    }
}

RowStyle main_row_style()
{
    RowStyle rs;
    rs.name.face = Face::kDisplay;
    rs.name.css = kMainType;
    rs.name.upper = true;
    rs.number = mono12(kMuted, false);
    rs.number.css = kLabelTextSize;
    rs.line_height_ = kMainRowLineHeight;
    rs.row_pad_top_ = kMainRowPadTop;
    rs.row_height_ = kMainRowHeight;
    rs.text_x_ = kColumnLeft + kRowNumberWidth + kMainRowNumberGap + kMainRowCutPad;
    rs.cut_left_ = kColumnLeft + kRowNumberWidth + kMainRowNumberGap - kRowCutLeadIn;
    rs.cut_right_pad_ = kMainRowCutPad + kMainRowCutTail;
    rs.cut_top_ = kMainRowCutTop;
    rs.cut_bottom_ = kMainRowCutBottom;
    rs.top_inset_ = kMainRowTopInset;
    rs.bottom_inset_ = kMainRowBottomInset;
    return rs;
}

RowStyle level_row_style()
{
    RowStyle rs;
    rs.name.face = Face::kDisplay;
    rs.name.css = kLevelType;
    rs.name.upper = true;
    rs.number = mono12(kMuted2, false);
    rs.line_height_ = kTabLineHeight;
    rs.row_pad_top_ = 0.0F;
    rs.row_height_ = kLevelRowHeight;
    rs.text_x_ = kColumnLeft + kRowNumberWidth + kLevelRowNumberGap + kLevelRowCutPad;
    rs.cut_left_ = kColumnLeft + kRowNumberWidth + kLevelRowNumberGap - kRowCutLeadIn;
    rs.cut_right_pad_ = kLevelRowCutPad + kLevelRowCutTail;
    rs.cut_top_ = kLevelRowCutTop;
    rs.cut_bottom_ = kLevelRowCutBottom;
    rs.top_inset_ = kLevelRowTopInset;
    rs.bottom_inset_ = kLevelRowBottomInset;
    return rs;
}

// The big name at the right's foot: lines of `name` (Anton `css`, line-height 0.85) ending at design y `bottom`, from
// x `left` to `right`. Returns the top of the block. With `slice` > 0 (the launch), the top half moves up and the
// bottom half down along a cut, fading.
float big_name(Ctx& ctx, const std::string& name, float css, Band band, float bottom, Slice cut)
{
    const float left = band.left;
    const float right = band.right;
    const float slice = cut.amount;
    const uint32_t slice_colour = cut.colour;
    Style style;
    style.face = Face::kDisplay;
    style.css = css;
    style.upper = true;
    std::vector<std::string> lines = get_wrap(style, *ctx.f, name, ctx.f->current_s(right - left));
    if (lines.size() > 3) {
        lines.resize(3);
    }
    // (a word wider than the space - CREATE-A-SKATER - makes the type smaller, to two thirds at most)
    float widest = 0.0F;
    for (const std::string& line : lines) {
        widest = std::max(widest, get_measure(style, *ctx.f, line.c_str()));
    }
    if (widest > ctx.f->current_s(right - left)) {
        css = std::max(css * kBigNameMinScale, css * ctx.f->current_s(right - left) / widest);
        style.css = css;
    }
    const float line_h = css * 0.85F;
    const float top = bottom - (line_h * static_cast<float>(lines.size()));
    const float mid = ctx.f->get_y(top + ((bottom - top) * 0.55F));
    for (int half = 0; half < (slice > 0.0F ? 2 : 1); ++half) {
        const float dy = slice > 0.0F ? (half == 0 ? -1.0F : 1.0F) * ctx.f->current_s(70.0F) * slice : 0.0F;
        style.alpha = 1.0F - slice;
        if (slice > 0.0F) {
            // (each half keeps its own part of the name: the cut moves with it, the gap between opens)
            if (half == 0) {
                ctx.dl->PushClipRect(ImVec2(0, 0), ImVec2(kClipUnbounded, mid + dy), true);
            } else {
                ctx.dl->PushClipRect(ImVec2(0, mid + dy), ImVec2(kClipUnbounded, kClipUnbounded), true);
            }
        }
        for (size_t i = 0; i < lines.size(); ++i) {
            const float line_top = top + (line_h * static_cast<float>(i));
            const float base = datum(style, *ctx.f, ctx.f->get_y(line_top), 0.85F);
            draw_text(ctx.dl, style, *ctx.f, {.x = ctx.f->get_x(left), .y = base + dy}, core::at(lines, i).c_str());
        }
        if (slice > 0.0F) {
            ctx.dl->PopClipRect();
        }
    }
    if (slice > 0.0F) {
        const float w = ctx.f->current_s(right - left) * std::min(1.0F, slice * 2.0F);
        ctx.dl->AddRectFilled(ImVec2(ctx.f->get_x(left), mid - ctx.f->current_s(kSliceRuleHalfThickness)),
                              ImVec2(ctx.f->get_x(left) + w, mid + ctx.f->current_s(kSliceRuleHalfThickness)),
                              get_col(slice_colour, 1.0F - slice));
    }
    return top;
}

// A kicker line (mono 12, tracked, upper case) from design x `at.x`, its box ending at design y `at.y`. Returns its top.
float get_kicker(Ctx& ctx, const std::string& text, maths::Vec2 at, Ink ink)
{
    const float left = at.x;
    const float bottom = at.y;
    Style style = mono12(ink.colour);
    style.alpha = ink.alpha;
    const float top = bottom - (12.0F * 1.3F);
    draw_text(ctx.dl, style, *ctx.f,
              {.x = ctx.f->get_x(left), .y = datum(style, *ctx.f, ctx.f->get_y(top), kTextLineHeight)}, text.c_str());
    return top;
}

// The hints row (a rule over it), from `left` to `right`, its text's box ending at `bottom`: spread as CSS's
// justify-content: space-between. Returns the rule's y.
float get_hints(Ctx& ctx, const std::vector<std::string>& hints, Band band, float bottom,
                std::vector<std::pair<float, float>>* spans = nullptr)
{
    const float left = band.left;
    const float right = band.right;
    const Style style = mono12(kMuted, false);
    const float top = bottom - (12.0F * 1.3F);
    const float base = datum(style, *ctx.f, ctx.f->get_y(top), 1.3F);
    float total = 0.0F;
    std::vector<float> widths;
    for (const std::string& hint : hints) {
        widths.push_back(get_measure(style, *ctx.f, hint.c_str()));
        total += widths.back();
    }
    const float gap =
        hints.size() > 1 ? (ctx.f->current_s(right - left) - total) / static_cast<float>(hints.size() - 1) : 0.0F;
    float x = ctx.f->get_x(left);
    for (size_t i = 0; i < hints.size(); ++i) {
        draw_text(ctx.dl, style, *ctx.f, {.x = x, .y = base}, core::at(hints, i).c_str());
        if (spans) { // (design x: each hint's left and right)
            spans->emplace_back(ctx.f->design_x(x), ctx.f->design_x(x + core::at(widths, i)));
        }
        x += core::at(widths, i) + gap;
    }
    const float rule = top - 10.0F - 1.0F;
    ctx.dl->AddRectFilled(ImVec2(ctx.f->get_x(left), ctx.f->get_y(rule)),
                          ImVec2(ctx.f->get_x(right), ctx.f->get_y(rule) + std::max(1.0F, ctx.f->current_s(1.0F))),
                          get_col(kRule));
    return rule;
}

Art art_for(const Extras& extras, const std::string& image)
{
    return extras.art && !image.empty() ? extras.art(image) : Art{};
}

// --- the screens ---

// The art for an item: its own, levels.json's menu_art for its id, else THUG2's loading screen for it.
std::string art_by_id(const Catalogue* data, const std::string& id)
{
    const std::string image = data ? data->menu_art(id) : std::string();
    return image.empty() ? default_art(id) : image;
}

// The pause screens' ground: the game dimmed, the left column nearly opaque. `dim`: how much (an editor's view less).
void pause_ground(Ctx& ctx, float window_width, float window_height, float dim = kPauseGroundDim)
{
    ctx.dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(window_width, window_height), get_col(kGround, dim));
    scrim(ctx.dl, *ctx.f, Art{}, {.pan = 0.0F, .alpha = 1.0F});
}

// A mono line of records spread in three columns across `band`, its box ending at `bottom` (a rule above).
// Returns the rule's y (or `bottom` when there is nothing).
float records_row(Ctx& ctx, const std::vector<std::string>& cols, Band band, float bottom)
{
    if (cols.empty()) {
        return bottom;
    }
    const float left = band.left;
    const float right = band.right;
    const float alpha = 1.0F;
    Style style = mono12(kMuted, false);
    style.alpha = alpha;
    const float top = bottom - (12.0F * 1.3F);
    const float col_w = (right - left) / 3.0F;
    for (size_t i = 0; i < cols.size() && i < 3; ++i) {
        draw_text(ctx.dl, style, *ctx.f,
                  {.x = ctx.f->get_x(left + (col_w * static_cast<float>(i))),
                   .y = datum(style, *ctx.f, ctx.f->get_y(top), kTextLineHeight)},
                  core::at(cols, i).c_str());
    }
    const float rule_y = top - 10.0F - 1.0F;
    ctx.dl->AddRectFilled(ImVec2(ctx.f->get_x(left), ctx.f->get_y(rule_y)),
                          ImVec2(ctx.f->get_x(right), ctx.f->get_y(rule_y) + std::max(1.0F, ctx.f->current_s(1.0F))),
                          get_col(kRule, alpha));
    return rule_y;
}

std::string get_clock(float seconds)
{
    const int total = static_cast<int>(seconds);
    std::array<char, kClockBufferSize> text{};
    if (total >= kSecondsPerHour) {
        text::format_to(text, "%d:%02d:%02d", total / kSecondsPerHour, total / kSecondsPerMinute % kSecondsPerMinute, total % kSecondsPerMinute);
    } else {
        text::format_to(text, "%02d:%02d", total / kSecondsPerMinute, total % kSecondsPerMinute);
    }
    return text.data();
}

std::string thousands(int value)
{
    std::string digits = std::to_string(value < 0 ? -value : value);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) {
            out += ',';
        }
        out += core::at(digits, i);
    }
    return value < 0 ? "-" + out : out;
}

// The pause menus' right side's titles: the kicker ("PAUSED - the game's tab"), the big name (the level), their colour;
// `top`: the session records' rule's y less a gap.
struct SideTitle {
    float top;
    std::string kicker;
    std::string name;
    uint32_t colour;
};

// The pause menus' right side: the session so far (what is known) over its rule from `bottom`, and its titles (the
// house yellow unless the level has a colour).
SideTitle pause_side(Ctx& ctx, Band band, float bottom)
{
    std::vector<std::string> cols;
    const Session& session = ctx.extras->session;
    if (session.best_combo_ >= 0) {
        cols.push_back(std::string(text_tools::kLabelBestChain) + " " + thousands(session.best_combo_));
    }
    if (session.gaps >= 0) {
        cols.push_back(std::string(text_tools::kGaps) + " " + std::to_string(session.gaps) +
                       (session.gaps_total_ > 0 ? "/" + std::to_string(session.gaps_total_) : ""));
    }
    if (session.seconds >= 0.0F) {
        cols.push_back(std::string(text_tools::kTime) + " " + get_clock(session.seconds));
    }
    SideTitle title{
        .top = records_row(ctx, cols, band, bottom) - kSideTitleLift,
        .kicker = std::string(text_tools::kTitlePaused) +
                  (ctx.extras->level_kicker_.empty() ? std::string() : " \xE2\x80\x94 " + ctx.extras->level_kicker_),
        .name = ctx.extras->level_name_.empty() ? std::string(text_tools::kTitlePaused) : ctx.extras->level_name_,
        .colour = (ctx.extras->level_colour_ != 0U) ? ctx.extras->level_colour_ : Menu::kHouseYellow};
    return title;
}

// A row of tabs: each one's text and colour, and which is selected.
struct TabRow {
    std::span<const std::pair<std::string, uint32_t>> tabs;
    int selected{};
};

// Mono tabs (the level select's game tabs, the options' groups) from x 48 to 520 at design y `y`, wrapping: the
// selected one in its colour, underlined. Returns the bottom of the last row.
float get_tabs(Ctx& ctx, TabRow row, float y)
{
    const std::span<const std::pair<std::string, uint32_t>> tabs = row.tabs;
    const int selected = row.selected;
    Style tab = mono12(kMuted, false);
    tab.css = kLabelTextSize;
    float x = kColumnLeft;
    const float tab_h = (13.0F * 1.3F) + 8.0F;
    for (int i = 0; std::cmp_less(i, tabs.size()); ++i) {
        const float w = get_measure(tab, *ctx.f, core::at(tabs, i).first.c_str()) / ctx.f->scale();
        if (x > kColumnLeft && x + w > kColumnInner) {
            x = kColumnLeft;
            y += tab_h + kTabRowGap;
        }
        const bool on = i == selected;
        tab.colour = on ? core::at(tabs, i).second : kMuted;
        const float base = datum(tab, *ctx.f, ctx.f->get_y(y + 4.0F), 1.3F);
        draw_text(ctx.dl, tab, *ctx.f, {.x = ctx.f->get_x(x), .y = base}, core::at(tabs, i).first.c_str());
        if (on) {
            ctx.dl->AddRectFilled(ImVec2(ctx.f->get_x(x), base + ctx.f->current_s(kTabUnderlineTop)),
                                  ImVec2(ctx.f->get_x(x + w), base + ctx.f->current_s(kTabUnderlineBottom)),
                                  get_col(core::at(tabs, i).second));
        }
        ctx.get_hit(Hit::Kind::kTab, i, x - kTabHitPad, y, x + w + kTabHitPad, y + tab_h);
        x += w + kTabGap;
    }
    return y + tab_h;
}

// A slanted button (the level select's Skate) from design x `at.x`, its text in Anton 30 ending at design y `at.y`.
void get_button(Ctx& ctx, maths::Vec2 at, const char* text, uint32_t colour, bool arrow, Hit::Kind kind, float alpha)
{
    const float left = at.x;
    const float bottom = at.y;
    Style style;
    style.face = Face::kDisplay;
    style.css = kHeadingTextSize;
    style.upper = true;
    style.colour = kGround;
    style.alpha = alpha;
    const float tw = get_measure(style, *ctx.f, text) / ctx.f->scale();
    const float arrow_w = arrow ? (30.0F * 0.9F) + 10.0F : 0.0F;
    const float bh = (30.0F * 1.15F) + 12.0F;
    const float bx0 = left;
    const float bx1 = left + 22.0F + tw + arrow_w + 22.0F;
    const float by0 = bottom - bh;
    const float by1 = bottom;
    get_cut(ctx.dl,
            {.x0 = ctx.f->get_x(bx0), .y0 = ctx.f->get_y(by0), .x1 = ctx.f->get_x(bx1), .y1 = ctx.f->get_y(by1)},
            {.top = ctx.f->current_s(kButtonCutInset), .bottom = ctx.f->current_s(kButtonCutInset)},
            get_col(colour, alpha));
    const float sb = datum(style, *ctx.f, ctx.f->get_y(by0 + 6.0F), 1.15F);
    draw_text(ctx.dl, style, *ctx.f, {.x = ctx.f->get_x(bx0 + kButtonPadding), .y = sb}, text);
    if (arrow) {
        float adv = 0.0F;
        get_arrow(ctx.dl, {.x = ctx.f->get_x(bx0 + kButtonPadding + tw + kButtonArrowGap), .y = sb},
                  ctx.f->current_s(kHeadingTextSize), false, get_col(kGround, alpha), &adv);
    }
    ctx.get_hit(kind, 0, bx0, by0, bx1, by1);
}

// The options: the groups as tabs, the group's settings down the left (Anton 40, each one's value in mono at the
// column's right), the selected one's name on the right with its choices as a segmented control, what it does, its
// ini key and when a change is seen.
void options_screen(Ctx& ctx, float window_width, float window_height)
{
    const Menu& menu = *ctx.menu;
    const std::vector<options::Spec>& specs = options::get_specs();
    const std::vector<options::GroupSet>& groups = options::bunches();
    if (menu.over_pause()) {
        pause_ground(ctx, window_width, window_height);
    } else {
        backdrop(ctx.dl, *ctx.f, art_for(*ctx.extras, art_by_id(menu.payload(), "options")), menu.get_pan());
    }
    const float rule = get_header(ctx, {.x = kColumnInner, .y = 36.0F}, text_tools::kTitleOptions, true, nullptr);
    std::vector<std::pair<std::string, uint32_t>> tabs;
    tabs.reserve(groups.size());
    for (const options::GroupSet& group : groups) {
        tabs.emplace_back(group.name, Menu::kHouseYellow);
    }
    const float list_top = get_tabs(ctx, {.tabs = tabs, .selected = menu.option_group()}, rule + 14.0F) + 18.0F;
    const std::vector<int>& rows = menu.option_rows();
    // (the options' rows: a size smaller than the level list's - their labels are long)
    RowStyle rs = level_row_style();
    rs.name.css = kNameTextSize;
    rs.row_height_ = (kNameTextSize * kTabLineHeight) + kOptionRowPad;
    const float row_h = rs.row_height_;
    Style value = mono12(kMuted, false);
    const int count = static_cast<int>(rows.size()) + 1;
    for (int i = 0; i < count; ++i) {
        const float top = list_top + (static_cast<float>(i) * row_h);
        const bool action = std::cmp_equal(i, rows.size());
        const char* label = action ? text_tools::kGameOptions : core::at(specs, core::at(rows, i)).label;
        // (a short label for the row: the design's 40 px type in a 470 px column)
        get_row(ctx, rs, top, pad2(i + 1).c_str(), label,
                {.amount = menu.row_amount(i), .cut = Menu::kHouseYellow, .dim = false});
        const std::string shown = action ? std::string(text_tools::kGameOptionsValue)
                                         : core::at(core::at(specs, core::at(rows, i)).choices, menu.option_choice(core::at(rows, i))).label;
        const float base = datum(rs.name, *ctx.f, ctx.f->get_y(top), rs.line_height_);
        value.colour = i == menu.option_row() ? Menu::kHouseYellow : kMuted;
        // (the value at the column's right, where the label leaves room for it)
        const float value_x = ctx.f->get_x(kColumnInner) - get_measure(value, *ctx.f, shown.c_str());
        if (ctx.f->get_x(rs.text_x_) + get_measure(rs.name, *ctx.f, label) + ctx.f->current_s(kOptionValueGap) <
            value_x) {
            draw_text(ctx.dl, value, *ctx.f, {.x = value_x, .y = base}, shown.c_str());
        }
        ctx.get_hit(Hit::Kind::kItem, i, kColumnLeft, top, kColumnInner + kRowHitOverhang, top + row_h);
    }

    // The right's foot.
    const float left = kRight;
    const float right = 1240.0F;
    const bool action = menu.option_row() >= static_cast<int>(rows.size());
    const std::vector<std::string> hints =
        action ? std::vector<std::string>{text_tools::kHintOpen, text_tools::kHintGroup, text_tools::kHintBack}
               : std::vector<std::string>{text_tools::kHintChange, text_tools::kHintGroup, text_tools::kHintBack};
    float bottom = get_hints(ctx, hints, {.left = left, .right = right}, kPanelHintsBottom) - kPanelStackGap;
    Style body;
    body.face = Face::kBody;
    body.css = kBodyTextSize;
    body.colour = kMuted;
    std::string name;
    std::string const kicker =
        std::string(core::at(groups, menu.option_group()).name) + " \xE2\x80\x94 " + pad2(menu.option_row() + 1);
    if (action) {
        get_button(ctx, {.x = left, .y = bottom}, text_tools::kOpen, Menu::kHouseYellow, true, Hit::Kind::kSkate, 1.0F);
        bottom -= (kHeadingTextSize * kButtonLineHeight) + kButtonVerticalPad + kPanelStackGap;
        draw_text(
            ctx.dl, body, *ctx.f,
            {.x = ctx.f->get_x(left), .y = datum(body, *ctx.f, ctx.f->get_y(bottom - kHelpLineBox), kBodyLineHeight)},
            text_tools::kGameOptionsHelp);
        bottom -= kHelpLineBox + kHelpGap;
        name = text_tools::kGameOptions;
    } else {
        const int option = core::at(rows, menu.option_row());
        const options::Spec& spec = core::at(specs, option);
        // The choices: a segmented control (mono 13; the chosen one filled).
        Style seg = mono12(kPaper, false);
        seg.css = kLabelTextSize;
        const float hints_height = (13.0F * 1.3F) + 16.0F;
        float x = left;
        float const y0 = bottom - hints_height;
        const int chosen = menu.option_choice(option);
        for (size_t i = 0; i < spec.choices.size(); ++i) {
            const float w = (get_measure(seg, *ctx.f, core::at(spec.choices, i).label) / ctx.f->scale()) + 28.0F;
            const bool on = ints::as_signed(i) == chosen;
            if (on) {
                get_cut(ctx.dl,
                        {.x0 = ctx.f->get_x(x),
                         .y0 = ctx.f->get_y(y0),
                         .x1 = ctx.f->get_x(x + w),
                         .y1 = ctx.f->get_y(bottom)},
                        {.top = ctx.f->current_s(kChoiceCutInset), .bottom = ctx.f->current_s(kChoiceCutInset)},
                        get_col(Menu::kHouseYellow));
            } else {
                ctx.dl->AddRect(ImVec2(ctx.f->get_x(x), ctx.f->get_y(y0)),
                                ImVec2(ctx.f->get_x(x + w), ctx.f->get_y(bottom)), get_col(kRule), 0.0F, 0,
                                std::max(1.0F, ctx.f->current_s(1.0F)));
            }
            seg.colour = on ? kGround : kPaper;
            draw_text(ctx.dl, seg, *ctx.f,
                      {.x = ctx.f->get_x(x + kChoicePadX),
                       .y = datum(seg, *ctx.f, ctx.f->get_y(y0 + kChoicePadY), kTextLineHeight)},
                      core::at(spec.choices, i).label);
            ctx.get_hit(Hit::Kind::kChoice, (menu.option_row() * kHitChoiceStride) + static_cast<int>(i), x, y0, x + w,
                        bottom);
            x += w + kChoiceGap;
        }
        bottom = y0 - kPanelStackGap;
        // When it's seen ("Restart to apply" - yellow once changed; nothing for a live one), its ini key only behind
        // [frontend] show_ini_keys=1, what it does (when it has a line).
        const bool pending = std::cmp_less(option, ctx.extras->restart_pending_.size()) &&
                             core::at(ctx.extras->restart_pending_, option);
        const std::string when =
            spec.apply == options::Apply::kLive ? std::string() : std::string(text_tools::kRestartToApply);
        const std::string where =
            ctx.extras->show_ini_keys_ ? std::string("[") + spec.section + "] " + spec.key : std::string();
        if (!when.empty() || !where.empty()) {
            const Style key = mono12(kMuted2, false);
            if (!where.empty()) {
                draw_text(ctx.dl, key, *ctx.f,
                          {.x = ctx.f->get_x(left),
                           .y = datum(key, *ctx.f, ctx.f->get_y(bottom - kNoteLineBox), kTextLineHeight)},
                          where.c_str());
            }
            const Style note = mono12(pending ? Menu::kHouseYellow : kMuted2, false);
            if (!when.empty()) {
                draw_text(ctx.dl, note, *ctx.f,
                          {.x = ctx.f->get_x(right) - get_measure(note, *ctx.f, when.c_str()),
                           .y = datum(note, *ctx.f, ctx.f->get_y(bottom - kNoteLineBox), kTextLineHeight)},
                          when.c_str());
            }
            bottom -= kNoteLineBox + kNoteGap;
        }
        if (spec.help && (*spec.help != '\0')) {
            draw_text(ctx.dl, body, *ctx.f,
                      {.x = ctx.f->get_x(left),
                       .y = datum(body, *ctx.f, ctx.f->get_y(bottom - kHelpLineBox), kBodyLineHeight)},
                      spec.help);
            bottom -= kHelpLineBox + kHelpGap;
        }
        name = spec.label;
    }
    const float name_top = big_name(ctx, name, 96.0F, {.left = left, .right = right}, bottom,
                                    {.amount = 0.0F, .colour = Menu::kHouseYellow});
    get_kicker(ctx, kicker, {.x = left, .y = name_top - kKickerGap}, {.colour = Menu::kHouseYellow});
    if (!ctx.extras->options_note_.empty()) {
        const Style note = mono12(kMuted2, false);
        draw_text(ctx.dl, note, *ctx.f,
                  {.x = ctx.f->get_x(left), .y = datum(note, *ctx.f, ctx.f->get_y(kScreenNoteTop), kTextLineHeight)},
                  ctx.extras->options_note_.c_str());
    }
}

// The level select.
void levels_screen(Ctx& ctx, int* visible_rows)
{
    const Menu& menu = *ctx.menu;
    const Catalogue* data = menu.payload();
    const SectionView* view = menu.get_view();
    const int section_index = menu.get_section_index();
    const Section* section = data && section_index >= 0 && std::cmp_less(section_index, data->sections().size())
                                 ? &core::at(data->sections(), section_index)
                                 : nullptr;
    const uint32_t colour = section ? section->colour : Menu::kHouseYellow;
    const StageMap* level = menu.selected_level();
    const float launch = menu.get_launch();
    const float grow = launch >= 0.0F ? ease_out(launch * 1.4F) : 0.0F;

    // The level's loading screen behind the whole screen, the column's scrim over it (a launch: the scrim and the
    // list go under the sharp picture), the thin edge in the game's colour.
    const Art art = art_for(*ctx.extras, level ? current_art(*level) : std::string());
    backdrop(ctx.dl, *ctx.f, art, menu.get_pan());
    ctx.dl->AddRectFilled(ImVec2(ctx.f->get_x(kColumn), ctx.f->get_y(0.0F)),
                          ImVec2(ctx.f->get_x(kColumn + kColumnEdgeWidth), ctx.f->get_y(kDesignHeight)),
                          get_col(colour, 1.0F - grow));

    if (grow < kLaunchGrowDone) {
        ctx.dl->PushClipRect(ImVec2(ctx.f->get_x(0.0F), ctx.f->get_y(0.0F)),
                             ImVec2(ctx.f->get_x(kColumn + kScrimFeather), ctx.f->get_y(kDesignHeight)), true);
        const char* title = menu.levels_purpose() == LevelsFor::kEdit     ? text_tools::kTitleEditLevel
                            : menu.levels_purpose() == LevelsFor::kFreeze ? text_tools::kTitleChangeLevel
                            : menu.levels_purpose() == LevelsFor::kNet    ? text_tools::kTitleNetLevel
                                                                          : text_tools::kTitleFreeSkate;
        const float rule = get_header(ctx, {.x = kColumnInner, .y = 36.0F}, title, true, nullptr);
        // The game tabs (mono 13, wrapping; the selected one in its colour, underlined).
        std::vector<std::pair<std::string, uint32_t>> tabs;
        for (int i = 0; data && std::cmp_less(i, data->sections().size()); ++i) {
            tabs.emplace_back(core::at(data->sections(), i).tab, core::at(data->sections(), i).colour);
        }
        const float tabs_bottom = get_tabs(ctx, {.tabs = tabs, .selected = section_index}, rule + 14.0F);
        const float tab_h = 0.0F;
        const float y = tabs_bottom;              // The list.
        const float list_top = y + tab_h + 18.0F; // (tab_h 0: Tabs gave the bottom of its last row)
        const float list_bottom = 692.0F;
        const int fit = std::max(1, static_cast<int>((list_bottom - list_top) / kLevelRowHeight));
        if (visible_rows) {
            *visible_rows = fit;
        }
        const RowStyle rs = level_row_style();
        ctx.dl->PushClipRect(ImVec2(ctx.f->get_x(0.0F), ctx.f->get_y(list_top - kListClipAbove)),
                             ImVec2(ctx.f->get_x(kColumn + kScrimFeather), ctx.f->get_y(list_bottom + kListClipBelow)),
                             true);
        const float scroll = menu.list_scroll();
        const int count = view ? static_cast<int>(view->levels.size()) : 0;
        const int first = std::max(0, static_cast<int>(std::floor(scroll)) - 1);
        const int last = std::min(count - 1, static_cast<int>(std::ceil(scroll)) + fit + 1);
        for (int row = first; row <= last; ++row) {
            const StageMap& stage = core::at(section->levels, core::at(view->levels, row));
            const float top = list_top + ((static_cast<float>(row) - scroll) * kLevelRowHeight);
            const bool locked = std::cmp_less(row, view->locked.size()) && core::at(view->locked, row);
            get_row(ctx, rs, top, pad2(row + 1).c_str(), stage.name.c_str(),
                    {.amount = menu.row_amount(row), .cut = colour, .dim = locked});
            if (top >= list_top - 1.0F && top + kLevelRowHeight <= list_bottom + 1.0F) {
                ctx.get_hit(Hit::Kind::kStageMap, row, kColumnLeft, top, kColumnInner + kRowHitOverhang,
                            top + kLevelRowHeight);
            }
        }
        ctx.dl->PopClipRect();
        if (count == 0) {
            const Style empty = mono12(kMuted2);
            draw_text(
                ctx.dl, empty, *ctx.f,
                {.x = ctx.f->get_x(kColumnLeft), .y = datum(empty, *ctx.f, ctx.f->get_y(list_top), kTextLineHeight)},
                text_tools::kNoLevels);
        }
        ctx.dl->PopClipRect();
    }
    if (grow > 0.0F) {
        // (the launch: the picture comes up sharp over the column)
        ctx.dl->PushClipRect(ImVec2(ctx.f->get_x(0.0F), ctx.f->get_y(0.0F)),
                             ImVec2(ctx.f->get_x(kColumn + kScrimFeather), ctx.f->get_y(kDesignHeight)), true);
        art_box(ctx.dl, art,
                {.x0 = ctx.f->get_x(0.0F),
                 .y0 = ctx.f->get_y(0.0F),
                 .x1 = ctx.f->get_x(kDesignWidth),
                 .y1 = ctx.f->get_y(kDesignHeight)},
                {.pan = menu.get_pan(), .alpha = grow});
        ctx.dl->PopClipRect();
    }

    // The picture's number at the top right (Q / E change it) - only for a level with more than one.
    if (level && level->art_variants_.size() > 1 && grow < kLaunchGrowDone) {
        std::array<char, kVariantLabelBuffer> text{};
        if (level->variant >= 0 && std::cmp_less(level->variant, level->art_labels_.size())) {
            text::format_to(text, "Q  %s  E", core::at(level->art_labels_, level->variant).c_str());
        } else {
            text::format_to(text, "Q  %d / %d  E", level->variant + 1, static_cast<int>(level->art_variants_.size()));
        }
        Style note = mono12(kMuted);
        note.alpha = 1.0F - grow;
        const float nw = get_measure(note, *ctx.f, text.data()) / ctx.f->scale();
        draw_text(ctx.dl, note, *ctx.f,
                  {.x = ctx.f->get_x(kRowRight - nw),
                   .y = datum(note, *ctx.f, ctx.f->get_y(kScreenNoteTop), kTextLineHeight)},
                  text.data());
    }

    // The right's foot: Skate, the records, the name, the kicker.
    const float left = kRight + ((32.0F - kRight) * grow);
    const float right = 1240.0F;
    const float bottom = 692.0F;
    if (level) {
        const float fade = 1.0F - grow;
        // Skate (Anton 30 on the game's colour, a slanted button).
        Style skate;
        skate.face = Face::kDisplay;
        skate.css = kHeadingTextSize;
        skate.upper = true;
        skate.colour = kGround;
        skate.alpha = fade;
        const bool locked = menu.selected_locked();
        const char* skate_text = locked                                      ? text_tools::kLocked
                                 : menu.levels_purpose() == LevelsFor::kEdit ? text_tools::kEdit
                                 : menu.levels_purpose() == LevelsFor::kNet  ? text_tools::kSelect
                                                                             : text_tools::kSkate;
        const float tw = get_measure(skate, *ctx.f, skate_text) / ctx.f->scale();
        const float arrow_w = locked ? 0.0F : (30.0F * 0.9F) + 10.0F;
        const float bh = (30.0F * 1.15F) + 12.0F;
        const float bx0 = left;
        const float bx1 = left + 22.0F + tw + arrow_w + 22.0F;
        const float by0 = bottom - bh;
        const float by1 = bottom;
        get_cut(ctx.dl,
                {.x0 = ctx.f->get_x(bx0), .y0 = ctx.f->get_y(by0), .x1 = ctx.f->get_x(bx1), .y1 = ctx.f->get_y(by1)},
                {.top = ctx.f->current_s(kButtonCutInset), .bottom = ctx.f->current_s(kButtonCutInset)},
                get_col(locked ? kMuted2 : colour, fade));
        const float sb = datum(skate, *ctx.f, ctx.f->get_y(by0 + 6.0F), 1.15F);
        draw_text(ctx.dl, skate, *ctx.f, {.x = ctx.f->get_x(bx0 + kButtonPadding), .y = sb}, skate_text);
        if (!locked) {
            float adv = 0.0F;
            get_arrow(ctx.dl, {.x = ctx.f->get_x(bx0 + kButtonPadding + tw + kButtonArrowGap), .y = sb},
                      ctx.f->current_s(kHeadingTextSize), false, get_col(kGround, fade), &adv);
        }
        ctx.get_hit(Hit::Kind::kSkate, 0, bx0, by0, bx1, by1);
        float y = by0 - kButtonRecordsGap;
        // The records (only what the career keeps for this level).
        const Highscores rec = ctx.extras->records ? ctx.extras->records(*level) : Highscores{};
        std::vector<std::string> cols;
        std::array<char, kRecordsBuffer> text{};
        if (rec.gaps_total_ > 0) {
            text::format_to(text, "%s %d/%d", text_tools::kGaps, rec.gaps_got_, rec.gaps_total_);
            cols.emplace_back(text.data());
        }
        if (rec.goals >= 0) {
            text::format_to(text, "%s %d", text_tools::kGoals, rec.goals);
            cols.emplace_back(text.data());
        }
        if (!cols.empty()) {
            Style style = mono12(kMuted, false);
            style.alpha = fade;
            const float top = y - 10.0F - (12.0F * 1.3F);
            const float col_w = (right - left) / 3.0F;
            for (size_t i = 0; i < cols.size(); ++i) {
                draw_text(ctx.dl, style, *ctx.f,
                          {.x = ctx.f->get_x(left + (col_w * static_cast<float>(i))),
                           .y = datum(style, *ctx.f, ctx.f->get_y(top), kTextLineHeight)},
                          core::at(cols, i).c_str());
            }
            const float rule_y = top - 10.0F - 1.0F;
            ctx.dl->AddRectFilled(
                ImVec2(ctx.f->get_x(left), ctx.f->get_y(rule_y)),
                ImVec2(ctx.f->get_x(right), ctx.f->get_y(rule_y) + std::max(1.0F, ctx.f->current_s(1.0F))),
                get_col(kRule, fade));
            y = rule_y;
        }
        const float slice = launch >= 0.0F ? ease_out(launch * 1.6F) : 0.0F;
        const float name_top = big_name(ctx, level->name, 112.0F, {.left = left, .right = right}, y - 10.0F,
                                        {.amount = slice, .colour = colour});
        std::string kicker = (section ? section->tab : std::string()) + " \xE2\x80\x94 " + pad2(menu.level_row() + 1);
        if (locked) {
            kicker += std::string(" \xC2\xB7 ") + text_tools::kLocked;
        }
        get_kicker(ctx, kicker, {.x = left, .y = name_top - kLevelKickerGap},
                   {.colour = colour, .alpha = 1.0F - slice});
    }
}

// The screen change: one diagonal band in `colour` across the window - in over the first half (the screen changes
// under it), out over the second.
void wipe(ImDrawList* dl, const Menu& menu, maths::Size window)
{
    const float wipe_amount = menu.wipe();
    const uint32_t colour = menu.wipe_colour();
    const float width = window.width;
    const float height = window.height;
    if (wipe_amount < 0.0F) {
        return;
    }
    const float skew = height * 0.35F;
    float lead = -skew + ((width + (skew * kWipeSkewSides)) * ease_out(std::min(1.0F, wipe_amount * kWipePhases)));
    float trail =
        -skew + ((width + (skew * kWipeSkewSides)) * ease_out(std::max(0.0F, (wipe_amount * kWipePhases) - 1.0F)));
    if (lead <= trail) {
        return;
    }
    std::array<ImVec2, 4> points = {{{trail + skew, 0.0F}, {lead + skew, 0.0F}, {lead, height}, {trail, height}}};
    dl->AddConvexPolyFilled(points.data(), 4, get_col(colour));
}

// --- phase 3: the original screens restyled (new_frontend_mirror.h) ---

Style get_body(uint32_t colour = kMuted)
{
    Style style;
    style.face = Face::kBody;
    style.css = kBodyTextSize;
    style.colour = colour;
    return style;
}

// Where paragraphs go: from design y `top`, at most `lines` lines, in `colour`.
struct TextBlock {
    float top{};
    int lines{};
    uint32_t colour = kMuted;
};

// Paragraphs of body text in `block` across `band`. Returns the bottom.
float paragraphs(Ctx& ctx, const std::vector<std::string>& texts, Band band, TextBlock block)
{
    const float left = band.left;
    const float right = band.right;
    float top = block.top;
    const int max_lines = block.lines;
    const Style body = get_body(block.colour);
    const float line_h = 14.0F * 1.5F;
    int drawn = 0;
    for (const std::string& text : texts) {
        // (each \n of a dialog's text a line of its own)
        size_t at = 0;
        while (at <= text.size() && drawn < max_lines) {
            const size_t end = text.find('\n', at);
            const std::string part = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
            for (const std::string& line : get_wrap(body, *ctx.f, part, ctx.f->current_s(right - left))) {
                if (drawn >= max_lines) {
                    break;
                }
                draw_text(ctx.dl, body, *ctx.f,
                          {.x = ctx.f->get_x(left), .y = datum(body, *ctx.f, ctx.f->get_y(top), kBodyLineHeight)},
                          line.c_str());
                top += line_h;
                ++drawn;
            }
            if (end == std::string::npos) {
                break;
            }
            at = end + 1;
        }
        top += kParagraphGap;
    }
    return top;
}

// A dialog box: the screen dimmed, a band across it - the kicker and the title (Anton) with its text on the left,
// the choices down the right (the level list's rows, the kinetic selection).
void dialog_screen(Ctx& ctx, const mirror::Tracker& tracker, float window_width, float window_height)
{
    const mirror::View& v = tracker.get_shown();
    ctx.dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(window_width, window_height), get_col(kGround, kDialogDim));
    const float y0 = 196.0F;
    const float y1 = 548.0F;
    ctx.dl->AddRectFilled(ImVec2(0.0F, ctx.f->get_y(y0)), ImVec2(window_width, ctx.f->get_y(y1)),
                          get_col(kGround, kDialogBandAlpha));
    ctx.dl->AddRectFilled(ImVec2(0.0F, ctx.f->get_y(y0)), ImVec2(window_width, ctx.f->get_y(y0 + kDialogEdgeHeight)),
                          get_col(Menu::kHouseYellow));
    const float left = 96.0F;
    const float text_right = 700.0F;
    int choices = 0;
    for (const mirror::Row& row : v.rows) {
        choices += row.focusable ? 1 : 0;
    }
    const char* kicker = choices >= 2   ? text_tools::kDialogConfirm
                         : choices == 1 ? text_tools::kDialogNotice
                                        : text_tools::kPleaseWait;
    const float kicker_top = get_kicker(ctx, kicker, {.x = left, .y = y0 + 44.0F}, {.colour = Menu::kHouseYellow});
    (void)kicker_top;
    // The title (Anton 72, up to two lines) under the kicker.
    Style title;
    title.face = Face::kDisplay;
    title.css = kTitleTextSize;
    title.upper = true;
    std::vector<std::string> title_lines = get_wrap(title, *ctx.f, v.title, ctx.f->current_s(text_right - left));
    if (title_lines.size() > 2) {
        title_lines.resize(2);
    }
    float y = y0 + kDialogTitleDrop;
    for (const std::string& line : title_lines) {
        draw_text(ctx.dl, title, *ctx.f,
                  {.x = ctx.f->get_x(left), .y = datum(title, *ctx.f, ctx.f->get_y(y), kDialogTitleLineHeight)},
                  line.c_str());
        y += kTitleTextSize * kDialogTitleLineHeight;
    }
    paragraphs(ctx, v.lines, {.left = left, .right = text_right},
               {.top = y + kDialogTextGap, .lines = kDialogTextLines, .colour = kPaper});
    // The choices.
    RowStyle rs = level_row_style();
    const float rows_left = 760.0F;
    rs.text_x_ = rows_left + kRowNumberWidth + kLevelRowNumberGap + kLevelRowCutPad;
    rs.cut_left_ = rows_left + kRowNumberWidth + kLevelRowNumberGap - kRowCutLeadIn;
    const int row_count = static_cast<int>(v.rows.size());
    const float top = ((y0 + y1) * 0.5F) - (static_cast<float>(row_count) * kLevelRowHeight * 0.5F);
    for (int i = 0; i < row_count; ++i) {
        const float row_top = top + (static_cast<float>(i) * kLevelRowHeight);
        // (Row puts the number at the design's x 48: drawn here on the right instead)
        ImDrawList* dl = ctx.dl;
        const int start = dl->VtxBuffer.Size;
        const float amount = tracker.row_amount(i);
        const float base = datum(rs.name, *ctx.f, ctx.f->get_y(row_top), rs.line_height_);
        const float width = get_measure(rs.name, *ctx.f, core::at(v.rows, i).label.c_str());
        if (amount > kVisibleThreshold) {
            const float x0 = ctx.f->get_x(rs.cut_left_);
            float x1 = ctx.f->get_x(rs.text_x_) + width + ctx.f->current_s(rs.cut_right_pad_);
            x1 = x0 + ((x1 - x0) * amount);
            const float line_h = rs.name.css * rs.line_height_;
            get_cut(dl,
                    {.x0 = x0,
                     .y0 = ctx.f->get_y(row_top + (line_h * rs.cut_top_)),
                     .x1 = x1,
                     .y1 = ctx.f->get_y(row_top + (line_h * rs.cut_bottom_))},
                    {.top = ctx.f->current_s(rs.top_inset_), .bottom = ctx.f->current_s(rs.bottom_inset_)},
                    get_col(Menu::kHouseYellow));
        }
        Style name = rs.name;
        name.colour = get_mix(core::at(v.rows, i).focusable ? kPaper : kMuted2, kGround, amount);
        draw_text(dl, name, *ctx.f, {.x = ctx.f->get_x(rs.text_x_), .y = base}, core::at(v.rows, i).label.c_str());
        draw_text(dl, rs.number, *ctx.f, {.x = ctx.f->get_x(rows_left), .y = base}, pad2(i + 1).c_str());
        if (amount > kVisibleThreshold) {
            xform(dl, start, {.x = ctx.f->get_x(rows_left), .y = ctx.f->get_y(row_top + (kLevelRowHeight * kHalf))},
                  -kRowTiltDegrees * maths::kPi / kDegreesPerHalfTurn * amount,
                  {.x = ctx.f->current_s(kRowSlideDistance) * amount, .y = 0.0F});
        }
        if (core::at(v.rows, i).focusable) {
            ctx.get_hit(Hit::Kind::kItem, i, rows_left, row_top, kRowRight, row_top + kLevelRowHeight);
        }
    }
    std::vector<std::string> hints = {text_tools::kHintSelect, text_tools::kHintBack};
    if (choices == 0) {
        hints = {text_tools::kPleaseWait};
    }
    get_hints(ctx, hints, {.left = left, .right = kColumnSpan}, y1 - kDialogHintsLift);
}

// The skater select: the game's 3D skater stays in view (left of centre, as the original's camera puts it); the
// skaters as a strip across the top, the selected one's name and stats on a panel on the right.
void skaters_screen(Ctx& ctx, const mirror::Tracker& tracker, float window_width, float window_height)
{
    const mirror::View& v = tracker.get_shown();
    // The top: a shade for the strip and the header.
    ctx.dl->AddRectFilledMultiColor(ImVec2(0.0F, 0.0F), ImVec2(window_width, ctx.f->get_y(kSkaterTopShadeHeight)),
                                    get_col(kGround, kSkaterTopShadeAlpha), get_col(kGround, kSkaterTopShadeAlpha),
                                    get_col(kGround, 0.0F), get_col(kGround, 0.0F));
    // The right: a panel feathered from x 720.
    const float panel = 800.0F;
    ctx.dl->AddRectFilledMultiColor(ImVec2(ctx.f->get_x(panel - kSkaterPanelFeather), 0.0F),
                                    ImVec2(ctx.f->get_x(panel), window_height), get_col(kGround, 0.0F),
                                    get_col(kGround, kSkaterPanelAlpha), get_col(kGround, kSkaterPanelAlpha),
                                    get_col(kGround, 0.0F));
    ctx.dl->AddRectFilled(ImVec2(ctx.f->get_x(panel), 0.0F), ImVec2(window_width, window_height),
                          get_col(kGround, kSkaterPanelAlpha));
    // The foot: a shade under the hints.
    ctx.dl->AddRectFilledMultiColor(ImVec2(0.0F, ctx.f->get_y(kSkaterFootShadeTop)),
                                    ImVec2(window_width, window_height), get_col(kGround, 0.0F), get_col(kGround, 0.0F),
                                    get_col(kGround, kSkaterFootShadeAlpha), get_col(kGround, kSkaterFootShadeAlpha));

    const std::string count =
        v.selected >= 0 ? pad2(v.selected + 1) + " / " + pad2(static_cast<int>(v.rows.size())) : std::string();
    get_header(ctx, {.x = kColumnRight, .y = kHeaderTop}, v.title.c_str(), true, count.c_str());

    // The strip: every skater's name (Anton 28), the selected one centred, cut and turned.
    Style name;
    name.face = Face::kDisplay;
    name.css = kNameTextSize;
    name.upper = true;
    const float gap = 34.0F;
    std::vector<float> widths;
    float total = 0.0F;
    float selected_centre = 0.0F;
    for (size_t i = 0; i < v.rows.size(); ++i) {
        widths.push_back(get_measure(name, *ctx.f, core::at(v.rows, i).label.c_str()) / ctx.f->scale());
        if (ints::as_signed(i) == v.selected) {
            selected_centre = total + (widths.back() * kHalf);
        }
        total += widths.back() + gap;
    }
    float shift = kStripCenterX - selected_centre; // (the strip's x 0 in design px)
    shift = std::min(shift, kColumnLeft);
    shift = std::max(shift, kColumnRight - (total - gap));
    if (total - gap < kColumnSpan) {
        shift = kColumnLeft;
    }
    const float strip_top = 88.0F;
    ctx.dl->PushClipRect(ImVec2(ctx.f->get_x(0.0F), ctx.f->get_y(strip_top - kStripClipAbove)),
                         ImVec2(ctx.f->get_x(kDesignWidth), ctx.f->get_y(strip_top + kStripClipBelow)), true);
    float x = shift;
    for (size_t i = 0; i < v.rows.size(); ++i) {
        const float amount = tracker.row_amount(static_cast<int>(i));
        ImDrawList* dl = ctx.dl;
        const int start = dl->VtxBuffer.Size;
        const float line_h = 28.0F * 1.05F;
        if (amount > kVisibleThreshold) {
            get_cut(dl,
                    {.x0 = ctx.f->get_x(x - kStripCutLeadIn),
                     .y0 = ctx.f->get_y(strip_top + (line_h * kStripCutTop)),
                     .x1 = ctx.f->get_x(x - kStripCutLeadIn) +
                           ((ctx.f->current_s(core::at(widths, i) + kStripCutExtra)) * amount),
                     .y1 = ctx.f->get_y(strip_top + (line_h * kStripCutBottom))},
                    {.top = ctx.f->current_s(kStripCutTopInset), .bottom = ctx.f->current_s(kStripCutBottomInset)},
                    get_col(Menu::kHouseYellow));
        }
        Style style = name;
        style.colour = get_mix(core::at(v.rows, i).focusable ? kPaper : kMuted2, kGround, amount);
        draw_text(dl, style, *ctx.f,
                  {.x = ctx.f->get_x(x), .y = datum(style, *ctx.f, ctx.f->get_y(strip_top), kTabLineHeight)},
                  core::at(v.rows, i).label.c_str());
        if (amount > kVisibleThreshold) {
            xform(dl, start, {.x = ctx.f->get_x(x), .y = ctx.f->get_y(strip_top + (line_h * kHalf))},
                  -kRowTiltDegrees * maths::kPi / kDegreesPerHalfTurn * amount,
                  {.x = 0.0F, .y = -ctx.f->current_s(kStripTiltLift) * amount});
        }
        if (core::at(v.rows, i).focusable && x + core::at(widths, i) > 0.0F && x < kDesignWidth) {
            ctx.get_hit(Hit::Kind::kItem, static_cast<int>(i), x - kStripHitLeft, strip_top - kStripHitVertical,
                        x + core::at(widths, i) + kStripHitRight, strip_top + line_h + kStripHitVertical);
        }
        x += core::at(widths, i) + gap;
    }
    ctx.dl->PopClipRect();
    // (a rule under the strip)
    ctx.dl->AddRectFilled(ImVec2(ctx.f->get_x(kColumnLeft), ctx.f->get_y(strip_top + kColumnLeft)),
                          ImVec2(ctx.f->get_x(kColumnRight),
                                 ctx.f->get_y(strip_top + kColumnLeft) + std::max(1.0F, ctx.f->current_s(1.0F))),
                          get_col(kRule));

    // The panel: the stats as a mono table with bars, the name over it, the kicker.
    const float left = panel + 40.0F;
    const float right = 1232.0F;
    float bottom =
        get_hints(
            ctx, {text_tools::kHintSkater, text_tools::kHintAccept, text_tools::kHintBack, text_tools::kHintLoadSkater},
            {.left = kColumnLeft, .right = kColumnRight}, kPanelHintsBottom) -
        kSkaterHintsLift;
    Style label = mono12(kMuted, false);
    label.css = kLabelTextSize;
    Style number = mono12(kPaper, false);
    number.css = kLabelTextSize;
    const float row_h = 26.0F;
    const int stats = static_cast<int>(v.stats.size());
    const float table_top = bottom - (row_h * static_cast<float>(stats));
    for (int i = 0; i < stats; ++i) {
        const float top = table_top + (row_h * static_cast<float>(i));
        const float base = datum(label, *ctx.f, ctx.f->get_y(top + 4.0F), 1.3F);
        draw_text(ctx.dl, label, *ctx.f, {.x = ctx.f->get_x(left), .y = base}, core::at(v.stats, i).first.c_str());
        const int value = std::clamp(core::at(v.stats, i).second, 0, 10);
        const float bar_left = left + 110.0F;
        const float bar_right = right - 40.0F;
        const float seg = (bar_right - bar_left) / 10.0F;
        for (int k = 0; k < kStatBlockCount; ++k) {
            const float sx = bar_left + (seg * static_cast<float>(k));
            get_cut(ctx.dl,
                    {.x0 = ctx.f->get_x(sx),
                     .y0 = ctx.f->get_y(top + kStatBlockTop),
                     .x1 = ctx.f->get_x(sx + seg - kStatBlockGap),
                     .y1 = ctx.f->get_y(top + kStatBlockBottom)},
                    {.top = ctx.f->current_s(kStatBlockInset), .bottom = ctx.f->current_s(kStatBlockInset)},
                    get_col(k < value ? Menu::kHouseYellow : kRule));
        }
        const std::string text = std::to_string(core::at(v.stats, i).second);
        draw_text(ctx.dl, number, *ctx.f,
                  {.x = ctx.f->get_x(right) - get_measure(number, *ctx.f, text.c_str()), .y = base}, text.c_str());
    }
    if (stats > 0) {
        const float rule = table_top - 12.0F;
        ctx.dl->AddRectFilled(ImVec2(ctx.f->get_x(left), ctx.f->get_y(rule)),
                              ImVec2(ctx.f->get_x(right), ctx.f->get_y(rule) + std::max(1.0F, ctx.f->current_s(1.0F))),
                              get_col(kRule));
        bottom = rule - kPanelStackGap;
    }
    const float name_top = big_name(ctx, v.big_name_, 84.0F, {.left = left, .right = right}, bottom,
                                    {.amount = 0.0F, .colour = Menu::kHouseYellow});
    std::string kicker = text_tools::kSkater;
    if (v.selected >= 0 && !core::at(v.rows, v.selected).focusable) {
        kicker += std::string(" \xC2\xB7 ") + text_tools::kLocked;
    }
    get_kicker(ctx, kicker, {.x = left, .y = name_top - kKickerGap}, {.colour = Menu::kHouseYellow});
}

// The stats screen (EDIT STATS, the story's STATS): the stats as the skater select's table - each stat's name in mono
// caps, ten slanted blocks filled to its value (the story's earned but unspent ones faint), the number - on a panel on
// the left, the selected stat marked; the points left and the selected stat big on the right; the game behind, dimmed,
// in a level. The game's scripts change the stats (Left / Right reach them); this only shows what its stars show.
// Hits: Item for a row (its index), Choice for a row's blocks - row * 16 + 0 under its value (Left), + 1 past it (Right).
void stats_screen(Ctx& ctx, const mirror::Tracker& tracker, float window_width, float window_height)
{
    const mirror::View& v = tracker.get_shown();
    if (v.over_game_) {
        ctx.dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(window_width, window_height),
                              get_col(kGround, kPauseGroundDim));
    } else {
        ctx.dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(window_width, window_height), get_col(kGround));
    }
    // The panel: x 0 .. 720, feathered to 800.
    const float panel = 720.0F;
    ctx.dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(ctx.f->get_x(panel), window_height),
                          get_col(kGround, kStatsPanelAlpha));
    ctx.dl->AddRectFilledMultiColor(ImVec2(ctx.f->get_x(panel), 0.0F),
                                    ImVec2(ctx.f->get_x(panel + kStatsPanelFeather), window_height),
                                    get_col(kGround, kStatsPanelAlpha), get_col(kGround, 0.0F), get_col(kGround, 0.0F),
                                    get_col(kGround, kStatsPanelAlpha));
    const float rule = get_header(ctx, {.x = panel - 48.0F, .y = 40.0F}, v.title.c_str(), true, nullptr);

    // The table.
    const int row_count = static_cast<int>(v.rows.size());
    const float row_h = std::min(44.0F, (600.0F - rule - 40.0F) / static_cast<float>(std::max(row_count, 1)));
    const float table_top = rule + 36.0F;
    const float label_x = 72.0F;
    const float bar_left = 250.0F;
    const float bar_right = 590.0F;
    const float number_right = panel - 48.0F;
    const float seg = (bar_right - bar_left) / 10.0F;
    Style label = mono12(kMuted, true);
    label.css = kStatTextSize;
    Style number = mono12(kPaper, false);
    number.css = kStatTextSize;
    for (int i = 0; i < row_count; ++i) {
        const mirror::Row& row = core::at(v.rows, i);
        const float top = table_top + (row_h * static_cast<float>(i));
        const bool selected = i == v.selected;
        const float amount = tracker.row_amount(i);
        const float base = datum(label, *ctx.f, ctx.f->get_y(top + ((row_h - (16.0F * 1.3F)) * 0.5F)), 1.3F);
        if (amount > kVisibleThreshold) {
            // (the selected stat: a slanted yellow cut behind its name, a faint band across its row)
            ctx.dl->AddRectFilled(
                ImVec2(ctx.f->get_x(kColumnLeft), ctx.f->get_y(top + kStatRowBandInset)),
                ImVec2(ctx.f->get_x(number_right + kStatRowRightPad), ctx.f->get_y(top + row_h - kStatRowBandInset)),
                get_col(kPaper, kStatRowBandAlpha * amount));
            const float w = get_measure(label, *ctx.f, row.label.c_str()) / ctx.f->scale();
            get_cut(ctx.dl,
                    {.x0 = ctx.f->get_x(label_x - kStatCutLeadIn),
                     .y0 = ctx.f->get_y(top + (row_h * kStatCutTop)),
                     .x1 = ctx.f->get_x(label_x - kStatCutLeadIn) + (ctx.f->current_s(w + kStatCutExtra) * amount),
                     .y1 = ctx.f->get_y(top + (row_h * kStatCutBottom))},
                    {.top = ctx.f->current_s(kStatCutInset), .bottom = ctx.f->current_s(kStatCutInset)},
                    get_col(Menu::kHouseYellow));
        }
        Style row_label = label;
        row_label.colour = get_mix(row.focusable ? kMuted : kMuted2, kGround, amount);
        draw_text(ctx.dl, row_label, *ctx.f, {.x = ctx.f->get_x(label_x), .y = base}, row.label.c_str());
        const int value = std::clamp(row.stat_value_, 0, 10);
        const int cap = std::clamp(row.stat_cap_, value, 10);
        const float block_top = top + (row_h * 0.5F) - 7.0F;
        for (int k = 0; k < kStatsBlockCount; ++k) {
            const float sx = bar_left + (seg * static_cast<float>(k));
            const ImU32 colour = k < value ? get_col(Menu::kHouseYellow)
                                 : k < cap ? get_col(Menu::kHouseYellow, 0.3F)
                                           : get_col(kRule);
            get_cut(ctx.dl,
                    {.x0 = ctx.f->get_x(sx),
                     .y0 = ctx.f->get_y(block_top),
                     .x1 = ctx.f->get_x(sx + seg - kStatsBlockGap),
                     .y1 = ctx.f->get_y(block_top + kStatsBlockHeight)},
                    {.top = ctx.f->current_s(kStatsBlockInset), .bottom = ctx.f->current_s(kStatsBlockInset)}, colour);
        }
        const std::string text = std::to_string(value);
        Style num = number;
        num.colour = selected ? Menu::kHouseYellow : kPaper;
        draw_text(ctx.dl, num, *ctx.f,
                  {.x = ctx.f->get_x(number_right) - get_measure(num, *ctx.f, text.c_str()), .y = base}, text.c_str());
        // (the blocks: under the value Left, past it Right; the row: select / choose)
        ctx.get_hit(Hit::Kind::kChoice, i * kHitChoiceStride, bar_left - kStatBarHitPad, top,
                    bar_left + (seg * static_cast<float>(value)), top + row_h);
        ctx.get_hit(Hit::Kind::kChoice, (i * kHitChoiceStride) + 1, bar_left + (seg * static_cast<float>(value)), top,
                    bar_right + kStatBarHitPad, top + row_h);
        ctx.get_hit(Hit::Kind::kItem, i, kColumnLeft, top, number_right + kStatRowRightPad, top + row_h);
    }

    // The right: the hints, the screen's other words (the description, the story's goals), the points left, the stat.
    const float left = 800.0F;
    const float right = 1232.0F;
    std::vector<std::string> hints = {text_tools::kHintStat};
    if (v.stat_columns_ > 1 && !v.stats_editing_) {
        hints.emplace_back(text_tools::kHintColumn);
        hints.emplace_back(text_tools::kHintEditStat);
        hints.emplace_back(text_tools::kHintBack);
    } else {
        hints.emplace_back(text_tools::kHintChange);
        hints.emplace_back(v.stats_editing_ ? text_tools::kHintDone : text_tools::kHintBack);
    }
    float bottom = get_hints(ctx, hints, {.left = kColumnLeft, .right = right}, kHintsBottom) - kStatsHintsGap;
    if (v.points >= 0) {
        Style big;
        big.face = Face::kDisplay;
        big.css = kTitleTextSize;
        big.colour = v.points > 0 ? Menu::kHouseYellow : kMuted2;
        const std::string points = std::to_string(v.points);
        const float top = bottom - (72.0F * 0.95F);
        draw_text(ctx.dl, big, *ctx.f,
                  {.x = ctx.f->get_x(left), .y = datum(big, *ctx.f, ctx.f->get_y(top), kPointsLineHeight)},
                  points.c_str());
        bottom = get_kicker(ctx, text_tools::kPointsLeft, {.x = left, .y = top - kPointsKickerGap},
                            {.colour = Menu::kHouseYellow}) -
                 kPointsGap;
    }
    const float name_top = big_name(ctx, v.big_name_, 84.0F, {.left = left, .right = right}, bottom,
                                    {.amount = 0.0F, .colour = Menu::kHouseYellow});
    get_kicker(ctx, v.title, {.x = left, .y = name_top - kKickerGap}, {.colour = Menu::kHouseYellow});
    if (!v.lines.empty()) {
        paragraphs(ctx, v.lines, {.left = left, .right = right}, {.top = kStatsTextTop, .lines = kStatsTextLines});
    }
}

// A colour menu's slider track (Mirror::Row::track 1 hue, 2 saturation, 3 value, 4 alpha, 5 any other bar): a thin rail
// in the colour the slider sets - the hue spectrum at the colour's saturation limit and value, grey to the full saturation
// at the colour's hue and value, dark to bright at its hue and saturation - the colours as the skater shows them
// (Mirror::skater_colour), and a marker where the value stands. The numbers are the menu's own (View::hue / sat / val).
ImU32 skater_col(float hue, float sat, float val)
{
    std::array<float, 3> rgb{};
    mirror::skater_colour({.hue = hue, .sat = sat, .val = val}, rgb);
    return IM_COL32(maths::round_non_negative<int>(core::at(rgb, 0) * kChannelMax), maths::round_non_negative<int>(core::at(rgb, 1) * kChannelMax),
                    maths::round_non_negative<int>(core::at(rgb, 2) * kChannelMax), 255);
}

void colour_track(Ctx& ctx, const mirror::View& v, const mirror::Row& row, bool selected, Band band, float y_mid)
{
    const float x0 = band.left;
    const float x1 = band.right;
    constexpr float kSatLimit = 90.0F;
    constexpr float kValLow = 12.0F;
    constexpr float kValHigh = 60.0F;
    float hue = v.has_colour_ ? v.hue : 0.0F;
    float sat = v.has_colour_ ? v.sat : kSatLimit;
    float val = v.has_colour_ ? v.val : kDefaultTrackValue;
    const float rail_half = 3.0F;
    const float px0 = ctx.f->get_x(x0);
    const float px1 = ctx.f->get_x(x1);
    const float py0 = ctx.f->get_y(y_mid - rail_half);
    const float py1 = ctx.f->get_y(y_mid + rail_half);
    const int steps = row.track == 1 ? 12 : 8;
    if (row.track >= 1 && row.track <= 3) {
        for (int k = 0; k < steps; ++k) {
            const float from_fraction = static_cast<float>(k) / static_cast<float>(steps);
            const float to_fraction = static_cast<float>(k + 1) / static_cast<float>(steps);
            auto at = [&](float tracker) {
                if (row.track == 1) {
                    return skater_col(tracker * kDegreesPerTurn, kSatLimit, val);
                }
                if (row.track == 2) {
                    return skater_col(hue, tracker * kSatLimit, val);
                }
                return skater_col(hue, sat, kValLow + (tracker * (kValHigh - kValLow)));
            };
            ctx.dl->AddRectFilledMultiColor(ImVec2(px0 + ((px1 - px0) * from_fraction), py0),
                                            ImVec2(px0 + ((px1 - px0) * to_fraction), py1), at(from_fraction),
                                            at(to_fraction), at(to_fraction), at(from_fraction));
        }
    } else {
        ctx.dl->AddRectFilled(ImVec2(px0, py0), ImVec2(px1, py1), get_col(kRule));
        ctx.dl->AddRectFilled(ImVec2(px0, py0),
                              ImVec2(px0 + ((px1 - px0) * std::clamp(row.track_at_, 0.0F, 1.0F)), py1),
                              get_col(selected ? Menu::kHouseYellow : kTrackFillIdle));
    }
    ctx.dl->AddRect(ImVec2(px0, py0), ImVec2(px1, py1), get_col(kOpaqueBlack, kTrackOutlineAlpha), 0.0F, 0,
                    std::max(1.0F, ctx.f->current_s(1.0F)));
    // The marker: a post over the rail, ink-edged, the accent when its row is the selected one.
    const float mx = px0 + ((px1 - px0) * std::clamp(row.track_at_, 0.0F, 1.0F));
    const float half = std::max(1.5F, ctx.f->current_s(2.0F));
    const float top = ctx.f->get_y(y_mid - 9.0F);
    const float bottom = ctx.f->get_y(y_mid + 9.0F);
    if (row.track >= 1 && row.track <= 4) {
        ctx.dl->AddRectFilled(ImVec2(mx - half - 1.0F, top - 1.0F), ImVec2(mx + half + 1.0F, bottom + 1.0F),
                              get_col(kOpaqueBlack));
        ctx.dl->AddRectFilled(ImVec2(mx - half, top), ImVec2(mx + half, bottom),
                              get_col(selected ? Menu::kHouseYellow : kMarkerIdle));
    }
}

// A plain list: the pause menus' look - the screen dimmed, the column's scrim, the items numbered down the left (each
// one's value in mono at the column's right), the selected item's name on the right with the screen's other words.
void list_of_items(Ctx& ctx, const mirror::Tracker& tracker, float window_width, float window_height, float* scroll)
{
    const mirror::View& v = tracker.get_shown();
    pause_ground(ctx, window_width, window_height,
                 v.editor ? kEditorGroundDim : kPauseGroundDim); // (an editor's view shows through)
    const float rule = get_header(ctx, {.x = kColumnInner, .y = 40.0F},
                                  v.title.empty() ? text_tools::kTitleMenu : v.title.c_str(), true, nullptr);
    const int row_count = static_cast<int>(v.rows.size());
    bool big = row_count <= kListBigRows;
    if (big) {
        // (big type only while every label - and its value, when it has one - fits the column)
        const RowStyle probe = main_row_style();
        const Style probe_value = mono12(kMuted, false);
        for (const mirror::Row& row : v.rows) {
            float need = ctx.f->current_s(probe.text_x_) + get_measure(probe.name, *ctx.f, row.label.c_str());
            if (!row.value.empty()) {
                need += ctx.f->current_s(kLabelValueGap) + get_measure(probe_value, *ctx.f, row.value.c_str());
            }
            if (need > ctx.f->current_s(kColumnInner + kRowFitSlack)) {
                big = false;
            }
        }
    }
    const RowStyle rs = big ? main_row_style() : level_row_style();
    const float row_h = big ? kMainRowHeight + 2.0F : kLevelRowHeight;
    const float list_top = rule + (big ? 36.0F : 20.0F);
    const float list_bottom = 620.0F;
    const int fit = std::max(1, static_cast<int>((list_bottom - list_top) / row_h));
    // (the selection kept in view, a row of margin)
    float first = scroll ? *scroll : 0.0F;
    if (v.selected >= 0) {
        if (static_cast<float>(v.selected) < first + 1.0F) {
            first = static_cast<float>(v.selected) - 1.0F;
        }
        if (static_cast<float>(v.selected) > first + static_cast<float>(fit) - kScrollMargin) {
            first = static_cast<float>(v.selected) - static_cast<float>(fit) + kScrollMargin;
        }
    }
    first = std::clamp(first, 0.0F, static_cast<float>(std::max(0, row_count - fit)));
    if (scroll) {
        *scroll = first;
    }
    Style value = mono12(kMuted, false);
    ctx.dl->PushClipRect(ImVec2(ctx.f->get_x(0.0F), ctx.f->get_y(list_top - kListClipAbove)),
                         ImVec2(ctx.f->get_x(kColumn + kScrimFeather), ctx.f->get_y(list_bottom + kListClipBelow)),
                         true);
    for (int i = 0; i < row_count; ++i) {
        const float top = list_top + ((static_cast<float>(i) - first) * row_h);
        if (top < list_top - row_h || top > list_bottom) {
            continue;
        }
        const mirror::Row& row = core::at(v.rows, i);
        get_row(ctx, rs, top, pad2(i + 1).c_str(), row.label.c_str(),
                {.amount = tracker.row_amount(i), .cut = Menu::kHouseYellow, .dim = !row.focusable});
        if (!row.value.empty()) {
            const float base = datum(rs.name, *ctx.f, ctx.f->get_y(top + rs.row_pad_top_), rs.line_height_);
            value.colour = i == v.selected ? Menu::kHouseYellow : kMuted;
            const float value_x = ctx.f->get_x(kColumnInner) - get_measure(value, *ctx.f, row.value.c_str());
            if (ctx.f->get_x(rs.text_x_) + get_measure(rs.name, *ctx.f, row.label.c_str()) +
                    ctx.f->current_s(kLabelValueGap) <
                value_x) {
                draw_text(ctx.dl, value, *ctx.f, {.x = value_x, .y = base}, row.value.c_str());
            }
        }
        if (row.track > 0 && row.focusable) {
            // (between the label and the number: from past the widest label to a fixed width, at most; a selected row's
            // label sits in its cut, shifted right by Row's 24 px - the track starts past the cut's end)
            const float value_w =
                row.value.empty() ? 0.0F
                                  : (get_measure(value, *ctx.f, row.value.c_str()) / ctx.f->current_s(1.0F)) + 14.0F;
            const float label_end = rs.text_x_ +
                                    (get_measure(rs.name, *ctx.f, row.label.c_str()) / ctx.f->current_s(1.0F)) + 16.0F +
                                    (tracker.row_amount(i) * (rs.cut_right_pad_ + 24.0F + 4.0F));
            const float x1 = kColumnInner - value_w;
            const float x0 = std::max(label_end, x1 - 190.0F);
            if (x1 - x0 > kTrackMinWidth) {
                colour_track(ctx, v, row, i == v.selected, {.left = x0, .right = x1}, top + (row_h * kHalf));
            }
        }
        if (row.focusable && top >= list_top - 1.0F && top + row_h <= list_bottom + 1.0F) {
            ctx.get_hit(Hit::Kind::kItem, i, kColumnLeft, top, kColumnInner + kRowHitOverhang, top + row_h);
        }
    }
    ctx.dl->PopClipRect();

    // The right: the hints, the screen's other words, the selected item (its value under it), the title as the kicker.
    const float left = kRight;
    const float right = 1240.0F;
    float bottom = get_hints(ctx, {text_tools::kHintSelect, text_tools::kHintChange, text_tools::kHintBack},
                             {.left = left, .right = right}, kHintsBottom) -
                   kListHintsGap;
    if (v.has_colour_) {
        // The colour chosen, as the skater shows it, with its numbers.
        const float swatch_top = bottom - 30.0F;
        const ImVec2 swatch_min(ctx.f->get_x(left), ctx.f->get_y(swatch_top));
        const ImVec2 swatch_max(ctx.f->get_x(left + 64.0F), ctx.f->get_y(swatch_top + 30.0F));
        ctx.dl->AddRectFilled(swatch_min, swatch_max, skater_col(v.hue, v.sat, v.val));
        ctx.dl->AddRect(swatch_min, swatch_max, get_col(kRule), 0.0F, 0, std::max(1.0F, ctx.f->current_s(1.0F)));
        Style note_style = mono12(kMuted, false);
        note_style.css = kNoteTextSize;
        std::array<char, kSwatchNumbersBuffer> numbers{};
        text::format_to(numbers, "H %d  S %d  V %d", maths::round_non_negative<int>(v.hue),
                        maths::round_non_negative<int>(v.sat), maths::round_non_negative<int>(v.val));
        draw_text(ctx.dl, note_style, *ctx.f,
                  {.x = ctx.f->get_x(left + kSwatchNumbersX),
                   .y = datum(note_style, *ctx.f, ctx.f->get_y(swatch_top + kSwatchNumbersDrop), kTextLineHeight)},
                  numbers.data());
        bottom = swatch_top - kSwatchGap;
    }
    if (v.editor) {
        // (an editor's view - the skater, the park - keeps the right: only the selected item's value over the hints)
        const mirror::Row* sel = v.selected >= 0 ? &core::at(v.rows, v.selected) : nullptr;
        if (sel && !sel->value.empty()) {
            Style label_style = mono12(Menu::kHouseYellow, false);
            label_style.css = kLabelTextSize;
            draw_text(ctx.dl, label_style, *ctx.f,
                      {.x = ctx.f->get_x(left),
                       .y = datum(label_style, *ctx.f, ctx.f->get_y(bottom - (kLabelTextSize * kTextLineHeight)),
                                  kTextLineHeight)},
                      sel->value.c_str());
        }
        return;
    }
    const mirror::Row* sel = v.selected >= 0 ? &core::at(v.rows, v.selected) : nullptr;
    if (sel && !sel->value.empty()) {
        Style label_style = mono12(Menu::kHouseYellow, false);
        label_style.css = kLabelTextSize;
        const float top = bottom - (13.0F * 1.3F);
        draw_text(ctx.dl, label_style, *ctx.f,
                  {.x = ctx.f->get_x(left), .y = datum(label_style, *ctx.f, ctx.f->get_y(top), kTextLineHeight)},
                  sel->value.c_str());
        bottom = top - kSelectedValueGap;
    }
    const float name_top = big_name(ctx, v.big_name_, 96.0F, {.left = left, .right = right}, bottom,
                                    {.amount = 0.0F, .colour = Menu::kHouseYellow});
    get_kicker(ctx, v.title.empty() ? std::string(text_tools::kTitleMenu) : v.title,
               {.x = left, .y = name_top - kKickerGap}, {.colour = Menu::kHouseYellow});
    if (!v.lines.empty()) {
        paragraphs(ctx, v.lines, {.left = left, .right = right}, {.top = kColumnLeft, .lines = kPlainTextLines});
    }
}

// A level's pause menu restyled (Kind::Freeze): the game's items as numbered rows on the left (the main menu's type
// while they fit, the level list's past that), the level and the session on the right - the pause design, the menu
// itself the game's.
void pause_of_items(Ctx& ctx, const mirror::Tracker& tracker, float window_width, float window_height, float* scroll)
{
    const mirror::View& v = tracker.get_shown();
    pause_ground(ctx, window_width, window_height);
    const float rule = get_header(ctx, {.x = kColumnInner, .y = 40.0F}, text_tools::kTitlePaused, false, nullptr);
    const int row_count = static_cast<int>(v.rows.size());
    bool big = row_count <= kPauseBigRows;
    if (big) {
        const RowStyle probe = main_row_style();
        for (const mirror::Row& row : v.rows) {
            if (ctx.f->current_s(probe.text_x_) + get_measure(probe.name, *ctx.f, row.label.c_str()) >
                ctx.f->current_s(kColumnInner + kRowFitSlack)) {
                big = false;
            }
        }
    }
    const RowStyle rs = big ? main_row_style() : level_row_style();
    const float row_h = big ? kMainRowHeight + 2.0F : kLevelRowHeight;
    const float list_top = rule + (big ? 36.0F : 20.0F);
    const float list_bottom = 680.0F;
    const int fit = std::max(1, static_cast<int>((list_bottom - list_top) / row_h));
    float first = scroll ? *scroll : 0.0F;
    if (v.selected >= 0) {
        if (static_cast<float>(v.selected) < first + 1.0F) {
            first = static_cast<float>(v.selected) - 1.0F;
        }
        if (static_cast<float>(v.selected) > first + static_cast<float>(fit) - kScrollMargin) {
            first = static_cast<float>(v.selected) - static_cast<float>(fit) + kScrollMargin;
        }
    }
    first = std::clamp(first, 0.0F, static_cast<float>(std::max(0, row_count - fit)));
    if (scroll) {
        *scroll = first;
    }
    ctx.dl->PushClipRect(ImVec2(ctx.f->get_x(0.0F), ctx.f->get_y(list_top - kListClipAbove)),
                         ImVec2(ctx.f->get_x(kColumn + kScrimFeather), ctx.f->get_y(list_bottom + kListClipBelow)),
                         true);
    for (int i = 0; i < row_count; ++i) {
        const float top = list_top + ((static_cast<float>(i) - first) * row_h);
        if (top < list_top - row_h || top > list_bottom) {
            continue;
        }
        const mirror::Row& row = core::at(v.rows, i);
        get_row(ctx, rs, top, pad2(i + 1).c_str(), row.label.c_str(),
                {.amount = tracker.row_amount(i), .cut = Menu::kHouseYellow, .dim = !row.focusable});
        if (row.focusable && top >= list_top - 1.0F && top + row_h <= list_bottom + 1.0F) {
            ctx.get_hit(Hit::Kind::kItem, i, kColumnLeft, top, kColumnInner + kRowHitOverhang, top + row_h);
        }
    }
    ctx.dl->PopClipRect();

    const float left = kRight;
    const float right = 1240.0F;
    const float bottom = get_hints(ctx, {text_tools::kHintSelect, text_tools::kHintResumeLevel},
                                   {.left = left, .right = right}, 688.0F) -
                         8.0F;
    const SideTitle title = pause_side(ctx, {.left = left, .right = right}, bottom);
    const float name_top = big_name(ctx, title.name, 120.0F, {.left = left, .right = right}, title.top,
                                    {.amount = 0.0F, .colour = title.colour});
    get_kicker(ctx, title.kicker, {.x = left, .y = name_top - kKickerGap}, {.colour = title.colour, .alpha = 1.0F});
}

// A front end menu restyled (Kind::Menu): the main menu's look - the highlighted item's art full-bleed behind the
// column, the game's items as numbered rows in big type (a note after a label), the item's line at the foot, its name
// big on the right under the kicker; the main menu's Continue hint (X: the last level played).
void menu_of_items(Ctx& ctx, const mirror::Tracker& tracker, float* scroll)
{
    const mirror::View& v = tracker.get_shown();
    const mirror::Row* sel = v.selected >= 0 ? &core::at(v.rows, v.selected) : nullptr;
    std::string art = sel && !sel->art.empty() ? art_by_id(ctx.extras->catalogue, sel->art) : std::string();
    if (art.empty()) {
        art = art_by_id(ctx.extras->catalogue, v.screen_art_);
    }
    if (!v.over_game_) {
        backdrop(ctx.dl, *ctx.f, art_for(*ctx.extras, art), 0.0F);
    }
    const float rule = get_header(ctx, {.x = kColumnInner, .y = 40.0F}, v.title.c_str(), !v.main_menu_, nullptr);
    const int row_count = static_cast<int>(v.rows.size());
    bool big = row_count <= kPauseBigRows;
    if (big) {
        const RowStyle probe = main_row_style();
        for (const mirror::Row& row : v.rows) {
            if (ctx.f->current_s(probe.text_x_) + get_measure(probe.name, *ctx.f, row.label.c_str()) >
                ctx.f->current_s(kColumnInner + kRowFitSlack)) {
                big = false;
            }
        }
    }
    const RowStyle rs = big ? main_row_style() : level_row_style();
    const float row_h = big ? kMainRowHeight + 2.0F : kLevelRowHeight;
    const float list_top = rule + (big ? 36.0F : 20.0F);
    const float list_bottom = 620.0F;
    const int fit = std::max(1, static_cast<int>((list_bottom - list_top) / row_h));
    float first = scroll ? *scroll : 0.0F;
    if (v.selected >= 0) {
        if (static_cast<float>(v.selected) < first + 1.0F) {
            first = static_cast<float>(v.selected) - 1.0F;
        }
        if (static_cast<float>(v.selected) > first + static_cast<float>(fit) - kScrollMargin) {
            first = static_cast<float>(v.selected) - static_cast<float>(fit) + kScrollMargin;
        }
    }
    first = std::clamp(first, 0.0F, static_cast<float>(std::max(0, row_count - fit)));
    if (scroll) {
        *scroll = first;
    }
    ctx.dl->PushClipRect(ImVec2(ctx.f->get_x(0.0F), ctx.f->get_y(list_top - kListClipAbove)),
                         ImVec2(ctx.f->get_x(kColumn + kScrimFeather), ctx.f->get_y(list_bottom + kListClipBelow)),
                         true);
    for (int i = 0; i < row_count; ++i) {
        const float top = list_top + ((static_cast<float>(i) - first) * row_h);
        if (top < list_top - row_h || top > list_bottom) {
            continue;
        }
        const mirror::Row& row = core::at(v.rows, i);
        get_row(ctx, rs, top, pad2(i + 1).c_str(), row.label.c_str(),
                {.amount = tracker.row_amount(i), .cut = Menu::kHouseYellow, .dim = !row.focusable});
        if (!row.tag.empty()) {
            const Style tag = mono12(kMuted2);
            const float base = datum(rs.name, *ctx.f, ctx.f->get_y(top + rs.row_pad_top_), rs.line_height_);
            const float x =
                ctx.f->get_x(rs.text_x_) + get_measure(rs.name, *ctx.f, row.label.c_str()) + ctx.f->current_s(16.0F);
            draw_text(ctx.dl, tag, *ctx.f, {.x = x, .y = base - ctx.f->current_s(rs.name.css * kTagRaise)},
                      row.tag.c_str());
        }
        if (row.focusable && top >= list_top - 1.0F && top + row_h <= list_bottom + 1.0F) {
            ctx.get_hit(Hit::Kind::kItem, i, kColumnLeft, top, kColumnInner + kRowHitOverhang, top + row_h);
        }
    }
    ctx.dl->PopClipRect();
    Style body;
    body.face = Face::kBody;
    body.css = kBodyTextSize;
    body.colour = kMuted;
    const std::vector<std::string> lines =
        get_wrap(body, *ctx.f, sel ? sel->blurb : std::string(), ctx.f->current_s(kColumnInner - 48.0F));
    const float line_h = 14.0F * 1.5F;
    float y = kHintsBottom - (line_h * static_cast<float>(lines.size()));
    for (const std::string& line : lines) {
        draw_text(ctx.dl, body, *ctx.f,
                  {.x = ctx.f->get_x(kColumnLeft), .y = datum(body, *ctx.f, ctx.f->get_y(y), kBodyLineHeight)},
                  line.c_str());
        y += line_h;
    }

    const float left = kRight;
    const float right = 1240.0F;
    std::vector<std::string> hints = {text_tools::kHintSelect};
    int resume_hint = -1;
    if (v.main_menu_ && !ctx.extras->continue_name_.empty()) {
        resume_hint = static_cast<int>(hints.size());
        hints.push_back(std::string(text_tools::kHintResume) + " " + ctx.extras->continue_name_);
    }
    if (!v.main_menu_) {
        hints.emplace_back(text_tools::kHintBack);
    }
    std::vector<std::pair<float, float>> spans;
    const float bottom = get_hints(ctx, hints, {.left = left, .right = right}, 688.0F, &spans) - 8.0F;
    if (resume_hint >= 0 && std::cmp_less(resume_hint, spans.size())) {
        ctx.get_hit(Hit::Kind::kContinue, 0, core::at(spans, resume_hint).first - kResumeHitPad,
                    kHintsBottom - (kNoteTextSize * kTextLineHeight) - kResumeHitPad,
                    core::at(spans, resume_hint).second + kResumeHitPad, kHintsBottom + kResumeHitPad);
    }
    const float name_top = big_name(ctx, v.big_name_, 120.0F, {.left = left, .right = right}, bottom,
                                    {.amount = 0.0F, .colour = Menu::kHouseYellow});
    get_kicker(ctx, v.kicker, {.x = left, .y = name_top - kKickerGap}, {.colour = Menu::kHouseYellow, .alpha = 1.0F});
}

// The on-screen keyboard restyled (Kind::Keyboard): the text typed big under the title, the keys as rows of mono boxes
// (the selected one in the house yellow); a click on a key selects it in the game's keyboard and chooses it (Hit::StageMap,
// row * 64 + key).
void keyboard_screen(Ctx& ctx, const mirror::Tracker& tracker, float window_width, float window_height)
{
    const mirror::View& v = tracker.get_shown();
    if (v.over_game_) {
        pause_ground(ctx, window_width, window_height, kKeyboardGroundDim);
    }
    const float rule = get_header(ctx, {.x = 1232.0F, .y = 40.0F}, v.title.c_str(), true, nullptr);
    // The text typed (the game's cursor "_" kept as it shows it).
    Style entry;
    entry.face = Face::kDisplay;
    entry.css = kTitleTextSize;
    entry.upper = false;
    entry.colour = kPaper;
    const float entry_top = rule + 30.0F;
    // (the game's cursor "_": drawn as our own bar after the text - the display face has no underscore)
    std::string typed = v.entry;
    if (!typed.empty() && typed.back() == '_') {
        typed.pop_back();
    }
    Style typed_style = entry;
    typed_style.face = Face::kBody; // (the case as typed: the display face is capitals only)
    typed_style.css = kTypedTextSize;
    const float entry_x = ctx.f->get_x(48.0F);
    draw_text(ctx.dl, typed_style, *ctx.f,
              {.x = entry_x, .y = datum(typed_style, *ctx.f, ctx.f->get_y(entry_top + kTypedTextDrop), 1.0F)},
              typed.c_str());
    const float cursor_x = entry_x + get_measure(typed_style, *ctx.f, typed.c_str()) + ctx.f->current_s(4.0F);
    ctx.dl->AddRectFilled(ImVec2(cursor_x, ctx.f->get_y(entry_top + kCursorTop)),
                          ImVec2(cursor_x + ctx.f->current_s(kCursorWidth), ctx.f->get_y(entry_top + kCursorBottom)),
                          get_col(Menu::kHouseYellow));
    const float line = entry_top + 72.0F + 10.0F;
    ctx.dl->AddRectFilled(
        ImVec2(ctx.f->get_x(kColumnLeft), ctx.f->get_y(line)),
        ImVec2(ctx.f->get_x(kColumnRight), ctx.f->get_y(line) + std::max(1.0F, ctx.f->current_s(kEntryRuleThickness))),
        get_col(Menu::kHouseYellow));
    // The keys.
    Style key = mono12(kPaper, false);
    key.css = kKeyTextSize;
    key.upper = false; // (the keys as the game shows them: lower case is lower case)
    float top = line + kKeyRowsDrop;
    float const key_height = kKeyHeight;
    float const gap = kKeyGap;
    for (size_t row = 0; row < v.keys.size(); ++row) {
        float x = kColumnLeft;
        for (size_t k = 0; k < core::at(v.keys, row).size(); ++k) {
            const std::string& label = core::at(core::at(v.keys, row), k);
            const float w = std::max(52.0F, (get_measure(key, *ctx.f, label.c_str()) / ctx.f->scale()) + 28.0F);
            const bool sel = ints::as_signed(row) == v.key_row_ && ints::as_signed(k) == v.key_col_;
            ctx.dl->AddRectFilled(ImVec2(ctx.f->get_x(x), ctx.f->get_y(top)),
                                  ImVec2(ctx.f->get_x(x + w), ctx.f->get_y(top + key_height)),
                                  sel ? get_col(Menu::kHouseYellow) : get_col(kGround, kKeyFillAlpha));
            ctx.dl->AddRect(ImVec2(ctx.f->get_x(x), ctx.f->get_y(top)),
                            ImVec2(ctx.f->get_x(x + w), ctx.f->get_y(top + key_height)), get_col(kRule), 0.0F, 0,
                            std::max(1.0F, ctx.f->current_s(1.0F)));
            Style style = key;
            style.colour = sel ? kGround : kPaper;
            const float tx = ctx.f->get_x(x + (w * 0.5F)) - (get_measure(style, *ctx.f, label.c_str()) * 0.5F);
            draw_text(ctx.dl, style, *ctx.f,
                      {.x = tx,
                       .y = datum(style, *ctx.f,
                                  ctx.f->get_y(top + ((key_height - (kKeyTextSize * kTextLineHeight)) * kHalf)),
                                  kTextLineHeight)},
                      label.c_str());
            ctx.get_hit(Hit::Kind::kStageMap, (static_cast<int>(row) * kKeyHitStride) + static_cast<int>(k), x, top,
                        x + w, top + key_height);
            x += w + gap;
        }
        top += key_height + gap;
    }
    get_hints(ctx, {text_tools::kHintSelect, text_tools::kHintBack}, {.left = kRight, .right = kRowRight},
              kHintsBottom);
}

} // namespace

// --- the public parts -------------------------------------------------------------------------------------------

float baseline_in_line(float ascent, float descent, float css, float line_height)
{
    const float content = (ascent - descent) * css;
    return (((css * line_height) - content) * kHalf) + (ascent * css);
}

float main_row_top(int index)
{
    // (the header's rule at 40 + 12 * 1.3 + 12, 1 px; the nav 36 below; rows 2 apart)
    const float rule = 40.0F + (12.0F * 1.3F) + 12.0F + 1.0F;
    return rule + kMainListTopGap + (static_cast<float>(index) * (kMainRowHeight + kMainRowStep));
}

float level_list_top(int tab_rows)
{
    const float rule = 36.0F + (12.0F * 1.3F) + 12.0F + 1.0F;
    const float tab_h = (13.0F * 1.3F) + 8.0F;
    const int rows = std::max(1, tab_rows);
    const float last_tab_top = rule + 14.0F + (static_cast<float>(rows - 1) * (tab_h + 4.0F));
    return last_tab_top + tab_h + kTabListGap;
}

int level_list_rows(int tab_rows)
{
    return std::max(1, static_cast<int>((kPanelHintsBottom - level_list_top(tab_rows)) / kLevelRowHeight));
}

std::optional<FontMetrics> read_font_metrics(std::span<const uint8_t> data)
{
    if (data.size() < kSfntHeaderBytes) {
        return std::nullopt;
    }
    const uint32_t tag = be32(data);
    if (tag != kSfntVersionTrueType && tag != kTagTrue /* true */ && tag != kTagOtto /* OTTO */) {
        return std::nullopt;
    }
    const int tables = be16(data.subspan(4));
    std::span<const uint8_t> head;
    std::span<const uint8_t> hhea;
    for (int i = 0; i < tables; ++i) {
        const size_t entry = 12 + (static_cast<size_t>(i) * 16);
        if (entry + kSfntTableRecordBytes > data.size()) {
            return std::nullopt;
        }
        const uint32_t name = be32(data.subspan(entry));
        const uint32_t offset = be32(data.subspan(entry + 8));
        if (offset + kHeadTableBytes > data.size()) {
            continue;
        }
        if (name == kTagHead) { // head
            head = data.subspan(offset);
        } else if (name == kTagHhea) { // hhea
            hhea = data.subspan(offset);
        }
    }
    if (head.empty() || hhea.empty()) {
        return std::nullopt;
    }
    auto units_per_em = static_cast<float>(be16(head.subspan(kHeadUnitsPerEmOffset)));
    if (units_per_em <= 0.0F) {
        return std::nullopt;
    }
    const FontMetrics metrics{.ascent = static_cast<float>(static_cast<int16_t>(be16(hhea.subspan(4)))) / units_per_em,
                              .descent = static_cast<float>(static_cast<int16_t>(be16(hhea.subspan(6)))) / units_per_em};
    if (metrics.ascent <= 0.0F) {
        return std::nullopt;
    }
    return metrics;
}

namespace {

// The face's metrics from its file (false, the face's defaults kept, if they can't be read).
bool read_face_metrics(FaceLayout& face)
{
    const std::optional<FontMetrics> metrics = read_font_metrics(face.file);
    if (!metrics) {
        return false;
    }
    face.ascent = metrics->ascent;
    face.descent = metrics->descent;
    return true;
}

} // namespace

void forget_fonts()
{
    new_frontend_draw_state().atlas_ = nullptr;
    new_frontend_draw_state().scale_ = 0.0F;
    for (Baked& baked : new_frontend_draw_state().baked_) {
        baked = Baked{};
    }
}

float built_scale()
{
    return ImGui::GetCurrentContext() && ImGui::GetIO().Fonts == new_frontend_draw_state().atlas_
               ? new_frontend_draw_state().scale_
               : 0.0F;
}

bool build_fonts(const FontFiles& files, float scale)
{
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    atlas->Clear();
    for (Baked& baked : new_frontend_draw_state().baked_) {
        baked = Baked{};
    }
    new_frontend_draw_state().condense_ = std::clamp(files.condense, kCondenseMin, kCondenseMax);
    new_frontend_draw_state().bold_ = std::clamp(files.bold, 0.0F, kBoldMax);
    const std::array<const std::string*, 3> paths = {{&files.display, &files.mono, &files.body}};
    bool all = true;
    for (int i = 0; i < 3; ++i) {
        FaceLayout& face = core::at(stored_faces(), i);
        face = FaceLayout{};
        if (core::at(paths, i)->empty() || !read_file(*core::at(paths, i), &face.file) ||
            !read_face_metrics(face)) {
            get_log("new front end: can't read the font %s", core::at(paths, i)->empty() ? "(none)" : core::at(paths, i)->c_str());
            face.file.clear();
            all = false;
            continue;
        }
        face.ok = true;
    }
    for (int i = 0; i < kBakeCount; ++i) {
        FaceLayout& face = core::at(stored_faces(), static_cast<int>(core::at(kBakes, i).face));
        if (!face.ok) {
            continue;
        }
        const float em = std::max(4.0F, std::round(core::at(kBakes, i).css * scale));
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        const bool big = em > 32.0F;
        config.OversampleH = big ? 1 : 2;
        config.OversampleV = 1;
        config.PixelSnapH = big;
        text::format_to(config.Name, "fe %d %.0f", static_cast<int>(core::at(kBakes, i).face), core::at(kBakes, i).css);
        // (stb_truetype's size is ascent - descent: the em asked for times that)
        const float pixel_height = em * (face.ascent - face.descent);
        ImFont* font =
            atlas->AddFontFromMemoryTTF(face.file.data(), static_cast<int>(face.file.size()), pixel_height, &config,
                                        core::at(kBakes, i).face == Face::kDisplay ? kDisplayRanges.data() : kTextRanges.data());
        core::at(new_frontend_draw_state().baked_, i) = {.font = font, .face = core::at(kBakes, i).face, .em_px_ = em};
    }
    new_frontend_draw_state().scale_ = scale;
    new_frontend_draw_state().atlas_ = atlas;
    atlas->Build();
    get_log("new front end: fonts for scale %.2f - atlas %dx%d", scale, atlas->TexWidth, atlas->TexHeight);
    return all;
}

void get_screen(const Menu& menu, const TickCycle& frame, float window_width, float window_height, const Extras& extras,
                std::vector<Hit>* hits, int* visible_rows)
{
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushTextureID(ImGui::GetIO().Fonts->TexID);
    // (the pause menus: over the game, dimmed - the rest on the ground's colour)
    const bool over_game =
        is_pause_screen(menu.get_shown()) || (menu.get_shown() == Screen::kOptions && menu.over_pause());
    if (!over_game) {
        dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(window_width, window_height), get_col(kGround));
    }
    Ctx ctx{.dl = dl, .f = &frame, .menu = &menu, .extras = &extras, .hits = hits};
    if (hits) {
        hits->clear();
    }
    if (menu.get_shown() == Screen::kLevels) {
        levels_screen(ctx, visible_rows);
    } else if (menu.get_shown() == Screen::kOptions) {
        options_screen(ctx, window_width, window_height);
    }
    // (the roots - Main, Pause - are never drawn: ours goes aside at them)
    // (a 16:9 frame in a wider or taller window: the bars the ground's colour, over anything that spilled)
    if (frame.left() > kLetterboxMargin && !over_game) {
        dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(frame.left(), window_height), get_col(kGround));
        dl->AddRectFilled(ImVec2(frame.get_x(kDesignWidth), 0.0F), ImVec2(window_width, window_height),
                          get_col(kGround));
    }
    if (frame.top() > kLetterboxMargin && !over_game) {
        dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(window_width, frame.top()), get_col(kGround));
        dl->AddRectFilled(ImVec2(0.0F, frame.get_y(kDesignHeight)), ImVec2(window_width, window_height),
                          get_col(kGround));
    }
    wipe(dl, menu, {.width = window_width, .height = window_height});
    dl->PopTextureID();
}

void restyled(const mirror::Tracker& tracker, const TickCycle& frame, float window_width, float window_height,
              std::vector<Hit>* hits, float* scroll, const Extras* extras)
{
    static const Menu kNoMenu;
    static const Extras kNoExtras;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushTextureID(ImGui::GetIO().Fonts->TexID);
    Ctx ctx{.dl = dl, .f = &frame, .menu = &kNoMenu, .extras = extras ? extras : &kNoExtras, .hits = hits};
    if (hits) {
        hits->clear();
    }
    switch (tracker.get_shown().kind) {
    case mirror::Kind::kDialog:
        dialog_screen(ctx, tracker, window_width, window_height);
        break;
    case mirror::Kind::kSkaters:
        skaters_screen(ctx, tracker, window_width, window_height);
        break;
    case mirror::Kind::kLinkChain:
        list_of_items(ctx, tracker, window_width, window_height, scroll);
        break;
    case mirror::Kind::kFreeze:
        pause_of_items(ctx, tracker, window_width, window_height, scroll);
        break;
    case mirror::Kind::kMenu:
        // (in a level - the pause menu's GAME OPTIONS... - the game stays behind it, dimmed, as the pause menus; the
        // user's change, 2026-11-02)
        if (tracker.get_shown().over_game_) {
            pause_ground(ctx, window_width, window_height);
        } else {
            dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(window_width, window_height), get_col(kGround));
        }
        menu_of_items(ctx, tracker, scroll);
        break;
    case mirror::Kind::kKeyboard:
        if (!tracker.get_shown().over_game_) {
            dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(window_width, window_height), get_col(kGround));
        }
        keyboard_screen(ctx, tracker, window_width, window_height);
        break;
    case mirror::Kind::kStats:
        stats_screen(ctx, tracker, window_width, window_height);
        break;
    case mirror::Kind::kNone:
        break;
    }
    dl->PopTextureID();
}

void get_waiting(const TickCycle& frame, float window_width, float window_height, const Extras& extras,
                 const char* art_id)
{
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->PushTextureID(ImGui::GetIO().Fonts->TexID);
    dl->AddRectFilled(ImVec2(0.0F, 0.0F), ImVec2(window_width, window_height), get_col(kGround));
    backdrop(dl, frame, art_for(extras, art_by_id(extras.catalogue, art_id)), 0.0F);
    dl->PopTextureID();
}

} // namespace frontend::new_frontend::paint
