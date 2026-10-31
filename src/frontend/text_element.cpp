// The text element and its blur effect: see text_element.h.

#include "frontend/text_element.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/stubs.h"
#include "frontend/sprite_element.h"
#include "engine/gfx/engine_text.h"
#include "engine/gfx/font.h"
#include "engine/script/param_struct.h"
#include "platform/stopwatch.h"
#include "engine/core/code_address.h"
#include "engine/objects/entity.h"
#include "frontend/screen_element.h"
#include "frontend/screen_constants.h"
#include "engine/gfx/engine_geom.h"

namespace MenuSystem {

namespace {
extern void** const kCTextElementVtable;
} // namespace

namespace {

using Scripts::CStruct;

// Names (checksums).
constexpr uint32_t kTextElement = 0x5200DFB6, kFont = 0x2F6BF72D, kRotAngle = 0xED344E1B;
constexpr uint32_t kInnerAlign = 0x67E093E4, kText = 0xC4745838, kBlurEffect = 0xD1653C6C;
constexpr uint32_t kNoBlurEffect = 0x4B73E4F8, kHaloColor = 0x4F0E1182, kShadowOffs = 0x2A1FE0CC;
constexpr uint32_t kShadow = 0x8A897DD2, kShadowOffFlag = 0x95B5B9A0, kShadowRgba = 0x1E7BB1F5;
constexpr uint32_t kRememberOverrideRgbaState = 0x0BF51A0C, kRestoreOverrideRgbaState = 0x61B7B58B;
constexpr uint32_t kOverrideEncodedRgba = 0xB7686F92, kDontOverrideEncodedRgba = 0xD25BC6CD;
constexpr uint32_t kOverrideEncodedAlpha = 0x1A226DCD, kDontOverrideEncodedAlpha = 0xE1931201;
constexpr uint32_t kCharProperties = 0x44B3B0F4, kIndex = 0x7F8C98FE, kOffset = 0xA6F5352F, kScale = 0x13B9DA7B;
constexpr uint32_t kBlurAlphaPair = 0x21DE240C, kBlurBottomScales = 0x3E10436D;
constexpr uint32_t kBlurMaxDisplacement = 0x4C2A3CDE, kTime = 0x906B67BA;

// strcpy / strcat (the original's: no length limit).
void CopyText(char* to, const char* from)
{
    while ((*to++ = *from++) != '\0') {
    }
}
void AppendText(char* to, const char* from) { CopyText(to + std::strlen(to), from); }

float Degrees() { return ::ScriptCommands::g_degreeToRadianRatio; }        // pi / 180
float RotationOffset() { return ::GoalSystem::g_goalLifeSecondsPerMillisecond; }  // 0.001 (added to a rotation in radians)
float MinimumAlpha() { return ::GameModules::g_competitionTiny; }    // 0.0001: below it, transparent
float LayoutTop() { return MenuSystem::g_layoutTop; }       // -200: text whose top is above it is hidden
float LayoutBottom() { return MenuSystem::g_layoutBottom; }    // 648: ... or below it
float Zero() { return ::GameObjects::g_floatZero; }
float Half() { return ::Gfx2D::g_half; }
float One() { return ::GameObjects::g_animMoviesOne; }
float ShadowBehind() { return MenuSystem::g_shadowBehind; }    // 0.005: the shadow's priority below the text's
float BlurBehind() { return ::Engine3D::g_onePercent; }      // 0.01: each blur copy's below the one before
float Sixteenth() { return MenuSystem::g_sixteenth; }       // 1/16
float Milliseconds() { return ::Gfx2D::g_thousand; }    // 1000
double TwoTo32() { return ::Gfx2D::g_twoTo32; }        // (unsigned to floating point)

// The original's _ftol (0x006236FC): truncated towards zero.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// An unsigned millisecond count as the original converts it (signed, then 2^32 added when negative).
double Unsigned(int value)
{
    double d = value;
    if (value < 0)
        d += TwoTo32();
    return d;
}

// Memory from the front end's heap (the strings and the text object lists).
void* FrontEndNew(unsigned int size)
{
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_frontEndHeap);
    void* block = MemSys::Alloc(size);
    MemSys::Hub::Pull()->LeaveHeapScope();
    return block;
}

// A colour with its alpha byte replaced.
uint32_t WithAlpha(uint32_t rgba, uint8_t alpha) { return (rgba & 0x00FFFFFFu) | (static_cast<uint32_t>(alpha) << 24); }
// The alpha byte: `a` times the element's summed alpha (none below 0.0001).
uint8_t Faded(int a, float alpha)
{
    if (alpha >= MinimumAlpha()) // (NaN: none)
        return static_cast<uint8_t>(Truncated(static_cast<double>(a) * alpha));
    return 0;
}

// Fonts (0x0048D760, cdecl) and a string's size in one (0x0048D060, the font's slot 8).
void* GetFont(uint32_t name) { return Engine3D::GetFont(name); }
void ReadStringParam(void* font, const char* text, float* width, float* height)
{
    reinterpret_cast<void(__thiscall*)(void*, const char*, float*, float*)>(&Engine3D::VirtualSlot8Jump)(font, text, width, height);
}
// A button glyph's character for the text after \m (0x0048D6A0, cdecl).
char ButtonGlyph(const char* text) { return Engine3D::ButtonGlyph(text); }

// The text objects: made / freed (cdecl), and their setters (thiscall).
void* NewTextObject() { return Engine3D::NewText(); }
void FreeTextObject(void* text) { Engine3D::FreeText(static_cast<Engine3D::CGlyphString*>(text)); }
void TextHidden(void* t, bool hidden) { static_cast<Engine3D::CGlyphString*>(t)->ChangeConcealed(hidden); }
void TextFont(void* t, void* font) { static_cast<Engine3D::CGlyphString*>(t)->ChangeTypeface(static_cast<Engine3D::CTypeface*>(font)); }
void TextPos(void* t, float x, float y) { static_cast<Engine3D::CGlyphString*>(t)->ChangeLocation(x, y); }
void TextScale(void* t, float x, float y) { static_cast<Engine3D::CGlyphString*>(t)->ChangeScale(x, y); }
void TextRotation(void* t, float angle, float x, float y) { static_cast<Engine3D::CGlyphString*>(t)->ChangeSpin(angle, x, y); }
void TextPriority(void* t, float priority) { static_cast<Engine3D::CGlyphString*>(t)->ChangeRank(priority); }
void TextDepth(void* t, float depth) { static_cast<Engine3D::CGlyphString*>(t)->SetDepth(depth); }
void TextColour(void* t, uint32_t rgba, bool overrideEncoded) { static_cast<Engine3D::CGlyphString*>(t)->ChangeColour(rgba, overrideEncoded); }
void TextOverrideAlpha(void* t, bool overrideAlpha) { static_cast<Engine3D::CGlyphString*>(t)->SetOverrideAlpha(overrideAlpha); }
void TextViewport(void* t, void* window) { static_cast<Engine3D::CGlyphString*>(t)->ChangePane(window); }
void ChangeGlyphText(void* t, const char* text) { static_cast<Engine3D::CGlyphString*>(t)->ChangeText(text); }

// The blur's empty destructor (0x005480A0) and its memory freed.
void DeleteBlur(CSmearEffect* blur)
{
    if (blur) {
        Stubs::DoNothing(); // (its destructor: the shared empty stub the original calls)
        MemSys::Dealloc(blur);
    }
}

} // namespace

