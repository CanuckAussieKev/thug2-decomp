// The text element and its blur effect: see text_element.h.

#include "game/front/text_element.h"

#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/front/sprite_element.h"
#include "game/script/struct.h"
#include "game/sys/timer.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Front {

namespace {

using Script::CStruct;

// Names (checksums).
constexpr uint32_t kTextElement = 0x5200DFB6, kFont = 0x2F6BF72D, kRotAngle = 0xED344E1B;
constexpr uint32_t kInternalJust = 0x67E093E4, kText = 0xC4745838, kBlurEffect = 0xD1653C6C;
constexpr uint32_t kNoBlurEffect = 0x4B73E4F8, kBlurRgba = 0x4F0E1182, kShadowOffs = 0x2A1FE0CC;
constexpr uint32_t kShadow = 0x8A897DD2, kNoShadow = 0x95B5B9A0, kShadowRgba = 0x1E7BB1F5;
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

float Constant(uint32_t address) { return GameVariable<float>(address); }
float Degrees() { return Constant(0x00645464); }        // pi / 180
float RotationOffset() { return Constant(0x006461D8); }  // 0.001 (added to a rotation in radians)
float MinimumAlpha() { return Constant(0x006453D4); }    // 0.0001: below it, transparent
float LayoutTop() { return Constant(0x0064A258); }       // -200: text whose top is above it is hidden
float LayoutBottom() { return Constant(0x0064A254); }    // 648: ... or below it
float Zero() { return Constant(0x0066D520); }
float Half() { return Constant(0x006453BC); }
float One() { return Constant(0x006453A8); }
float ShadowBehind() { return Constant(0x0064541C); }    // 0.005: the shadow's priority below the text's
float BlurBehind() { return Constant(0x0064544C); }      // 0.01: each blur copy's below the one before
float Sixteenth() { return Constant(0x00645A84); }       // 1/16
float Milliseconds() { return Constant(0x006453C0); }    // 1000
double TwoTo32() { return Constant(0x006453B4); }        // (unsigned to floating point)

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
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_frontEndHeap);
    void* block = Mem::New(size);
    Mem::Manager::Get()->PopContext();
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
void* GetFont(uint32_t name) { return GameFunction<void*(__cdecl*)(uint32_t)>(0x0048D760)(name); }
void QueryString(void* font, const char* text, float* width, float* height)
{
    GameFunction<void(__thiscall*)(void*, const char*, float*, float*)>(0x0048D060)(font, text, width, height);
}
// A button glyph's character for the text after \m (0x0048D6A0, cdecl).
char ButtonGlyph(const char* text) { return GameFunction<char(__cdecl*)(const char*)>(0x0048D6A0)(text); }

// The text objects: made / freed (cdecl), and their setters (thiscall).
void* NewTextObject() { return GameFunction<void*(__cdecl*)()>(0x0048D1D0)(); }
void FreeTextObject(void* text) { GameFunction<void(__cdecl*)(void*)>(0x0048D5F0)(text); }
template <typename... A>
void Set(uint32_t address, void* text, A... args)
{
    GameFunction<void(__thiscall*)(void*, A...)>(address)(text, args...);
}
void TextHidden(void* t, bool hidden) { Set(0x0048D0C0, t, hidden); }
void TextFont(void* t, void* font) { Set(0x0048D0D0, t, font); }
void TextPos(void* t, float x, float y) { Set(0x0048D0E0, t, x, y); }
void TextScale(void* t, float x, float y) { Set(0x0048D100, t, x, y); }
void TextRotation(void* t, float angle, float x, float y) { Set(0x0048D120, t, angle, x, y); }
void TextPriority(void* t, float priority) { Set(0x0048D140, t, priority); }
void TextDepth(void* t, float depth) { Set(0x0048D160, t, depth); }
void TextColour(void* t, uint32_t rgba, bool overrideEncoded) { Set(0x0048D180, t, rgba, overrideEncoded); }
void TextOverrideAlpha(void* t, bool overrideAlpha) { Set(0x0048D1A0, t, overrideAlpha); }
void TextViewport(void* t, void* window) { Set(0x0048D1B0, t, window); }
void TextString(void* t, const char* text) { Set(0x0048D530, t, text); }

