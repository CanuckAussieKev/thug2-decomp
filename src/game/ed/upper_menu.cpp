// The park editor's piece menu.

#include "game/ed/piece_menu.h"

#include "game/core/mem.h"
#include "game/script/cscript.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Ed {

namespace {

void RunMenuScript(uint32_t script, Script::CStruct* params = nullptr)
{
    Script::RunScript(script, params, nullptr, false, nullptr);
}

} // namespace

CUpperMenuManager* CUpperMenuManager::sInstance() { return Instance(); }

void CUpperMenuManager::Create()
{
    if (CUpperMenuManager::Instance())
        return;
    Mem::PushContext(Mem::BottomUpHeap());
    auto* menu = static_cast<CUpperMenuManager*>(Mem::New(sizeof(CUpperMenuManager)));
    if (menu) {
        menu->mp_set = nullptr;
        menu->m_selected = 0;
        menu->m_setNumber = 0;
        menu->mp_manager = CParkManager::sInstance();
    }
    CUpperMenuManager::Instance() = menu;
    Mem::PopContext();
}

void CUpperMenuManager::Enable()
{
    RunMenuScript(0x4693636A); // parked_make_piece_menu
    RunMenuScript(0xED885B50); // parked_make_set_menu
    for (int i = 0; i < mp_manager->m_numSets; ++i) {
        CPieceSet& set = mp_manager->m_sets[i];
        if (!set.m_visible)
            continue;
        Script::CStruct params;
        params.Init();
        params.AddString(0x341B6370, set.m_name); // set_name
        RunMenuScript(0xA4F522E9, &params);       // parked_make_set_menu_item
        params.Clear();
    }
    mp_set = nullptr;
}

void CUpperMenuManager::Disable()
{
    RunMenuScript(0x5C7A3276); // parked_destroy_piece_menu
    RunMenuScript(0x6BF4AA3D); // parked_destroy_set_menu
    mp_set = nullptr;
}

} // namespace Ed

using Ed::CUpperMenuManager;

REPLACE(0x00595570, &CUpperMenuManager::sInstance);
REPLACE(0x00595580, &CUpperMenuManager::Disable);
REPLACE(0x00597400, &CUpperMenuManager::Create);
REPLACE(0x00597490, &CUpperMenuManager::Enable);
