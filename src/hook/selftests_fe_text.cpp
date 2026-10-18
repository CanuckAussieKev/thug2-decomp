// Self-tests for the text element and its blur effect (src/game/front/text_element.cpp), on the shared element
// world (fe_element_fixture.h): CScreenElement's slots 1 / 9, the justification and colour resolvers, the type,
// the memory manager (a bump allocator), the text objects (made, freed, every setter), fonts and button glyphs
// are recording stand-ins.

#include "game/front/screen_element_manager.h"
#include "game/front/text_block.h"
#include "game/front/text_element.h"
#include "hook/fe_element_fixture.h"
#include "hook/selftest.h"

namespace {

using namespace FeTest;
using Front::CBlurEffect;
using Front::CTextBlockElement;
using Front::CTextElement;

constexpr uint32_t kWindow = 0x4380;   // the window element's 2D window
constexpr uint32_t kFont = 0x43A0;
constexpr uint32_t kStrings = 0x4400;  // test strings
constexpr uint32_t kTextObjs = 0x4800; // text objects handed out (0x10 apart)
constexpr uint32_t kHeap = 0x5000;     // the bump allocator's memory
constexpr uint32_t kHeapEnd = 0x9000;
constexpr uint32_t kWorldEnd = 0x9000;

uint32_t g_heapNext;
uint32_t g_textNext;
float g_glyphWidth;

void InstallTextStandIns(StandIns& s)
{
    InstallElementStandIns(s);
    s.At({0x00461970, AddressOf(&Obj::CObject::SetType)}, 4, [](Regs& r) {
        Rec(0x1970, {r.ecx, r.args[0]});
        *reinterpret_cast<uint32_t*>(r.ecx + 0x18) = r.args[0];
        return 0u;
    });
    s.At({0x004A96B0, AddressOf(&CScreenElement::SetProperties)}, 4, [](Regs& r) {
        Rec(0x96B0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x004A9AA0, AddressOf(&CScreenElement::SetMorph)}, 4, [](Regs& r) {
        Rec(0x9AA0, {r.ecx, r.args[0]});
        return 0u;
    });
    // the resolvers: 'J' answers two floats, 'R' a colour
    s.At({0x004A8DC0, AddressOf(&CScreenElement::ResolveJust)}, 16, [](Regs& r) {
        Rec(0x8DC0, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3]});
        const std::vector<uint32_t>* value = Lookup('J', r.args[0], r.args[1]);
        if (!value)
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[2]) = (*value)[0];
        *reinterpret_cast<uint32_t*>(r.args[3]) = (*value)[1];
        return 1u;
    });
    s.At({0x004A8E90, AddressOf(&CScreenElement::ResolveRGBA)}, 12, [](Regs& r) {
        Rec(0x8E90, {r.ecx, r.args[0], r.args[1], r.args[2]});
        const std::vector<uint32_t>* value = Lookup('R', r.args[0], r.args[1]);
        if (!value)
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[2]) = (*value)[0];
        return 1u;
    });
    // the string getter with a bool assert argument
    s.At({0x00476C60}, 12, [](Regs& r) {
        Rec(0x6E57, {r.ecx, r.args[0], r.args[2] & 0xFF});
        const std::vector<uint32_t>* value = Lookup('T', r.ecx, r.args[0]);
        if (!value)
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[1]) = (*value)[0];
        return 1u;
    });
    // memory: the heap stack and a bump allocator
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Rec(0xB0E0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Rec(0xB110, {r.ecx});
        return 0u;
    });
    s.At({0x005BAFC0, AddressOf(&Mem::Manager::Allocate)}, 12, [](Regs& r) {
        Rec(0xAFC0, {r.ecx, r.args[0], r.args[1], r.args[2]});
        uint32_t block = g_heapNext;
        g_heapNext += (r.args[0] + 15) & ~15u;
        if (g_heapNext > kHeapEnd)
            g_heapNext = kHeap;
        std::memset(Ptr<uint8_t>(block), 0xC5, r.args[0]);
        return At(block);
    });
    s.At({0x005480A0}, 0, [](Regs& r) {
        Rec(0x80A0, {r.ecx});
        return 0u;
    });
    // text objects
    s.At({0x0048D1D0}, 0, [](Regs&) {
        uint32_t t = g_textNext;
        g_textNext += 0x10;
        Rec(0xD1D0, {At(t)});
        return At(t);
    });
    s.At({0x0048D5F0}, 0, [](Regs& r) {
        Rec(0xD5F0, {r.args[0]});
        return 0u;
    });
    struct Setter {
        uint32_t address;
        uint16_t pop;
        uint32_t masks[3];
    };
    static const Setter setters[] = {
        {0x0048D0C0, 4, {0xFF, 0, 0}},     {0x0048D0D0, 4, {~0u, 0, 0}},      {0x0048D0E0, 8, {~0u, ~0u, 0}},
        {0x0048D100, 8, {~0u, ~0u, 0}},    {0x0048D120, 12, {~0u, ~0u, ~0u}}, {0x0048D140, 4, {~0u, 0, 0}},
        {0x0048D160, 4, {~0u, 0, 0}},      {0x0048D180, 8, {~0u, 0xFF, 0}},   {0x0048D1A0, 4, {0xFF, 0, 0}},
        {0x0048D1B0, 4, {~0u, 0, 0}},      {0x0048D450, 8, {~0u, ~0u, 0}},    {0x0048D280, 12, {~0u, ~0u, ~0u}},
        {0x0048D360, 12, {~0u, ~0u, ~0u}},
    };
    for (const Setter& setter : setters) {
        uint32_t tag = setter.address & 0xFFFF;
        uint32_t m0 = setter.masks[0], m1 = setter.masks[1], m2 = setter.masks[2];
        s.At({setter.address}, setter.pop, [tag, m0, m1, m2](Regs& r) {
            Rec(tag, {r.ecx, r.args[0] & m0, r.args[1] & m1, r.args[2] & m2});
            return 0u;
        });
    }
    s.At({0x0048D530}, 4, [](Regs& r) {
        Rec(0xD530, {r.ecx, Hash(reinterpret_cast<const char*>(r.args[0]))});
        return 0u;
    });
    // fonts: a string's width from its length
    s.At({0x0048D760}, 0, [](Regs& r) {
        Rec(0xD760, {r.args[0]});
        return At(kFont);
    });
    s.At({0x0048D060}, 12, [](Regs& r) {
        const char* text = reinterpret_cast<const char*>(r.args[0]);
        Rec(0xD060, {r.ecx, Hash(text)});
        *reinterpret_cast<float*>(r.args[1]) = static_cast<float>(std::strlen(text)) * g_glyphWidth;
        *reinterpret_cast<float*>(r.args[2]) = 13.5f;
        return 0u;
    });
    s.At({0x0048D6A0}, 0, [](Regs& r) {
        const char* text = reinterpret_cast<const char*>(r.args[0]);
        Rec(0xD6A0, {static_cast<uint32_t>(text[0])});
        return static_cast<uint32_t>(0x41 + (text[0] & 7));
    });
}