// The blur's empty destructor (0x005480A0) and its memory freed.
void DeleteBlur(CBlurEffect* blur)
{
    if (blur) {
        GameFunction<void(__thiscall*)(void*)>(0x005480A0)(blur);
        Mem::Free(blur);
    }
}

} // namespace

// --- the blur effect ------------------------------------------------------------------------------------------

// 0x004A5770
CBlurEffect::Copy* CBlurEffect::GetCopy(int i)
{
    return &m_copies[i];
}

// 0x004A5780
void CBlurEffect::StartMorph(int ms)
{
    if (ms == 0)
        m_current = m_target;
    m_remaining = ms;
    m_lastTime = static_cast<int>(Tmr::GetTime());
}

// 0x004A57D0
void CBlurEffect::Update()
{
    int now = static_cast<int>(Tmr::GetTime());
    double k = One();
    if (m_remaining)
        k = Unsigned(now - m_lastTime) / Unsigned(m_remaining);
    if (static_cast<uint32_t>(now - m_lastTime) < static_cast<uint32_t>(m_remaining))
        m_remaining = m_lastTime - now + m_remaining;
    else
        m_remaining = 0;
    m_lastTime = now;
    float* current = &m_current.alpha[0];
    const float* target = &m_target.alpha[0];
    if (m_remaining) {
        for (int i = 0; i < 6; ++i)
            current[i] = static_cast<float>((static_cast<double>(target[i]) - current[i]) * k + current[i]);
    } else {
        m_current = m_target;
    }
    for (int n = 0; n < 16; ++n) {
        double f = static_cast<double>(n) * Sixteenth();
        Copy& c = m_copies[n];
        c.dx = static_cast<float>(f * m_current.maxDisplacement[0]);
        c.dy = static_cast<float>(f * m_current.maxDisplacement[1]);
        c.scaleY = static_cast<float>((static_cast<double>(m_current.bottomScales[1]) - One()) * f + One());
        c.scaleX = static_cast<float>((static_cast<double>(m_current.bottomScales[0]) - One()) * f + One());
        c.alpha = static_cast<float>(
            ((static_cast<double>(m_current.alpha[1]) - m_current.alpha[0]) * f + m_current.alpha[0]) * Sixteenth());
    }
}

// 0x004A5CF0
void CBlurEffect::SetTarget(const Settings* target, int ms)
{
    m_target = *target;
    StartMorph(ms);
}

// 0x004A5D30
CBlurEffect::Settings* CBlurEffect::SetFromParams(CStruct* params, int* outMs)
{
    Script::CPair pair;
    pair.Init();
    if (params->GetPair(kBlurAlphaPair, &pair, false)) {
        m_target.alpha[0] = pair.x;
        m_target.alpha[1] = pair.y;
    }
    pair.Init();
    if (params->GetPair(kBlurBottomScales, &pair, false)) {
        m_target.bottomScales[0] = pair.x;
        m_target.bottomScales[1] = pair.y;
    }
    pair.Init();
    if (params->GetPair(kBlurMaxDisplacement, &pair, false)) {
        m_target.maxDisplacement[0] = pair.x;
        m_target.maxDisplacement[1] = pair.y;
    }
    float seconds;
    int ms = 0;
    if (params->GetFloat(kTime, &seconds, 0))
        ms = Truncated(static_cast<double>(seconds) * Milliseconds());
    StartMorph(ms);
    if (outMs)
        *outMs = ms;
    return &m_target;
}

// --- TextElement ----------------------------------------------------------------------------------------------

// 0x004B0920
CTextElement* CTextElement::Construct()
{
    CScreenElement::Construct();
    m_vtable = reinterpret_cast<void**>(0x0064A290);
    m_shadowRgba[0] = m_shadowRgba[1] = m_shadowRgba[2] = 0;
    m_shadowRgba[3] = 0x80;
    m_fontName = 0;
    mp_font = nullptr;
    m_string = nullptr;
    mp_blur = nullptr;
    m_shadow = false;
    MakeTextObjects(1, false);
    m_internalJustX = 0.0f;
    m_internalJustY = 0.0f;
    m_overrideRgba = false;
    m_rememberedOverride = false;
    SetType(kTextElement);
    return this;
}

// 0x004B1830
void CTextElement::DestroyBody()
{
    m_vtable = reinterpret_cast<void**>(0x0064A290);
    DisableBlur();
    FreeTextObjects(false);
    if (m_string)
        Mem::Free(m_string);
    CScreenElement::DestroyBody();
}

