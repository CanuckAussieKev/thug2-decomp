// The text block element: see text_block.h.

#include "game/front/text_block.h"

#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/str.h"
#include "game/core/stubs.h"
#include "game/front/screen_element_manager.h"
#include "game/front/text_element.h"
#include "game/gfx/font.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/game_code.h"
#include "game/obj/object.h"
#include "game/front/screen_element.h"
#include "game/front/screen_constants.h"
#include "game/gfx/engine_geom.h"

namespace Front {

namespace {
extern void** const kCTextBlockElementVtable;
} // namespace


namespace {

using Script::CStruct;

// Names (checksums).
constexpr uint32_t kTextBlockElement = 0x40D92263, kFont = 0x2F6BF72D, kInternalJust = 0x67E093E4;
constexpr uint32_t kInternalScale = 0x1FE341D2, kLineSpacing = 0xE3FA22FC, kAllowExpansion = 0x322839A2;
constexpr uint32_t kText = 0xC4745838, kBlurEffect = 0xD1653C6C, kNoBlurEffect = 0x4B73E4F8;
constexpr uint32_t kBlurRgba = 0x4F0E1182, kShadowOffs = 0x2A1FE0CC, kShadow = 0x8A897DD2;
constexpr uint32_t kNoShadow = 0x95B5B9A0, kShadowRgba = 0x1E7BB1F5;
constexpr uint32_t kRememberOverrideRgbaState = 0x0BF51A0C, kRestoreOverrideRgbaState = 0x61B7B58B;
constexpr uint32_t kOverrideEncodedRgba = 0xB7686F92, kDontOverrideEncodedRgba = 0xD25BC6CD;
constexpr uint32_t kOverrideEncodedAlpha = 0x1A226DCD, kDontOverrideEncodedAlpha = 0xE1931201;

constexpr int kLineBuffers = 30;   // lines while they are made
constexpr int kLineChars = 0x60;
constexpr int kMaxTexts = 29;      // (the original's stack room for an array's strings)

// strcpy / strcat (the original's: no length limit).
void CopyText(char* to, const char* from)
{
    while ((*to++ = *from++) != '\0') {
    }
}
void AppendText(char* to, const char* from) { CopyText(to + std::strlen(to), from); }

float Half() { return ::Gfx::g_half; }
float One() { return ::Obj::g_animMoviesOne; }

// The original's _ftol (0x006236FC): truncated towards zero.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// A capital's size in a font (the lines' height): "AAA" (0x0064A260).
const char* SizingText() { return GAME_TEXT(0x0064A260, "AAA"); }
// " " (0x0064A2BC): what joins the lines.
const char* Space() { return GAME_TEXT(0x0064A2BC, " "); }

void* GetFont(uint32_t name) { return Nx::GetFont(name); }
void QueryString(void* font, const char* text, float* width, float* height)
{
    reinterpret_cast<void(__thiscall*)(void*, const char*, float*, float*)>(&Nx::VirtualSlot8Jump)(font, text, width, height);
}

void* FrontEndNew(unsigned int size)
{
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_frontEndHeap);
    void* block = Mem::New(size);
    Mem::Manager::Get()->PopContext();
    return block;
}

void DeleteBlur(CBlurEffect* blur)
{
    if (blur) {
        Stubs::DoNothing(); // (its destructor: the shared empty stub the original calls)
        Mem::Free(blur);
    }
}

CTextElement* AsText(CScreenElement* e) { return static_cast<CTextElement*>(e); }

} // namespace

// 0x004B0810
CTextBlockElement* CTextBlockElement::Construct()
{
    CScreenElement::Construct();
    m_vtable = kCTextBlockElementVtable;
    m_shadowRgba[0] = m_shadowRgba[1] = m_shadowRgba[2] = 0;
    m_shadowRgba[3] = 0x80;
    m_internalScale = 1.0f;
    m_lineSpacing = 1.0f;
    m_fontName = 0;
    m_linesHeight = 0.0f;
    m_numElements = 0;
    mp_blur = nullptr;
    m_allowExpansion = false;
    m_overrideRgba = false;
    m_rememberedOverride = false;
    SetType(kTextBlockElement);
    return this;
}

