// The park editor's piece menu.

#include <cstdint>

#include "game/park_editing/piece_menu.h"

#include "game/core/mem.h"
#include "game/script/cscript.h"
#include "game/script/param_struct.h"

namespace ParkEditing {

namespace {

void RunMenuScript(uint32_t script, Scripts::CStruct* params = nullptr)
{
    Scripts::LaunchScript(script, params, nullptr, false, nullptr);
}

} // namespace

CTopBarMenus* CTopBarMenus::sSingleton() { return Sole(); }

void CTopBarMenus::Fabricate()
{
    if (CTopBarMenus::Sole())
        return;
    MemSys::EnterHeapScope(MemSys::BottomUpHeap());
    auto* menu = static_cast<CTopBarMenus*>(MemSys::Alloc(sizeof(CTopBarMenus)));
    if (menu) {
        menu->mp_set = nullptr;
        menu->m_selected = 0;
        menu->m_setNumber = 0;
        menu->m_parkOwner = CParkLayout::sSingleton();
    }
    CTopBarMenus::Sole() = menu;
    MemSys::LeaveHeapScope();
}

void CTopBarMenus::TurnOn()
{
    RunMenuScript(0x4693636A); // parked_make_piece_menu
    RunMenuScript(0xED885B50); // parked_make_set_menu
    for (int i = 0; i < m_parkOwner->m_numSets; ++i) {
        CPieceFamily& set = m_parkOwner->m_sets[i];
        if (!set.m_isDisplayed)
            continue;
        Scripts::CStruct params;
        params.PrepareAll();
        params.AppendWord(0x341B6370, set.m_label); // set_name
        RunMenuScript(0xA4F522E9, &params);       // parked_make_set_menu_item
        params.Clear();
    }
    mp_set = nullptr;
}

void CTopBarMenus::TurnOffNow()
{
    RunMenuScript(0x5C7A3276); // parked_destroy_piece_menu
    RunMenuScript(0x6BF4AA3D); // parked_destroy_set_menu
    mp_set = nullptr;
}

} // namespace Ed

using ParkEditing::CTopBarMenus;
