// The network manager's list types' destructors. See gamenet_lists.h.

#include "game/net/gamenet_lists.h"

#include "game/core/manager.h"
#include "game/core/code_address.h"

namespace GameNet {

static VtableSlot kPlayerNodeVtable_slots[] = {&PlayerNode::Destroy};
void** const kPlayerNodeVtable = reinterpret_cast<void**>(kPlayerNodeVtable_slots);
static VtableSlot kNewPlayerNodeVtable_slots[] = {&NewPlayerNode::Destroy};
void** const kNewPlayerNodeVtable = reinterpret_cast<void**>(kNewPlayerNodeVtable_slots);
static VtableSlot kServerNodeVtable_slots[] = {&ServerNode::Destroy};
void** const kServerNodeVtable = reinterpret_cast<void**>(kServerNodeVtable_slots);
static VtableSlot kQueuedScriptNodeVtable_slots[] = {&QueuedScriptNode::Destroy};
void** const kQueuedScriptNodeVtable = reinterpret_cast<void**>(kQueuedScriptNodeVtable_slots);
static VtableSlot kQueuedScriptVtable_slots[] = {&QueuedScript::DestroyRecord};
void** const kQueuedScriptVtable = reinterpret_cast<void**>(kQueuedScriptVtable_slots);
static VtableSlot kPlayerHeadVtable_slots[] = {&PlayerNode::DestroyHead};
void** const kPlayerHeadVtable = reinterpret_cast<void**>(kPlayerHeadVtable_slots);
static VtableSlot kNewPlayerHeadVtable_slots[] = {&NewPlayerNode::DestroyHead};
void** const kNewPlayerHeadVtable = reinterpret_cast<void**>(kNewPlayerHeadVtable_slots);
static VtableSlot kServerHeadVtable_slots[] = {&ServerNode::DestroyHead};
void** const kServerHeadVtable = reinterpret_cast<void**>(kServerHeadVtable_slots);

namespace {

void Free(void* memory) { Mem::Manager::Get()->Free(memory); }

} // namespace

// 0x005366A0
void PlayerNode::DestroyBody() { UnlinkAs(kPlayerNodeVtable); }

// 0x00536760
PlayerNode* PlayerNode::Destroy(unsigned int flags)
{
    UnlinkAs(kPlayerNodeVtable);
    if (flags & 1)
        Free(this);
    return this;
}

// 0x00536CE0
PlayerNode* PlayerNode::DestroyHead(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Free(this);
    return this;
}

// 0x005366E0
void NewPlayerNode::DestroyBody() { UnlinkAs(kNewPlayerNodeVtable); }

// 0x005367E0
NewPlayerNode* NewPlayerNode::Destroy(unsigned int flags)
{
    UnlinkAs(kNewPlayerNodeVtable);
    if (flags & 1)
        Free(this);
    return this;
}

// 0x00536D10
NewPlayerNode* NewPlayerNode::DestroyHead(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Free(this);
    return this;
}

// 0x005366C0
void ServerNode::DestroyBody() { UnlinkAs(kServerNodeVtable); }

// 0x005367A0
ServerNode* ServerNode::Destroy(unsigned int flags)
{
    UnlinkAs(kServerNodeVtable);
    if (flags & 1)
        Free(this);
    return this;
}

// 0x00536D40
ServerNode* ServerNode::DestroyHead(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Free(this);
    return this;
}

// 0x00536700
void QueuedScriptNode::DestroyBody() { UnlinkAs(kQueuedScriptNodeVtable); }

// 0x00536720
QueuedScriptNode* QueuedScriptNode::Destroy(unsigned int flags)
{
    UnlinkAs(kQueuedScriptNodeVtable);
    if (flags & 1)
        Free(this);
    return this;
}

// 0x005371F0
QueuedScript* QueuedScript::DestroyRecord(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Free(this);
    return this;
}

} // namespace GameNet