// --- the blur effect ------------------------------------------------------------------------------------------

// 0x004A5770
CSmearEffect::DupFrom* CSmearEffect::GetCopy(int i)
{
    return &m_copies[i];
}

// 0x004A5780
void CSmearEffect::StartMorph(int ms)
{
    if (ms == 0)
        m_ongoing = m_aimPoint;
    m_remaining = ms;
    m_lastTime = static_cast<int>(FrameClock::GetTime());
}

// 0x004A57D0
void CSmearEffect::TickFrame()
{
    int now = static_cast<int>(FrameClock::GetTime());
    double k = One();
    if (m_remaining)
        k = Unsigned(now - m_lastTime) / Unsigned(m_remaining);
    if (static_cast<uint32_t>(now - m_lastTime) < static_cast<uint32_t>(m_remaining))
        m_remaining = m_lastTime - now + m_remaining;
    else
        m_remaining = 0;
    m_lastTime = now;
    float* current = &m_ongoing.alpha[0];
    const float* target = &m_aimPoint.alpha[0];
    if (m_remaining) {
        for (int i = 0; i < 6; ++i)
            current[i] = static_cast<float>((static_cast<double>(target[i]) - current[i]) * k + current[i]);
    } else {
        m_ongoing = m_aimPoint;
    }
    for (int n = 0; n < 16; ++n) {
        double f = static_cast<double>(n) * Sixteenth();
        DupFrom& c = m_copies[n];
        c.dx = static_cast<float>(f * m_ongoing.maxDisplacement[0]);
        c.dy = static_cast<float>(f * m_ongoing.maxDisplacement[1]);
        c.scaleY = static_cast<float>((static_cast<double>(m_ongoing.bottomScales[1]) - One()) * f + One());
        c.scaleX = static_cast<float>((static_cast<double>(m_ongoing.bottomScales[0]) - One()) * f + One());
        c.alpha = static_cast<float>(
            ((static_cast<double>(m_ongoing.alpha[1]) - m_ongoing.alpha[0]) * f + m_ongoing.alpha[0]) * Sixteenth());
    }
}

