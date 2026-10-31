// The screen element manager's element creation and the screen element script commands (CFunctions):
// ScreenElementSystemInit, CreateScreenElement, DestroyScreenElement, SetScreenElementProps, DoScreenElementMorph,
// RunScriptOnScreenElement, SetScreenElementLock, GetScreenElementProps / Dims / Position / Children,
// SetRootScreenElement, the text element commands, MenuSelectedIndexIs, ScreenElementExists,
// CreateMultipleTextElements.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/str.h"
#include "game/front/element3d.h"
#include "game/front/menu_element.h"
#include "game/front/screen_element_cfuncs.h"
#include "game/front/screen_element_manager.h"
#include "game/front/scrolling_menu.h"
#include "game/front/sprite_element.h"
#include "game/front/text_block.h"
#include "game/front/text_element.h"
#include "game/gfx/font.h"
#include "game/obj/tracker.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "game/gfx/engine_geom.h"

namespace MenuSystem {

uint32_t g_unassignedId{0xFFFFFFFF};
namespace {

using Scripts::CScript;
using Scripts::CStruct;

// Names (checksums).
constexpr uint32_t kId = 0x40C698AF, kType = 0x7321A8D6, kHeap = 0x42D6DD1E, kPriority = 0x9D5923D8;
constexpr uint32_t kSkaterIndex = 0x6E9C34F7, kRootWindow = 0x56A1EAE3, kDontAssert = 0x3D92465E;
constexpr uint32_t kAngleX = 0x0AFFD09D, kAngleY = 0x7DF8E00B, kAngleZ = 0xE4F1B1B1, kChildren = 0x5E684E45;
constexpr uint32_t kCallback = 0x86068BD9, kParamsName = 0x7031F10C, kCallbackParams = 0xE6CF88C4;
constexpr uint32_t kOff = 0xD443A2BC, kWidth = 0x73E5BAD0, kHeight = 0x0AB21AF0, kLength = 0xFE82614D;
constexpr uint32_t kLastLine = 0xB8C08F55, kEnforceMaxWidth = 0x27E7A420, kString = 0x61414D56;
constexpr uint32_t kScreenElementPos = 0x062A4596, kText = 0xC4745838, kFont = 0x2F6BF72D;
// Element types.
constexpr uint32_t kContainerElement = 0x5B9DA842, kHScrollingMenu = 0x2900E3D0, kTextureElement = 0x118F6383;
constexpr uint32_t kVMenu = 0x130EF802, kTextBlockElement = 0x40D92263, kTextElement = 0x5200DFB6;
constexpr uint32_t kWindowElement = 0xA13ADBB7, kElement3D = 0x82C59D78, kVScrollingMenu = 0x84298A3C;
constexpr uint32_t kSpriteElement = 0xB12B510A, kHMenu = 0xCCDED1E1;
// Heaps (the heap parameter).
constexpr uint32_t kHeapNetwork = 0x9F7B7843, kHeapTopDown = 0x477FC6DE, kHeap3C84A59 = 0x03C84A59;
constexpr uint32_t kHeapDefault = 0x1CA1FF20, kHeapSkaterGeom = 0x39FB63CC, kHeapSkater = 0x5B8AB877;
constexpr uint32_t kHeapDebug = 0x935AB858, kHeapScript = 0xE37E78C5, kHeapInternetBottomUp = 0xBAA81175;
constexpr uint32_t kHeapBottomUp = 0xC80BF12D, kHeapInternetTopDown = 0xE3F81B18, kHeapSkaterInfo = 0xEABD217B;

CUiTree* Manager() { return CUiTree::Instance(); }

uint32_t UnassignedId() { return g_unassignedId; }

// The element classes still in game code (thiscall constructors; the memory zeroed by Calloc).
void* NewMemory(unsigned int size) { return MemSys::AllocZeroed(size); }
template <typename T>
CUiWidget* Made(T* element) { return element; }

void VSetProperties(CUiWidget* e, CStruct* params)
{
    reinterpret_cast<void(__thiscall*)(CUiWidget*, CStruct*)>(e->m_vtable[1])(e, params);
}
bool VCallMemberFunction(CUiWidget* e, uint32_t name, CStruct* params, CScript* script)
{
    return reinterpret_cast<bool(__thiscall*)(CUiWidget*, uint32_t, CStruct*, CScript*)>(e->m_vtable[3])(
        e, name, params, script);
}
void VGetProperties(CUiWidget* e, CStruct* out)
{
    reinterpret_cast<void(__thiscall*)(CUiWidget*, CStruct*)>(e->m_vtable[8])(e, out);
}
void VSetMorph(CUiWidget* e, CStruct* params)
{
    reinterpret_cast<void(__thiscall*)(CUiWidget*, CStruct*)>(e->m_vtable[9])(e, params);
}

// The text elements' pieces.
CParagraphWidget* TextBlock(CUiWidget* e) { return static_cast<CParagraphWidget*>(e); }
CLabelWidget* Text(CUiWidget* e) { return static_cast<CLabelWidget*>(e); }
bool TextBlockConcatenate(CUiWidget* e, const char* text, bool enforceMaxWidth, bool lastLine)
{
    return TextBlock(e)->JoinOnto(text, enforceMaxWidth, lastLine);
}
bool TextConcatenate(CUiWidget* e, const char* text) { return Text(e)->JoinOnto(text); }
bool TextBlockBackspace(CUiWidget* e) { return TextBlock(e)->Backspace(); }
bool TextBackspace(CUiWidget* e) { return Text(e)->Backspace(); }
bool TextBlockString(CUiWidget* e, char* out, int size) { return TextBlock(e)->GetText(out, size); }
int TextBlockLength(CUiWidget* e) { return TextBlock(e)->GetLength(); }
int TextLength(CUiWidget* e) { return Text(e)->GetLength(); }

// A font (0x0048D760, cdecl) and the size of a string in it (0x0048D060, thiscall).
void* GetFont(uint32_t name) { return Engine3D::GetFont(name); }
void ReadStringParam(void* font, const char* text, float* width, float* height)
{
    reinterpret_cast<void(__thiscall*)(void*, const char*, float*, float*)>(&Engine3D::VirtualSlot8Jump)(font, text, width, height);
}

} // namespace

// --- creation -------------------------------------------------------------------------------------------------

// 0x004ACB70: an element of `type` (unknown types: none), on the heap the parameters name (none given: the front
// end's; Default: the current one), its id set, registered, its properties and morph from `params`, a pad-event
// priority if given.
ElementPtr* CUiTree::CreateElement(ElementPtr* out, uint32_t type, uint32_t id, Scripts::CStruct* params)
{
    ElementPtr element;
    MemSys::Manager* memory = MemSys::Manager::Get();
    bool heapPushed = false;
    uint32_t heap = 0;
    bool pushHeap = true;
    MemSys::Allocator* chosen = memory->m_frontEndHeap;
    if (params->ReadHashValue(kHeap, &heap, 0)) {
        int index;
        switch (heap) {
        case kHeapNetwork:
            chosen = memory->m_networkHeap;
            break;
        case kHeapTopDown:
            chosen = memory->m_topDownHeap;
            break;
        case kHeap3C84A59:
            chosen = reinterpret_cast<MemSys::Allocator*>(memory->m_unknown158[1]);
            break;
        case kHeapDefault:
            pushHeap = false;
            break;
        case kHeapSkaterGeom:
            params->FetchIntValue(kSkaterIndex, &index, 0);
            chosen = memory->m_skaterGeomHeaps[index];
            break;
        case kHeapSkater:
            params->FetchIntValue(kSkaterIndex, &index, 0);
            chosen = memory->m_skaterHeaps[index];
            break;
        case kHeapDebug:
            chosen = memory->m_debugHeap;
            break;
        case kHeapScript:
            chosen = memory->m_scriptHeap;
            break;
        case kHeapInternetBottomUp:
            chosen = memory->m_internetBottomUpHeap;
            break;
        case kHeapBottomUp:
            chosen = memory->m_bottomUpHeap;
            break;
        case kHeapInternetTopDown:
            chosen = memory->m_internetTopDownHeap;
            break;
        case kHeapSkaterInfo:
            chosen = memory->m_skaterInfoHeap;
            break;
        default:
            break;
        }
    }
    if (pushHeap) {
        memory->EnterHeapScope(chosen);
        heapPushed = true;
    }
    void* m;
    switch (type) {
    case kContainerElement:
        m = NewMemory(sizeof(CGroupWidget));
        element.Set(m ? static_cast<CGroupWidget*>(m)->Construct() : nullptr);
        break;
    case kHScrollingMenu:
        m = NewMemory(sizeof(CScrollingMenu));
        element.Set(m ? static_cast<CScrollingMenu*>(m)->ConstructHorizontal() : nullptr);
        break;
    case kTextureElement:
        m = NewMemory(sizeof(CTextureElement));
        element.Set(m ? static_cast<CTextureElement*>(m)->Construct() : nullptr);
        break;
    case kVMenu:
        m = NewMemory(sizeof(CMenuBase));
        element.Set(m ? static_cast<CMenuBase*>(m)->ConstructVMenu() : nullptr);
        break;
    case kTextBlockElement:
        m = NewMemory(0x1C4);
        element.Set(m ? static_cast<CParagraphWidget*>(m)->Construct() : nullptr);
        break;
    case kTextElement:
        m = NewMemory(0x1B0);
        element.Set(m ? static_cast<CLabelWidget*>(m)->Construct() : nullptr);
        break;
    case kWindowElement:
        m = NewMemory(sizeof(CPanelWidget));
        element.Set(m ? static_cast<CPanelWidget*>(m)->Construct() : nullptr);
        break;
    case kElement3D:
        m = NewMemory(sizeof(CModelWidget));
        element.Set(m ? static_cast<CModelWidget*>(m)->Construct() : nullptr);
        break;
    case kVScrollingMenu:
        m = NewMemory(sizeof(CScrollingMenu));
        element.Set(m ? static_cast<CScrollingMenu*>(m)->ConstructVertical() : nullptr);
        break;
    case kSpriteElement:
        m = NewMemory(sizeof(CImageWidget));
        element.Set(m ? static_cast<CImageWidget*>(m)->Construct() : nullptr);
        break;
    case kHMenu:
        m = NewMemory(sizeof(CMenuBase));
        element.Set(m ? static_cast<CMenuBase*>(m)->ConstructHMenu() : nullptr);
        break;
    default:
        break;
    }
    element->SetID(id);
    VirtualRegisterObject(element.m_ownerObject);
    VSetProperties(element.m_ownerObject, params);
    VSetMorph(element.m_ownerObject, params);
    int priority = 0;
    if (params->FetchIntValue(kPriority, &priority, 0))
        PushPriority(id, static_cast<uint32_t>(priority));
    if (heapPushed)
        memory->LeaveHeapScope();
    out->m_preceding = out->m_following = nullptr;
    out->m_ownerObject = nullptr;
    out->Set(element.m_ownerObject);
    return out;
}

// --- the script commands --------------------------------------------------------------------------------------

// 0x004AAD90: the root window (on the front end's heap), id root_window, registered.
bool __cdecl CFunc_ScreenElementSystemInit(Scripts::CStruct*, Scripts::CScript*)
{
    MemSys::Manager* memory = MemSys::Manager::Get();
    memory->EnterHeapScope(memory->m_frontEndHeap);
    void* m = MemSys::AllocZeroed(sizeof(CPanelWidget));
    CUiWidget* root = m ? static_cast<CPanelWidget*>(m)->Construct() : nullptr;
    MemSys::Manager::Get()->LeaveHeapScope();
    root->SetID(kRootWindow);
    Manager()->VirtualRegisterObject(root);
    return true;
}

// 0x004AD240: the element made; its id into the script's parameters.
bool __cdecl CFunc_CreateScreenElement(Scripts::CStruct* params, Scripts::CScript* script)
{
    uint32_t type;
    params->ReadHashValue(kType, &type, 0);
    uint32_t id = UnassignedId();
    params->ReadHashValue(kId, &id, 0);
    ElementPtr element;
    Manager()->CreateElement(&element, type, id, params);
    script->m_params->PutNameKey(kId, element->m_id);
    return true;
}

// 0x004AD300: dont_recurse (just the element), preserve_parent (keep the element itself).
bool __cdecl CFunc_DestroyScreenElement(Scripts::CStruct* params, Scripts::CScript* script)
{
    CUiTree* manager = Manager();
    uint32_t id;
    if (!manager->ResolveId(params, kId, &id))
        return false;
    int recurse = params->HasBareFlag("dont_recurse") ? 0 : 1; // dont_recurse
    int keep = params->HasBareFlag("preserve_parent") ? 1 : 0;    // preserve_parent
    if (recurse || !keep)
        manager->DestroyElement(id, recurse, keep, script);
    return true;
}

// 0x004AD370: a member function of the element, else a script spawned on it (with callback / Params /
// callback_params) and run once.
bool __cdecl CFunc_RunScriptOnScreenElement(Scripts::CStruct* params, Scripts::CScript* script)
{
    ElementPtr element;
    Manager()->ResolveElement(&element, params, kId, 1);
    uint32_t name = 0;
    params->ReadHashValue(0u, &name, true);
    uint32_t callback = 0;
    params->ReadHashValue(kCallback, &callback, 0);
    CStruct* scriptParams = nullptr;
    params->ReadSubStruct(kParamsName, &scriptParams, 0);
    CStruct* callbackParams = nullptr;
    params->ReadSubStruct(kCallbackParams, &callbackParams, 0);
    Scripts::CSymbolTableEntry* symbol = Scripts::ResolveSymbol(name);
    if (symbol && symbol->type == Scripts::QBVAL_NATIVE_METHOD) {
        VCallMemberFunction(element.m_ownerObject, name, scriptParams, script);
    } else if (CScript* spawned = Scripts::SpawnScript(name, scriptParams, callback, callbackParams, 0, 0, false,
                                                      false, false, false)) {
        spawned->m_object.Set(element.m_ownerObject);
        spawned->Update();
    }
    return true;
}

// 0x004AD4C0
bool __cdecl CFunc_SetScreenElementProps(Scripts::CStruct* params, Scripts::CScript*)
{
    ElementPtr element;
    Manager()->ResolveElement(&element, params, kId, 1);
    if (element)
        VSetProperties(element.m_ownerObject, params);
    return true;
}

// 0x004AD540 (also called directly)
bool __cdecl CFunc_DoScreenElementMorph(Scripts::CStruct* params, Scripts::CScript*)
{
    ElementPtr element;
    Manager()->ResolveElement(&element, params, kId, 1);
    VSetMorph(element.m_ownerObject, params);
    return true;
}

// 0x004AD5C0: the element's focus flag (its event sent) set, or cleared with Off.
bool __cdecl CFunc_SetScreenElementLock(Scripts::CStruct* params, Scripts::CScript*)
{
    ElementPtr element;
    Manager()->ResolveElement(&element, params, kId, 1);
    element->SetFocusSent(params->HasBareFlag(kOff) ? 0 : 1);
    return true;
}

// 0x004AD650: width / height (the size times the shown scale, truncated).
bool __cdecl CFunc_GetScreenElementDims(Scripts::CStruct* params, Scripts::CScript* script)
{
    ElementPtr element;
    Manager()->ResolveElement(&element, params, kId, 1);
    int width = static_cast<int>(static_cast<int64_t>(static_cast<double>(element->m_shown.scaleX) * element->m_span));
    int height =
        static_cast<int>(static_cast<int64_t>(static_cast<double>(element->m_shown.scaleY) * element->m_levelY));
    script->m_params->PutInt(kWidth, width);
    script->m_params->PutInt(kHeight, height);
    return true;
}

// 0x004AC750: the element's properties (an Element3d's angles too) into the script's parameters. No element:
// false.
bool __cdecl CFunc_GetScreenElementProps(Scripts::CStruct* params, Scripts::CScript* script)
{
    CUiTree* manager = Manager();
    ElementPtr element;
    uint32_t id;
    if (manager->ResolveId(params, kId, &id)) {
        ElementPtr found;
        element.Set(manager->GetElement(&found, id, 0)->m_ownerObject);
    }
    if (!element) {
        params->HasBareFlag(kDontAssert);
        return false;
    }
    VGetProperties(element.m_ownerObject, script->m_params);
    if (element->m_type == kElement3D) {
        auto* e3d = static_cast<CModelWidget*>(GameObjects::Tracker()->LookUpObject(id));
        script->m_params->PushFloat(kAngleX, e3d->m_angle[0]);
        script->m_params->PushFloat(kAngleY, e3d->m_angle[1]);
        script->m_params->PushFloat(kAngleZ, e3d->m_angle[2]);
    }
    return true;
}

// 0x004AC900
bool __cdecl CFunc_SetRootScreenElement(Scripts::CStruct* params, Scripts::CScript*)
{
    CUiTree* manager = Manager();
    uint32_t id;
    if (!manager->ResolveId(params, kId, &id))
        return false;
    manager->SetRoot(id);
    return true;
}

// 0x004AC940: the children's ids as an array `children`; none: false.
bool __cdecl CFunc_GetScreenElementChildren(Scripts::CStruct* params, Scripts::CScript* script)
{
    CUiTree* manager = Manager();
    ElementPtr element;
    uint32_t id;
    if (manager->ResolveId(params, kId, &id)) {
        ElementPtr found;
        element.Set(manager->GetElement(&found, id, 1)->m_ownerObject);
    }
    int count = element->ChildTotal();
    if (count == 0)
        return false;
    Scripts::CArray* array = Scripts::NewArray();
    if (array)
        array = array->Init();
    array->ConfigureSizeKind(count, Scripts::QBVAL_NAME);
    for (int i = 0; i < count; ++i)
        array->SetElement(i, element->FindChildAtSlot(i)->m_id);
    script->m_params->AppendArrayRef(kChildren, array);
    return true;
}

// 0x004AD710
bool __cdecl CFunc_TextElementConcatenate(Scripts::CStruct* params, Scripts::CScript*)
{
    ElementPtr element;
    Manager()->ResolveElement(&element, params, kId, 1);
    const char* text = nullptr;
    params->GetString(0u, &text, 1);
    if (element->m_type == kTextBlockElement) {
        bool lastLine = params->HasBareFlag(kLastLine);
        bool enforce = params->HasBareFlag(kEnforceMaxWidth);
        return TextBlockConcatenate(element.m_ownerObject, text, enforce, lastLine);
    }
    if (element->m_type == kTextElement)
        return TextConcatenate(element.m_ownerObject, text);
    return false;
}

// 0x004AD800
bool __cdecl CFunc_TextElementBackspace(Scripts::CStruct* params, Scripts::CScript*)
{
    ElementPtr element;
    Manager()->ResolveElement(&element, params, kId, 1);
    if (element->m_type == kTextBlockElement)
        return TextBlockBackspace(element.m_ownerObject);
    if (element->m_type == kTextElement)
        return TextBackspace(element.m_ownerObject);
    return false;
}

// 0x004AD8D0 (the result is always false)
bool __cdecl CFunc_GetTextElementString(Scripts::CStruct* params, Scripts::CScript* script)
{
    char text[0x200];
    ElementPtr element;
    Manager()->ResolveElement(&element, params, kId, 1);
    if (element->m_type == kTextBlockElement) {
        if (TextBlockString(element.m_ownerObject, text, sizeof(text)))
            script->m_params->AddString(kString, text);
    } else if (element->m_type == kTextElement) {
        if (const char* string = *reinterpret_cast<const char**>(reinterpret_cast<uint8_t*>(element.m_ownerObject) + 0x178))
            script->m_params->AddString(kString, string);
    }
    return false;
}

// 0x004AD9C0
bool __cdecl CFunc_GetTextElementLength(Scripts::CStruct* params, Scripts::CScript* script)
{
    ElementPtr element;
    Manager()->ResolveElement(&element, params, kId, 1);
    int length;
    if (element->m_type == kTextBlockElement)
        length = TextBlockLength(element.m_ownerObject);
    else if (element->m_type == kTextElement)
        length = TextLength(element.m_ownerObject);
    else
        return false;
    script->m_params->PutInt(kLength, length);
    return true;
}

// 0x004ADA80: the top left morphed to, as the pair ScreenElementPos.
bool __cdecl CFunc_GetScreenElementPosition(Scripts::CStruct* params, Scripts::CScript* script)
{
    ElementPtr element;
    Manager()->ResolveElement(&element, params, kId, 1);
    float x, y;
    element->GetTopLeft(&x, &y);
    script->m_params->InsertPair(kScreenElementPos, x, y);
    return true;
}

// 0x004ADB20: the menu's selected index against an unnamed integer, first or last. Nothing selected: false.
bool __cdecl CFunc_MenuSelectedIndexIs(Scripts::CStruct* params, Scripts::CScript*)
{
    ElementPtr element;
    Manager()->ResolveElement(&element, params, kId, 1);
    auto* menu = static_cast<CMenuBase*>(element.m_ownerObject);
    int selected = menu->m_selectedIndex;
    if (selected == -1)
        return false;
    int index;
    if (params->FetchIntValue(0u, &index, 0))
        return index == selected;
    if (params->HasBareFlag("first")) // first
        return selected == 0;
    if (params->HasBareFlag("last")) // last
        return selected == menu->ChildTotal() - 1;
    return false;
}

// 0x004ADC10
bool __cdecl CFunc_ScreenElementExists(Scripts::CStruct* params, Scripts::CScript*)
{
    ElementPtr element;
    Manager()->ResolveElement(&element, params, kId, 0);
    return static_cast<bool>(element);
}

// 0x004ADC80: text elements made from `text` (a string, or an array of strings run together with spaces), one
// per line of at most `width` in `font` (and fewer than 0x60 characters), each with the other parameters. (A last
// word that starts a new line is lost, as in the original.)
bool __cdecl CFunc_CreateMultipleTextElements(Scripts::CStruct* params, Scripts::CScript*)
{
    // (the word buffer right before the line's, as on the original's stack: an empty line's "last character" is
    // the word buffer's last)
    struct {
        char word[0x60];
        char line[0x1F4];
    } buffers;
    char* word = buffers.word;
    char* line = buffers.line;
    uint32_t id = 0;
    params->ReadHashValue(kId, &id, 0);
    CUiTree* manager = Manager();
    Scripts::CArray* texts = nullptr;
    uint32_t textIndex = 0;
    const char* text = nullptr;
    if (params->GetArray(kText, &texts, 0))
        text = texts->GetString(0);
    else
        params->GetString(kText, &text, 1);
    uint32_t fontName;
    params->ReadHashValue(kFont, &fontName, 1);
    float maxWidth;
    params->GetFloat(kWidth, &maxWidth, 1);
    void* font = GetFont(fontName);
    const char* cursor = text;
    float lineWidth = 0.0f;
    int lineLength = 0;
    int previous = 0;
    auto makeElement = [&]() {
        CStruct* elementParams = Scripts::NewStruct();
        if (elementParams)
            elementParams = elementParams->Init();
        elementParams->AppendStructureIfAny(params);
        elementParams->AddString(kText, line);
        ElementPtr made;
        manager->CreateElement(&made, kTextElement, UnassignedId(), elementParams);
        made.Release();
        if (elementParams) {
            elementParams->Clear();
            Scripts::DeleteStruct(elementParams);
        }
    };
    int result;
    do {
        result = TextUtil::NextWord(word, cursor);
        if (texts && result == 2 && textIndex < texts->m_size - 1) {
            // the next string of the array, a space between
            text = texts->GetString(++textIndex);
            cursor = text;
            if (previous != 1) {
                size_t length = std::strlen(word);
                word[length] = ' ';
                word[length + 1] = 0;
            }
            result = 0;
        }
        float wordWidth, wordHeight;
        ReadStringParam(font, word, &wordWidth, &wordHeight);
        bool newLine = false;
        bool full = lineWidth > 0.0f && static_cast<double>(wordWidth) + lineWidth > maxWidth;
        if (full || previous == 1 || std::strlen(word) + lineLength >= 0x60) {
            // the line so far (without its trailing space) becomes an element
            char* last = lineLength ? &line[lineLength - 1] : &word[sizeof(buffers.word) - 1];
            if (*last == ' ')
                *last = 0;
            newLine = true;
            lineLength = 0;
            lineWidth = 0.0f;
            makeElement();
        }
        std::memcpy(line + lineLength, word, std::strlen(word) + 1);
        lineLength += static_cast<int>(std::strlen(word));
        lineWidth = static_cast<float>(static_cast<double>(wordWidth) + lineWidth);
        if (result == 2 && !newLine)
            makeElement();
        previous = result;
    } while (result != 2);
    return true;
}

} // namespace Front

using MenuSystem::CUiTree;
