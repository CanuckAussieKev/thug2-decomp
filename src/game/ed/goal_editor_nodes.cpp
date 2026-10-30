// The goal editor's nodes added to the level's node array (Ed::AddGoalEditorNodes): see goal_editor.h. From the
// disassembly of 0x004E8F50-0x004E93B7.

#include <cstdint>
#include "hook/game_code.h"

#include "game/core/crc.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/ed/goal_editor.h"
#include "game/script/node_manager.h"
#include "game/script/nodes.h"
#include "game/script/struct.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Ed {

namespace {

template <typename... Args>
int Sprintf(char* out, const char* format, Args... args)
{
    return GameFunction<int(__cdecl*)(char*, const char*, ...)>(0x006234FE)(out, format, args...);
}

} // namespace

// 0x004E8F50
void __cdecl AddGoalEditorNodes(Script::CArray* nodes, int* index, Mem::Heap* heap)
{
    Dbg::Printf("M:%s:%i: %s\n", GAME_TEXT(0x0064B5E8, "mponents\\GoalEditorComponent.cpp"), 0x152, "Inserting Goal Editor Nodes");
    bool inPlace = true;
    uint32_t arrayName = 0;
    SkateScript::CNodeManager* manager = nullptr;
    int count = 0;
    if (!nodes) {
        // Added to the node manager's last array, grown for them, and hashed there.
        nodes = reinterpret_cast<Script::CArray*>(SkateScript::GetNodeOwnerValue58());
        manager = SkateScript::NodeManagerInstance();
        arrayName = SkateScript::GetLastNodeArrayName();
        inPlace = false;
        count = static_cast<int>(nodes->m_size);
        index = &count;
        if (!count)
            nodes->SetSizeAndType(0x82, Script::ESYMBOLTYPE_STRUCTURE);
        else
            nodes->Resize(nodes->m_size + 0x82);
    }
    Mem::Manager::Get()->PushContext(reinterpret_cast<Mem::Allocator*>(heap));
    char name[100];
    for (int goal = 0; goal < 10; ++goal) {
        // The pro, the restart, then eleven objects.
        for (int which = 0; which < 13; ++which) {
            Script::CStruct* node;
            if (inPlace) {
                node = reinterpret_cast<Script::CStruct*>(nodes->GetElement(static_cast<uint32_t>(*index)));
            } else {
                node = Script::NewStruct();
                node = node ? node->Init() : nullptr;
            }
            static const uint32_t kKinds[3] = {0x8F0F67D7, 0xC9119673, 0x000B52B3};
            node->AppendStructureIfAny(Script::GetStructure(kKinds[which < 2 ? which : 2]));
            name[0] = '\0';
            if (which == 0)
                Sprintf(name, "TRG_EditedGoal%d_Pro", goal);
            else if (which == 1)
                Sprintf(name, "TRG_EditedGoal%d_Restart", goal);
            else
                Sprintf(name, "TRG_EditedGoal%d_Object_%d", goal, which - 2);
            node->AddChecksum(0xA1DC81F9, Crc::GenerateCRCFromString(name));
            node->AddInteger(0xE50D6573, *index);
            if (!inPlace) {
                nodes->SetElement(static_cast<uint32_t>(*index), reinterpret_cast<uint32_t>(node));
                manager->HashNodeIn(arrayName, static_cast<uint32_t>(*index), reinterpret_cast<uint32_t>(node));
            }
            ++*index;
        }
    }
    Mem::Manager::Get()->PopContext();
}

} // namespace Ed

REPLACE(0x004E8F50, Ed::AddGoalEditorNodes);