// 0x004B2030
CTextElement* CTextElement::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x004AF650
int CTextElement::GetLength() const
{
    return static_cast<int>(std::strlen(m_string));
}

// 0x004AF670
void CTextElement::SetViewports()
{
    auto* window = static_cast<CWindowElement*>(GetWindow());
    if (!window)
        return;
    if (m_count)
        TextViewport(mp_texts[0], window->GetWindow2d());
    if (m_shadow)
        TextViewport(mp_shadows[0], window->GetWindow2d());
}

// 0x004AF8C0
void CTextElement::SetText(const char* text)
{
    size_t size = std::strlen(text) + 1;
    bool fits = m_string && static_cast<int>(std::strlen(m_string)) >= static_cast<int>(size - 1);
    if (!fits) {
        if (m_string)
            Mem::Free(m_string);
        m_string = static_cast<char*>(FrontEndNew(static_cast<unsigned int>(size)));
    }
    char* out = m_string;
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
    if (mp_font) {
        float width, height;
        QueryString(mp_font, m_string, &width, &height);
        if (!(m_objectFlags & kDimsLocked))
            SetDims(width, height, 0);
    }
    m_objectFlags |= kVisibleNow;
}

// 0x004AFA00 (the target compared unrounded, the shown one with the float)
void CTextElement::SetTextRotation(float degrees, int now)
{
    double radians = static_cast<double>(degrees) * Degrees() + RotationOffset();
    float angle = static_cast<float>(radians);
    if (radians != m_target.angle) {
        m_target.angle = angle;
        m_objectFlags |= kDirty;
    }
    if (!now || angle == m_shown.angle)
        return;
    float width, height;
    QueryString(mp_font, m_string, &width, &height);
    // the point turned about: internal_just (left / top 0, middle half, right / bottom all) of the scaled size
    float halfX = static_cast<float>(static_cast<double>(width) * m_summed.scaleX * Half());
    float halfY = static_cast<float>(static_cast<double>(height) * m_summed.scaleY * Half());
    for (int i = 0; i < m_count; ++i) {
        float x = halfX, y = halfY;
        if (m_internalJustX < Zero())
            x = 0.0f;
        else if (m_internalJustX > Zero())
            x = static_cast<float>(static_cast<double>(width) * m_summed.scaleX);
        if (m_internalJustY < Zero())
            y = 0.0f;
        else if (m_internalJustY > Zero())
            y = static_cast<float>(static_cast<double>(height) * m_summed.scaleY);
        TextRotation(mp_texts[i], angle, x, y);
        if (m_shadow)
            TextRotation(mp_shadows[i], angle, x, y);
    }
    m_objectFlags |= kDirty;
    m_shown.angle = angle;
}

// 0x004AFBB0
void CTextElement::SetCharProperties(Script::CArray* chars)
{
    TextString(mp_texts[0], m_string);
    for (uint32_t i = 0; i < chars->m_size; ++i) {
        auto* c = reinterpret_cast<CStruct*>(chars->GetElement(i));
        int index = 0;
        c->GetInteger(kIndex, &index, 1);
        float degrees = 0.0f;
        if (c->GetFloat(kRotAngle, &degrees, 0))
            Set(0x0048D450, mp_texts[0], index, static_cast<float>(static_cast<double>(degrees) * Degrees()));
        float x = 0.0f, y = 0.0f;
        if (c->GetPair(kOffset, &x, &y, 0)) {
            int iy = Truncated(y);
            int ix = Truncated(x);
            Set(0x0048D280, mp_texts[0], index, ix, iy);
        }
        float scaleX = 0.0f, scaleY = 0.0f;
        if (c->GetPair(kScale, &scaleX, &scaleY, 0))
            Set(0x0048D360, mp_texts[0], index, scaleX, scaleY);
    }
}

// 0x004AFD00
bool CTextElement::Concatenate(const char* text)
{
    int length = m_string ? static_cast<int>(std::strlen(m_string)) : 0;
    int total = static_cast<int>(std::strlen(text)) + length;
    if (total + 1 >= 0x60 || total <= 0)
        return false;
    char joined[0x60];
    joined[0] = 0;
    if (m_string)
        CopyText(joined, m_string);
    AppendText(joined, text);
    SetText(joined);
    return true;
}

