#pragma once

#include <cstddef>
#include <cstdint>

#include "game/core/alloc.h"

namespace Script {
class CScript;
class CStruct;
} // namespace Script
namespace Tsk {
class BaseTask;
} // namespace Tsk

// The content vault and HTTP file transfers: GameNet::Manager's +0x14, on GameSpy's HTTP SDK (ghttp: library code,
// not decompiled - called by address). Created parks, goals, skaters and tricks are uploaded to and listed / fetched
// from www.thugonline.com: a directory list of categories (<folder>/dir_list.txt), each category's list of items
// (<folder>/<category>/dir_list.txt, one line each: "file:name:creator:...:description"), and the items themselves.
// The top stats (stats.cpp) download through the same object. Each transfer runs on a thread (ghttp's blocking set-up
// under the object's semaphore); the task watches it, times it out after 25 s, and runs the transfer's scripts.
// 0x0054A660-0x0054C9B0.
namespace GameNet {

// The vault's item types (the scripts' `type`) and their folders.
constexpr uint32_t kVaultParks = 0x5BE5569F;   // "levels"
constexpr uint32_t kVaultSkaters = 0x07364EA1; // "skaters"
constexpr uint32_t kVaultTricks = 0x1E26FD3E;  // "tricks"
constexpr uint32_t kVaultGoals = 0x38DBE1D0;   // "goals"

// An item of a category (0x214 bytes): Lst::Node<VaultItem> (vtable 0x0064D414; its node destructor 0x0054B2D0).
struct VaultItem {
    Mem::ListNode m_node;    // +0x000 data = this
    char m_url[0x80];        // +0x014 http://www.thugonline.com/<folder>/<category>/<file>
    char m_name[0x18];       // +0x094
    char m_creator[0x10];    // +0x0AC
    char m_size[0x10];       // +0x0BC (parks)
    char m_unknownCC[0x21];  // +0x0CC the field before the description
    char m_description[0x103]; // +0x0ED the rest of the line
    int m_score;             // +0x1F0 (tricks)
    int m_width;             // +0x1F4 (never read from the list)
    int m_height;            // +0x1F8 (never read from the list)
    int m_numPieces;         // +0x1FC (parks)
    int m_numGaps;           // +0x200 (parks)
    int m_numGoals;          // +0x204 (parks, goals)
    int m_theme;             // +0x208 (parks)
    uint32_t m_todScript;    // +0x20C (parks)
    int m_isMale;            // +0x210 (skaters)

    // 0x0054B570: unlinked, every string and number empty. Returns this.
    VaultItem* Construct();
    // 0x0054B5E0 (vtable 0x0064D414 slot 0, scalar deleting): the node destructor, freed with flag 1.
    VaultItem* Destroy(unsigned int flags);
};
static_assert(sizeof(VaultItem) == 0x214 && offsetof(VaultItem, m_name) == 0x94 &&
                  offsetof(VaultItem, m_description) == 0xED && offsetof(VaultItem, m_score) == 0x1F0 &&
                  offsetof(VaultItem, m_isMale) == 0x210,
              "layout must match the original");

// A category of the vault (0x44 bytes): Lst::Node<VaultCategory> (vtable 0x0064D41C) with its items.
struct VaultCategory {
    Mem::ListNode m_node;    // +0x00 data = this
    bool m_skip;             // +0x14 not chosen when moving through the categories at the start ("Uploads")
    char m_name[0x1B];       // +0x15
    Mem::ListNode m_items;   // +0x30 Lst::Head<VaultItem> (vtable 0x0064D414)

    // 0x0054B670: unlinked, no items, not skipped. Returns this.
    VaultCategory* Construct();
    // 0x0054C890 (destructor body): the items deleted, the head and the node unlinked.
    void DestroyBody();
    // 0x0054C980 (vtable 0x0064D41C slot 0, scalar deleting)
    VaultCategory* Destroy(unsigned int flags);
};
static_assert(sizeof(VaultCategory) == 0x44 && offsetof(VaultCategory, m_name) == 0x15 &&
                  offsetof(VaultCategory, m_items) == 0x30,
              "layout must match the original");

// Lst::Node<VaultItem> / Lst::Node<VaultCategory> as the original destroys them.
struct VaultNode {
    Mem::ListNode m_node;

