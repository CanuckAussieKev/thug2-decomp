// The network manager's list types' destructors. See gamenet_lists.h.

#include "engine/net/gamenet_lists.h"

#include "engine/core/manager.h"
#include "engine/core/code_address.h"

namespace online {

static VtableSlot g_player_node_vtable_slots[] = {&PlayerNode::teardown};
void** const kPlayerNodeVtable = reinterpret_cast<void**>(g_player_node_vtable_slots);
static VtableSlot g_new_player_node_vtable_slots[] = {&NewPlayerNode::teardown};
void** const kNewPlayerNodeVtable = reinterpret_cast<void**>(g_new_player_node_vtable_slots);
static VtableSlot g_server_node_vtable_slots[] = {&ServerNode::teardown};
void** const kServerNodeVtable = reinterpret_cast<void**>(g_server_node_vtable_slots);
static VtableSlot g_queued_script_node_vtable_slots[] = {&QueuedScriptNode::teardown};
void** const kQueuedScriptNodeVtable = reinterpret_cast<void**>(g_queued_script_node_vtable_slots);
static VtableSlot g_queued_script_vtable_slots[] = {&PendingScriptItem::destroy_record};
void** const kQueuedScriptVtable = reinterpret_cast<void**>(g_queued_script_vtable_slots);
static VtableSlot g_player_head_vtable_slots[] = {&PlayerNode::destroy_head};
void** const kPlayerHeadVtable = reinterpret_cast<void**>(g_player_head_vtable_slots);
static VtableSlot g_new_player_head_vtable_slots[] = {&NewPlayerNode::destroy_head};
void** const kNewPlayerHeadVtable = reinterpret_cast<void**>(g_new_player_head_vtable_slots);
static VtableSlot g_server_head_vtable_slots[] = {&ServerNode::destroy_head};
void** const kServerHeadVtable = reinterpret_cast<void**>(g_server_head_vtable_slots);

namespace {

void dealloc(void* memory) { memory::Hub::get_pull()->dealloc(memory); }

} // namespace

// 0x005366A0
void PlayerNode::destroy_body() { unlink_as(kPlayerNodeVtable); }

// 0x00536760
PlayerNode* PlayerNode::teardown(unsigned int flags)
{
    unlink_as(kPlayerNodeVtable);
    if (flags & 1)
        dealloc(this);
    return this;
}

// 0x00536CE0
PlayerNode* PlayerNode::destroy_head(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        dealloc(this);
    return this;
}

// 0x005366E0
void NewPlayerNode::destroy_body() { unlink_as(kNewPlayerNodeVtable); }

// 0x005367E0
NewPlayerNode* NewPlayerNode::teardown(unsigned int flags)
{
    unlink_as(kNewPlayerNodeVtable);
    if (flags & 1)
        dealloc(this);
    return this;
}

// 0x00536D10
NewPlayerNode* NewPlayerNode::destroy_head(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        dealloc(this);
    return this;
}

// 0x005366C0
void ServerNode::destroy_body() { unlink_as(kServerNodeVtable); }

// 0x005367A0
ServerNode* ServerNode::teardown(unsigned int flags)
{
    unlink_as(kServerNodeVtable);
    if (flags & 1)
        dealloc(this);
    return this;
}

// 0x00536D40
ServerNode* ServerNode::destroy_head(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        dealloc(this);
    return this;
}

// 0x00536700
void QueuedScriptNode::destroy_body() { unlink_as(kQueuedScriptNodeVtable); }

// 0x00536720
QueuedScriptNode* QueuedScriptNode::teardown(unsigned int flags)
{
    unlink_as(kQueuedScriptNodeVtable);
    if (flags & 1)
        dealloc(this);
    return this;
}

// 0x005371F0
PendingScriptItem* PendingScriptItem::destroy_record(unsigned int flags)
{
    destroy_body();
    if (flags & 1)
        dealloc(this);
    return this;
}

} // namespace online