// 0x004AFDD0
bool CTextElement::Backspace()
{
    if (!m_string)
        return false;
    int length = static_cast<int>(std::strlen(m_string));
    if (length == 0)
        return false;
    char shorter[0x60];
    CopyText(shorter, m_string);
    if (length > 1 && shorter[length - 1] == '\\' && shorter[length - 2] == '\\')
        shorter[length - 2] = 0;
    shorter[length - 1] = 0;
    SetText(shorter);
    return true;
}

// 0x004B0620
void CTextElement::MakeTextObjects(int count, bool shadowOnly)
{
    if (m_shadow) {
        mp_shadows = static_cast<void**>(FrontEndNew(static_cast<unsigned int>(count) * 4));
        for (int i = 0; i < count; ++i)
            mp_shadows[i] = NewTextObject();
    }
    if (!shadowOnly) {
        m_count = count;
        mp_texts = static_cast<void**>(FrontEndNew(static_cast<unsigned int>(count) * 4));
        for (int i = 0; i < count; ++i)
            mp_texts[i] = NewTextObject();
    }
    SetViewports();
}

// 0x004B0760
void CTextElement::FreeTextObjects(bool shadowOnly)
{
    if (mp_shadows) {
        for (int i = 0; i < m_count; ++i)
            FreeTextObject(mp_shadows[i]);
        Mem::Free(mp_shadows);
        mp_shadows = nullptr;
    }
    if (!shadowOnly) {
        for (int i = 0; i < m_count; ++i)
            FreeTextObject(mp_texts[i]);
        Mem::Free(mp_texts);
        m_count = 0;
    }
}

// 0x004B0A20 (the blur's memory isn't initialised: its constructor 0x005902E0 only returns it)
void CTextElement::EnableBlur()
{
    if (mp_blur)
        return;
    FreeTextObjects(false);
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_frontEndHeap);
    mp_blur = static_cast<CBlurEffect*>(Mem::New(sizeof(CBlurEffect)));
    std::memcpy(&mp_blur->m_rgba, m_shown.rgba, 4);
    Mem::Manager::Get()->PopContext();
    MakeTextObjects(17, false);
}

// 0x004B0AE0
void CTextElement::DisableBlur()
{
    if (!mp_blur)
        return;
    FreeTextObjects(false);
    DeleteBlur(mp_blur);
    mp_blur = nullptr;
    MakeTextObjects(1, false);
}

// 0x004B18E0
void CTextElement::SetProperties(CStruct* params)
{
    CScreenElement::SetProperties(params);
    uint32_t font;
    if (params->GetChecksum(kFont, &font, 0)) {
        m_fontName = font;
        mp_font = GetFont(font);
        m_objectFlags |= kVisibleNow;
    }
    float degrees = 0.0f;
    if (params->GetFloat(kRotAngle, &degrees, 0))
        SetTextRotation(degrees, 1);
    if (ResolveJust(params, kInternalJust, &m_internalJustX, &m_internalJustY))
        m_objectFlags |= kVisibleNow;
    const char* text;
    if (params->GetString(kText, &text, 0))
        SetText(text);
    if (params->ContainsFlag(kBlurEffect))
        EnableBlur();
    if (params->ContainsFlag(kNoBlurEffect))
        DisableBlur();
    uint32_t rgba;
    if (ResolveRGBA(params, kBlurRgba, &rgba) && mp_blur)
        mp_blur->m_rgba = rgba;
    Script::CPair offset;
    offset.Init();
    if (params->GetPair(kShadowOffs, &offset, false)) {
        m_shadowX = offset.x;
        m_shadowY = offset.y;
    }
    if (params->ContainsFlag(kShadow) && !m_shadow) {
        m_shadow = true;
        MakeTextObjects(m_count, true);
    }
    if (params->ContainsFlag(kNoShadow) && m_shadow) {
        m_shadow = false;
        FreeTextObjects(true);
    }
    ResolveRGBA(params, kShadowRgba, reinterpret_cast<uint32_t*>(m_shadowRgba));
    if (params->ContainsFlag(kRememberOverrideRgbaState))
        m_rememberedOverride = m_overrideRgba;
    if (params->ContainsFlag(kRestoreOverrideRgbaState))
        m_overrideRgba = m_rememberedOverride;
    if (params->ContainsFlag(kOverrideEncodedRgba))
        m_overrideRgba = true;
    if (params->ContainsFlag(kDontOverrideEncodedRgba))
        m_overrideRgba = false;
    if (params->ContainsFlag(kOverrideEncodedAlpha))
        m_overrideAlpha = true;
    if (params->ContainsFlag(kDontOverrideEncodedAlpha))
        m_overrideAlpha = false;
    Script::CArray* chars = nullptr;
    if (params->GetArray(kCharProperties, &chars, 0))
        SetCharProperties(chars);
    SetViewports();
}