    // 0x0054B2D0 / 0x0054B2B0: the node destructors (the base vtable 0x0064D3AC / 0x0064D3B0, unlinked).
    void DestroyItemNode();
    void DestroyCategoryNode();
    // 0x0054B2F0 (vtable 0x0064D3AC slot 0) / 0x0054B330 (0x0064D3B0) / 0x0054B610 (the categories head's,
    // 0x0064D418): the scalar deleting destructors.
    VaultNode* DeleteItemNode(unsigned int flags);
    VaultNode* DeleteCategoryNode(unsigned int flags);
    VaultNode* DeleteCategoriesHead(unsigned int flags);
};

// Lst::Search<VaultCategory> (vtable 0x0064D29C) / Lst::Search<VaultItem> (0x0064D2A0): the walk's node.
struct VaultSearch {
    void** m_vtable;
    Mem::ListNode* m_node;

    // 0x0054B6B0: the first item of the list `head` (null if none).
    void* FirstItem(Mem::ListNode* head);
    // 0x0054B640: the next item (null at the end).
    void* NextItem();
    // 0x0054A770 / 0x0054A760: the destructor bodies (the vtable reset).
    void DestroyCategorySearch();
    void DestroyItemSearch();
    // 0x0054A780 / 0x0054B280: the scalar deleting destructors.
    VaultSearch* DestroyCategorySearchAndFree(unsigned int flags);
    VaultSearch* DestroyItemSearchAndFree(unsigned int flags);
};

// A file to download (Download: the top stats and DownloadFile build it on the stack).
struct DownloadRequest {
    uint32_t bufferSize;     // +0x000
    char url[256];           // +0x004
    const void* callback;    // +0x104 ghttp's completed callback (param: the VaultMan)
    uint32_t failScript;     // +0x108
    uint32_t doneScript;     // +0x10C
    uint32_t progressScript; // +0x110
    uint32_t errorScript;    // +0x114 (run on failure with ghttp's result 11)
};
static_assert(sizeof(DownloadRequest) == 0x118, "built in the callers' stack frames");

class VaultMan {
public:
    // What m_transfer is doing.
    enum Transfer : int {
        kNone = 0,
        kCategories = 1,  // the directory list (dir_list.txt of the folder)
        kItems = 2,       // a category's items
        kDownload = 3,    // a file (Download)
        kUpload = 4,
        kUploads = 5,     // the uploads list (uploads.txt; profile 0x011B0C16 only)
        kDownload6 = 6,   // (treated as a download; never set in this range)
    };

    // 0x0054C1D0: no categories, the task (VaultTask, not in a list), the semaphore (1 of 10). Returns this.
    VaultMan* Construct();
    // 0x0054B470 (destructor body): the semaphore released once more, the task deleted, the search and the
    // categories' head reset.
    void DestroyBody();

    // 0x0054A910 (thiscall; the manager's object is used, not `this`): download `request`'s file on a thread
    // (DownloadThread); the task joins the logic tasks.
    void Download(const DownloadRequest* request);
    // 0x0054B370: fetch the list at `url` (sprintf'd into m_url) on a thread (ListThread) with the list scripts.
    void StartList(const char* url);
    // 0x0054B400: the items of `category` (transfer 2).
    void ListItems(VaultCategory* category);
    // 0x0054B500: the categories of m_folder (transfer 1); the task joins the logic tasks.
    void ListCategories();
    // 0x0054C130: every category (and its items) deleted; ghttp cleaned up.
    void FreeDirectoryListing();

    // 0x0054B6E0: the task: a transfer running for more than 25 s is cancelled (the fail script); else ghttp is
    // processed and the progress script run until 100%; then the answer read: the categories (then the first
    // category's items), the items (then the next category's, until all are in: net_vault_menu_create), the uploads
    // list, or the download / upload's done / fail scripts.
    static void __cdecl VaultTask(Tsk::BaseTask* task);
    // 0x0054A7B0 (thread, stdcall): ghttpGetEx of m_url into a new 100000-byte buffer.
    static unsigned long __stdcall ListThread(void* vault);
    // 0x0054A860 (thread, stdcall): ghttpGetEx of `url` (goastrdup'd by Download, freed here) into a buffer of
    // m_bufferSize, with the request's callback.
    static unsigned long __stdcall DownloadThread(void* url);
    // 0x0054AAC0 (thread, stdcall): the upload: the item's data structure written to a buffer and posted with the
    // type's fields, the description, the network id and the file name to thugonline.com/cgi-bin/upload2.pl.
    static unsigned long __stdcall UploadThread(void* vault);

