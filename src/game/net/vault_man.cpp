// The content vault and HTTP file transfers (GameNet::VaultMan). See vault_man.h.

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

namespace GameNet {

namespace {

using Detail::DeleteParams;
using Detail::NewParams;

static VtableSlot kItemVtable_slots[] = {&GameNet::VaultItem::Destroy};
void** const kItemVtable = reinterpret_cast<void**>(kItemVtable_slots); // VaultItem (and a category's items head)
static VtableSlot kCategoriesHeadVtable_slots[] = {&GameNet::VaultNode::DeleteCategoriesHead};
void** const kCategoriesHeadVtable = reinterpret_cast<void**>(kCategoriesHeadVtable_slots); // the vault's categories head
static VtableSlot kCategoryVtable_slots[] = {&GameNet::VaultCategory::Destroy};
void** const kCategoryVtable = reinterpret_cast<void**>(kCategoryVtable_slots); // VaultCategory
static VtableSlot kItemNodeVtable_slots[] = {&GameNet::VaultNode::DeleteItemNode};
void** const kItemNodeVtable = reinterpret_cast<void**>(kItemNodeVtable_slots); // Lst::Node<VaultItem>
static VtableSlot kCategoryNodeVtable_slots[] = {&GameNet::VaultNode::DeleteCategoryNode};
void** const kCategoryNodeVtable = reinterpret_cast<void**>(kCategoryNodeVtable_slots); // Lst::Node<VaultCategory>
static VtableSlot kCategorySearchVtable_slots[] = {&GameNet::VaultSearch::DestroyCategorySearchAndFree};
void** const kCategorySearchVtable = reinterpret_cast<void**>(kCategorySearchVtable_slots);
static VtableSlot kItemSearchVtable_slots[] = {&GameNet::VaultSearch::DestroyItemSearchAndFree};
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

Manager* NetManager() { return g_gameNetManager; }
VaultMan* Vault() { return reinterpret_cast<VaultMan*>(NetManager()->m_vaultMan); }
Mem::Manager* Memory() { return Mem::Manager::Get(); }
void PushTopDown() { Memory()->PushContext(Memory()->m_internetTopDownHeap); }
void PushBottomUp() { Memory()->PushContext(Memory()->m_internetBottomUpHeap); }
void PopHeap() { Memory()->PopContext(); }
Tsk::Stack* LogicTasks() { return reinterpret_cast<Tsk::Stack*>(reinterpret_cast<uint8_t*>(Ass::g_mainLoopManager) + 0x04); }

uint32_t Crc(const char* name) { return Crc::GenerateCRCFromString(name); }
void Run(const char* script, Script::CStruct* params = nullptr) { Script::RunScript(script, params, nullptr, false); }
void Run(uint32_t script, Script::CStruct* params = nullptr) { Script::RunScript(script, params, nullptr, false, nullptr); }

// The semaphore (the threads set their request up while holding it).
void Wait(void* semaphore) { WaitForSingleObject(static_cast<HANDLE>(semaphore), INFINITE); }
void Release(void* semaphore) { ReleaseSemaphore(static_cast<HANDLE>(semaphore), 1, nullptr); }
// A thread made suspended; the manager notes its id (SetLobbyId) before it starts.
HANDLE StartThread(LPTHREAD_START_ROUTINE thread, void* param)
{
    DWORD id;
    HANDLE handle = CreateThread(nullptr, 0x8000, thread, param, CREATE_SUSPENDED, &id);
    NetManager()->SetLobbyId(id);
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
void GameCrtFree(void* memory) { Crt::Free(memory); }
// The GameSpy SDK's id of this machine (GOAGetUniqueID).
const char* GameSpyUniqueId() { return GameSpy::UniqueId(); }
// 0x005A8EB0: a downloaded item loaded from `buffer` by the type's script (not rewritten yet).
void LoadDownloadedItem(char* buffer, uint32_t script)
{
    Mc::KeepLoadedContents(reinterpret_cast<const uint8_t*>(buffer), script);
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

void DeleteNode(void* node)
{
    auto* vtable = *static_cast<void***>(node);
    reinterpret_cast<void*(__thiscall*)(void*, unsigned int)>(vtable[0])(node, 1);
}
void InitNode(Mem::ListNode* node, void** vtable)
{
    node->data = node;
    node->priority = 0;
    node->next = node;
    node->prev = node;
    node->vtable = vtable;
}
void InitHead(Mem::ListNode* head, void** vtable)
{
    head->data = reinterpret_cast<void*>(-1);
    head->priority = 0;
    head->next = head;
    head->prev = head;
    head->vtable = vtable;
}
// `node` at the front of the list (the lists of the vault are built that way: the last line first).
void Prepend(Mem::ListNode* head, Mem::ListNode* node)
{
    node->prev = head;
    node->next = head->next;
    head->next->prev = node;
    head->next = node;
}
// The list walk's step: `node`'s successor, or null at the head.
Mem::ListNode* NextNode(Mem::ListNode* node)
{
    Mem::ListNode* next = node->next;
    return next->data == reinterpret_cast<void*>(-1) ? nullptr : next;
}

// The next field of a list line: the text up to the next ':' (which is cut there), and `*rest` moved past it.
// (A line with too few fields writes through null, as the original's strstr does.)
char* Field(char** rest)
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
    Text::Copy(number, Field(rest));
    return std::atol(number);
}

} // namespace

// --- the list nodes, items and categories ---

// 0x0054B2D0
void VaultNode::DestroyItemNode() { m_node.UnlinkAs(kItemNodeVtable); }

// 0x0054B2B0
void VaultNode::DestroyCategoryNode() { m_node.UnlinkAs(kCategoryNodeVtable); }

// 0x0054B2F0
VaultNode* VaultNode::DeleteItemNode(unsigned int flags)
{
    DestroyItemNode();
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// 0x0054B330
VaultNode* VaultNode::DeleteCategoryNode(unsigned int flags)
{
    DestroyCategoryNode();
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// 0x0054B610
VaultNode* VaultNode::DeleteCategoriesHead(unsigned int flags)
{
    DestroyCategoryNode();
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// 0x0054B570
VaultItem* VaultItem::Construct()
{
    InitNode(&m_node, kItemVtable);
    m_url[0] = '\0';
    m_name[0] = '\0';
    m_creator[0] = '\0';
    m_size[0] = '\0';
    m_unknownCC[0] = '\0';
    m_description[0] = '\0';
    m_score = 0;
    m_numPieces = 0;
    m_numGaps = 0;
    m_numGoals = 0;
    m_theme = 0;
    m_todScript = 0;
    m_isMale = 0;
    m_width = 0;
    m_height = 0;
    return this;
}

// 0x0054B5E0
VaultItem* VaultItem::Destroy(unsigned int flags)
{
    reinterpret_cast<VaultNode*>(this)->DestroyItemNode();
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// 0x0054B670
VaultCategory* VaultCategory::Construct()
{
    InitNode(&m_node, kCategoryVtable);
    InitHead(&m_items, kItemVtable);
    m_skip = false;
    return this;
}

// 0x0054C890
void VaultCategory::DestroyBody()
{
    m_node.vtable = kCategoryVtable;
    for (Mem::ListNode* node = NextNode(&m_items); node;) {
        Mem::ListNode* next = NextNode(node);
        DeleteNode(node->data);
        node = next;
    }
    m_items.UnlinkAs(kItemNodeVtable);
    m_node.UnlinkAs(kCategoryNodeVtable);
}

// 0x0054C980
VaultCategory* VaultCategory::Destroy(unsigned int flags)
{
    DestroyBody();
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// 0x0054B6B0
void* VaultSearch::FirstItem(Mem::ListNode* head)
{
    m_node = head;
    return NextItem();
}

// 0x0054B640
void* VaultSearch::NextItem()
{
    m_node = NextNode(m_node);
    return m_node ? m_node->data : nullptr;
}

// 0x0054A770 / 0x0054A760
void VaultSearch::DestroyCategorySearch() { m_vtable = kCategorySearchVtable; }
void VaultSearch::DestroyItemSearch() { m_vtable = kItemSearchVtable; }

// 0x0054A780
VaultSearch* VaultSearch::DestroyCategorySearchAndFree(unsigned int flags)
{
    m_vtable = kCategorySearchVtable;
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// 0x0054B280
VaultSearch* VaultSearch::DestroyItemSearchAndFree(unsigned int flags)
{
    m_vtable = kItemSearchVtable;
    if (flags & 1)
        Memory()->Free(this);
    return this;
}

// --- the vault ---

// 0x0054C1D0
VaultMan* VaultMan::Construct()
{
    InitHead(&m_categories, kCategoriesHeadVtable);
    m_search.m_vtable = kCategorySearchVtable;
    m_search.m_node = nullptr;
    m_transfer = kNone;
    m_progress = 0;
    m_startTime = 0;
    auto* task = static_cast<Tsk::BaseTask*>(Mem::Calloc(0x20));
    if (task) {
        task->Construct(0);
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
        reinterpret_cast<void*(__thiscall*)(Tsk::BaseTask*, unsigned int)>(m_task->m_vtable[2])(m_task, 1);
    m_search.m_vtable = kCategorySearchVtable;
    m_categories.UnlinkAs(kCategoryNodeVtable);
}

// 0x0054A910
void VaultMan::Download(const DownloadRequest* request)
{
    PushBottomUp();
    VaultMan* vault = Vault();
    vault->m_transfer = kDownload;
    vault->m_result = 0;
    LogicTasks()->AddTask(vault->m_task);
    vault->m_progress = 0;
    vault->m_startTime = Tmr::GetTime();
    vault->m_bufferSize = request->bufferSize;
    vault->m_failScript = request->failScript;
    vault->m_doneScript = request->doneScript;
    vault->m_progressScript = request->progressScript;
    vault->m_errorScript = request->errorScript;
    vault->m_callback = request->callback;
    char* url = GameSpyStrdup(request->url);
    HANDLE thread = StartThread(&DownloadThread, url);
    Wait(vault->m_semaphore);
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
    m_startTime = Tmr::GetTime();
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
    std::snprintf(url, sizeof(url), "http://www.thugonline.com/%s/%s/dir_list.txt", m_folder, category->m_name);
    m_transfer = kItems;
    m_result = 0;
    StartList(url);
}

// 0x0054B500
void VaultMan::ListCategories()
{
    m_transfer = kCategories;
    char url[128];
    std::snprintf(url, sizeof(url), "http://www.thugonline.com/%s/dir_list.txt", m_folder);
    StartList(url);
    m_result = 0;
    LogicTasks()->AddTask(m_task);
}

// 0x0054C130
void VaultMan::FreeDirectoryListing()
{
    for (Mem::ListNode* node = NextNode(&m_categories); node;) {
        Mem::ListNode* next = NextNode(node);
        DeleteNode(node->data);
        node = next;
    }
    GhttpCleanup();
}

// 0x0054B6E0
void __cdecl VaultMan::VaultTask(Tsk::BaseTask* task)
{
    auto* vault = reinterpret_cast<VaultMan*>(reinterpret_cast<void**>(task)[7]);
    if (Tmr::GetTime() - vault->m_startTime > kTimeout) {
        if (vault->m_transfer != kNone)
            Memory()->Free(vault->m_buffer);
        GhttpCancelRequest(vault->m_request);
        Run(vault->m_failScript);
        vault->m_transfer = kNone;
        vault->m_task->Remove();
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
            Script::CStruct* params = NewParams();
            params->AddString(Crc("text"), Script::ESYMBOLTYPE_STRING, text);
            Run(vault->m_progressScript, params);
            DeleteParams(params);
        }
        return;
    }
    if (transfer == kDownload || transfer == kDownload6)
        Memory()->Free(vault->m_buffer);

    switch (transfer) {
    case kDownload:
    case kDownload6:
    case kUpload: {
        vault->m_task->Remove();
        vault->m_transfer = kNone;
        if (vault->m_result == 1) {
            Script::CStruct* params = NewParams();
            params->AddChecksum("type", vault->m_type);
            if (transfer == kUpload)
                Run("LaunchUploadCompleteDialog", params);
            else
                Run(vault->m_doneScript, params);
            DeleteParams(params);
        } else {
            Run(vault->m_error == kErrorScriptResult ? vault->m_errorScript : vault->m_failScript);
        }
        if (transfer == kUpload)
            Memory()->Free(vault->m_buffer);
        return;
    }
    case kCategories: {
        if (vault->m_result != 1) {
            vault->m_transfer = kNone;
            vault->m_task->Remove();
            Mem::Free(vault->m_buffer);
            Run(vault->m_failScript);
            return;
        }
        PushBottomUp();
        // (an empty list leaves the transfer as it is: it's read again next time)
        char* line = Text::Token(vault->m_buffer, "\r\n");
        if (line) {
            do {
                void* memory = Mem::Calloc(sizeof(VaultCategory));
                VaultCategory* category = memory ? static_cast<VaultCategory*>(memory)->Construct() : nullptr;
                Prepend(&vault->m_categories, &category->m_node);
                Text::Copy(category->m_name, line);
                line = Text::Token(nullptr, "\r\n");
            } while (line);
            vault->m_category = static_cast<VaultCategory*>(vault->m_search.FirstItem(&vault->m_categories));
            if (vault->m_category->m_skip)
                vault->m_category = static_cast<VaultCategory*>(vault->m_search.NextItem());
            vault->m_transfer = kNone;
            Mem::Free(vault->m_buffer);
            vault->ListItems(vault->m_category);
        }
        PopHeap();
        return;
    }
    case kItems: {
        PushBottomUp();
        if (vault->m_result != 1) {
            vault->m_transfer = kNone;
            vault->m_task->Remove();
            Mem::Free(vault->m_buffer);
            Run(vault->m_failScript);
            PopHeap();
            return;
        }
        // Each line: file:name:creator[:park, trick, skater or goal fields]:...:description
        for (char* line = Text::Token(vault->m_buffer, "\r\n"); line; line = Text::Token(nullptr, "\r\n")) {
            void* memory = Mem::Calloc(sizeof(VaultItem));
            VaultItem* item = memory ? static_cast<VaultItem*>(memory)->Construct() : nullptr;
            Prepend(&vault->m_category->m_items, &item->m_node);
            char* rest = line;
            char file[1024];
            Text::Copy(file, Field(&rest));
            // (the original makes the URL with sprintf: no limit)
            std::snprintf(item->m_url, sizeof(item->m_url) + 0x80, "http://www.thugonline.com/%s/%s/%s",
                          vault->m_folder, vault->m_category->m_name, file);
            Text::Copy(item->m_name, Field(&rest));
            Text::Copy(item->m_creator, Field(&rest));
            if (vault->m_type == kVaultParks) {
                Text::Copy(item->m_size, Field(&rest));
                item->m_numPieces = NumberField(&rest);
                item->m_numGaps = NumberField(&rest);
                item->m_numGoals = NumberField(&rest);
                item->m_theme = NumberField(&rest);
                item->m_todScript = static_cast<uint32_t>(NumberField(&rest));
            }
            if (vault->m_type == kVaultTricks)
                item->m_score = NumberField(&rest);
            if (vault->m_type == kVaultSkaters)
                item->m_isMale = NumberField(&rest);
            if (vault->m_type == kVaultGoals)
                item->m_numGoals = NumberField(&rest);
            Text::Copy(item->m_unknownCC, Field(&rest));
            Text::Copy(item->m_description, rest);
        }
        vault->m_transfer = kNone;
        Mem::Free(vault->m_buffer);
        vault->m_category = static_cast<VaultCategory*>(vault->m_search.NextItem());
        if (vault->m_category && !vault->m_category->m_skip) {
            vault->ListItems(vault->m_category); // the next category's
        } else {
            // All in: the menu.
            vault->m_category = static_cast<VaultCategory*>(vault->m_search.FirstItem(&vault->m_categories));
            vault->m_task->Remove();
            Script::CStruct* params = NewParams();
            params->AddString("category", vault->m_category->m_name);
            params->AddChecksum("type", vault->m_type);
            Run("net_vault_menu_create", params);
            DeleteParams(params);
        }
        PopHeap();
        return;
    }
    case kUploads: {
        PushBottomUp();
        // Each line: file:creator:size:...:description (the whole line also makes the URL).
        for (char* line = Text::Token(vault->m_buffer, "\r\n"); line; line = Text::Token(nullptr, "\r\n")) {
            void* memory = Mem::Calloc(sizeof(VaultItem));
            VaultItem* item = memory ? static_cast<VaultItem*>(memory)->Construct() : nullptr;
            Prepend(&vault->m_category->m_items, &item->m_node);
            std::snprintf(item->m_url, sizeof(item->m_url) + 0x80, "http://www.thugonline.com/%s/%s", vault->m_folder,
                          line);
            char* rest = line;
            Text::Copy(item->m_name, Field(&rest));
            Text::Copy(item->m_creator, Field(&rest));
            Text::Copy(item->m_size, Field(&rest));
            Text::Copy(item->m_unknownCC, Field(&rest));
            Text::Copy(item->m_description, rest);
        }
        vault->m_transfer = kNone;
        vault->m_task->Remove();
        Mem::Free(vault->m_buffer);
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
    Wait(vault->m_semaphore);
    PushBottomUp();
    auto* buffer = static_cast<char*>(Mem::New(kListBufferSize));
    vault->m_buffer = buffer;
    std::memset(buffer, 0, kListBufferSize);
    vault->m_request = GhttpGetEx(vault->m_url, nullptr, vault->m_buffer, kListBufferSize, nullptr, 0, 0,
                                  reinterpret_cast<const void*>(&OnProgress),
                                  reinterpret_cast<const void*>(&OnTransferDone), vault);
    PopHeap();
    Release(vault->m_semaphore);
    return 0;
}

// 0x0054A860
unsigned long __stdcall VaultMan::DownloadThread(void* url)
{
    Manager* manager = NetManager();
    VaultMan* vault = Vault();
    Wait(vault->m_semaphore);
    PushBottomUp();
    uint32_t size = vault->m_bufferSize;
    auto* buffer = static_cast<char*>(Mem::New(size));
    vault->m_buffer = buffer;
    int request = GhttpGetEx(static_cast<const char*>(url), nullptr, buffer, size, nullptr, 0, 0,
                             reinterpret_cast<const void*>(&OnProgress), vault->m_callback, vault);
    reinterpret_cast<VaultMan*>(manager->m_vaultMan)->m_request = request;
    PopHeap();
    Release(reinterpret_cast<VaultMan*>(manager->m_vaultMan)->m_semaphore);
    GameCrtFree(url);
    return 0;
}

// 0x0054AAC0
unsigned long __stdcall VaultMan::UploadThread(void* param)
{
    auto* vault = static_cast<VaultMan*>(param);
    Prefs::Preferences* prefs = NetManager()->GetNetworkPreferences();
    const char* networkId;
    prefs->GetPreference(Crc("network_id"))->GetString("ui_string", &networkId, 1);
    Wait(vault->m_semaphore);
    PushBottomUp();
    void* post = GhttpNewPost();
    char number[32];
    char fileName[0x80] = {}; // (left unset in the original for an unknown type)
    char* buffer = nullptr;
    uint32_t size = 0;
    // The item's data written to a buffer (from the current heap: Mem::New for a trick, its steps inlined for the
    // others).
    auto writeData = [&]() {
        size = Script::CalculateBufferSize(vault->m_data, kListBufferSize);
        buffer = static_cast<char*>(Mem::New(size));
        Script::WriteToBuffer(vault->m_data, reinterpret_cast<uint8_t*>(buffer), size, 1, nullptr);
        uint32_t checksum = Crc::GenerateCRCCaseless(buffer, static_cast<int>(size));
        Text::Copy(fileName, vault->m_fileName);
        return checksum;
    };
    auto addNumber = [&](const char* name, int value) {
        std::snprintf(number, sizeof(number), "%d", value);
        GhttpPostAddString(post, name, number);
    };
    switch (vault->m_type) {
    case kVaultTricks:
    case kVaultSkaters:
    case kVaultGoals: {
        if (vault->m_type == kVaultTricks) {
            GhttpPostAddString(post, "filetype", "cat");
            addNumber("score", vault->m_score);
        } else if (vault->m_type == kVaultSkaters) {
            GhttpPostAddString(post, "filetype", "cas");
            addNumber("is_male", vault->m_isMale);
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
        addNumber("width", vault->m_width);
        addNumber("height", vault->m_length);
        addNumber("checksum", static_cast<int>(checksum));
        addNumber("num_pieces", vault->m_numPieces);
        addNumber("num_gaps", vault->m_numGaps);
        addNumber("num_goals", vault->m_numGoals);
        addNumber("theme", vault->m_theme);
        std::snprintf(number, sizeof(number), "0x%x", vault->m_todScript);
        GhttpPostAddString(post, "tod_script", number);
        break;
    }
    default:
        break;
    }
    vault->m_buffer = buffer;
    vault->m_bufferSize = size;
    GhttpPostAddFileFromMemory(post, "filename", buffer, size, fileName, nullptr);
    char description[0x400];
    std::snprintf(description, sizeof(description), "%s", vault->m_description);
    GhttpPostAddString(post, "description", description);
    GhttpPostAddString(post, "uniqueid", GameSpyUniqueId());
    if (const char* fileType = UploadFileType(vault->m_type)) // (a trick, skater or goal gets it twice)
        GhttpPostAddString(post, "filetype", fileType);
    GhttpPostAddString(post, "dblog", "1");
    vault->m_request = GhttpPostEx("http://www.thugonline.com/cgi-bin/upload2.pl", nullptr, post, 0, 0,
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
    vault->m_startTime = Tmr::GetTime();
}

// 0x0054A740
int __cdecl VaultMan::OnTransferDone(void*, int result, char*, int64_t, VaultMan* vault)
{
    vault->m_progress = 100;
    vault->m_result = result != 0 ? 2 : 1;
    return 1;
}

// 0x0054A6B0
int __cdecl VaultMan::OnFileDownloaded(void*, int result, char* buffer, int64_t, VaultMan* vault)
{
    vault->m_progress = 100;
    if (result != 0) {
        vault->m_error = result;
        vault->m_result = 2;
        return 1;
    }
    vault->m_result = 1;
    uint32_t script = 0;
    switch (vault->m_type) {
    case kVaultTricks: script = 0x61A1BC57; break;
    case kVaultSkaters: script = 0xB010F357; break;
    case kVaultGoals: script = 0x62896EDF; break;
    case kVaultParks: script = 0x3BF882CC; break;
    default: break;
    }
    LoadDownloadedItem(buffer, script);
    vault->m_error = 0;
    return 1;
}

// --- the script commands ---

// 0x0054AA10: DownloadFile file=<url>: the item, loaded when it comes (OnFileDownloaded).
bool __cdecl DownloadFile(Script::CStruct* params, Script::CScript*)
{
    const char* file = "";
    params->GetString("file", &file, 0);
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
bool __cdecl UploadFile(Script::CStruct* params, Script::CScript*)
{
    VaultMan* vault = Vault();
    vault->m_transfer = VaultMan::kUpload;
    vault->m_result = 0;
    LogicTasks()->AddTask(vault->m_task);
    vault->m_progress = 0;
    vault->m_startTime = Tmr::GetTime();
    vault->m_failScript = kFailScript;
    params->GetChecksum("type", &vault->m_type, 1);
    params->GetStructure("data", &vault->m_data, 0);
    const char* description;
    const char* fileName;
    params->GetString("description", &description, 1);
    params->GetString("filename", &fileName, 1);
    Text::Copy(vault->m_fileName, fileName);
    Text::Copy(vault->m_description, description);
    params->GetInteger("score", &vault->m_score, 0);
    params->GetInteger("width", &vault->m_width, 0);
    params->GetInteger("length", &vault->m_length, 0);
    params->GetInteger("num_pieces", &vault->m_numPieces, 0);
    params->GetInteger("num_gaps", &vault->m_numGaps, 0);
    params->GetInteger("num_goals", &vault->m_numGoals, 0);
    params->GetInteger("theme", &vault->m_theme, 0);
    params->GetChecksum("tod_script", &vault->m_todScript, 0);
    params->GetInteger("is_male", &vault->m_isMale, 0);
    HANDLE thread = StartThread(&VaultMan::UploadThread, vault);
    Wait(vault->m_semaphore);
    ResumeThread(thread);
    Release(vault->m_semaphore);
    return true;
}

// 0x0054B240
bool __cdecl CancelFileTransfer(Script::CStruct*, Script::CScript*)
{
    VaultMan* vault = Vault();
    if (vault->m_transfer != VaultMan::kNone) {
        Memory()->Free(vault->m_buffer);
        GhttpCancelRequest(vault->m_request);
        vault->m_transfer = VaultMan::kNone;
        vault->m_task->Remove();
    }
    return true;
}

// 0x0054C290: DownloadDirectoryList <type>: the type's categories and their items (for profile 0x011B0C16, first
// the uploads list, as an "Uploads" category).
bool __cdecl DownloadDirectoryList(Script::CStruct* params, Script::CScript*)
{
    Manager* manager = NetManager();
    VaultMan* vault = reinterpret_cast<VaultMan*>(manager->m_vaultMan);
    BuddyMan* buddies = static_cast<BuddyMan*>(manager->m_buddyMan);
    params->GetChecksum(0u, &vault->m_type, 1);
    if (const char* folder = TypeFolder(vault->m_type)) // (an unknown type keeps the folder)
        std::snprintf(vault->m_folder, sizeof(vault->m_folder), "%s", folder);
    if (buddies->m_profile != kUploadsProfile) {
        vault->ListCategories();
        return true;
    }
    PushBottomUp();
    void* memory = Mem::Calloc(sizeof(VaultCategory));
    VaultCategory* category = memory ? static_cast<VaultCategory*>(memory)->Construct() : nullptr;
    category->m_skip = true;
    Prepend(&vault->m_categories, &category->m_node);
    std::memcpy(category->m_name, "Uploads", 8);
    vault->m_category = static_cast<VaultCategory*>(vault->m_search.FirstItem(&vault->m_categories));
    vault->m_transfer = VaultMan::kUploads;
    vault->m_result = 0;
    char url[128];
    std::snprintf(url, sizeof(url), "http://www.thugonline.com/%s/uploads.txt", vault->m_folder);
    vault->StartList(url);
    LogicTasks()->AddTask(vault->m_task);
    PopHeap();
    return true;
}

// 0x0054C460
bool __cdecl FreeDirectoryListing(Script::CStruct*, Script::CScript*)
{
    Vault()->FreeDirectoryListing();
    return true;
}

// 0x0054C470: a menu item for each item of the current category (net_vault_menu_add_park / goal / skater / trick),
// its details as parameters, choosing it gets the file.
bool __cdecl FillVaultMenu(Script::CStruct*, Script::CScript*)
{
    VaultMan* vault = Vault();
    PushTopDown();
    for (Mem::ListNode* node = NextNode(&vault->m_category->m_items); node; node = NextNode(node)) {
        auto* item = static_cast<VaultItem*>(node->data);
        if (!item)
            break;
        Script::CStruct* params = NewParams();
        params->AddString("name", item->m_name);
        params->AddString("creator", item->m_creator);
        params->AddString("size", item->m_size);
        params->AddInteger("score", item->m_score);
        params->AddInteger("num_pieces", item->m_numPieces);
        params->AddInteger("num_gaps", item->m_numGaps);
        params->AddInteger("width", item->m_width);
        params->AddInteger("height", item->m_height);
        params->AddInteger("num_goals", item->m_numGoals);
        params->AddInteger("theme", item->m_theme);
        params->AddChecksum("tod_script", item->m_todScript);
        params->AddInteger("is_male", item->m_isMale);
        params->AddString("description", item->m_description);
        Script::CStruct* choose = NewParams();
        choose->AddString("file", item->m_url);
        params->AddStructure("pad_choose_params", choose);
        const char* script = nullptr;
        switch (vault->m_type) {
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
void MoveCategory(Script::CScript* script, bool forward)
{
    VaultMan* vault = Vault();
    Mem::ListNode*& node = vault->m_search.m_node;
    Mem::ListNode* step = forward ? node->next : node->prev;
    node = step->data == reinterpret_cast<void*>(-1) ? nullptr : step;
    vault->m_category = node ? static_cast<VaultCategory*>(node->data) : nullptr;
    if (!vault->m_category) {
        node = &vault->m_categories;
        step = forward ? node->next : node->prev;
        node = step->data == reinterpret_cast<void*>(-1) ? nullptr : step;
        vault->m_category = node ? static_cast<VaultCategory*>(node->data) : nullptr;
    }
    script->m_params->AddString("category", vault->m_category->m_name);
}
} // namespace

// 0x0054C770
bool __cdecl NextVaultCategory(Script::CStruct*, Script::CScript* script)
{
    MoveCategory(script, true);
    return true;
}

// 0x0054C800
bool __cdecl PrevVaultCategory(Script::CStruct*, Script::CScript* script)
{
    MoveCategory(script, false);
    return true;
}

} // namespace GameNet

using GameNet::VaultMan;
