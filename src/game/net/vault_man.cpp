// The content vault and HTTP file transfers (OnlinePlay::VaultMan). See vault_man.h.

#include "game/net/vault_man.h"
#include "game/net/gamespy.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/core/crc.h"
#include "game/core/manager.h"
#include "game/core/mem.h"
#include "game/core/task.h"
#include "game/net/buddy_man.h"
#include "game/net/gamenet_handler_util.h"
#include "game/net/gamenet_manager.h"
#include "game/net/online_text.h"
#include "game/script/cscript.h"
#include "game/script/preferences.h"
#include "game/script/struct.h"
#include "game/script/struct_buffer.h"
#include "game/sys/timer.h"
#include "game/mc/save_types.h"
#include "game/core/code_address.h"
#include "game/ass/assman.h"
#include "game/net/task_vtable.h"
#include "game/sys/crt.h"

namespace OnlinePlay {

namespace {

using Detail::DeleteParams;
using Detail::NewParams;

static VtableSlot kItemVtable_slots[] = {&OnlinePlay::VaultItem::Teardown};
void** const kItemVtable = reinterpret_cast<void**>(kItemVtable_slots); // VaultItem (and a category's items head)
static VtableSlot kCategoriesHeadVtable_slots[] = {&OnlinePlay::VaultNode::DeleteCategoriesHead};
void** const kCategoriesHeadVtable = reinterpret_cast<void**>(kCategoriesHeadVtable_slots); // the vault's categories head
static VtableSlot kCategoryVtable_slots[] = {&OnlinePlay::VaultCategory::Teardown};
void** const kCategoryVtable = reinterpret_cast<void**>(kCategoryVtable_slots); // VaultCategory
static VtableSlot kItemNodeVtable_slots[] = {&OnlinePlay::VaultNode::DeleteItemNode};
void** const kItemNodeVtable = reinterpret_cast<void**>(kItemNodeVtable_slots); // ListKit::Elem<VaultItem>
static VtableSlot kCategoryNodeVtable_slots[] = {&OnlinePlay::VaultNode::DeleteCategoryNode};
void** const kCategoryNodeVtable = reinterpret_cast<void**>(kCategoryNodeVtable_slots); // ListKit::Elem<VaultCategory>
static VtableSlot kCategorySearchVtable_slots[] = {&OnlinePlay::VaultSearch::DestroyCategorySearchAndFree};
void** const kCategorySearchVtable = reinterpret_cast<void**>(kCategorySearchVtable_slots);
static VtableSlot kItemSearchVtable_slots[] = {&OnlinePlay::VaultSearch::DestroyItemSearchAndFree};
void** const kItemSearchVtable = reinterpret_cast<void**>(kItemSearchVtable_slots);

constexpr uint32_t kTimeout = 25000;     // ms without progress
constexpr uint32_t kListBufferSize = 100000;
constexpr uint32_t kDownloadFileSize = 0x14000;
constexpr int kErrorScriptResult = 11;   // ghttp's result that runs the error script instead of the fail script
// The profile that may list the uploads (DownloadDirectoryList).
constexpr int kUploadsProfile = 0x011B0C16;

// The list and download scripts (StartList, DownloadFile).
constexpr uint32_t kFailScript = 0x7335AB7F;
constexpr uint32_t kDoneScript = 0x0089990F;
constexpr uint32_t kProgressScript = 0x4B85EE64;
constexpr uint32_t kErrorScript = 0xCF040665;

Hub* NetManager() { return g_gameNetManager; }
VaultMan* Vault() { return reinterpret_cast<VaultMan*>(NetManager()->m_vaultMan); }
MemSys::Hub* RamStats() { return MemSys::Hub::Pull(); }
void PushTopDown() { RamStats()->EnterHeapScope(RamStats()->m_internetTopDownHeap); }
void PushBottomUp() { RamStats()->EnterHeapScope(RamStats()->m_internetBottomUpHeap); }
void PopHeap() { RamStats()->LeaveHeapScope(); }
TaskList::PileUp* LogicTasks() { return reinterpret_cast<TaskList::PileUp*>(reinterpret_cast<uint8_t*>(AssetStore::g_mainLoopManager) + 0x04); }

uint32_t Checksums(const char* name) { return Checksums::HashFromText(name); }
void Run(const char* script, Scripts::CStruct* params = nullptr) { Scripts::LaunchScript(script, params, nullptr, false); }
void Run(uint32_t script, Scripts::CStruct* params = nullptr) { Scripts::LaunchScript(script, params, nullptr, false, nullptr); }

// The semaphore (the threads set their request up while holding it).
void Stall(void* semaphore) { WaitForSingleObject(static_cast<HANDLE>(semaphore), INFINITE); }
void Release(void* semaphore) { ReleaseSemaphore(static_cast<HANDLE>(semaphore), 1, nullptr); }
// A thread made suspended; the manager notes its id (SetRoomId) before it starts.
HANDLE StartThread(LPTHREAD_START_ROUTINE thread, void* param)
{
    DWORD id;
    HANDLE handle = CreateThread(nullptr, 0x8000, thread, param, CREATE_SUSPENDED, &id);
    NetManager()->SetRoomId(id);
    return handle; // (never closed)
}

// GameSpy's HTTP SDK (library code, not decompiled; cdecl).
void GhttpThink() { GameSpy::HttpThink(); }
void GhttpCancelRequest(int request) { GameSpy::HttpCancelRequest(request); }
void GhttpCleanup() { GameSpy::HttpCleanup(); }
int GhttpGetEx(const char* url, const char* headers, char* buffer, uint32_t size, void* post, int throttle,
               int blocking, const void* progress, const void* completed, void* param)
{
    return GameSpy::HttpGetEx(url, headers, buffer, size, post, throttle,
                                                                       blocking, progress, completed, param);
}
void* GhttpNewPost() { return GameSpy::HttpNewPost(); }
void GhttpPostAddString(void* post, const char* name, const char* value)
{
    GameSpy::HttpPostAddString(post, name, value);
}
void GhttpPostAddFileFromMemory(void* post, const char* name, const char* buffer, uint32_t size,
                                const char* reportName, const char* contentType)
{
    GameSpy::HttpPostAddFileFromMemory(
        post, name, buffer, size, reportName, contentType);
}
int GhttpPostEx(const char* url, const char* headers, void* post, int throttle, int blocking, const void* progress,
                const void* completed, void* param)
{
    return GameSpy::HttpPostEx(url, headers, post, throttle, blocking, progress, completed, param);
}
// The GameSpy SDK's strdup (goastrdup, the game's C runtime malloc) and the game runtime's free for it.
char* GameSpyStrdup(const char* text) { return GameSpy::Strdup(text); }
void GameCrtFree(void* memory) { Crt::Dealloc(memory); }
// The GameSpy SDK's id of this machine (GOAGetUniqueID).
const char* GameSpyUniqueId() { return GameSpy::UniqueId(); }
// 0x005A8EB0: a downloaded item loaded from `buffer` by the type's script (not rewritten yet).
void LoadDownloadedItem(char* buffer, uint32_t script)
{
    MemCard::KeepLoadedContents(reinterpret_cast<const uint8_t*>(buffer), script);
}

// The folder of an item type ("levels"...; null for an unknown type).
const char* TypeFolder(uint32_t type)
{
    switch (type) {
    case kVaultParks: return "levels";
    case kVaultSkaters: return "skaters";
    case kVaultTricks: return "tricks";
    case kVaultGoals: return "goals";
    default: return nullptr;
    }
}
// The upload's file type ("cap" park, "cas" skater, "cat" trick, "cag" goal; null for an unknown type).
const char* UploadFileType(uint32_t type)
{
    switch (type) {
    case kVaultParks: return "cap";
    case kVaultSkaters: return "cas";
    case kVaultTricks: return "cat";
    case kVaultGoals: return "cag";
    default: return nullptr;
    }
}

void UnlinkListNode(void* node)
{
    auto* vtable = *static_cast<void***>(node);
    reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(vtable[0])(node, 1);
}
void InitNode(MemSys::ListNode* node, void** vtable)
{
    node->data = node;
    node->priority = 0;
    node->next = node;
    node->prev = node;
    node->vtable = vtable;
}
void SetupListSentinel(MemSys::ListNode* head, void** vtable)
{
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = vtable;
}
// `node` at the front of the list (the lists of the vault are built that way: the last line first).
void Prepend(MemSys::ListNode* head, MemSys::ListNode* node)
{
    node->prev = head;
    node->next = head->next;
    head->next->prev = node;
    head->next = node;
}
// The list walk's step: `node`'s successor, or null at the head.
MemSys::ListNode* NextNode(MemSys::ListNode* node)
{
    MemSys::ListNode* next = node->next;
    return next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
}

// The next field of a list line: the text up to the next ':' (which is cut there), and `*rest` moved past it.
// (A line with too few fields writes through null, as the original's strstr does.)
char* Prop(char** rest)
{
    char* field = *rest;
    char* end = std::strstr(field, ":");
    *end = '\0';
    *rest = end + 1;
    return field;
}
int NumberField(char** rest)
{
    char number[128];
    Text::DupFrom(number, Prop(rest));
    return std::atol(number);
}

} // namespace

// --- the list nodes, items and categories ---

// 0x0054B2D0
void VaultNode::DestroyItemNode() { m_listLink.UnlinkAs(kItemNodeVtable); }

// 0x0054B2B0
void VaultNode::DestroyCategoryNode() { m_listLink.UnlinkAs(kCategoryNodeVtable); }

// 0x0054B2F0
VaultNode* VaultNode::DeleteItemNode(unsigned int flags)
{
    DestroyItemNode();
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

// 0x0054B330
VaultNode* VaultNode::DeleteCategoryNode(unsigned int flags)
{
    DestroyCategoryNode();
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

// 0x0054B610
VaultNode* VaultNode::DeleteCategoriesHead(unsigned int flags)
{
    DestroyCategoryNode();
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

// 0x0054B570
VaultItem* VaultItem::MakeNew()
{
    InitNode(&m_listLink, kItemVtable);
    m_url[0] = '\0';
    m_label[0] = '\0';
    m_creator[0] = '\0';
    m_extent[0] = '\0';
    m_unknownCC[0] = '\0';
    m_summaryText[0] = '\0';
    m_pointsHeld = 0;
    m_numPieces = 0;
    m_numGaps = 0;
    m_numGoals = 0;
    m_styleSet = 0;
    m_todScript = 0;
    m_hasMaleBody = 0;
    m_span = 0;
    m_levelY = 0;
    return this;
}

// 0x0054B5E0
VaultItem* VaultItem::Teardown(unsigned int flags)
{
    reinterpret_cast<VaultNode*>(this)->DestroyItemNode();
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

// 0x0054B670
VaultCategory* VaultCategory::MakeNew()
{
    InitNode(&m_listLink, kCategoryVtable);
    SetupListSentinel(&m_items, kItemVtable);
    m_skip = false;
    return this;
}

// 0x0054C890
void VaultCategory::DestroyBody()
{
    m_listLink.vtable = kCategoryVtable;
    for (MemSys::ListNode* node = NextNode(&m_items); node;) {
        MemSys::ListNode* next = NextNode(node);
        UnlinkListNode(node->data);
        node = next;
    }
    m_items.UnlinkAs(kItemNodeVtable);
    m_listLink.UnlinkAs(kCategoryNodeVtable);
}

// 0x0054C980
VaultCategory* VaultCategory::Teardown(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

// 0x0054B6B0
void* VaultSearch::HeadElement(MemSys::ListNode* head)
{
    m_listLink = head;
    return FollowingItem();
}

// 0x0054B640
void* VaultSearch::FollowingItem()
{
    m_listLink = NextNode(m_listLink);
    return m_listLink ? m_listLink->data : nullptr;
}

// 0x0054A770 / 0x0054A760
void VaultSearch::DestroyCategorySearch() { m_vtable = kCategorySearchVtable; }
void VaultSearch::DestroyItemSearch() { m_vtable = kItemSearchVtable; }

// 0x0054A780
VaultSearch* VaultSearch::DestroyCategorySearchAndFree(unsigned int flags)
{
    m_vtable = kCategorySearchVtable;
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

// 0x0054B280
VaultSearch* VaultSearch::DestroyItemSearchAndFree(unsigned int flags)
{
    m_vtable = kItemSearchVtable;
    if (flags & 1)
        RamStats()->Dealloc(this);
    return this;
}

// --- the vault ---

// 0x0054C1D0
VaultMan* VaultMan::MakeNew()
{
    SetupListSentinel(&m_categories, kCategoriesHeadVtable);
    m_search.m_vtable = kCategorySearchVtable;
    m_search.m_listLink = nullptr;
    m_transfer = kNone;
    m_progress = 0;
    m_startTime = 0;
    auto* task = static_cast<TaskList::SchedulerTask*>(MemSys::AllocZeroed(0x20));
    if (task) {
        task->MakeNew(0);
        task->m_vtable = GameCode::kTaskVtable;
        reinterpret_cast<void**>(task)[6] = reinterpret_cast<void*>(&VaultTask); // +0x18 the callback
        reinterpret_cast<void**>(task)[7] = this;                                // +0x1C the data
    }
    m_task = task;
    m_semaphore = CreateSemaphoreA(nullptr, 1, 10, nullptr);
    return this;
}

// 0x0054B470
void VaultMan::DestroyBody()
{
    Release(m_semaphore);
    if (m_task) // (its scalar deleting destructor, vtable slot 2)
        reinterpret_cast<void*(__thiscall*)(TaskList::SchedulerTask*, unsigned int)>(m_task->m_vtable[2])(m_task, 1);
    m_search.m_vtable = kCategorySearchVtable;
    m_categories.UnlinkAs(kCategoryNodeVtable);
}

// 0x0054A910
void VaultMan::Download(const DownloadRequest* request)
{
    PushBottomUp();
    VaultMan* vault = Vault();
    vault->m_transfer = kDownload;
    vault->m_outcomeVal = 0;
    LogicTasks()->QueueJob(vault->m_task);
    vault->m_progress = 0;
    vault->m_startTime = FrameClock::GetTime();
    vault->m_bufferSize = request->bufferSize;
    vault->m_failScript = request->failScript;
    vault->m_doneScript = request->doneScript;
    vault->m_progressScript = request->progressScript;
    vault->m_errorScript = request->errorScript;
    vault->m_callback = request->callback;
    char* url = GameSpyStrdup(request->url);
    HANDLE thread = StartThread(&DownloadThread, url);
    Stall(vault->m_semaphore);
    ResumeThread(thread);
    Release(vault->m_semaphore);
    PopHeap();
}

// 0x0054B370
void VaultMan::StartList(const char* url)
{
    // (the original uses `url` as sprintf's format)
    std::snprintf(m_url, sizeof(m_url), "%s", url);
    m_progress = 0;
    m_startTime = FrameClock::GetTime();
    m_doneScript = kDoneScript;
    m_failScript = kFailScript;
    m_errorScript = kErrorScript;
    m_progressScript = kProgressScript;
    HANDLE thread = StartThread(&ListThread, this);
    ResumeThread(thread);
}

// 0x0054B400
void VaultMan::ListItems(VaultCategory* category)
{
    char url[128];
    std::snprintf(url, sizeof(url), "http://www.thugonline.com/%s/%s/dir_list.txt", m_folder, category->m_label);
    m_transfer = kItems;
    m_outcomeVal = 0;
    StartList(url);
}

// 0x0054B500
void VaultMan::ListCategories()
{
    m_transfer = kCategories;
    char url[128];
    std::snprintf(url, sizeof(url), "http://www.thugonline.com/%s/dir_list.txt", m_folder);
    StartList(url);
    m_outcomeVal = 0;
    LogicTasks()->QueueJob(m_task);
}

// 0x0054C130
void VaultMan::FreeDirectoryListing()
{
    for (MemSys::ListNode* node = NextNode(&m_categories); node;) {
        MemSys::ListNode* next = NextNode(node);
        UnlinkListNode(node->data);
        node = next;
    }
    GhttpCleanup();
}

// 0x0054B6E0
void __cdecl VaultMan::VaultTask(TaskList::SchedulerTask* task)
{
    auto* vault = reinterpret_cast<VaultMan*>(reinterpret_cast<void**>(task)[7]);
    if (FrameClock::GetTime() - vault->m_startTime > kTimeout) {
        if (vault->m_transfer != kNone)
            RamStats()->Dealloc(vault->m_octets);
        GhttpCancelRequest(vault->m_req);
        Run(vault->m_failScript);
        vault->m_transfer = kNone;
        vault->m_task->DropOut();
        return;
    }
    PushTopDown();
    GhttpThink();
    PopHeap();

    int transfer = vault->m_transfer;
    if (vault->m_progress < 100) {
        if (transfer == kDownload || transfer == kDownload6) {
            char text[16];
            std::snprintf(text, sizeof(text), "%d%%", vault->m_progress);
            Scripts::CStruct* params = NewParams();
            params->AppendWord(Checksums("text"), Scripts::QBVAL_STRING, text);
            Run(vault->m_progressScript, params);
            DeleteParams(params);
        }
        return;
    }
    if (transfer == kDownload || transfer == kDownload6)
        RamStats()->Dealloc(vault->m_octets);

    switch (transfer) {
    case kDownload:
    case kDownload6:
    case kUpload: {
        vault->m_task->DropOut();
        vault->m_transfer = kNone;
        if (vault->m_outcomeVal == 1) {
            Scripts::CStruct* params = NewParams();
            params->PutNameKey("type", vault->m_kindTag);
            if (transfer == kUpload)
                Run("LaunchUploadCompleteDialog", params);
            else
                Run(vault->m_doneScript, params);
            DeleteParams(params);
        } else {
            Run(vault->m_fault == kErrorScriptResult ? vault->m_errorScript : vault->m_failScript);
        }
        if (transfer == kUpload)
            RamStats()->Dealloc(vault->m_octets);
        return;
    }
    case kCategories: {
        if (vault->m_outcomeVal != 1) {
            vault->m_transfer = kNone;
            vault->m_task->DropOut();
            MemSys::Dealloc(vault->m_octets);
            Run(vault->m_failScript);
            return;
        }
        PushBottomUp();
        // (an empty list leaves the transfer as it is: it's read again next time)
        char* line = Text::Token(vault->m_octets, "\r\n");
        if (line) {
            do {
                void* memory = MemSys::AllocZeroed(sizeof(VaultCategory));
                VaultCategory* category = memory ? static_cast<VaultCategory*>(memory)->MakeNew() : nullptr;
                Prepend(&vault->m_categories, &category->m_listLink);
                Text::DupFrom(category->m_label, line);
                line = Text::Token(nullptr, "\r\n");
            } while (line);
            vault->m_category = static_cast<VaultCategory*>(vault->m_search.HeadElement(&vault->m_categories));
            if (vault->m_category->m_skip)
                vault->m_category = static_cast<VaultCategory*>(vault->m_search.FollowingItem());
            vault->m_transfer = kNone;
            MemSys::Dealloc(vault->m_octets);
            vault->ListItems(vault->m_category);
        }
        PopHeap();
        return;
    }
    case kItems: {
        PushBottomUp();
        if (vault->m_outcomeVal != 1) {
            vault->m_transfer = kNone;
            vault->m_task->DropOut();
            MemSys::Dealloc(vault->m_octets);
            Run(vault->m_failScript);
            PopHeap();
            return;
        }
        // Each line: file:name:creator[:park, trick, skater or goal fields]:...:description
        for (char* line = Text::Token(vault->m_octets, "\r\n"); line; line = Text::Token(nullptr, "\r\n")) {
            void* memory = MemSys::AllocZeroed(sizeof(VaultItem));
            VaultItem* item = memory ? static_cast<VaultItem*>(memory)->MakeNew() : nullptr;
            Prepend(&vault->m_category->m_items, &item->m_listLink);
            char* rest = line;
            char file[1024];
            Text::DupFrom(file, Prop(&rest));
            // (the original makes the URL with sprintf: no limit)
            std::snprintf(item->m_url, sizeof(item->m_url) + 0x80, "http://www.thugonline.com/%s/%s/%s",
                          vault->m_folder, vault->m_category->m_label, file);
            Text::DupFrom(item->m_label, Prop(&rest));
            Text::DupFrom(item->m_creator, Prop(&rest));
            if (vault->m_kindTag == kVaultParks) {
                Text::DupFrom(item->m_extent, Prop(&rest));
                item->m_numPieces = NumberField(&rest);
                item->m_numGaps = NumberField(&rest);
                item->m_numGoals = NumberField(&rest);
                item->m_styleSet = NumberField(&rest);
                item->m_todScript = static_cast<uint32_t>(NumberField(&rest));
            }
            if (vault->m_kindTag == kVaultTricks)
                item->m_pointsHeld = NumberField(&rest);
            if (vault->m_kindTag == kVaultSkaters)
                item->m_hasMaleBody = NumberField(&rest);
            if (vault->m_kindTag == kVaultGoals)
                item->m_numGoals = NumberField(&rest);
            Text::DupFrom(item->m_unknownCC, Prop(&rest));
            Text::DupFrom(item->m_summaryText, rest);
        }
        vault->m_transfer = kNone;
        MemSys::Dealloc(vault->m_octets);
        vault->m_category = static_cast<VaultCategory*>(vault->m_search.FollowingItem());
        if (vault->m_category && !vault->m_category->m_skip) {
            vault->ListItems(vault->m_category); // the next category's
        } else {
            // All in: the menu.
            vault->m_category = static_cast<VaultCategory*>(vault->m_search.HeadElement(&vault->m_categories));
            vault->m_task->DropOut();
            Scripts::CStruct* params = NewParams();
            params->AppendWord("category", vault->m_category->m_label);
            params->PutNameKey("type", vault->m_kindTag);
            Run("net_vault_menu_create", params);
            DeleteParams(params);
        }
        PopHeap();
        return;
    }
    case kUploads: {
        PushBottomUp();
        // Each line: file:creator:size:...:description (the whole line also makes the URL).
        for (char* line = Text::Token(vault->m_octets, "\r\n"); line; line = Text::Token(nullptr, "\r\n")) {
            void* memory = MemSys::AllocZeroed(sizeof(VaultItem));
            VaultItem* item = memory ? static_cast<VaultItem*>(memory)->MakeNew() : nullptr;
            Prepend(&vault->m_category->m_items, &item->m_listLink);
            std::snprintf(item->m_url, sizeof(item->m_url) + 0x80, "http://www.thugonline.com/%s/%s", vault->m_folder,
                          line);
            char* rest = line;
            Text::DupFrom(item->m_label, Prop(&rest));
            Text::DupFrom(item->m_creator, Prop(&rest));
            Text::DupFrom(item->m_extent, Prop(&rest));
            Text::DupFrom(item->m_unknownCC, Prop(&rest));
            Text::DupFrom(item->m_summaryText, rest);
        }
        vault->m_transfer = kNone;
        vault->m_task->DropOut();
        MemSys::Dealloc(vault->m_octets);
        vault->ListCategories();
        PopHeap();
        return;
    }
    default:
        return;
    }
}

// 0x0054A7B0
unsigned long __stdcall VaultMan::ListThread(void* param)
{
    auto* vault = static_cast<VaultMan*>(param);
    Stall(vault->m_semaphore);
    PushBottomUp();
    auto* buffer = static_cast<char*>(MemSys::Alloc(kListBufferSize));
    vault->m_octets = buffer;
    std::memset(buffer, 0, kListBufferSize);
    vault->m_req = GhttpGetEx(vault->m_url, nullptr, vault->m_octets, kListBufferSize, nullptr, 0, 0,
                                  reinterpret_cast<const void*>(&OnProgress),
                                  reinterpret_cast<const void*>(&OnTransferDone), vault);
    PopHeap();
    Release(vault->m_semaphore);
    return 0;
}

// 0x0054A860
unsigned long __stdcall VaultMan::DownloadThread(void* url)
{
    Hub* manager = NetManager();
    VaultMan* vault = Vault();
    Stall(vault->m_semaphore);
    PushBottomUp();
    uint32_t size = vault->m_bufferSize;
    auto* buffer = static_cast<char*>(MemSys::Alloc(size));
    vault->m_octets = buffer;
    int request = GhttpGetEx(static_cast<const char*>(url), nullptr, buffer, size, nullptr, 0, 0,
                             reinterpret_cast<const void*>(&OnProgress), vault->m_callback, vault);
    reinterpret_cast<VaultMan*>(manager->m_vaultMan)->m_req = request;
    PopHeap();
    Release(reinterpret_cast<VaultMan*>(manager->m_vaultMan)->m_semaphore);
    GameCrtFree(url);
    return 0;
}

// 0x0054AAC0
unsigned long __stdcall VaultMan::UploadThread(void* param)
{
    auto* vault = static_cast<VaultMan*>(param);
    OptionStore::Preferences* prefs = NetManager()->GetOnlineSettings();
    const char* networkId;
    prefs->FetchSetting(Checksums("network_id"))->FetchText("ui_string", &networkId, 1);
    Stall(vault->m_semaphore);
    PushBottomUp();
    void* post = GhttpNewPost();
    char number[32];
    char fileName[0x80] = {}; // (left unset in the original for an unknown type)
    char* buffer = nullptr;
    uint32_t size = 0;
    // The item's data written to a buffer (from the current heap: MemSys::Alloc for a trick, its steps inlined for the
    // others).
    auto writeData = [&]() {
        size = Scripts::MeasureBufferBytes(vault->m_content, kListBufferSize);
        buffer = static_cast<char*>(MemSys::Alloc(size));
        Scripts::EmitIntoBuffer(vault->m_content, reinterpret_cast<uint8_t*>(buffer), size, 1, nullptr);
        uint32_t checksum = Checksums::GenerateCRCCaseless(buffer, static_cast<int>(size));
        Text::DupFrom(fileName, vault->m_sourcePath);
        return checksum;
    };
    auto addNumber = [&](const char* name, int value) {
        std::snprintf(number, sizeof(number), "%d", value);
        GhttpPostAddString(post, name, number);
    };
    switch (vault->m_kindTag) {
    case kVaultTricks:
    case kVaultSkaters:
    case kVaultGoals: {
        if (vault->m_kindTag == kVaultTricks) {
            GhttpPostAddString(post, "filetype", "cat");
            addNumber("score", vault->m_pointsHeld);
        } else if (vault->m_kindTag == kVaultSkaters) {
            GhttpPostAddString(post, "filetype", "cas");
            addNumber("is_male", vault->m_hasMaleBody);
        } else {
            GhttpPostAddString(post, "filetype", "cag");
            addNumber("num_goals", vault->m_numGoals);
        }
        uint32_t checksum = writeData();
        GhttpPostAddString(post, "player", networkId);
        addNumber("checksum", static_cast<int>(checksum));
        break;
    }
    case kVaultParks: {
        uint32_t checksum = writeData();
        GhttpPostAddString(post, "player", networkId);
        addNumber("width", vault->m_span);
        addNumber("height", vault->m_byteCount);
        addNumber("checksum", static_cast<int>(checksum));
        addNumber("num_pieces", vault->m_numPieces);
        addNumber("num_gaps", vault->m_numGaps);
        addNumber("num_goals", vault->m_numGoals);
        addNumber("theme", vault->m_styleSet);
        std::snprintf(number, sizeof(number), "0x%x", vault->m_todScript);
        GhttpPostAddString(post, "tod_script", number);
        break;
    }
    default:
        break;
    }
    vault->m_octets = buffer;
    vault->m_bufferSize = size;
    GhttpPostAddFileFromMemory(post, "filename", buffer, size, fileName, nullptr);
    char description[0x400];
    std::snprintf(description, sizeof(description), "%s", vault->m_summaryText);
    GhttpPostAddString(post, "description", description);
    GhttpPostAddString(post, "uniqueid", GameSpyUniqueId());
    if (const char* fileType = UploadFileType(vault->m_kindTag)) // (a trick, skater or goal gets it twice)
        GhttpPostAddString(post, "filetype", fileType);
    GhttpPostAddString(post, "dblog", "1");
    vault->m_req = GhttpPostEx("http://www.thugonline.com/cgi-bin/upload2.pl", nullptr, post, 0, 0,
                                   reinterpret_cast<const void*>(&OnProgress),
                                   reinterpret_cast<const void*>(&OnTransferDone), vault);
    PopHeap();
    Release(vault->m_semaphore);
    return 0;
}

// 0x0054A660
void __cdecl VaultMan::OnProgress(void*, int state, const char*, int64_t, int64_t received, int64_t total,
                                  VaultMan* vault)
{
    // (states 3 and 7: receiving the file / posting)
    if ((state == 7 || state == 3) && total > 0)
        vault->m_progress = static_cast<int>(static_cast<double>(received) / static_cast<double>(total) * 100.0f);
    vault->m_startTime = FrameClock::GetTime();
}

// 0x0054A740
int __cdecl VaultMan::OnTransferDone(void*, int result, char*, int64_t, VaultMan* vault)
{
    vault->m_progress = 100;
    vault->m_outcomeVal = result != 0 ? 2 : 1;
    return 1;
}

// 0x0054A6B0
int __cdecl VaultMan::OnFileDownloaded(void*, int result, char* buffer, int64_t, VaultMan* vault)
{
    vault->m_progress = 100;
    if (result != 0) {
        vault->m_fault = result;
        vault->m_outcomeVal = 2;
        return 1;
    }
    vault->m_outcomeVal = 1;
    uint32_t script = 0;
    switch (vault->m_kindTag) {
    case kVaultTricks: script = 0x61A1BC57; break;
    case kVaultSkaters: script = 0xB010F357; break;
    case kVaultGoals: script = 0x62896EDF; break;
    case kVaultParks: script = 0x3BF882CC; break;
    default: break;
    }
    LoadDownloadedItem(buffer, script);
    vault->m_fault = 0;
    return 1;
}

// --- the script commands ---

// 0x0054AA10: DownloadFile file=<url>: the item, loaded when it comes (OnFileDownloaded).
bool __cdecl DownloadFile(Scripts::CStruct* params, Scripts::CScript*)
{
    const char* file = "";
    params->FetchText("file", &file, 0);
    DownloadRequest request;
    request.doneScript = kDoneScript;
    request.failScript = kFailScript;
    request.errorScript = kErrorScript;
    request.progressScript = kProgressScript;
    request.bufferSize = kDownloadFileSize;
    // (the original uses the URL as sprintf's format)
    std::snprintf(request.url, sizeof(request.url), "%s", file);
    request.callback = reinterpret_cast<const void*>(&VaultMan::OnFileDownloaded);
    Vault()->Download(&request);
    return true;
}

// 0x0054B060: UploadFile type data description filename [score width length num_pieces num_gaps num_goals theme
// tod_script is_male]: the upload on a thread (UploadThread).
bool __cdecl UploadFile(Scripts::CStruct* params, Scripts::CScript*)
{
    VaultMan* vault = Vault();
    vault->m_transfer = VaultMan::kUpload;
    vault->m_outcomeVal = 0;
    LogicTasks()->QueueJob(vault->m_task);
    vault->m_progress = 0;
    vault->m_startTime = FrameClock::GetTime();
    vault->m_failScript = kFailScript;
    params->ReadHashValue("type", &vault->m_kindTag, 1);
    params->ReadSubStruct("data", &vault->m_content, 0);
    const char* description;
    const char* fileName;
    params->FetchText("description", &description, 1);
    params->FetchText("filename", &fileName, 1);
    Text::DupFrom(vault->m_sourcePath, fileName);
    Text::DupFrom(vault->m_summaryText, description);
    params->FetchIntValue("score", &vault->m_pointsHeld, 0);
    params->FetchIntValue("width", &vault->m_span, 0);
    params->FetchIntValue("length", &vault->m_byteCount, 0);
    params->FetchIntValue("num_pieces", &vault->m_numPieces, 0);
    params->FetchIntValue("num_gaps", &vault->m_numGaps, 0);
    params->FetchIntValue("num_goals", &vault->m_numGoals, 0);
    params->FetchIntValue("theme", &vault->m_styleSet, 0);
    params->ReadHashValue("tod_script", &vault->m_todScript, 0);
    params->FetchIntValue("is_male", &vault->m_hasMaleBody, 0);
    HANDLE thread = StartThread(&VaultMan::UploadThread, vault);
    Stall(vault->m_semaphore);
    ResumeThread(thread);
    Release(vault->m_semaphore);
    return true;
}

// 0x0054B240
bool __cdecl CancelFileTransfer(Scripts::CStruct*, Scripts::CScript*)
{
    VaultMan* vault = Vault();
    if (vault->m_transfer != VaultMan::kNone) {
        RamStats()->Dealloc(vault->m_octets);
        GhttpCancelRequest(vault->m_req);
        vault->m_transfer = VaultMan::kNone;
        vault->m_task->DropOut();
    }
    return true;
}

// 0x0054C290: DownloadDirectoryList <type>: the type's categories and their items (for profile 0x011B0C16, first
// the uploads list, as an "Uploads" category).
bool __cdecl DownloadDirectoryList(Scripts::CStruct* params, Scripts::CScript*)
{
    Hub* manager = NetManager();
    VaultMan* vault = reinterpret_cast<VaultMan*>(manager->m_vaultMan);
    FriendsManager* buddies = static_cast<FriendsManager*>(manager->m_buddyMan);
    params->ReadHashValue(0u, &vault->m_kindTag, 1);
    if (const char* folder = TypeFolder(vault->m_kindTag)) // (an unknown type keeps the folder)
        std::snprintf(vault->m_folder, sizeof(vault->m_folder), "%s", folder);
    if (buddies->m_account != kUploadsProfile) {
        vault->ListCategories();
        return true;
    }
    PushBottomUp();
    void* memory = MemSys::AllocZeroed(sizeof(VaultCategory));
    VaultCategory* category = memory ? static_cast<VaultCategory*>(memory)->MakeNew() : nullptr;
    category->m_skip = true;
    Prepend(&vault->m_categories, &category->m_listLink);
    std::memcpy(category->m_label, "Uploads", 8);
    vault->m_category = static_cast<VaultCategory*>(vault->m_search.HeadElement(&vault->m_categories));
    vault->m_transfer = VaultMan::kUploads;
    vault->m_outcomeVal = 0;
    char url[128];
    std::snprintf(url, sizeof(url), "http://www.thugonline.com/%s/uploads.txt", vault->m_folder);
    vault->StartList(url);
    LogicTasks()->QueueJob(vault->m_task);
    PopHeap();
    return true;
}

// 0x0054C460
bool __cdecl FreeDirectoryListing(Scripts::CStruct*, Scripts::CScript*)
{
    Vault()->FreeDirectoryListing();
    return true;
}

// 0x0054C470: a menu item for each item of the current category (net_vault_menu_add_park / goal / skater / trick),
// its details as parameters, choosing it gets the file.
bool __cdecl FillVaultMenu(Scripts::CStruct*, Scripts::CScript*)
{
    VaultMan* vault = Vault();
    PushTopDown();
    for (MemSys::ListNode* node = NextNode(&vault->m_category->m_items); node; node = NextNode(node)) {
        auto* item = static_cast<VaultItem*>(node->data);
        if (!item)
            break;
        Scripts::CStruct* params = NewParams();
        params->AppendWord("name", item->m_label);
        params->AppendWord("creator", item->m_creator);
        params->AppendWord("size", item->m_extent);
        params->PutInt("score", item->m_pointsHeld);
        params->PutInt("num_pieces", item->m_numPieces);
        params->PutInt("num_gaps", item->m_numGaps);
        params->PutInt("width", item->m_span);
        params->PutInt("height", item->m_levelY);
        params->PutInt("num_goals", item->m_numGoals);
        params->PutInt("theme", item->m_styleSet);
        params->PutNameKey("tod_script", item->m_todScript);
        params->PutInt("is_male", item->m_hasMaleBody);
        params->AppendWord("description", item->m_summaryText);
        Scripts::CStruct* choose = NewParams();
        choose->AppendWord("file", item->m_url);
        params->PutSubStruct("pad_choose_params", choose);
        const char* script = nullptr;
        switch (vault->m_kindTag) {
        case kVaultParks: script = "net_vault_menu_add_park"; break;
        case kVaultGoals: script = "net_vault_menu_add_goal"; break;
        case kVaultSkaters: script = "net_vault_menu_add_skater"; break;
        case kVaultTricks: script = "net_vault_menu_add_trick"; break;
        default: break;
        }
        if (script)
            Run(script, params);
        DeleteParams(params);
        DeleteParams(choose);
    }
    PopHeap();
    return true;
}

namespace {
// The category after / before the current one (round to the first / last), its name into the script's parameters.
void MoveCategory(Scripts::CScript* script, bool forward)
{
    VaultMan* vault = Vault();
    MemSys::ListNode*& node = vault->m_search.m_listLink;
    MemSys::ListNode* step = forward ? node->next : node->prev;
    node = step->data == reinterpret_cast<void*>(-1) ? nullptr : step;
    vault->m_category = node ? static_cast<VaultCategory*>(node->data) : nullptr;
    if (!vault->m_category) {
        node = &vault->m_categories;
        step = forward ? node->next : node->prev;
        node = step->data == reinterpret_cast<void*>(-1) ? nullptr : step;
        vault->m_category = node ? static_cast<VaultCategory*>(node->data) : nullptr;
    }
    script->m_args->AppendWord("category", vault->m_category->m_label);
}
} // namespace

// 0x0054C770
bool __cdecl NextVaultCategory(Scripts::CStruct*, Scripts::CScript* script)
{
    MoveCategory(script, true);
    return true;
}

// 0x0054C800
bool __cdecl PrevVaultCategory(Scripts::CStruct*, Scripts::CScript* script)
{
    MoveCategory(script, false);
    return true;
}

} // namespace OnlinePlay

using OnlinePlay::VaultMan;
