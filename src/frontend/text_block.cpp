// The text block element: see text_block.h.

#include "frontend/text_block.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/str.h"
#include "engine/core/stubs.h"
#include "frontend/screen_element_manager.h"
#include "frontend/text_element.h"
#include "engine/gfx/font.h"
#include "engine/script/param_struct.h"
#include "engine/core/code_address.h"
#include "engine/objects/entity.h"
#include "frontend/screen_element.h"
#include "frontend/screen_constants.h"
#include "engine/gfx/engine_geom.h"

namespace MenuSystem {

namespace {
extern void** const kCTextBlockElementVtable;
} // namespace

namespace {

using Scripts::CStruct;

// Names (checksums).
constexpr uint32_t kTextBlockElement = 0x40D92263, kFont = 0x2F6BF72D, kInnerAlign = 0x67E093E4;
constexpr uint32_t kInternalScale = 0x1FE341D2, kLineSpacing = 0xE3FA22FC, kMayGrow = 0x322839A2;
constexpr uint32_t kText = 0xC4745838, kBlurEffect = 0xD1653C6C, kNoBlurEffect = 0x4B73E4F8;
constexpr uint32_t kHaloColor = 0x4F0E1182, kShadowOffs = 0x2A1FE0CC, kShadow = 0x8A897DD2;
constexpr uint32_t kShadowOffFlag = 0x95B5B9A0, kShadowRgba = 0x1E7BB1F5;
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

float Half() { return ::Gfx2D::g_half; }
float One() { return ::GameObjects::g_animMoviesOne; }

// The original's _ftol (0x006236FC): truncated towards zero.
int Truncated(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

// A capital's size in a font (the lines' height): "AAA" (0x0064A260).
const char* SizingText() { return "AAA"; }
// " " (0x0064A2BC): what joins the lines.
const char* Space() { return " "; }

void* GetFont(uint32_t name) { return Engine3D::GetFont(name); }
void ReadStringParam(void* font, const char* text, float* width, float* height)
{
    reinterpret_cast<void(__thiscall*)(void*, const char*, float*, float*)>(&Engine3D::VirtualSlot8Jump)(font, text, width, height);
}

void* FrontEndNew(unsigned int size)
{
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_frontEndHeap);
    void* block = MemSys::Alloc(size);
    MemSys::Hub::Pull()->LeaveHeapScope();
    return block;
}

void DeleteBlur(CSmearEffect* blur)
{
    if (blur) {
        Stubs::DoNothing(); // (its destructor: the shared empty stub the original calls)
        MemSys::Dealloc(blur);
    }
}

CLabelWidget* AsText(CUiWidget* e) { return static_cast<CLabelWidget*>(e); }

} // namespace

// 0x004B0810
CParagraphWidget* CParagraphWidget::MakeNew()
{
    CUiWidget::MakeNew();
    m_vtable = kCTextBlockElementVtable;
    m_shadowRgba[0] = m_shadowRgba[1] = m_shadowRgba[2] = 0;
    m_shadowRgba[3] = 0x80;
    m_internalScale = 1.0f;
    m_lineSpacing = 1.0f;
    m_fontName = 0;
    m_linesHeight = 0.0f;
    m_rowsFilled = 0;
    m_blur = nullptr;
    m_mayGrow = false;
    m_overrideRgba = false;
    m_rememberedOverride = false;
    ChangeSpecies(kTextBlockElement);
    return this;
}

// 0x004B08B0
void CParagraphWidget::DestroyBody()
{
    m_vtable = kCTextBlockElementVtable;
    DeleteBlur(m_blur);
    CUiWidget::DestroyBody();
}

// 0x004B0B30
CParagraphWidget* CParagraphWidget::Teardown(uint8_t flags)
{
    DestroyBody();
    if (flags & 1)
        MemSys::Dealloc(this);
    return this;
}

// 0x004AF6C0 (a font with no height: a division by zero, as the original)
void CParagraphWidget::ChangeTypeface(uint32_t font)
{
    m_fontName = font;
    float width, height;
    ReadStringParam(GetFont(font), SizingText(), &width, &height);
    int lineHeight = Truncated(static_cast<double>(height) * m_internalScale);
    m_lineCapacity = Truncated(m_levelY) / lineHeight;
    m_objectFlags |= kVisibleNow;
}

// 0x004AF720 (a new line from an empty one "trims" the byte before it, as the original)
void CParagraphWidget::AddWords(const char* text)
{
    void* font = GetFont(m_fontName);
    const char* cursor = text;
    int previous = 0;
    int result;
    do {
        char word[0x64];
        result = TextUtil::NextWord(word, cursor);
        float width, height;
        ReadStringParam(font, word, &width, &height);
        width = static_cast<float>(static_cast<double>(width) * m_internalScale);
        bool full = m_lineWidth > 0.0f && static_cast<double>(width) + m_lineWidth > m_span;
        if (full || previous == 1 || std::strlen(word) + static_cast<size_t>(m_lineLength) >= kLineChars) {
            char* last = m_lines[m_line] + m_lineLength - 1;
            if (*last == ' ')
                *last = 0;
            ++m_numLines;
            if (++m_line >= m_lineCapacity)
                m_line = 0;
            m_lineLength = 0;
            m_lineWidth = 0.0f;
        }
        CopyText(m_lines[m_line] + m_lineLength, word);
        m_lineLength += static_cast<int>(std::strlen(word));
        m_lineWidth = static_cast<float>(static_cast<double>(width) + m_lineWidth);
        previous = result;
    } while (result != 2);
}

// 0x004B0CD0
void CParagraphWidget::ChangeCaption(const char** texts, int count)
{
    CUiTree* manager = CUiTree::Sole();
    SetFocusSent(0);
    {
        ElementPtr child;
        child.AssignValue(m_eldest.m_ownerObject);
        while (child) {
            ElementPtr next;
            next.AssignValue(child->m_following.m_ownerObject);
            manager->DismantlePart(child->m_ident, 1, 0, nullptr);
            child.AssignValue(next.m_ownerObject);
        }
    }
    MemSys::Hub* memory = MemSys::Hub::Pull();
    memory->EnterHeapScope(memory->m_frontEndHeap);
    m_lines = static_cast<char**>(MemSys::Alloc(kLineBuffers * sizeof(char*)));
    m_lines[0] = static_cast<char*>(MemSys::Alloc(kLineBuffers * kLineChars));
    MemSys::Hub::Pull()->LeaveHeapScope();
    for (int i = 1; i < kLineBuffers; ++i)
        m_lines[i] = m_lines[0] + kLineChars * i;
    for (int i = 0; i < kLineBuffers; ++i)
        m_lines[i][0] = 0;
    m_lineLength = 0;
    m_numLines = 0;
    m_line = 0;
    m_lineWidth = 0.0f;
    if (m_mayGrow) {
        m_levelY = 0.0f;
        m_lineCapacity = 0;
    }
    for (int i = 0; i < count; ++i)
        AddWords(texts[i]);
    if (count != 0) {
        ++m_numLines;
        ++m_line;
    }
    m_rowsFilled = m_line;
    int first = 0;
    if (m_numLines >= m_lineCapacity) {
        if (m_mayGrow) {
            // the block grows to the lines' height and they are made again
            float width, height;
            ReadStringParam(GetFont(m_fontName), SizingText(), &width, &height);
            double lineHeight = static_cast<double>(m_lineSpacing) * m_internalScale * height;
            double total = static_cast<double>(m_numLines) * lineHeight;
            m_levelY = static_cast<float>(total);
            int maxLines = Truncated(total) / Truncated(lineHeight);
            m_lineLength = 0;
            m_numLines = 0;
            m_line = 0;
            m_lineWidth = 0.0f;
            m_lineCapacity = maxLines;
            for (int i = 0; i < count; ++i)
                AddWords(texts[i]);
            m_rowsFilled = m_lineCapacity;
        } else {
            // the last lines that fit (the lines wrapped round: the oldest kept first)
            first = m_numLines % m_lineCapacity;
            m_rowsFilled = m_lineCapacity;
        }
    }
    m_linesHeight = 0.0f;
    for (int k = 0; k < m_rowsFilled; ++k) {
        memory = MemSys::Hub::Pull();
        memory->EnterHeapScope(memory->m_frontEndHeap);
        void* block = MemSys::AllocZeroed(sizeof(CLabelWidget));
        CLabelWidget* line = block ? static_cast<CLabelWidget*>(block)->MakeNew() : nullptr;
        MemSys::Hub::Pull()->LeaveHeapScope();
        manager->VirtualRegisterObject(line);
        {
            ElementPtr child, self;
            child.AssignValue(line);
            self.AssignValue(this);
            manager->ChangeOwner(&self, &child, 1);
        }
        line->m_fontName = m_fontName;
        line->m_typeface = GetFont(m_fontName);
        line->m_objectFlags |= kVisibleNow;
        line->ChangeCaption(m_lines[first]);
        line->ChangeScale(m_internalScale, m_internalScale, false, 0);
        m_linesHeight = static_cast<float>(static_cast<double>(line->m_levelY) * m_internalScale * m_lineSpacing +
                                           m_linesHeight);
        if (++first >= m_lineCapacity)
            first = 0;
    }
    MemSys::Dealloc(m_lines[0]);
    MemSys::Dealloc(m_lines);
    SetFocusSent(1);
    m_objectFlags |= kVisibleNow;
}

// 0x004B11C0
bool CParagraphWidget::FetchCaption(char* out, int size)
{
    *out = 0;
    int total = 0;
    ElementPtr child;
    child.AssignValue(m_eldest.m_ownerObject);
    while (child) {
        ElementPtr next;
        next.AssignValue(child->m_following.m_ownerObject);
        const char* text = AsText(child.m_ownerObject)->m_chars;
        total += static_cast<int>(std::strlen(text));
        if (next)
            ++total;
        if (total >= size)
            return false;
        AppendText(out, text);
        if (next)
            AppendText(out, Space());
        child.AssignValue(next.m_ownerObject);
    }
    return true;
}

// 0x004B1390
int CParagraphWidget::FetchMagnitude()
{
    int total = 0;
    ElementPtr child;
    child.AssignValue(m_eldest.m_ownerObject);
    while (child) {
        ElementPtr next;
        next.AssignValue(child->m_following.m_ownerObject);
        total += static_cast<int>(std::strlen(AsText(child.m_ownerObject)->m_chars));
        if (next)
            ++total;
        child.AssignValue(next.m_ownerObject);
    }
    return total;
}

// 0x004B14A0
bool CParagraphWidget::Backspace()
{
    int length = FetchMagnitude();
    if (length == 0)
        return false;
    char text[0x200];
    FetchCaption(text, sizeof(text));
    if (length > 1 && text[length - 1] == '\\' && text[length - 2] == '\\')
        text[length - 2] = 0;
    const char* texts[1] = {text};
    text[length - 1] = 0;
    ChangeCaption(texts, 1);
    return true;
}

// 0x004B16D0 (the joined text in a block from the current heap)
bool CParagraphWidget::JoinOnto(const char* text, bool enforceMaxWidth, bool lastLine)
{
    char* joined = static_cast<char*>(MemSys::Alloc(0x200));
    FetchCaption(joined, 0x200);
    AppendText(joined, text);
    if ((enforceMaxWidth && std::strcmp(text, Space()) != 0) || lastLine) {
        // the last word must fit the width
        void* font = GetFont(m_fontName);
        const char* cursor = joined;
        char word[0x64];
        while (TextUtil::NextWord(word, cursor) != 2) {
        }
        float width, height;
        ReadStringParam(font, word, &width, &height);
        width = static_cast<float>(static_cast<double>(width) * m_internalScale);
        if (width > m_span) {
            MemSys::Dealloc(joined);
            return false;
        }
    }
    const char* texts[1] = {joined};
    ChangeCaption(texts, 1);
    MemSys::Dealloc(joined);
    return true;
}

// 0x004B1540
void CParagraphWidget::UpdateLines()
{
    ElementPtr child;
    child.AssignValue(m_eldest.m_ownerObject);
    while (child) {
        CLabelWidget* line = AsText(child.m_ownerObject);
        if (m_blur)
            line->EnableBlur();
        else
            line->DisableBlur();
        if (m_shadow) {
            if (!line->m_shadow) {
                line->m_shadow = true;
                line->MakeTextObjects(line->m_num, true);
            }
        } else if (line->m_shadow) {
            line->m_shadow = false;
            line->FreeTextObjects(true);
        }
        line->m_shadowX = m_shadowX;
        line->m_shadowY = m_shadowY;
        std::memcpy(line->m_shadowRgba, m_shadowRgba, 4);
        line->m_overrideRgba = m_overrideRgba;
        child.AssignValue(child->m_following.m_ownerObject);
    }
}

// 0x004B1B60 (an array of more than 29 strings overran the original's stack)
void CParagraphWidget::ChangeTraits(CStruct* params)
{
    CUiWidget::ChangeTraits(params);
    if (params->FetchFloat(kInternalScale, &m_internalScale, 0))
        m_objectFlags |= kVisibleNow;
    params->FetchFloat(kLineSpacing, &m_lineSpacing, 0);
    uint32_t font;
    if (params->ReadHashValue(kFont, &font, 0))
        ChangeTypeface(font);
    if (ReadAlignmentParam(params, kInnerAlign, &m_innerAlignH, &m_innerAlignV))
        m_objectFlags |= kVisibleNow;
    if (params->HasBareFlag(kMayGrow))
        m_mayGrow = true;
    const char* texts[kMaxTexts];
    Scripts::CArray* array;
    if (params->FetchText(kText, &texts[0], 0)) {
        ChangeCaption(texts, 1);
    } else if (params->FetchList(kText, &array, 0)) {
        int count = static_cast<int>(array->m_extent);
        for (int i = 0; i < count && i < kMaxTexts; ++i)
            texts[i] = array->FetchText(static_cast<uint32_t>(i));
        ChangeCaption(texts, count < kMaxTexts ? count : kMaxTexts);
    }
    if (params->HasBareFlag(kBlurEffect) && !m_blur) {
        MemSys::Hub* memory = MemSys::Hub::Pull();
        memory->EnterHeapScope(memory->m_frontEndHeap);
        m_blur = static_cast<CSmearEffect*>(MemSys::Alloc(sizeof(CSmearEffect)));
        std::memcpy(&m_blur->m_rgbaValue, m_shown.rgba, 4);
        MemSys::Hub::Pull()->LeaveHeapScope();
    } else if (params->HasBareFlag(kNoBlurEffect) && m_blur) {
        DeleteBlur(m_blur);
        m_blur = nullptr;
    }
    uint32_t rgba;
    if (ReadColourParam(params, kHaloColor, &rgba) && m_blur)
        m_blur->m_rgbaValue = rgba;
    Scripts::CPair offset;
    offset.PrepareAll();
    if (params->FetchPair(kShadowOffs, &offset, false)) {
        m_shadowX = offset.x;
        m_shadowY = offset.y;
    }
    if (params->HasBareFlag(kShadow))
        m_shadow = true;
    if (params->HasBareFlag(kShadowOffFlag))
        m_shadow = false;
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
    UpdateLines();
}

// 0x004B0B60
void CParagraphWidget::BlendTowards(CStruct* params)
{
    if (m_blur) {
        reinterpret_cast<void(__thiscall*)(CParagraphWidget*)>(m_vtable[10])(this);
        int ms;
        CSmearEffect::Settings* target = m_blur->SetFromParams(params, &ms);
        ElementPtr child;
        child.AssignValue(m_eldest.m_ownerObject);
        while (child) {
            // each line's blur displaced towards the block's middle by how far its middle is from it
            CSmearEffect::Settings settings = *target;
            float x, y;
            child->FetchLocation(&x, &y);
            y = static_cast<float>(static_cast<double>(child->m_levelY) * Half() + y);
            double offset = static_cast<double>(m_levelY) * Half() - y;
            settings.maxDisplacement[1] = static_cast<float>(offset * target->bottomScales[1] - offset +
                                                             settings.maxDisplacement[1]);
            AsText(child.m_ownerObject)->m_blur->SetTarget(&settings, ms);
            child.AssignValue(child->m_following.m_ownerObject);
        }
    }
    CUiWidget::BlendTowards(params);
}

// 0x004B1EC0 (as many lines as were made: the original doesn't check for fewer children)
void CParagraphWidget::TickFrame()
{
    if (!(m_objectFlags & kVisibleNow))
        return;
    float y = static_cast<float>((static_cast<double>(m_levelY) - m_linesHeight) * (static_cast<double>(m_innerAlignV) + One()) *
                                 Half());
    ElementPtr child;
    child.AssignValue(m_eldest.m_ownerObject);
    for (int i = 0; i < m_rowsFilled; ++i) {
        child->ChangeAlignment(m_innerAlignH, -1.0f);
        uint32_t rgba;
        std::memcpy(&rgba, m_shown.rgba, 4);
        child->ChangeRgba(rgba, 1);
        float x = static_cast<float>((static_cast<double>(m_innerAlignH) + One()) * m_span * Half());
        child->ChangeLocation(x, y, 0);
        y = static_cast<float>(static_cast<double>(child->m_levelY) * m_internalScale * m_lineSpacing + y);
        child.AssignValue(child->m_following.m_ownerObject);
    }
    UpdateLines();
}

namespace {
static VtableSlot kCTextBlockElementVtable_slots[] = {&CParagraphWidget::Teardown, &CParagraphWidget::ChangeTraits,
                static_cast<bool (GameObjects::CEntity::*)(Scripts::CMemberFunctionEntry*, Scripts::CStruct*, Scripts::CScript*)>(&GameObjects::CEntity::InvokeMethodByName),
                &CUiWidget::InvokeMethodByName, Stubs::IgnoreArgument, &GameObjects::CEntity::PassEventToScript,
                &GameObjects::CEntity::FlagAsDead, Stubs::ReturnTrue, &CUiWidget::GetProperties,
                &CParagraphWidget::BlendTowards, &CParagraphWidget::TickFrame};
void** const kCTextBlockElementVtable = reinterpret_cast<void**>(kCTextBlockElementVtable_slots);
} // namespace

} // namespace Front

using MenuSystem::CParagraphWidget;