// 0x004A5CF0
void CSmearEffect::SetTarget(const Settings* target, int ms)
{
    m_aimPoint = *target;
    StartMorph(ms);
}

// 0x004A5D30
CSmearEffect::Settings* CSmearEffect::SetFromParams(CStruct* params, int* outMs)
{
    Scripts::CPair pair;
    pair.PrepareAll();
    if (params->FetchPair(kBlurAlphaPair, &pair, false)) {
        m_aimPoint.alpha[0] = pair.x;
        m_aimPoint.alpha[1] = pair.y;
    }
    pair.PrepareAll();
    if (params->FetchPair(kBlurBottomScales, &pair, false)) {
        m_aimPoint.bottomScales[0] = pair.x;
        m_aimPoint.bottomScales[1] = pair.y;
    }
    pair.PrepareAll();
    if (params->FetchPair(kBlurMaxDisplacement, &pair, false)) {
        m_aimPoint.maxDisplacement[0] = pair.x;
        m_aimPoint.maxDisplacement[1] = pair.y;
    }
    float seconds;
    int ms = 0;
    if (params->FetchFloat(kTime, &seconds, 0))
        ms = Truncated(static_cast<double>(seconds) * Milliseconds());
    StartMorph(ms);
    if (outMs)
        *outMs = ms;
    return &m_aimPoint;
}

// --- TextElement ----------------------------------------------------------------------------------------------

// 0x004B0920
CLabelWidget* CLabelWidget::MakeNew()
{
    CUiWidget::MakeNew();
    m_vtable = kCTextElementVtable;
    m_shadowRgba[0] = m_shadowRgba[1] = m_shadowRgba[2] = 0;
    m_shadowRgba[3] = 0x80;
    m_fontName = 0;
    m_typeface = nullptr;
    m_chars = nullptr;
    m_blur = nullptr;
    m_shadow = false;
    MakeTextObjects(1, false);
    m_innerAlignH = 0.0f;
    m_innerAlignV = 0.0f;
    m_overrideRgba = false;
    m_rememberedOverride = false;
    ChangeSpecies(kTextElement);
    return this;
}

// 0x004B1830
void CLabelWidget::DestroyBody()
{
    m_vtable = kCTextElementVtable;
    DisableBlur();
    FreeTextObjects(false);
    if (m_chars)
        MemSys::Dealloc(m_chars);
    CUiWidget::DestroyBody();
}

// 0x004B2030
CLabelWidget* CLabelWidget::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x004AF650
int CLabelWidget::FetchMagnitude() const
{
    return static_cast<int>(std::strlen(m_chars));
}

// 0x004AF670
void CLabelWidget::SetViewports()
{
    auto* window = static_cast<CPanelWidget*>(FetchPane());
    if (!window)
        return;
    if (m_num)
        TextViewport(m_texts[0], window->FetchScreenRect());
    if (m_shadow)
        TextViewport(m_shadows[0], window->FetchScreenRect());
}

