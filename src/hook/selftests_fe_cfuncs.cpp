// Self-tests for the screen element script commands and the manager's element creation
// (src/game/front/screen_element_cfuncs.cpp), on the shared element world (fe_element_fixture.h). The commands
// are compared with the manager's functions stood in for (ResolveId answers from the stand-in structures, the
// lookups from the world); the creation is compared with every element constructor, the heap stack, the
// registering and the element's slots stood in for.

#include "game/front/element3d.h"
#include "game/front/menu_element.h"
#include "game/front/screen_element_cfuncs.h"
#include "game/front/screen_element_manager.h"
#include "game/front/scrolling_menu.h"
#include "game/front/sprite_element.h"
#include "hook/fe_element_fixture.h"
#include "hook/selftest.h"

namespace {

using namespace FeTest;
using Front::CScreenElementManager;

constexpr uint32_t kMgr = 0x5000;
constexpr uint32_t kNew = 0x5800;        // memory for new elements (0x200 each)
constexpr uint32_t kFont = 0x7000;
constexpr uint32_t kSymbol = 0x7100;
constexpr uint32_t kSpawned = 0x7200;
constexpr uint32_t kText = 0x7400;       // a text element's string
constexpr uint32_t kWorldEnd = 0x7600;

void* g_mgrVtable[8];
int g_newCount;
uint32_t g_symbolType;
bool g_spawnNone;
float g_wordWidth;

CScreenElementManager* Mgr() { return Ptr<CScreenElementManager>(kMgr); }
void MarkAll(uint32_t n) { Snapshot(0, kWorldEnd, n); }

using Handler = std::function<uint32_t(Regs&)>;

// `createElement`: the manager's CreateElement stood in too (for the commands; not when testing it).
void InstallCfuncStandIns(StandIns& s, bool createElement)
{
    InstallElementStandIns(s, false);
    // the element's slots 0 and 3
    g_vtable[3] = s.Thunk(12, [](Regs& r) {
        Rec(0xE003, {r.ecx, r.args[0], r.args[1], r.args[2]});
        return 1u;
    });
    // the manager's functions
    s.At({0x004AC120, AddressOf(&CScreenElementManager::ResolveId)}, 12, [](Regs& r) {
        Rec(0xC120, {r.ecx, r.args[0], r.args[1]});
        const std::vector<uint32_t>* value = Lookup('E', r.args[0], r.args[1]);
        if (!value)
            return 0u;
        *reinterpret_cast<uint32_t*>(r.args[2]) = (*value)[0];
        return 1u;
    });
    auto fill = [](uint32_t outAddress, uint32_t id) {
        auto* out = reinterpret_cast<ElementPtr*>(outAddress);
        out->mp_object = nullptr;
        out->mp_prev = out->mp_next = nullptr;
        out->Set(id ? ById(id) : nullptr); // (the unused elements have id 0)
    };
    s.At({0x004AAE20, AddressOf(&CScreenElementManager::GetElement)}, 12, [fill](Regs& r) {
        Rec(0xAE20, {r.ecx, r.args[1], r.args[2]});
        fill(r.args[0], r.args[1]);
        return r.args[0];
    });
    s.At({0x004AD1E0, AddressOf(&CScreenElementManager::ResolveElement)}, 16, [fill](Regs& r) {
        Rec(0xD1E0, {r.ecx, r.args[1], r.args[2], r.args[3]});
        const std::vector<uint32_t>* value = Lookup('E', r.args[1], r.args[2]);
        fill(r.args[0], value ? (*value)[0] : 0);
        return r.args[0];
    });
    s.At({0x004AB730, AddressOf(&CScreenElementManager::SetRoot)}, 4, [](Regs& r) {
        Rec(0xB730, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x004AD100, AddressOf(&CScreenElementManager::DestroyElement)}, 16, [](Regs& r) {
        Rec(0xD100, {r.ecx, r.args[0], r.args[1], r.args[2], r.args[3]});
        return 0u;
    });
    s.At({0x004AC570, AddressOf(&CScreenElementManager::PushPriority)}, 8, [](Regs& r) {
        Rec(0xC570, {r.ecx, r.args[0], r.args[1]});
        return 1u;
    });
    for (void*& slot : g_mgrVtable)
        slot = nullptr;
    g_mgrVtable[1] = s.Thunk(4, [](Regs& r) {
        Rec(0xEE01, {r.ecx, r.args[0]});
        return 0u;
    });
    // the element constructors (the game's and ours): the element's memory given the stand-in vtable
    auto ctor = [&s](std::initializer_list<uint32_t> addresses, uint32_t tag) {
        s.At(addresses, 0, [tag](Regs& r) {
            Rec(tag, {r.ecx});
            *reinterpret_cast<void***>(r.ecx) = g_vtable;
            *reinterpret_cast<uint32_t*>(r.ecx + 0x14) = 0x5555;
            return r.ecx;
        });
    };
    ctor({0x004AAC20, AddressOf(&Front::CContainerElement::Construct)}, 0xC001);
    ctor({0x004AE1C0, AddressOf(&Front::CScrollingMenu::ConstructHorizontal)}, 0xC002);
    ctor({0x004AED90, AddressOf(&Front::CTextureElement::Construct)}, 0xC003);
    ctor({0x004A75C0, AddressOf(&Front::CBaseMenu::ConstructVMenu)}, 0xC004);
    ctor({0x004B0810}, 0xC005);
    ctor({0x004B0920}, 0xC006);
    ctor({0x004B2060, AddressOf(&Front::CWindowElement::Construct)}, 0xC007);
    ctor({0x004A6060, AddressOf(&Front::CElement3d::Construct)}, 0xC008);
    ctor({0x004AE170, AddressOf(&Front::CScrollingMenu::ConstructVertical)}, 0xC009);
    ctor({0x004AF1A0, AddressOf(&Front::CSpriteElement::Construct)}, 0xC00A);
    ctor({0x004A7630, AddressOf(&Front::CBaseMenu::ConstructHMenu)}, 0xC00B);
    s.At({0x00401CC0, AddressOf(static_cast<void* (*)(unsigned int)>(&Mem::Calloc))}, 0, [](Regs& r) {
        Rec(0x1CC0, {r.args[0]});
        if (g_newCount < 0)
            return 0u;
        return At(kNew + 0x200 * (g_newCount++ % 6));
    });
    s.At({0x005BB0E0, AddressOf(&Mem::Manager::PushContext)}, 4, [](Regs& r) {
        Rec(0xB0E0, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x005BB110, AddressOf(&Mem::Manager::PopContext)}, 0, [](Regs& r) {
        Rec(0xB110, {r.ecx});
        return 0u;
    });
    s.At({0x00461930, AddressOf(&Obj::CObject::SetID)}, 4, [](Regs& r) {
        Rec(0x1930, {r.ecx, r.args[0]});
        *reinterpret_cast<uint32_t*>(r.ecx + 0x14) = r.args[0];
        return 0u;
    });
    // scripts
    s.At({0x00478CF0, AddressOf(&Script::LookUpSymbol)}, 0, [](Regs& r) {
        Rec(0x8CF0, {r.args[0]});
        *Ptr<uint8_t>(kSymbol + 1) = static_cast<uint8_t>(g_symbolType);
        return g_symbolType ? At(kSymbol) : 0u;
    });
    s.At({0x00475420, AddressOf(static_cast<Script::CScript* (*)(uint32_t, CStruct*, uint32_t, CStruct*, int, uint32_t,
                                                                  bool, bool, bool, bool)>(&Script::SpawnScript))},
         0, [](Regs& r) {
             Rec(0x5420, {r.args[0], r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6] & 0xFF,
                          r.args[7] & 0xFF, r.args[8] & 0xFF, r.args[9] & 0xFF});
             return g_spawnNone ? 0u : At(kSpawned);
         });
    s.At({0x004750B0, AddressOf(&Script::CScript::Update)}, 0, [](Regs& r) {
        Rec(0x50B0, {r.ecx});
        return 0u;
    });
    s.At({0x00462120, AddressOf(&Obj::CTracker::LookUpObject)}, 4, [](Regs& r) {
        Rec(0x2120, {r.ecx, r.args[0]});
        return reinterpret_cast<uint32_t>(ById(r.args[0]));
    });
    // the text elements
    auto text = [&s](uint32_t address, uint16_t pop, uint32_t tag, uint32_t result) {
        s.At({address}, pop, [tag, result, pop](Regs& r) {
            // (only the arguments it takes: 0x004B16D0's text and two flags, 0x004AFD00's text)
            Rec(tag, {r.ecx, pop >= 4 ? r.args[0] : 0u, pop >= 8 ? r.args[1] & 0xFF : 0u, pop >= 12 ? r.args[2] & 0xFF : 0u});
            return result;
        });
    };
    text(0x004B16D0, 12, 0x16D0, 1);
    text(0x004AFD00, 4, 0xFD00, 0x101);
    text(0x004B14A0, 0, 0x14A0, 1);
    text(0x004AFDD0, 0, 0xFDD0, 0);
    text(0x004B1390, 0, 0x1390, 17);
    text(0x004AF650, 0, 0xF650, 5);
    s.At({0x004B11C0}, 8, [](Regs& r) {
        Rec(0x11C0, {r.ecx, r.args[1]});
        CopyText(reinterpret_cast<char*>(r.args[0]), "block text");
        return 1u;
    });
    s.At({0x00477A00, AddressOf(static_cast<void (CStruct::*)(uint32_t, const char*)>(&CStruct::AddString))}, 8,
         [](Regs& r) {
             Rec(0x79D0, {r.ecx, r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
             return 0u;
         });
    s.At({0x004779D0}, 8, [](Regs& r) {
        Rec(0x79D0, {r.ecx, r.args[0], Hash(reinterpret_cast<const char*>(r.args[1]))});
        return 0u;
    });
    s.At({0x004780D0, AddressOf(static_cast<void (CStruct::*)(uint32_t, Script::CArray*)>(&CStruct::AddArrayPointer))}, 8,
         [](Regs& r) {
             auto* array = reinterpret_cast<Script::CArray*>(r.args[1]);
             Rec(0x80D0, {r.ecx, r.args[0], array->m_type, array->m_size});
             for (uint32_t i = 0; i < array->m_size && i < 8; ++i)
                 Rec(0x80D1, {array->m_elements[i]});
             return 0u;
         });
    // fonts: a word's width from its length
    s.At({0x0048D760}, 0, [](Regs& r) {
        Rec(0xD760, {r.args[0]});
        return At(kFont);
    });
    s.At({0x0048D060}, 12, [](Regs& r) {
        const char* word = reinterpret_cast<const char*>(r.args[0]);
        Rec(0xD060, {r.ecx, Hash(word)});
        *reinterpret_cast<float*>(r.args[1]) = static_cast<float>(std::strlen(word)) * g_wordWidth;
        *reinterpret_cast<float*>(r.args[2]) = 12.0f;
        return 0u;
    });
    s.At({0x00478540, AddressOf(&CStruct::AppendStructureIfAny)}, 4, [](Regs& r) {
        Rec(0x8540, {r.ecx, r.args[0]});
        return 0u;
    });
    s.At({0x00476410, AddressOf(&CStruct::Init)}, 0, [](Regs& r) {
        Rec(0x6410, {r.ecx});
        return r.ecx;
    });
    s.At({0x00477130, 0x00477090, AddressOf(&CStruct::Clear)}, 0, [](Regs& r) {
        Rec(0x7130, {r.ecx});
        return 0u;
    });
    s.At({0x00409070, AddressOf(&Script::DeleteStruct)}, 0, [](Regs& r) {
        Rec(0x9070, {r.args[0]});
        return 0u;
    });
    if (!createElement)
        return;
    s.At({0x004ACB70, AddressOf(&CScreenElementManager::CreateElement)}, 16, [fill](Regs& r) {
        Rec(0xCB70, {r.ecx, r.args[1], r.args[2], r.args[3]});
        fill(r.args[0], 0x1002);
        return r.args[0];
    });
}

void CfuncWorld(bool original)
{
    BeginWorld(original);
    g_newCount = 0;
    g_symbolType = 0;
    g_spawnNone = false;
    g_wordWidth = 9.0f;
    GameVariable<uint32_t>(0x00701440) = At(kMgr);
    std::memset(&GameVariable<uint8_t>(0x006F3614), 0, 16);
    Mgr()->m_vtable = g_mgrVtable;
    // the memory manager's heaps: numbers to recognise
    auto* memory = Ptr<uint32_t>(kMemManager);
    for (int i = 0x110 / 4; i < 0x200 / 4; ++i)
        memory[i] = 0x4EA00000u + i;
    memory[0x5D4 / 4] = 0x4EA005D4u;
}

bool Run(bool original, uint32_t address, bool(__cdecl* ours)(CStruct*, Script::CScript*), CStruct* params,
         Script::CScript* script)
{
    if (original)
        return Original<bool(__cdecl*)(CStruct*, Script::CScript*)>(address)(params, script);
    return ours(params, script);
}

void TestScreenElementCfuncs()
{
    SaveElementGlobals();
    g_saved.push_back({0x006F3614, 16, {}});
    SaveAll(g_saved);
    {
        StandIns s;
        InstallCfuncStandIns(s, false);

        RunBoth("screen element commands: creation", [&](bool original) {
            CfuncWorld(original);
            auto* script = Ptr<Script::CScript>(kScript);
            // every type (on the heap named, the front end's, Default: none), an unknown type, no memory
            const uint32_t types[] = {0x5B9DA842, 0x2900E3D0, 0x118F6383, 0x130EF802, 0x40D92263, 0x5200DFB6,
                                      0xA13ADBB7, 0x82C59D78, 0x84298A3C, 0xB12B510A, 0xCCDED1E1, 0x12345};
            const uint32_t heaps[] = {0x9F7B7843, 0x477FC6DE, 0x03C84A59, 0x1CA1FF20, 0x39FB63CC, 0x5B8AB877,
                                      0x935AB858, 0xE37E78C5, 0xBAA81175, 0xC80BF12D, 0xE3F81B18, 0xEABD217B, 0x777};
            auto* out = Ptr<ElementPtr>(kOut);
            for (int k = 0; k < 13; ++k) {
                uint32_t p = At(kParams + 0x40 * (k % 8));
                g_answers.clear();
                Answer('K', p, 0x42D6DD1E, {heaps[k]});
                Answer('I', p, 0x6E9C34F7, {static_cast<uint32_t>(k % 3)});
                if (k % 4 == 1)
                    Answer('I', p, 0x9D5923D8, {static_cast<uint32_t>(k)});
                if (k == 12)
                    g_answers.clear();
                uint32_t type = types[k % 11];
                std::memset(out, 0xCD, 12);
                Call(original, 0x004ACB70, Mgr(), &CScreenElementManager::CreateElement, out, type, 0x2000u + k,
                     Ptr<CStruct>(kParams + 0x40 * (k % 8)));
                Rec(0x500, {reinterpret_cast<uint32_t>(out->mp_object)});
                out->Release();
            }
            MarkAll(1);
            // (no memory: the original calls through null - not run)
            // the root window
            Rec(0x501, {static_cast<uint32_t>(Run(original, 0x004AAD90, Front::CFunc_ScreenElementSystemInit, nullptr, script)) & 0xFF});
            MarkAll(2);
        });

    }
    {
        StandIns s;
        InstallCfuncStandIns(s, true);

        RunBoth("screen element commands", [&](bool original) {
            CfuncWorld(original);
            auto* script = Ptr<Script::CScript>(kScript);
            auto params = [](int n) { return Ptr<CStruct>(kParams + 0x40 * n); };
            auto p = [](int n) { return At(kParams + 0x40 * n); };
            constexpr uint32_t kId = 0x40C698AF;
            // 0: element 5; 1: element 3 (a text block); 2: element 4 (a text element); 3: nothing resolved
            Answer('E', p(0), kId, {0x1005});
            Answer('E', p(1), kId, {0x1003});
            Answer('E', p(2), kId, {0x1004});
            E(3)->m_type = 0x40D92263;
            E(4)->m_type = 0x5200DFB6;
            *Ptr<uint32_t>(ElemAt(4) + 0x178) = At(kText);
            CopyText(Ptr<char>(kText), "hello");
            Answer('K', p(0), 0x7321A8D6, {0xB12B510A});
            Answer('K', p(3), 0x7321A8D6, {0x5B9DA842}); // (no type: the original's is stack garbage)
            Answer('K', p(0), kId, {0x2222});
            uint32_t mark = 10;
            auto run = [&](uint32_t address, bool(__cdecl* ours)(CStruct*, Script::CScript*), int n) {
                Rec(0x510, {address, static_cast<uint32_t>(Run(original, address, ours, params(n), script)) & 0xFF});
                MarkAll(mark++);
            };
            run(0x004AD240, Front::CFunc_CreateScreenElement, 0);
            run(0x004AD240, Front::CFunc_CreateScreenElement, 3);
            // destroy: plain, dont_recurse, preserve_parent, both, nothing resolved
            Answer('E', p(4), kId, {0x1006});
            Answer('E', p(5), kId, {0x1006});
            Answer('E', p(6), kId, {0x1006});
            Answer('E', p(7), kId, {0x1006});
            Answer('L', p(5), Crc::GenerateCRCFromString("dont_recurse"), {1});
            Answer('L', p(6), Crc::GenerateCRCFromString("preserve_parent"), {1});
            Answer('L', p(7), Crc::GenerateCRCFromString("dont_recurse"), {1});
            Answer('L', p(7), Crc::GenerateCRCFromString("preserve_parent"), {1});
            for (int n : {4, 5, 6, 7, 3})
                run(0x004AD300, Front::CFunc_DestroyScreenElement, n);
            // run a script: a member function, a script spawned (with callback, Params, callback_params), none
            Answer('K', p(0), 0, {0x5C5C});
            Answer('K', p(0), 0x86068BD9, {0xCA11});
            Answer('S', p(0), 0x7031F10C, {p(8)});
            Answer('S', p(0), 0xE6CF88C4, {p(9)});
            g_symbolType = Script::ESYMBOLTYPE_MEMBERFUNCTION;
            run(0x004AD370, Front::CFunc_RunScriptOnScreenElement, 0);
            g_symbolType = Script::ESYMBOLTYPE_QSCRIPT;
            run(0x004AD370, Front::CFunc_RunScriptOnScreenElement, 0);
            g_symbolType = 0;
            g_spawnNone = true;
            run(0x004AD370, Front::CFunc_RunScriptOnScreenElement, 0);
            // props, morph, lock (on / Off), dims, props (an Element3d's angles; none), root, children
            run(0x004AD4C0, Front::CFunc_SetScreenElementProps, 0);
            run(0x004AD4C0, Front::CFunc_SetScreenElementProps, 3);
            run(0x004AD540, Front::CFunc_DoScreenElementMorph, 0);
            run(0x004AD5C0, Front::CFunc_SetScreenElementLock, 0);
            Answer('L', p(1), 0xD443A2BC, {1});
            run(0x004AD5C0, Front::CFunc_SetScreenElementLock, 1);
            run(0x004AD650, Front::CFunc_GetScreenElementDims, 0);
            run(0x004AC750, Front::CFunc_GetScreenElementProps, 0);
            E(5)->m_type = 0x82C59D78;
            run(0x004AC750, Front::CFunc_GetScreenElementProps, 0);
            run(0x004AC750, Front::CFunc_GetScreenElementProps, 3);
            run(0x004AC900, Front::CFunc_SetRootScreenElement, 0);
            run(0x004AC900, Front::CFunc_SetRootScreenElement, 3);
            Answer('E', p(10), kId, {0x1001});
            run(0x004AC940, Front::CFunc_GetScreenElementChildren, 10);
            run(0x004AC940, Front::CFunc_GetScreenElementChildren, 0);
            // the text commands on a block, a text element, another element
            Answer('T', p(1), 0, {At(kText)});
            Answer('T', p(2), 0, {At(kText)});
            Answer('L', p(1), 0xB8C08F55, {1});
            for (int n : {1, 2, 0}) {
                run(0x004AD710, Front::CFunc_TextElementConcatenate, n);
                run(0x004AD800, Front::CFunc_TextElementBackspace, n);
                run(0x004AD8D0, Front::CFunc_GetTextElementString, n);
                run(0x004AD9C0, Front::CFunc_GetTextElementLength, n);
            }
            *Ptr<uint32_t>(ElemAt(4) + 0x178) = 0;
            run(0x004AD8D0, Front::CFunc_GetTextElementString, 2);
            run(0x004ADA80, Front::CFunc_GetScreenElementPosition, 0);
            // the selected index: none, by number, first, last; exists
            Answer('E', p(11), kId, {0x1001});
            auto* menu = reinterpret_cast<Front::CBaseMenu*>(E(1));
            for (int k = 0; k < 6; ++k) {
                g_answers.erase({'I', Rel(p(11)), 0});
                g_answers.erase({'L', Rel(p(11)), Crc::GenerateCRCFromString("first")});
                g_answers.erase({'L', Rel(p(11)), Crc::GenerateCRCFromString("last")});
                menu->m_selectedIndex = k == 0 ? -1 : k == 5 ? 2 : k - 1;
                if (k == 1 || k == 2)
                    Answer('I', p(11), 0, {1});
                if (k == 3)
                    Answer('L', p(11), Crc::GenerateCRCFromString("first"), {1});
                if (k >= 4)
                    Answer('L', p(11), Crc::GenerateCRCFromString("last"), {1});
                run(0x004ADB20, Front::CFunc_MenuSelectedIndexIs, 11);
            }
            run(0x004ADC10, Front::CFunc_ScreenElementExists, 0);
            run(0x004ADC10, Front::CFunc_ScreenElementExists, 3);
        });

        RunBoth("screen element commands: multiple text elements", [&](bool original) {
            CfuncWorld(original);
            auto p = [](int n) { return At(kParams + 0x40 * n); };
            // a string; an array of strings; narrow and wide lines; a long word
            CopyText(Ptr<char>(kText), "the quick brown fox jumps over the lazy dog again and again");
            CopyText(Ptr<char>(kText + 0x80), "second part here");
            Answer('T', p(0), 0xC4745838, {At(kText)});
            Answer('K', p(0), 0x2F6BF72D, {0xF0F0});
            Answer('F', p(0), 0x73E5BAD0, {Bits(150.0f)});
            uint32_t array = MakeArray(0, Script::ESYMBOLTYPE_STRING, {At(kText), At(kText + 0x80)});
            Answer('S', p(1), 0xC4745838, {array});
            Answer('K', p(1), 0x2F6BF72D, {0xF0F0});
            Answer('F', p(1), 0x73E5BAD0, {Bits(95.0f)});
            // a line end before the array's next string (no space added); a first word wider than the width; exactly
            // 0x60 characters; the last word starting a line (lost, as the original)
            auto text = [](int k, const char* t) {
                CopyText(Ptr<char>(0x7800 + 0x100 * k), t);
                return At(0x7800 + 0x100 * k);
            };
            uint32_t breaks = MakeArray(1, Script::ESYMBOLTYPE_STRING, {text(0, "one two\\n"), text(1, "three four")});
            Answer('S', p(2), 0xC4745838, {breaks});
            Answer('F', p(2), 0x73E5BAD0, {Bits(400.0f)});
            uint32_t wide = MakeArray(2, Script::ESYMBOLTYPE_STRING, {text(2, "supercalifragilistic word"), text(3, "end")});
            Answer('S', p(3), 0xC4745838, {wide});
            Answer('F', p(3), 0x73E5BAD0, {Bits(95.0f)});
            Answer('T', p(4), 0xC4745838,
                   {text(4, "aaaaaaaaa aaaaaaaaa aaaaaaaaa aaaaaaaaa aaaaaaaaa aaaaaaaaa aaaaaaaaa aaaaaaaaa aaaaaaaaa bbbbb cc")});
            Answer('F', p(4), 0x73E5BAD0, {Bits(1000.0f)});
            Answer('T', p(5), 0xC4745838, {text(5, "aaaa bbbb")});
            Answer('F', p(5), 0x73E5BAD0, {Bits(60.0f)});
            for (int n = 2; n < 6; ++n)
                Answer('K', p(n), 0x2F6BF72D, {0xF0F0});
            for (int n : {0, 1, 2, 3, 4, 5}) {
                for (float w : {9.0f, 2.0f}) {
                    g_wordWidth = w;
                    Rec(0x520, {static_cast<uint32_t>(Run(original, 0x004ADC80, Front::CFunc_CreateMultipleTextElements,
                                                          Ptr<CStruct>(kParams + 0x40 * n), nullptr)) & 0xFF});
                }
            }
            MarkAll(40);
        });
    }
    RestoreAll(g_saved);
}

} // namespace

SELFTEST(Early, TestScreenElementCfuncs, FeTest::Cleanup);