CTextElement* T(int i) { return reinterpret_cast<CTextElement*>(E(i)); }
const char* Str(int n, const char* text)
{
    char* p = Ptr<char>(kStrings + 0x80 * n);
    CopyText(p, text);
    return p;
}
void MarkAll(uint32_t n) { Snapshot(0, kWorldEnd, n); }

// Element 2 (in window 0) a text element with one text object; element 12 a fresh one.
void TextWorld(bool original)
{
    BeginWorld(original);
    g_heapNext = kHeap;
    g_textNext = kTextObjs;
    g_glyphWidth = 9.5f;
    *Ptr<uint32_t>(ElemAt(0) + 0x178) = At(kWindow);
    CTextElement* t = T(2);
    std::memset(reinterpret_cast<uint8_t*>(t) + 0x178, 0, 0x38);
    t->mp_texts = Ptr<void*>(kHeapEnd - 0x40);
    t->mp_texts[0] = Ptr<void>(kTextObjs + 0x700);
    t->m_count = 1;
    t->m_fontName = 0xF0F0;
    t->mp_font = Ptr<void>(kFont);
    t->m_shadowRgba[3] = 0x80;
    t->m_shadowRgba[0] = 0x21;
    t->m_shadowX = 2.5f;
    t->m_shadowY = -1.75f;
}

void Blur(CBlurEffect* b)
{
    std::memset(b, 0, sizeof(*b));
    const float target[6] = {1.0f, 0.25f, 1.5f, 0.75f, 12.0f, -6.0f};
    const float current[6] = {0.5f, 0.5f, 1.0f, 1.0f, 0.0f, 0.0f};
    std::memcpy(&b->m_target, target, sizeof(target));
    std::memcpy(&b->m_current, current, sizeof(current));
    b->m_lastTime = 49000;
    b->m_remaining = 2000;
    b->m_rgba = 0xC0405060;
}