// 0x004B09D0
void CTextElement::SetMorph(CStruct* params)
{
    if (mp_blur)
        mp_blur->SetFromParams(params, nullptr);
    CScreenElement::SetMorph(params);
    float degrees;
    if (params->GetFloat(kRotAngle, &degrees, 0))
        SetTextRotation(degrees, 0);
}

// 0x004AFE70
void CTextElement::Update()
{
    if (!m_string)
        return;
    float top = m_summed.top;
    bool outside = top < LayoutTop() || top > LayoutBottom();
    float shownAngle = (m_objectFlags & kFlag1000000) ? m_shown.angle : m_summed.angle;
    if (m_count) {
        uint32_t shown;
        std::memcpy(&shown, m_shown.rgba, 4);
        void* t = mp_texts[0];
        uint32_t rgba = WithAlpha(shown, Faded(static_cast<int>(shown >> 24), m_summed.alpha));
        TextOverrideAlpha(t, m_overrideAlpha);
        // the point turned about (as SetTextRotation), from the text's size when it is turned
        float width = 0.0f, height = 0.0f;
        if (m_summed.angle != Zero())
            QueryString(mp_font, m_string, &width, &height);
        double scaledWidth = static_cast<double>(width) * m_summed.scaleX;
        float x = static_cast<float>(Half() * scaledWidth);
        double scaledHeight = static_cast<double>(height) * m_summed.scaleY;
        float scaledHeightF = static_cast<float>(scaledHeight);
        float y = static_cast<float>(scaledHeight * Half());
        if (m_internalJustX < Zero())
            x = 0.0f;
        else if (m_internalJustX > Zero())
            x = static_cast<float>(scaledWidth);
        if (m_internalJustY < Zero())
            y = 0.0f;
        else if (m_internalJustY > Zero())
            y = scaledHeightF;
        TextFont(t, mp_font);
        TextPos(t, m_summed.left, m_summed.top);
        TextColour(t, rgba, m_overrideRgba);
        TextRotation(t, shownAngle, x, y);
        TextScale(t, m_summed.scaleX, m_summed.scaleY);
        TextString(t, m_string);
        bool hidden;
        if (m_objectFlags & kPos3d) {
            TextDepth(t, m_summed.z);
            hidden = (m_objectFlags & kOffScreen) || IsHidden();
        } else {
            TextPriority(t, m_zPriority);
            hidden = outside || IsHidden();
        }
        TextHidden(t, hidden);
        if (m_shadow) {
            void* s = mp_shadows[0];
            uint32_t shadow;
            std::memcpy(&shadow, m_shadowRgba, 4);
            uint32_t shadowRgba = WithAlpha(shadow, Faded(m_shadowRgba[3], m_summed.alpha));
            TextOverrideAlpha(s, m_overrideAlpha);
            TextFont(s, mp_font);
            TextPos(s, static_cast<float>(static_cast<double>(m_shadowX) + m_summed.left),
                   static_cast<float>(static_cast<double>(m_shadowY) + m_summed.top));
            TextColour(s, shadowRgba, m_overrideRgba);
            TextRotation(s, shownAngle, x, y);
            TextScale(s, m_summed.scaleX, m_summed.scaleY);
            TextString(s, m_string);
            if (m_objectFlags & kPos3d) {
                TextDepth(s, m_summed.z);
                TextHidden(s, (m_objectFlags & kOffScreen) || IsHidden());
            } else {
                TextPriority(s, static_cast<float>(static_cast<double>(m_zPriority) - ShadowBehind()));
                TextHidden(s, outside || IsHidden());
            }
        }
    }
    if (!mp_blur)
        return;
    // the blur copies: around the element's centre, each displaced, scaled and fainter, behind the one before
    mp_blur->Update();
    float centreX =
        static_cast<float>(static_cast<double>(m_summed.scaleX) * m_width * Half() + m_summed.left);
    float centreY =
        static_cast<float>(static_cast<double>(m_summed.scaleY) * m_height * Half() + m_summed.top);
    for (int i = 1; i < m_count; ++i) {
        CBlurEffect::Copy* copy = mp_blur->GetCopy(i - 1);
        uint32_t blur = mp_blur->m_rgba;
        uint32_t rgba = WithAlpha(
            blur, static_cast<uint8_t>(Truncated(static_cast<double>(static_cast<int>(blur >> 24)) * copy->alpha)));
        void* t = mp_texts[i];
        float x = static_cast<float>(
            centreX - (static_cast<double>(m_width) * copy->scaleX * Half() + copy->dx) * m_summed.scaleX);
        float y = static_cast<float>(
            centreY - (static_cast<double>(m_height) * copy->scaleY * Half() + copy->dy) * m_summed.scaleY);
        float scaleX = static_cast<float>(static_cast<double>(m_summed.scaleX) * copy->scaleX);
        float scaleY = static_cast<float>(static_cast<double>(m_summed.scaleY) * copy->scaleY);
        TextOverrideAlpha(t, m_overrideAlpha);
        TextPos(t, x, y);
        TextColour(t, rgba, m_overrideRgba);
        TextScale(t, scaleX, scaleY);
        float behind = static_cast<float>(static_cast<double>(i) * BlurBehind());
        TextPriority(t, static_cast<float>(static_cast<double>(m_zPriority) - behind));
        TextFont(t, mp_font);
        TextString(t, m_string);
        TextHidden(t, outside || IsHidden());
        if (m_shadow) {
            void* s = mp_shadows[i];
            uint32_t shadow;
            std::memcpy(&shadow, m_shadowRgba, 4);
            TextOverrideAlpha(s, m_overrideAlpha);
            uint32_t shadowRgba = WithAlpha(shadow, Faded(m_shadowRgba[3], m_summed.alpha));
            TextPos(s, static_cast<float>(static_cast<double>(scaleX) * m_shadowX + x),
                   static_cast<float>(static_cast<double>(scaleY) * m_shadowY + y));
            TextColour(s, shadowRgba, m_overrideRgba);
            TextScale(s, scaleX, scaleY);
            TextPriority(s, static_cast<float>(static_cast<double>(m_zPriority) - behind - ShadowBehind()));
            TextFont(s, mp_font);
            TextString(s, m_string);
            TextHidden(s, outside || IsHidden());
        }
    }
}

} // namespace Front