// 0x004AF8C0
void CLabelWidget::ChangeCaption(const char* text)
{
    size_t size = std::strlen(text) + 1;
    bool fits = m_chars && static_cast<int>(std::strlen(m_chars)) >= static_cast<int>(size - 1);
    if (!fits) {
        if (m_chars)
            MemSys::Dealloc(m_chars);
        m_chars = static_cast<char*>(FrontEndNew(static_cast<unsigned int>(size)));
    }
    char* out = m_chars;
    while (*text) {
        char c = *text;
        if (c == '\\') {
            if (text[1] == '\\') {
                *out++ = '\\';
                c = *++text;
            } else if (text[1] == '_') {
                text += 2;
                *out++ = ' ';
                continue;
            } else if (text[1] == 'm') {
                // a button: \b and its glyph (the character after \m taken too)
                *out++ = '\\';
                text += 2;
                *out++ = 'b';
                *out++ = ButtonGlyph(text);
                ++text;
                continue;
            }
        }
        *out++ = c;
        ++text;
    }
    *out = 0;
    if (m_typeface) {
        float width, height;
        ReadStringParam(m_typeface, m_chars, &width, &height);
        if (!(m_objectFlags & kDimsLocked))
            FitToSize(width, height, 0);
    }
    m_objectFlags |= kVisibleNow;
}

// 0x004AFA00 (the target compared unrounded, the shown one with the float)
void CLabelWidget::SetTextRotation(float degrees, int now)
{
    double radians = static_cast<double>(degrees) * Degrees() + RotationOffset();
    float angle = static_cast<float>(radians);
    if (radians != m_aimPoint.angle) {
        m_aimPoint.angle = angle;
        m_objectFlags |= kDirty;
    }
    if (!now || angle == m_shown.angle)
        return;
    float width, height;
    ReadStringParam(m_typeface, m_chars, &width, &height);
    // the point turned about: internal_just (left / top 0, middle half, right / bottom all) of the scaled size
    float halfX = static_cast<float>(static_cast<double>(width) * m_summed.scaleX * Half());
    float halfY = static_cast<float>(static_cast<double>(height) * m_summed.scaleY * Half());
    for (int i = 0; i < m_num; ++i) {
        float x = halfX, y = halfY;
        if (m_innerAlignH < Zero())
            x = 0.0f;
        else if (m_innerAlignH > Zero())
            x = static_cast<float>(static_cast<double>(width) * m_summed.scaleX);
        if (m_innerAlignV < Zero())
            y = 0.0f;
        else if (m_innerAlignV > Zero())
            y = static_cast<float>(static_cast<double>(height) * m_summed.scaleY);
        TextRotation(m_texts[i], angle, x, y);
        if (m_shadow)
            TextRotation(m_shadows[i], angle, x, y);
    }
    m_objectFlags |= kDirty;
    m_shown.angle = angle;
}

// 0x004AFBB0
void CLabelWidget::SetCharProperties(Scripts::CArray* chars)
{
    ChangeGlyphText(m_texts[0], m_chars);
    for (uint32_t i = 0; i < chars->m_extent; ++i) {
        auto* c = reinterpret_cast<CStruct*>(chars->FetchPart(i));
        int index = 0;
        c->FetchIntValue(kIndex, &index, 1);
        float degrees = 0.0f;
        if (c->FetchFloat(kRotAngle, &degrees, 0))
            static_cast<Engine3D::CGlyphString*>(m_texts[0])->SetCharRotation(index, static_cast<float>(static_cast<double>(degrees) * Degrees()));
        float x = 0.0f, y = 0.0f;
        if (c->FetchPair(kOffset, &x, &y, 0)) {
            int iy = Truncated(y);
            int ix = Truncated(x);
            static_cast<Engine3D::CGlyphString*>(m_texts[0])->SetCharOffset(index, static_cast<int16_t>(ix), static_cast<int16_t>(iy));
        }
        float scaleX = 0.0f, scaleY = 0.0f;
        if (c->FetchPair(kScale, &scaleX, &scaleY, 0))
            static_cast<Engine3D::CGlyphString*>(m_texts[0])->SetCharScale(index, scaleX, scaleY);
    }
}