void TestTextElements()
{
    SaveElementGlobals();
    {
        StandIns s;
        InstallTextStandIns(s);

        RunBoth("text elements: construction, text, destruction", [&](bool original) {
            TextWorld(original);
            std::memset(E(12), 0, 0x200); // (calloc'd memory: the constructor leaves the shadow list alone)
            Rec(0x400, {reinterpret_cast<uint32_t>(Call(original, 0x004B0920, T(12), &CTextElement::Construct))});
            MarkAll(1);
            CTextElement* t = T(2);
            // the text: escapes, a longer one (a new string), a shorter one (kept), no font, locked dims
            const char* texts[] = {"plain", "a\\\\b\\_c\\mXd\\qe", "a much longer line of text", "short", "x\\"};
            for (int k = 0; k < 5; ++k) {
                if (k == 3)
                    t->mp_font = nullptr;
                if (k == 4) {
                    t->mp_font = Ptr<void>(kFont);
                    t->m_objectFlags |= CScreenElement::kDimsLocked;
                }
                Call(original, 0x004AF8C0, t, &CTextElement::SetText, Str(k, texts[k]));
                Rec(0x401, {static_cast<uint32_t>(CallC(original, 0x004AF650, t, &CTextElement::GetLength))});
                MarkAll(2u + k);
            }
            t->m_objectFlags = 0;
            // adding and taking off: fits, too long, nothing; an escaped backslash, the last character, empty
            const char* adds[] = {"!?", "0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890", ""};
            for (int k = 0; k < 3; ++k)
                Rec(0x402, {static_cast<uint32_t>(Call(original, 0x004AFD00, t, &CTextElement::Concatenate, Str(5 + k, adds[k]))) & 0xFF});
            Call(original, 0x004AF8C0, t, &CTextElement::SetText, Str(8, "ab\\\\\\\\"));
            for (int k = 0; k < 5; ++k)
                Rec(0x403, {static_cast<uint32_t>(Call(original, 0x004AFDD0, t, &CTextElement::Backspace)) & 0xFF});
            Call(original, 0x004AF8C0, t, &CTextElement::SetText, Str(11, "xy\\"));
            Rec(0x403, {static_cast<uint32_t>(Call(original, 0x004AFDD0, t, &CTextElement::Backspace)) & 0xFF});
            MarkAll(8);
            char* keep = t->m_string;
            t->m_string = nullptr;
            Rec(0x404, {static_cast<uint32_t>(Call(original, 0x004AFDD0, t, &CTextElement::Backspace)) & 0xFF,
                        static_cast<uint32_t>(Call(original, 0x004AFD00, t, &CTextElement::Concatenate, Str(9, "new"))) & 0xFF});
            t->m_string = keep;
            MarkAll(9);
            // text objects: with a shadow, the shadow alone, freed; the blur on and off (twice each)
            t->m_shadow = true;
            Call(original, 0x004B0620, t, &CTextElement::MakeTextObjects, 2, true);
            Call(original, 0x004AF670, t, &CTextElement::SetViewports);
            MarkAll(10);
            Call(original, 0x004B0760, t, &CTextElement::FreeTextObjects, true);
            Call(original, 0x004B0620, t, &CTextElement::MakeTextObjects, 3, false);
            MarkAll(11);
            for (int k = 0; k < 2; ++k)
                Call(original, 0x004B0A20, t, &CTextElement::EnableBlur);
            MarkAll(12);
            for (int k = 0; k < 2; ++k)
                Call(original, 0x004B0AE0, t, &CTextElement::DisableBlur);
            MarkAll(13);
            // no window above: no viewports
            Call(original, 0x004AF670, T(12), &CTextElement::SetViewports);
            // the destructors: with the blur, without; and the deleting one
            Call(original, 0x004B0A20, t, &CTextElement::EnableBlur);
            Call(original, 0x004B1830, t, &CTextElement::DestroyBody);
            MarkAll(14);
            for (uint8_t flags : {uint8_t(0), uint8_t(1)}) {
                T(12)->m_string = flags ? nullptr : Ptr<char>(kStrings + 0x80 * 10);
                Rec(0x405, {reinterpret_cast<uint32_t>(Call(original, 0x004B2030, T(12), &CTextElement::Destroy, flags))});
            }
            MarkAll(15);
        });

        RunBoth("text elements: properties, rotation, morphs", [&](bool original) {
            TextWorld(original);
            CTextElement* t = T(2);
            Call(original, 0x004AF8C0, t, &CTextElement::SetText, Str(0, "rotate me"));
            // rotation about each internal_just, at once or not, unchanged
            const float justs[] = {-1.0f, 0.0f, 1.0f, NAN};
            uint32_t mark = 20;
            for (int k = 0; k < 4; ++k) {
                t->m_internalJustX = justs[k];
                t->m_internalJustY = justs[3 - k];
                t->m_shadow = k == 2;
                t->mp_shadows = Ptr<void*>(kHeapEnd - 0x40);
                for (int now : {0, 1}) {
                    t->m_target.angle = 0.3f;
                    t->m_shown.angle = 0.4f;
                    t->m_objectFlags = 0;
                    Call(original, 0x004AFA00, t, &CTextElement::SetTextRotation, 15.0f * k - 20.0f, now);
                    MarkAll(mark++);
                }
            }
            double same = 10.0 * 0.017453292519943295 + 0.001;
            t->m_target.angle = static_cast<float>(same);
            t->m_shown.angle = static_cast<float>(same);
            Call(original, 0x004AFA00, t, &CTextElement::SetTextRotation, 10.0f, 1);
            t->m_count = 0;
            t->m_shown.angle = 0.0f;
            Call(original, 0x004AFA00, t, &CTextElement::SetTextRotation, 30.0f, 1);
            t->m_count = 1;
            t->m_shadow = false;
            MarkAll(mark++);
            // per-character properties: an entry with everything, one with only the index
            uint32_t c0 = At(kParams + 0x40 * 10), c1 = At(kParams + 0x40 * 11);
            Answer('I', c0, 0x7F8C98FE, {3});
            Answer('F', c0, 0xED344E1B, {Bits(45.0f)});
            Answer('P', c0, 0xA6F5352F, {Bits(2.75f), Bits(-3.5f)});
            Answer('P', c0, 0x13B9DA7B, {Bits(1.25f), Bits(0.5f)});
            Answer('I', c1, 0x7F8C98FE, {5});
            uint32_t chars = MakeArray(0, Script::ESYMBOLTYPE_STRUCTURE, {c0, c1});
            // SetProperties: everything (blur on, shadow on, overrides on); the opposites; nothing
            uint32_t p = At(kParams);
            Answer('K', p, 0x2F6BF72D, {0xF00D});
            Answer('F', p, 0xED344E1B, {Bits(12.0f)});
            Answer('J', p, 0x67E093E4, {Bits(1.0f), Bits(-1.0f)});
            Answer('T', p, 0xC4745838, {reinterpret_cast<uint32_t>(Str(1, "props"))});
            Answer('L', p, 0xD1653C6C, {1});
            Answer('R', p, 0x4F0E1182, {0x11223344});
            Answer('P', p, 0x2A1FE0CC, {Bits(4.0f), Bits(5.0f)});
            Answer('L', p, 0x8A897DD2, {1});
            Answer('R', p, 0x1E7BB1F5, {0x55667788});
            Answer('L', p, 0x0BF51A0C, {1});
            Answer('L', p, 0xB7686F92, {1});
            Answer('L', p, 0x1A226DCD, {1});
            Answer('S', p, 0x44B3B0F4, {chars});
            p = At(kParams + 0x40);
            Answer('L', p, 0x4B73E4F8, {1});
            Answer('L', p, 0x95B5B9A0, {1});
            Answer('L', p, 0x61B7B58B, {1});
            Answer('L', p, 0xD25BC6CD, {1});
            Answer('L', p, 0xE1931201, {1});
            Answer('R', p, 0x4F0E1182, {0x99999999});
            Answer('K', At(kParams + 0x40 * 5), 0x2F6BF72D, {0xF00E});
            Answer('L', At(kParams + 0x40 * 6), 0x61B7B58B, {1});
            t->m_overrideRgba = false;
            for (int n : {0, 6, 0, 1, 1, 2}) {
                if (n == 1) {
                    t->m_objectFlags = 0;
                    Call(original, 0x004B18E0, t, &CTextElement::SetProperties, Params(5));
                    MarkAll(mark++);
                }
                t->m_target.angle = 0.0f;
                Call(original, 0x004B18E0, t, &CTextElement::SetProperties, Params(n));
                MarkAll(mark++);
            }
            // the morph: with the blur (its settings from the parameters), without; rot_angle or not
            Call(original, 0x004B0A20, t, &CTextElement::EnableBlur);
            Blur(t->mp_blur);
            uint32_t m = At(kParams + 0x40 * 3);
            Answer('P', m, 0x21DE240C, {Bits(0.9f), Bits(0.1f)});
            Answer('P', m, 0x4C2A3CDE, {Bits(8.0f), Bits(4.0f)});
            Answer('F', m, 0x906B67BA, {Bits(0.25f)});
            Answer('F', m, 0xED344E1B, {Bits(-30.0f)});
            for (int n : {3, 4}) {
                Call(original, 0x004B09D0, t, &CTextElement::SetMorph, Params(n));
                MarkAll(mark++);
            }
            Call(original, 0x004B0AE0, t, &CTextElement::DisableBlur);
            Call(original, 0x004B09D0, t, &CTextElement::SetMorph, Params(3));
            MarkAll(mark++);
        });

        RunBoth("text elements: updates", [&](bool original) {
            TextWorld(original);
            CTextElement* t = T(2);
            Call(original, 0x004AF8C0, t, &CTextElement::SetText, Str(0, "update"));
            t->m_shadow = true;
            Call(original, 0x004B0620, t, &CTextElement::MakeTextObjects, 1, true);
            const uint32_t flagSets[] = {0, CScreenElement::kPos3d, CScreenElement::kPos3d | CScreenElement::kOffScreen,
                                         CScreenElement::kFlag1000000, CScreenElement::kHidden};
            const float tops[] = {100.0f, -200.5f, 648.5f, NAN, 300.0f};
            const float alphas[] = {0.6f, 0.00009f, NAN, 1.0f, 0.3333f};
            const float angles[] = {0.0f, 0.5f, -1.25f, 0.0f, 2.0f};
            const float justs[] = {-1.0f, 0.0f, 1.0f, NAN, 0.5f};
            uint32_t mark = 60;
            for (int k = 0; k < 5; ++k) {
                for (int j = 0; j < 2; ++j) {
                    int v = (k + 2 * j) % 5;
                    t->m_objectFlags = flagSets[k];
                    t->m_summed.top = tops[v];
                    t->m_summed.alpha = alphas[v];
                    t->m_summed.angle = angles[v];
                    t->m_internalJustX = justs[v];
                    t->m_internalJustY = justs[4 - v];
                    t->m_shadow = j == 1;
                    t->m_overrideAlpha = (v & 1) != 0;
                    t->m_overrideRgba = (v & 2) != 0;
                    Call(original, 0x004AFE70, t, &CTextElement::Update);
                    MarkAll(mark++);
                }
            }
            // with the blur: its copies around the centre, with and without the shadow
            t->m_objectFlags = 0;
            t->m_summed.top = 120.0f;
            t->m_summed.alpha = 0.8f;
            Call(original, 0x004B0A20, t, &CTextElement::EnableBlur);
            Blur(t->mp_blur);
            for (int j = 0; j < 2; ++j) {
                t->m_shadow = j == 1;
                if (j == 1)
                    Call(original, 0x004B0620, t, &CTextElement::MakeTextObjects, t->m_count, true);
                g_time = 50000 + 500 * j;
                Call(original, 0x004AFE70, t, &CTextElement::Update);
                MarkAll(mark++);
            }
            // no string: nothing
            t->m_string = nullptr;
            Call(original, 0x004AFE70, t, &CTextElement::Update);
            MarkAll(mark++);
        });

        RunBoth("text elements: the blur effect", [&](bool original) {
            TextWorld(original);
            auto* b = Ptr<CBlurEffect>(kHeap);
            Blur(b);
            Rec(0x410, {reinterpret_cast<uint32_t>(Call(original, 0x004A5770, b, &CBlurEffect::GetCopy, 5))});
            // morphing: part way, past the end, a clock gone back, nothing left
            const uint32_t times[] = {49500, 50000, 51500, 48000, 52000};
            uint32_t mark = 80;
            for (uint32_t time : times) {
                g_time = time;
                Call(original, 0x004A57D0, b, &CBlurEffect::Update);
                MarkAll(mark++);
            }
            b->m_remaining = -5;
            b->m_current.maxDisplacement[0] = 3.0f;
            g_time = 52300;
            Call(original, 0x004A57D0, b, &CBlurEffect::Update);
            MarkAll(mark++);
            for (int ms : {0, 750}) {
                Call(original, 0x004A5780, b, &CBlurEffect::StartMorph, ms);
                MarkAll(mark++);
            }
            CBlurEffect::Settings settings = {{0.2f, 0.3f}, {0.4f, 0.5f}, {0.6f, 0.7f}};
            for (int ms : {0, 400}) {
                Call(original, 0x004A5CF0, b, &CBlurEffect::SetTarget, static_cast<const CBlurEffect::Settings*>(&settings), ms);
                MarkAll(mark++);
            }
            // from parameters: all three pairs and a time (its ms out), none
            uint32_t p = At(kParams);
            Answer('P', p, 0x21DE240C, {Bits(0.9f), Bits(0.1f)});
            Answer('P', p, 0x3E10436D, {Bits(1.3f), Bits(1.7f)});
            Answer('P', p, 0x4C2A3CDE, {Bits(8.0f), Bits(4.0f)});
            Answer('F', p, 0x906B67BA, {Bits(0.3337f)});
            int* outMs = Ptr<int>(kOut);
            for (int n : {0, 1}) {
                *outMs = -7;
                Rec(0x411, {reinterpret_cast<uint32_t>(Call(original, 0x004A5D30, b, &CBlurEffect::SetFromParams, Params(n),
                                                            n ? static_cast<int*>(nullptr) : outMs)),
                            static_cast<uint32_t>(*outMs)});
                MarkAll(mark++);
            }
        });
    }
    RestoreAll(g_saved);
}