// 0x004B08B0
void CTextBlockElement::DestroyBody()
{
    m_vtable = kCTextBlockElementVtable;
    DeleteBlur(mp_blur);
    CScreenElement::DestroyBody();
}

// 0x004B0B30
CTextBlockElement* CTextBlockElement::Destroy(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        Mem::Free(this);
    return this;
}

// 0x004AF6C0 (a font with no height: a division by zero, as the original)
void CTextBlockElement::SetFont(uint32_t font)
{
    m_fontName = font;
    float width, height;
    QueryString(GetFont(font), SizingText(), &width, &height);
    int lineHeight = Truncated(static_cast<double>(height) * m_internalScale);
    m_maxLines = Truncated(m_height) / lineHeight;
    m_objectFlags |= kVisibleNow;
}

// 0x004AF720 (a new line from an empty one "trims" the byte before it, as the original)
void CTextBlockElement::AddWords(const char* text)
{
    void* font = GetFont(m_fontName);
    const char* cursor = text;
    int previous = 0;
    int result;
    do {
        char word[0x64];
        result = Str::NextWord(word, cursor);
        float width, height;
        QueryString(font, word, &width, &height);
        width = static_cast<float>(static_cast<double>(width) * m_internalScale);
        bool full = m_lineWidth > 0.0f && static_cast<double>(width) + m_lineWidth > m_width;
        if (full || previous == 1 || std::strlen(word) + static_cast<size_t>(m_lineLength) >= kLineChars) {
            char* last = mp_lines[m_line] + m_lineLength - 1;
            if (*last == ' ')
                *last = 0;
            ++m_numLines;
            if (++m_line >= m_maxLines)
                m_line = 0;
            m_lineLength = 0;
            m_lineWidth = 0.0f;
        }
        CopyText(mp_lines[m_line] + m_lineLength, word);
        m_lineLength += static_cast<int>(std::strlen(word));
        m_lineWidth = static_cast<float>(static_cast<double>(width) + m_lineWidth);
        previous = result;
    } while (result != 2);
}

