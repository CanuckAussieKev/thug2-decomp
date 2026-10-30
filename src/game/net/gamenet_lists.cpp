// The network manager's list types' destructors. See gamenet_lists.h.

#include "game/net/gamenet_lists.h"

#include "game/core/manager.h"
#include "hook/hook.h"
#include "hook/game_code.h"

namespace GameNet {

GAME_VTABLE(kPlayerNodeVtable, 0x0064C9E8, &PlayerNode::Destroy);
GAME_VTABLE(kNewPlayerNodeVtable, 0x0064C9E4, &NewPlayerNode::Destroy);
GAME_VTABLE(kServerNodeVtable, 0x0064C9EC, &ServerNode::Destroy);
GAME_VTABLE(kQueuedScriptNodeVtable, 0x0064C8EC, &QueuedScriptNode::Destroy);
GAME_VTABLE(kQueuedScriptVtable, 0x0064C9F8, &QueuedScript::DestroyRecord);
GAME_VTABLE(kPlayerHeadVtable, 0x0064C9F0, &PlayerNode::DestroyHead);
GAME_VTABLE(kNewPlayerHeadVtable, 0x0064C9F4, &NewPlayerNode::DestroyHead);
GAME_VTABLE(kServerHeadVtable, 0x0064C9FC, &ServerNode::DestroyHead);

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

REPLACE(0x005366A0, &PlayerNode::DestroyBody);
REPLACE(0x00536760, &PlayerNode::Destroy);
REPLACE(0x00536CE0, &PlayerNode::DestroyHead);
REPLACE(0x005366E0, &NewPlayerNode::DestroyBody);
REPLACE(0x005367E0, &NewPlayerNode::Destroy);
REPLACE(0x00536D10, &NewPlayerNode::DestroyHead);
REPLACE(0x005366C0, &ServerNode::DestroyBody);
REPLACE(0x005367A0, &ServerNode::Destroy);
REPLACE(0x00536D40, &ServerNode::DestroyHead);
REPLACE(0x00536700, &QueuedScriptNode::DestroyBody);
REPLACE(0x00536720, &QueuedScriptNode::Destroy);
REPLACE(0x005371F0, &QueuedScript::DestroyRecord);

} // namespace GameNet