// --- the text block (src/game/front/text_block.cpp): its lines are real text elements (the game's or ours)
// made in the bump allocator's memory; the manager registers them (ids from 0x6000), parents them (linked as
// SetParent does) and destroys them (taken out of the tree).

std::vector<uint32_t> g_registered; // (arena offsets)
void* g_blockMgrVtable[8];

CTextBlockElement* B(int i) { return reinterpret_cast<CTextBlockElement*>(E(i)); }

void InstallBlockStandIns(StandIns& s)
{
    InstallTextStandIns(s);
    for (void*& slot : g_blockMgrVtable)
        slot = nullptr;
    g_blockMgrVtable[1] = s.Thunk(4, [](Regs& r) {
        auto* e = reinterpret_cast<CScreenElement*>(r.args[0]);
        e->m_id = 0x6000 + static_cast<uint32_t>(g_registered.size());
        g_registered.push_back(r.args[0] - g_base);
        Rec(0xEE01, {r.ecx, r.args[0], e->m_id});
        return 0u;
    });
    s.At({0x004ABBE0, AddressOf(&Front::CScreenElementManager::SetParent)}, 12, [](Regs& r) {
        auto* parent = *reinterpret_cast<CScreenElement**>(r.args[0]);
        auto* child = *reinterpret_cast<CScreenElement**>(r.args[1]);
        Rec(0xBBE0, {r.ecx, reinterpret_cast<uint32_t>(parent), reinterpret_cast<uint32_t>(child), r.args[2]});
        child->mp_parent.Set(parent);
        CScreenElement* last = parent->GetLastChild();
        if (last)
            last->mp_next.Set(child);
        else
            parent->mp_firstChild.Set(child);
        child->mp_prev.Set(last);
        return 0u;
    });
    s.At({0x004AD100, AddressOf(&Front::CScreenElementManager::DestroyElement)}, 16, [](Regs& r) {
        Rec(0xD100, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3]});
        for (uint32_t offset : g_registered) {
            auto* e = Ptr<CScreenElement>(offset);
            if (e->m_id == r.args[0]) {
                ElementPtr none;
                none.mp_object = nullptr;
                none.mp_prev = none.mp_next = nullptr;
                e->SetParent(&none, 0);
                e->m_id = 0xDEAD0000u + r.args[0];
                break;
            }
        }
        return 0u;
    });
}