// 0x004B0CD0
void CTextBlockElement::SetText(const char** texts, int count)
{
    CScreenElementManager* manager = CScreenElementManager::Instance();
    SetFocusSent(0);
    {
        ElementPtr child;
        child.Set(mp_firstChild.mp_object);
        while (child) {
            ElementPtr next;
            next.Set(child->mp_next.mp_object);
            manager->DestroyElement(child->m_id, 1, 0, nullptr);
            child.Set(next.mp_object);
        }
    }
    Mem::Manager* memory = Mem::Manager::Get();
    memory->PushContext(memory->m_frontEndHeap);
    mp_lines = static_cast<char**>(Mem::New(kLineBuffers * sizeof(char*)));
    mp_lines[0] = static_cast<char*>(Mem::New(kLineBuffers * kLineChars));
    Mem::Manager::Get()->PopContext();
    for (int i = 1; i < kLineBuffers; ++i)
        mp_lines[i] = mp_lines[0] + kLineChars * i;
    for (int i = 0; i < kLineBuffers; ++i)
        mp_lines[i][0] = 0;
    m_lineLength = 0;
    m_numLines = 0;
    m_line = 0;
    m_lineWidth = 0.0f;
    if (m_allowExpansion) {
        m_height = 0.0f;
        m_maxLines = 0;
    }
    for (int i = 0; i < count; ++i)
        AddWords(texts[i]);
    if (count != 0) {
        ++m_numLines;
        ++m_line;
    }
    m_numElements = m_line;
    int first = 0;
    if (m_numLines >= m_maxLines) {
        if (m_allowExpansion) {
            // the block grows to the lines' height and they are made again
            float width, height;
            QueryString(GetFont(m_fontName), SizingText(), &width, &height);
            double lineHeight = static_cast<double>(m_lineSpacing) * m_internalScale * height;
            double total = static_cast<double>(m_numLines) * lineHeight;
            m_height = static_cast<float>(total);
            int maxLines = Truncated(total) / Truncated(lineHeight);
            m_lineLength = 0;
            m_numLines = 0;
            m_line = 0;
            m_lineWidth = 0.0f;
            m_maxLines = maxLines;
            for (int i = 0; i < count; ++i)
                AddWords(texts[i]);
            m_numElements = m_maxLines;
        } else {
            // the last lines that fit (the lines wrapped round: the oldest kept first)
            first = m_numLines % m_maxLines;
            m_numElements = m_maxLines;
        }
    }
    m_linesHeight = 0.0f;
    for (int k = 0; k < m_numElements; ++k) {
        memory = Mem::Manager::Get();
        memory->PushContext(memory->m_frontEndHeap);
        void* block = Mem::Calloc(sizeof(CTextElement));
        CTextElement* line = block ? static_cast<CTextElement*>(block)->Construct() : nullptr;
        Mem::Manager::Get()->PopContext();
        manager->VirtualRegisterObject(line);
        {
            ElementPtr child, self;
            child.Set(line);
            self.Set(this);
            manager->SetParent(&self, &child, 1);
        }
        line->m_fontName = m_fontName;
        line->mp_font = GetFont(m_fontName);
        line->m_objectFlags |= kVisibleNow;
        line->SetText(mp_lines[first]);
        line->SetScale(m_internalScale, m_internalScale, false, 0);
        m_linesHeight = static_cast<float>(static_cast<double>(line->m_height) * m_internalScale * m_lineSpacing +
                                           m_linesHeight);
        if (++first >= m_maxLines)
            first = 0;
    }
    Mem::Free(mp_lines[0]);
    Mem::Free(mp_lines);
    SetFocusSent(1);
    m_objectFlags |= kVisibleNow;
}

// 0x004B11C0
bool CTextBlockElement::GetText(char* out, int size)
{
    *out = 0;
    int total = 0;
    ElementPtr child;
    child.Set(mp_firstChild.mp_object);
    while (child) {
        ElementPtr next;
        next.Set(child->mp_next.mp_object);
        const char* text = AsText(child.mp_object)->m_string;
        total += static_cast<int>(std::strlen(text));
        if (next)
            ++total;
        if (total >= size)
            return false;
        AppendText(out, text);
        if (next)
            AppendText(out, Space());
        child.Set(next.mp_object);
    }
    return true;
}

// 0x004B1390
int CTextBlockElement::GetLength()
{
    int total = 0;
    ElementPtr child;
    child.Set(mp_firstChild.mp_object);
    while (child) {
        ElementPtr next;
        next.Set(child->mp_next.mp_object);
        total += static_cast<int>(std::strlen(AsText(child.mp_object)->m_string));
        if (next)
            ++total;
        child.Set(next.mp_object);
    }
    return total;
}

// 0x004B14A0
bool CTextBlockElement::Backspace()
{
    int length = GetLength();
    if (length == 0)
        return false;
    char text[0x200];
    GetText(text, sizeof(text));
    if (length > 1 && text[length - 1] == '\\' && text[length - 2] == '\\')
        text[length - 2] = 0;
    const char* texts[1] = {text};
    text[length - 1] = 0;
    SetText(texts, 1);
    return true;
}

