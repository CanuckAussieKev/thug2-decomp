// The goal editor's nodes added to the level's node array (ParkEditing::AddGoalEditorNodes): see goal_editor.h. From the
// disassembly of 0x004E8F50-0x004E93B7.

#include <cstdint>

#include "game/core/checksums.h"
#include "game/core/dbg.h"
#include "game/core/manager.h"
#include "game/park_editing/goal_editor.h"
#include "game/script/node_manager.h"
#include "game/script/nodes.h"
#include "game/script/param_struct.h"
#include "game/sys/crt.h"

namespace ParkEditing {

namespace {

template <typename... Args>
int Sprintf(char* out, const char* format, Args... args)
{
    return Crt::Sprintf(out, format, args...);
}

} // namespace

// 0x004E8F50
void __cdecl AddGoalEditorNodes(Scripts::CArray* nodes, int* index, MemSys::Arena* heap)
{
    DebugOut::Printf("M:%s:%i: %s\n", "mponents\\GoalEditorComponent.cpp", 0x152, "Inserting Goal Editor Nodes");
    bool inPlace = true;
    uint32_t arrayName = 0;
    LevelNodes::CNodeManager* manager = nullptr;
    int count = 0;
    if (!nodes) {
        // Added to the node manager's last array, grown for them, and hashed there.
        nodes = reinterpret_cast<Scripts::CArray*>(LevelNodes::GetNodeOwnerValue58());
        manager = LevelNodes::NodeManagerInstance();
        arrayName = LevelNodes::GetLastNodeArrayName();
        inPlace = false;
        count = static_cast<int>(nodes->m_extent);
        index = &count;
        if (!count)
            nodes->ConfigureSizeKind(0x82, Scripts::QBVAL_STRUCT);
        else
            nodes->Rescale(nodes->m_extent + 0x82);
    }
    MemSys::Hub::Pull()->EnterHeapScope(reinterpret_cast<MemSys::MemSource*>(heap));
    char name[100];
    for (int goal = 0; goal < 10; ++goal) {
        // The pro, the restart, then eleven objects.
        for (int which = 0; which < 13; ++which) {
            Scripts::CStruct* node;
            if (inPlace) {
                node = reinterpret_cast<Scripts::CStruct*>(nodes->FetchPart(static_cast<uint32_t>(*index)));
            } else {
                node = Scripts::SpawnParamBag();
                node = node ? node->PrepareAll() : nullptr;
            }
            static const uint32_t kKinds[3] = {0x8F0F67D7, 0xC9119673, 0x000B52B3};
            node->AppendStructureIfAny(Scripts::ReadSubStruct(kKinds[which < 2 ? which : 2]));
            name[0] = '\0';
            if (which == 0)
                Sprintf(name, "TRG_EditedGoal%d_Pro", goal);
            else if (which == 1)
                Sprintf(name, "TRG_EditedGoal%d_Restart", goal);
            else
                Sprintf(name, "TRG_EditedGoal%d_Object_%d", goal, which - 2);
            node->PutNameKey(0xA1DC81F9, Checksums::HashFromText(name));
            node->PutInt(0xE50D6573, *index);
            if (!inPlace) {
                nodes->SetElement(static_cast<uint32_t>(*index), reinterpret_cast<uint32_t>(node));
                manager->HashNodeIn(arrayName, static_cast<uint32_t>(*index), reinterpret_cast<uint32_t>(node));
            }
            ++*index;
        }
    }
    MemSys::Hub::Pull()->LeaveHeapScope();
}

} // namespace Ed