void BlockWorld(bool original)
{
    TextWorld(original);
    g_registered.clear();
    *Ptr<void**>(kManager) = g_blockMgrVtable;
    CTextBlockElement* b = B(7);
    std::memset(reinterpret_cast<uint8_t*>(b) + 0x178, 0, 0x4C);
    b->m_fontName = 0xF0F0;
    b->m_internalScale = 1.0f;
    b->m_lineSpacing = 1.0f;
    b->m_maxLines = 4;
    b->m_width = 150.0f;
    b->m_height = 60.0f;
    b->m_shadowRgba[3] = 0x80;
    Link(0, 7);
}

void TestTextBlocks()
{
    SaveElementGlobals();
    {
        StandIns s;
        InstallBlockStandIns(s);

        RunBoth("text blocks: construction, fonts, lines", [&](bool original) {
            BlockWorld(original);
            std::memset(E(12), 0, 0x200);
            Rec(0x420, {reinterpret_cast<uint32_t>(Call(original, 0x004B0810, B(12), &CTextBlockElement::Construct))});
            MarkAll(1);
            CTextBlockElement* b = B(7);
            // fonts: the lines that fit (a scaled font)
            for (float scale : {1.0f, 0.5f}) {
                b->m_internalScale = scale;
                Call(original, 0x004AF6C0, b, &CTextBlockElement::SetFont, 0xABCDu);
                MarkAll(2);
            }
            b->m_internalScale = 1.0f;
            b->m_maxLines = 4;
            // text: a few lines, more lines than fit (the last ones kept), a line end, expanding, nothing
            const char* one[] = {Str(0, "the quick brown fox jumps over the dog")};
            const char* many[] = {Str(1, "one two three four five six seven eight"), Str(2, "nine ten eleven twelve"),
                                  Str(3, "thirteen fourteen fifteen sixteen seventeen")};
            const char* breaks[] = {Str(4, "first\\nsecond line here")};
            uint32_t mark = 3;
            auto set = [&](const char** texts, int count) {
                Call(original, 0x004B0CD0, b, &CTextBlockElement::SetText, texts, count);
                Rec(0x421, {static_cast<uint32_t>(b->m_numElements), static_cast<uint32_t>(b->m_maxLines),
                            static_cast<uint32_t>(Call(original, 0x004B1390, b, &CTextBlockElement::GetLength))});
                MarkAll(mark++);
            };
            set(one, 1);
            set(many, 3);
            set(breaks, 1);
            b->m_allowExpansion = true;
            set(many, 3);
            b->m_lineSpacing = 0.1444f; // (lines 1.95 high: the height truncates to more lines than there are)
            set(many, 3);
            b->m_lineSpacing = 1.0f;
            b->m_allowExpansion = false;
            b->m_maxLines = 4;
            set(one, -1);
            b->m_maxLines = 4;
            set(one, 0);
            const char* wide[] = {Str(21, "abcdefghijklmnopqrs tail")};
            set(wide, 1);
            // the text back: fits, doesn't fit
            set(one, 1);
            char* out = Ptr<char>(kOut);
            for (int size : {0x80, 10}) {
                Rec(0x422, {static_cast<uint32_t>(Call(original, 0x004B11C0, b, &CTextBlockElement::GetText, out, size)) & 0xFF,
                            Hash(out)});
            }
            // adding: a word, a space (with the width enforced), a word too wide (enforced / last line / neither)
            struct Add {
                const char* text;
                bool enforce, last;
            };
            const Add adds[] = {{" end", false, false},
                                {" ", true, false},
                                {"x", true, false},
                                {"abcdefghijklmnopq", true, false},
                                {" abcdefghijklmnop", true, false}, // (16 characters: 152 wide)
                                {"abcdefghijklmnopq", false, true},
                                {"abcdefghijklmnopq", false, false}};
            int k = 10;
            for (const Add& a : adds) {
                Rec(0x423, {static_cast<uint32_t>(Call(original, 0x004B16D0, b, &CTextBlockElement::Concatenate,
                                                       Str(k++, a.text), a.enforce, a.last)) & 0xFF});
                MarkAll(mark++);
            }
            // taking off: an escaped backslash, characters, until empty
            const char* slashes[] = {Str(20, "ab\\\\\\\\")};
            set(slashes, 1);
            for (int n = 0; n < 6; ++n)
                Rec(0x424, {static_cast<uint32_t>(Call(original, 0x004B14A0, b, &CTextBlockElement::Backspace)) & 0xFF});
            const char* slash[] = {Str(22, "xy\\")};
            set(slash, 1);
            Rec(0x424, {static_cast<uint32_t>(Call(original, 0x004B14A0, b, &CTextBlockElement::Backspace)) & 0xFF});
            MarkAll(mark++);
            // destruction: with a blur, without
            B(12)->mp_blur = Ptr<CBlurEffect>(kHeapEnd - 0x500);
            Call(original, 0x004B08B0, B(12), &CTextBlockElement::DestroyBody);
            B(12)->mp_blur = nullptr;
            for (uint8_t flags : {uint8_t(0), uint8_t(1)})
                Rec(0x425, {reinterpret_cast<uint32_t>(Call(original, 0x004B0B30, B(12), &CTextBlockElement::Destroy, flags))});
            MarkAll(mark++);
        });

        RunBoth("text blocks: properties, morphs, updates", [&](bool original) {
            BlockWorld(original);
            CTextBlockElement* b = B(7);
            // everything (a string, the blur, the shadow, the overrides on); an array of strings with the
            // opposites; nothing
            uint32_t p = At(kParams);
            Answer('F', p, 0x1FE341D2, {Bits(1.25f)});
            Answer('F', p, 0xE3FA22FC, {Bits(1.5f)});
            Answer('K', p, 0x2F6BF72D, {0xF00D});
            Answer('J', p, 0x67E093E4, {Bits(1.0f), Bits(0.0f)});
            Answer('L', p, 0x322839A2, {1});
            Answer('T', p, 0xC4745838, {reinterpret_cast<uint32_t>(Str(0, "a block of text in lines"))});
            Answer('L', p, 0xD1653C6C, {1});
            Answer('R', p, 0x4F0E1182, {0x11223344});
            Answer('P', p, 0x2A1FE0CC, {Bits(3.0f), Bits(-2.0f)});
            Answer('L', p, 0x8A897DD2, {1});
            Answer('R', p, 0x1E7BB1F5, {0x55667788});
            Answer('L', p, 0x0BF51A0C, {1});
            Answer('L', p, 0xB7686F92, {1});
            Answer('L', p, 0x1A226DCD, {1});
            p = At(kParams + 0x40);
            uint32_t texts = MakeArray(0, Script::ESYMBOLTYPE_STRING,
                                       {reinterpret_cast<uint32_t>(Str(1, "an array")), reinterpret_cast<uint32_t>(Str(2, "of strings"))});
            Answer('S', p, 0xC4745838, {texts});
            Answer('L', p, 0x4B73E4F8, {1});
            Answer('L', p, 0x95B5B9A0, {1});
            Answer('L', p, 0x61B7B58B, {1});
            Answer('L', p, 0xD25BC6CD, {1});
            Answer('L', p, 0xE1931201, {1});
            uint32_t mark = 40;
            Answer('L', At(kParams + 0x40 * 7), 0x4B73E4F8, {1});
            Answer('L', At(kParams + 0x40 * 8), 0x95B5B9A0, {1});
            Answer('F', At(kParams + 0x40 * 9), 0x1FE341D2, {Bits(0.75f)});
            for (int n : {0, 0, 7, 0, 8, 9, 1, 2, 0}) {
                b->m_objectFlags = 0;
                Call(original, 0x004B1B60, b, &CTextBlockElement::SetProperties, Params(n));
                MarkAll(mark++);
            }
            // updates: after a change (each internal_just), not
            const float justs[] = {-1.0f, 0.0f, 1.0f};
            for (int k = 0; k < 4; ++k) {
                b->m_objectFlags = k == 3 ? 0 : CScreenElement::kVisibleNow;
                b->m_internalJustX = justs[k % 3];
                b->m_internalJustY = justs[2 - k % 3];
                Call(original, 0x004B1EC0, b, &CTextBlockElement::Update);
                MarkAll(mark++);
            }
            // the morph: with the blur (each line's aimed at the middle), without
            uint32_t m = At(kParams + 0x40 * 3);
            Answer('P', m, 0x21DE240C, {Bits(0.9f), Bits(0.1f)});
            Answer('P', m, 0x3E10436D, {Bits(1.3f), Bits(1.7f)});
            Answer('P', m, 0x4C2A3CDE, {Bits(8.0f), Bits(4.0f)});
            Answer('F', m, 0x906B67BA, {Bits(0.25f)});
            Call(original, 0x004B0B60, b, &CTextBlockElement::SetMorph, Params(3));
            MarkAll(mark++);
            Call(original, 0x004B1B60, b, &CTextBlockElement::SetProperties, Params(1));
            Call(original, 0x004B0B60, b, &CTextBlockElement::SetMorph, Params(3));
            MarkAll(mark++);
        });
    }
    RestoreAll(g_saved);
}

} // namespace

SELFTEST(Early, TestTextElements, FeTest::Cleanup);
SELFTEST(Early, TestTextBlocks, FeTest::Cleanup);