// 0x004AFD00
bool CLabelWidget::JoinOnto(const char* text)
{
    int length = m_chars ? static_cast<int>(std::strlen(m_chars)) : 0;
    int total = static_cast<int>(std::strlen(text)) + length;
    if (total + 1 >= 0x60 || total <= 0)
        return false;
    char joined[0x60];
    joined[0] = 0;
    if (m_chars)
        CopyText(joined, m_chars);
    AppendText(joined, text);
    ChangeCaption(joined);
    return true;
}

// 0x004AFDD0
bool CLabelWidget::Backspace()
{
    if (!m_chars)
        return false;
    int length = static_cast<int>(std::strlen(m_chars));
    if (length == 0)
        return false;
    char shorter[0x60];
    CopyText(shorter, m_chars);
    if (length > 1 && shorter[length - 1] == '\\' && shorter[length - 2] == '\\')
        shorter[length - 2] = 0;
    shorter[length - 1] = 0;
    ChangeCaption(shorter);
    return true;
}

// 0x004B0620
void CLabelWidget::MakeTextObjects(int count, bool onlyShadow)
{
    if (m_shadow) {
        m_shadows = static_cast<void**>(FrontEndNew(static_cast<unsigned int>(count) * 4));
        for (int i = 0; i < count; ++i)
            m_shadows[i] = NewTextObject();
    }
    if (!onlyShadow) {
        m_num = count;
        m_texts = static_cast<void**>(FrontEndNew(static_cast<unsigned int>(count) * 4));
        for (int i = 0; i < count; ++i)
            m_texts[i] = NewTextObject();
    }
    SetViewports();
}

// 0x004B0760
void CLabelWidget::FreeTextObjects(bool onlyShadow)
{
    if (m_shadows) {
        for (int i = 0; i < m_num; ++i)
            FreeTextObject(m_shadows[i]);
        MemSys::Dealloc(m_shadows);
        m_shadows = nullptr;
    }
    if (!onlyShadow) {
        for (int i = 0; i < m_num; ++i)
            FreeTextObject(m_texts[i]);
        MemSys::Dealloc(m_texts);
        m_num = 0;
    }
}

// 0x004B0A20 (the blur's memory isn't initialised: its constructor 0x005902E0 only returns it)
void CLabelWidget::EnableBlur()
{
    if (m_blur)
        return;
    FreeTextObjects(false);
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_frontEndHeap);
    m_blur = static_cast<CSmearEffect*>(MemSys::Alloc(sizeof(CSmearEffect)));
    std::memcpy(&m_blur->m_rgbaValue, m_shown.rgba, 4);
    MemSys::Hub::Pull()->LeaveHeapScope();
    MakeTextObjects(17, false);
}

// 0x004B0AE0
void CLabelWidget::DisableBlur()
{
    if (!m_blur)
        return;
    FreeTextObjects(false);
    DeleteBlur(m_blur);
    m_blur = nullptr;
    MakeTextObjects(1, false);
}

// 0x004B18E0
void CLabelWidget::ChangeTraits(CStruct* params)
{
    CUiWidget::ChangeTraits(params);
    uint32_t font;
    if (params->ReadHashValue(kFont, &font, 0)) {
        m_fontName = font;
        m_typeface = GetFont(font);
        m_objectFlags |= kVisibleNow;
    }
    float degrees = 0.0f;
    if (params->FetchFloat(kRotAngle, &degrees, 0))
        SetTextRotation(degrees, 1);
    if (ReadAlignmentParam(params, kInnerAlign, &m_innerAlignH, &m_innerAlignV))
        m_objectFlags |= kVisibleNow;
    const char* text;
    if (params->FetchText(kText, &text, 0))
        ChangeCaption(text);
    if (params->HasBareFlag(kBlurEffect))
        EnableBlur();
    if (params->HasBareFlag(kNoBlurEffect))
        DisableBlur();
    uint32_t rgba;
    if (ReadColourParam(params, kHaloColor, &rgba) && m_blur)
        m_blur->m_rgbaValue = rgba;
    Scripts::CPair offset;
    offset.PrepareAll();
    if (params->FetchPair(kShadowOffs, &offset, false)) {
        m_shadowX = offset.x;
        m_shadowY = offset.y;
    }
    if (params->HasBareFlag(kShadow) && !m_shadow) {
        m_shadow = true;
        MakeTextObjects(m_num, true);
    }
    if (params->HasBareFlag(kShadowOffFlag) && m_shadow) {
        m_shadow = false;
        FreeTextObjects(true);
    }
    ReadColourParam(params, kShadowRgba, reinterpret_cast<uint32_t*>(m_shadowRgba));
    if (params->HasBareFlag(kRememberOverrideRgbaState))
        m_rememberedOverride = m_overrideRgba;
    if (params->HasBareFlag(kRestoreOverrideRgbaState))
        m_overrideRgba = m_rememberedOverride;
    if (params->HasBareFlag(kOverrideEncodedRgba))
        m_overrideRgba = true;
    if (params->HasBareFlag(kDontOverrideEncodedRgba))
        m_overrideRgba = false;
    if (params->HasBareFlag(kOverrideEncodedAlpha))
        m_overrideAlpha = true;
    if (params->HasBareFlag(kDontOverrideEncodedAlpha))
        m_overrideAlpha = false;
    Scripts::CArray* chars = nullptr;
    if (params->FetchList(kCharProperties, &chars, 0))
        SetCharProperties(chars);
    SetViewports();
}