    // ghttp's callbacks (cdecl; param: this).
    // 0x0054A660: the progress (percent of a known total size) and the time of the last news.
    static void __cdecl OnProgress(void* request, int state, const char* buffer, int64_t bufferLength,
                                   int64_t received, int64_t total, VaultMan* vault);
    // 0x0054A740: a list or upload finished: 100%, the result 1 (ok) or 2 (failed). Returns 1.
    static int __cdecl OnTransferDone(void* request, int result, char* buffer, int64_t bufferLength, VaultMan* vault);
    // 0x0054A6B0 (DownloadFile's): a file downloaded: loaded by type (0x005A8EB0 with the type's script), or the
    // failure kept. Returns 1.
    static int __cdecl OnFileDownloaded(void* request, int result, char* buffer, int64_t bufferLength,
                                        VaultMan* vault);

    Tsk::BaseTask* m_task;     // +0x000 Tsk::Task<VaultMan> (VaultTask)
    int m_transfer;            // +0x004 Transfer
    int m_progress;            // +0x008 percent; 100 = finished
    int m_result;              // +0x00C 0 running, 1 done, 2 failed
    int m_error;               // +0x010 ghttp's result (OnFileDownloaded)
    uint32_t m_bufferSize;     // +0x014
    char* m_buffer;            // +0x018 what came (or the upload's data)
    uint32_t m_startTime;      // +0x01C the time of the start or of the last progress
    char m_url[0x80];          // +0x020
    char m_fileName[0x80];     // +0x0A0 the upload's
    char m_description[0x200]; // +0x120 the upload's
    void* m_semaphore;         // +0x320 (HANDLE) held while a thread sets its request up
    int m_request;             // +0x324 GHTTPRequest
    Script::CStruct* m_data;   // +0x328 the upload's item (its `data` structure)
    int m_score;               // +0x32C the upload's details (UploadFile)
    int m_width;               // +0x330
    int m_length;              // +0x334 (posted as "height")
    int m_numPieces;           // +0x338
    int m_numGaps;             // +0x33C
    int m_numGoals;            // +0x340
    int m_theme;               // +0x344
    uint32_t m_todScript;      // +0x348
    int m_isMale;              // +0x34C
    Mem::ListNode m_categories; // +0x350 Lst::Head<VaultCategory> (vtable 0x0064D418)
    VaultSearch m_search;      // +0x364 the current category's place in the list
    VaultCategory* m_category; // +0x36C
    uint32_t m_type;           // +0x370 kVaultParks...
    char m_folder[0x40];       // +0x374
    uint32_t m_failScript;     // +0x3B4
    uint32_t m_doneScript;     // +0x3B8
    uint32_t m_progressScript; // +0x3BC
    uint32_t m_errorScript;    // +0x3C0
    const void* m_callback;    // +0x3C4 Download's completed callback
};
static_assert(offsetof(VaultMan, m_url) == 0x20 && offsetof(VaultMan, m_semaphore) == 0x320 &&
                  offsetof(VaultMan, m_data) == 0x328 && offsetof(VaultMan, m_categories) == 0x350 &&
                  offsetof(VaultMan, m_search) == 0x364 && offsetof(VaultMan, m_type) == 0x370 &&
                  offsetof(VaultMan, m_failScript) == 0x3B4 && offsetof(VaultMan, m_callback) == 0x3C4,
              "layout must match the original");

// The script commands.
bool __cdecl DownloadFile(Script::CStruct* params, Script::CScript* script);          // 0x0054AA10
bool __cdecl UploadFile(Script::CStruct* params, Script::CScript* script);            // 0x0054B060
bool __cdecl CancelFileTransfer(Script::CStruct* params, Script::CScript* script);    // 0x0054B240
bool __cdecl DownloadDirectoryList(Script::CStruct* params, Script::CScript* script); // 0x0054C290
bool __cdecl FreeDirectoryListing(Script::CStruct* params, Script::CScript* script);  // 0x0054C460
bool __cdecl FillVaultMenu(Script::CStruct* params, Script::CScript* script);         // 0x0054C470
bool __cdecl NextVaultCategory(Script::CStruct* params, Script::CScript* script);     // 0x0054C770
bool __cdecl PrevVaultCategory(Script::CStruct* params, Script::CScript* script);     // 0x0054C800

} // namespace GameNet