using Front::CBlurEffect;
using Front::CTextElement;

REPLACE(0x004A5770, &CBlurEffect::GetCopy);
REPLACE(0x004A5780, &CBlurEffect::StartMorph);
REPLACE(0x004A57D0, &CBlurEffect::Update);
REPLACE(0x004A5CF0, &CBlurEffect::SetTarget);
REPLACE(0x004A5D30, &CBlurEffect::SetFromParams);
REPLACE(0x004B0920, &CTextElement::Construct);
REPLACE(0x004B1830, &CTextElement::DestroyBody);
REPLACE(0x004B2030, &CTextElement::Destroy);
REPLACE(0x004B18E0, &CTextElement::SetProperties);
REPLACE(0x004B09D0, &CTextElement::SetMorph);
REPLACE(0x004AFE70, &CTextElement::Update);
REPLACE(0x004AF650, &CTextElement::GetLength);
REPLACE(0x004AF670, &CTextElement::SetViewports);
REPLACE(0x004AF8C0, &CTextElement::SetText);
REPLACE(0x004AFA00, &CTextElement::SetTextRotation);
REPLACE(0x004AFBB0, &CTextElement::SetCharProperties);
REPLACE(0x004AFD00, &CTextElement::Concatenate);
REPLACE(0x004AFDD0, &CTextElement::Backspace);
REPLACE(0x004B0620, &CTextElement::MakeTextObjects);
REPLACE(0x004B0760, &CTextElement::FreeTextObjects);
REPLACE(0x004B0A20, &CTextElement::EnableBlur);
REPLACE(0x004B0AE0, &CTextElement::DisableBlur);