// 0x004B09D0
void CLabelWidget::BlendTowards(CStruct* params)
{
    if (m_blur)
        m_blur->SetFromParams(params, nullptr);
    CUiWidget::BlendTowards(params);
    float degrees;
    if (params->FetchFloat(kRotAngle, &degrees, 0))
        SetTextRotation(degrees, 0);
}

// 0x004AFE70
void CLabelWidget::TickFrame()
{
    if (!m_chars)
        return;
    float top = m_summed.top;
    bool outside = top < LayoutTop() || top > LayoutBottom();
    float shownAngle = (m_objectFlags & kFlag1000000) ? m_shown.angle : m_summed.angle;
    if (m_num) {
        uint32_t shown;
        std::memcpy(&shown, m_shown.rgba, 4);
        void* t = m_texts[0];
        uint32_t rgba = WithAlpha(shown, Faded(static_cast<int>(shown >> 24), m_summed.alpha));
        TextOverrideAlpha(t, m_overrideAlpha);
        // the point turned about (as SetTextRotation), from the text's size when it is turned
        float width = 0.0f, height = 0.0f;
        if (m_summed.angle != Zero())
            ReadStringParam(m_typeface, m_chars, &width, &height);
        double scaledWidth = static_cast<double>(width) * m_summed.scaleX;
        float x = static_cast<float>(Half() * scaledWidth);
        double scaledHeight = static_cast<double>(height) * m_summed.scaleY;
        float scaledHeightF = static_cast<float>(scaledHeight);
        float y = static_cast<float>(scaledHeight * Half());
        if (m_innerAlignH < Zero())
            x = 0.0f;
        else if (m_innerAlignH > Zero())
            x = static_cast<float>(scaledWidth);
        if (m_innerAlignV < Zero())
            y = 0.0f;
        else if (m_innerAlignV > Zero())
            y = scaledHeightF;
        TextFont(t, m_typeface);
        TextPos(t, m_summed.left, m_summed.top);
        TextColour(t, rgba, m_overrideRgba);
        TextRotation(t, shownAngle, x, y);
        TextScale(t, m_summed.scaleX, m_summed.scaleY);
        ChangeGlyphText(t, m_chars);
        bool hidden;
        if (m_objectFlags & kPos3d) {
            TextDepth(t, m_summed.z);
            hidden = (m_objectFlags & kOffScreen) || IsConcealed();
        } else {
            TextPriority(t, m_zPriority);
            hidden = outside || IsConcealed();
        }
        TextHidden(t, hidden);
        if (m_shadow) {
            void* s = m_shadows[0];
            uint32_t shadow;
            std::memcpy(&shadow, m_shadowRgba, 4);
            uint32_t shadowRgba = WithAlpha(shadow, Faded(m_shadowRgba[3], m_summed.alpha));
            TextOverrideAlpha(s, m_overrideAlpha);
            TextFont(s, m_typeface);
            TextPos(s, static_cast<float>(static_cast<double>(m_shadowX) + m_summed.left),
                   static_cast<float>(static_cast<double>(m_shadowY) + m_summed.top));
            TextColour(s, shadowRgba, m_overrideRgba);
            TextRotation(s, shownAngle, x, y);
            TextScale(s, m_summed.scaleX, m_summed.scaleY);
            ChangeGlyphText(s, m_chars);
            if (m_objectFlags & kPos3d) {
                TextDepth(s, m_summed.z);
                TextHidden(s, (m_objectFlags & kOffScreen) || IsConcealed());
            } else {
                TextPriority(s, static_cast<float>(static_cast<double>(m_zPriority) - ShadowBehind()));
                TextHidden(s, outside || IsConcealed());
            }
        }
    }
    if (!m_blur)
        return;
    // the blur copies: around the element's centre, each displaced, scaled and fainter, behind the one before
    m_blur->TickFrame();
    float centreX =
        static_cast<float>(static_cast<double>(m_summed.scaleX) * m_span * Half() + m_summed.left);
    float centreY =
        static_cast<float>(static_cast<double>(m_summed.scaleY) * m_levelY * Half() + m_summed.top);
    for (int i = 1; i < m_num; ++i) {
        CSmearEffect::DupFrom* copy = m_blur->GetCopy(i - 1);
        uint32_t blur = m_blur->m_rgbaValue;
        uint32_t rgba = WithAlpha(
            blur, static_cast<uint8_t>(Truncated(static_cast<double>(static_cast<int>(blur >> 24)) * copy->alpha)));
        void* t = m_texts[i];
        float x = static_cast<float>(
            centreX - (static_cast<double>(m_span) * copy->scaleX * Half() + copy->dx) * m_summed.scaleX);
        float y = static_cast<float>(
            centreY - (static_cast<double>(m_levelY) * copy->scaleY * Half() + copy->dy) * m_summed.scaleY);
        float scaleX = static_cast<float>(static_cast<double>(m_summed.scaleX) * copy->scaleX);
        float scaleY = static_cast<float>(static_cast<double>(m_summed.scaleY) * copy->scaleY);
        TextOverrideAlpha(t, m_overrideAlpha);
        TextPos(t, x, y);
        TextColour(t, rgba, m_overrideRgba);
        TextScale(t, scaleX, scaleY);
        float behind = static_cast<float>(static_cast<double>(i) * BlurBehind());
        TextPriority(t, static_cast<float>(static_cast<double>(m_zPriority) - behind));
        TextFont(t, m_typeface);
        ChangeGlyphText(t, m_chars);
        TextHidden(t, outside || IsConcealed());
        if (m_shadow) {
            void* s = m_shadows[i];
            uint32_t shadow;
            std::memcpy(&shadow, m_shadowRgba, 4);
            TextOverrideAlpha(s, m_overrideAlpha);
            uint32_t shadowRgba = WithAlpha(shadow, Faded(m_shadowRgba[3], m_summed.alpha));
            TextPos(s, static_cast<float>(static_cast<double>(scaleX) * m_shadowX + x),
                   static_cast<float>(static_cast<double>(scaleY) * m_shadowY + y));
            TextColour(s, shadowRgba, m_overrideRgba);
            TextScale(s, scaleX, scaleY);
            TextPriority(s, static_cast<float>(static_cast<double>(m_zPriority) - behind - ShadowBehind()));
            TextFont(s, m_typeface);
            ChangeGlyphText(s, m_chars);
            TextHidden(s, outside || IsConcealed());
        }
    }
}

namespace {
static VtableSlot kCTextElementVtable_slots[] = {&CLabelWidget::Teardown, &CLabelWidget::ChangeTraits,
                static_cast<bool (GameObjects::CEntity::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CEntity::InvokeMethodByName),
                &CUiWidget::InvokeMethodByName, Stubs::IgnoreArgument, &GameObjects::CEntity::PassEventToScript,
                &GameObjects::CEntity::FlagAsDead, Stubs::ReturnTrue, &CUiWidget::GetProperties, &CLabelWidget::BlendTowards,
                &CLabelWidget::TickFrame};
void** const kCTextElementVtable = reinterpret_cast<void**>(kCTextElementVtable_slots);
} // namespace

} // namespace Front

using MenuSystem::CSmearEffect;
using MenuSystem::CLabelWidget;