// 0x004B16D0 (the joined text in a block from the current heap)
bool CTextBlockElement::Concatenate(const char* text, bool enforceMaxWidth, bool lastLine)
{
    char* joined = static_cast<char*>(Mem::New(0x200));
    GetText(joined, 0x200);
    AppendText(joined, text);
    if ((enforceMaxWidth && std::strcmp(text, Space()) != 0) || lastLine) {
        // the last word must fit the width
        void* font = GetFont(m_fontName);
        const char* cursor = joined;
        char word[0x64];
        while (Str::NextWord(word, cursor) != 2) {
        }
        float width, height;
        QueryString(font, word, &width, &height);
        width = static_cast<float>(static_cast<double>(width) * m_internalScale);
        if (width > m_width) {
            Mem::Free(joined);
            return false;
        }
    }
    const char* texts[1] = {joined};
    SetText(texts, 1);
    Mem::Free(joined);
    return true;
}

// 0x004B1540
void CTextBlockElement::UpdateLines()
{
    ElementPtr child;
    child.Set(mp_firstChild.mp_object);
    while (child) {
        CTextElement* line = AsText(child.mp_object);
        if (mp_blur)
            line->EnableBlur();
        else
            line->DisableBlur();
        if (m_shadow) {
            if (!line->m_shadow) {
                line->m_shadow = true;
                line->MakeTextObjects(line->m_count, true);
            }
        } else if (line->m_shadow) {
            line->m_shadow = false;
            line->FreeTextObjects(true);
        }
        line->m_shadowX = m_shadowX;
        line->m_shadowY = m_shadowY;
        std::memcpy(line->m_shadowRgba, m_shadowRgba, 4);
        line->m_overrideRgba = m_overrideRgba;
        child.Set(child->mp_next.mp_object);
    }
}

// 0x004B1B60 (an array of more than 29 strings overran the original's stack)
void CTextBlockElement::SetProperties(CStruct* params)
{
    CScreenElement::SetProperties(params);
    if (params->GetFloat(kInternalScale, &m_internalScale, 0))
        m_objectFlags |= kVisibleNow;
    params->GetFloat(kLineSpacing, &m_lineSpacing, 0);
    uint32_t font;
    if (params->GetChecksum(kFont, &font, 0))
        SetFont(font);
    if (ResolveJust(params, kInternalJust, &m_internalJustX, &m_internalJustY))
        m_objectFlags |= kVisibleNow;
    if (params->ContainsFlag(kAllowExpansion))
        m_allowExpansion = true;
    const char* texts[kMaxTexts];
    Script::CArray* array;
    if (params->GetString(kText, &texts[0], 0)) {
        SetText(texts, 1);
    } else if (params->GetArray(kText, &array, 0)) {
        int count = static_cast<int>(array->m_size);
        for (int i = 0; i < count && i < kMaxTexts; ++i)
            texts[i] = array->GetString(static_cast<uint32_t>(i));
        SetText(texts, count < kMaxTexts ? count : kMaxTexts);
    }
    if (params->ContainsFlag(kBlurEffect) && !mp_blur) {
        Mem::Manager* memory = Mem::Manager::Get();
        memory->PushContext(memory->m_frontEndHeap);
        mp_blur = static_cast<CBlurEffect*>(Mem::New(sizeof(CBlurEffect)));
        std::memcpy(&mp_blur->m_rgba, m_shown.rgba, 4);
        Mem::Manager::Get()->PopContext();
    } else if (params->ContainsFlag(kNoBlurEffect) && mp_blur) {
        DeleteBlur(mp_blur);
        mp_blur = nullptr;
    }
    uint32_t rgba;
    if (ResolveRGBA(params, kBlurRgba, &rgba) && mp_blur)
        mp_blur->m_rgba = rgba;
    Script::CPair offset;
    offset.Init();
    if (params->GetPair(kShadowOffs, &offset, false)) {
        m_shadowX = offset.x;
        m_shadowY = offset.y;
    }
    if (params->ContainsFlag(kShadow))
        m_shadow = true;
    if (params->ContainsFlag(kNoShadow))
        m_shadow = false;
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
    UpdateLines();
}

// 0x004B0B60
void CTextBlockElement::SetMorph(CStruct* params)
{
    if (mp_blur) {
        reinterpret_cast<void(__thiscall*)(CTextBlockElement*)>(m_vtable[10])(this);
        int ms;
        CBlurEffect::Settings* target = mp_blur->SetFromParams(params, &ms);
        ElementPtr child;
        child.Set(mp_firstChild.mp_object);
        while (child) {
            // each line's blur displaced towards the block's middle by how far its middle is from it
            CBlurEffect::Settings settings = *target;
            float x, y;
            child->GetPos(&x, &y);
            y = static_cast<float>(static_cast<double>(child->m_height) * Half() + y);
            double offset = static_cast<double>(m_height) * Half() - y;
            settings.maxDisplacement[1] = static_cast<float>(offset * target->bottomScales[1] - offset +
                                                             settings.maxDisplacement[1]);
            AsText(child.mp_object)->mp_blur->SetTarget(&settings, ms);
            child.Set(child->mp_next.mp_object);
        }
    }
    CScreenElement::SetMorph(params);
}

// 0x004B1EC0 (as many lines as were made: the original doesn't check for fewer children)
void CTextBlockElement::Update()
{
    if (!(m_objectFlags & kVisibleNow))
        return;
    float y = static_cast<float>((static_cast<double>(m_height) - m_linesHeight) * (static_cast<double>(m_internalJustY) + One()) *
                                 Half());
    ElementPtr child;
    child.Set(mp_firstChild.mp_object);
    for (int i = 0; i < m_numElements; ++i) {
        child->SetJust(m_internalJustX, -1.0f);
        uint32_t rgba;
        std::memcpy(&rgba, m_shown.rgba, 4);
        child->SetRGBA(rgba, 1);
        float x = static_cast<float>((static_cast<double>(m_internalJustX) + One()) * m_width * Half());
        child->SetPos(x, y, 0);
        y = static_cast<float>(static_cast<double>(child->m_height) * m_internalScale * m_lineSpacing + y);
        child.Set(child->mp_next.mp_object);
    }
    UpdateLines();
}

namespace {
GAME_VTABLE(kCTextBlockElementVtable, 0x0064A264, &CTextBlockElement::Destroy, &CTextBlockElement::SetProperties,
            static_cast<bool (Obj::CObject::*)(Script::CMemberFunctionEntry*, Script::CStruct*, Script::CScript*)>(&Obj::CObject::CallMemberFunction),
            &CScreenElement::CallMemberFunction, Stubs::IgnoreArgument, &Obj::CObject::PassEventToScript,
            &Obj::CObject::MarkAsDead, Stubs::ReturnTrue, &CScreenElement::GetProperties,
            &CTextBlockElement::SetMorph, &CTextBlockElement::Update);
} // namespace

} // namespace Front

using Front::CTextBlockElement;

REPLACE(0x004B0810, &CTextBlockElement::Construct);
REPLACE(0x004B08B0, &CTextBlockElement::DestroyBody);
REPLACE(0x004B0B30, &CTextBlockElement::Destroy);
REPLACE(0x004B1B60, &CTextBlockElement::SetProperties);
REPLACE(0x004B0B60, &CTextBlockElement::SetMorph);
REPLACE(0x004B1EC0, &CTextBlockElement::Update);
REPLACE(0x004AF6C0, &CTextBlockElement::SetFont);
REPLACE(0x004AF720, &CTextBlockElement::AddWords);
REPLACE(0x004B0CD0, &CTextBlockElement::SetText);
REPLACE(0x004B11C0, &CTextBlockElement::GetText);
REPLACE(0x004B1390, &CTextBlockElement::GetLength);
REPLACE(0x004B14A0, &CTextBlockElement::Backspace);
REPLACE(0x004B16D0, &CTextBlockElement::Concatenate);
REPLACE(0x004B1540, &CTextBlockElement::UpdateLines);
