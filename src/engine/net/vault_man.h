#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"

namespace script {
class Script;
class Struct;
} // namespace script
namespace tasks {
class SchedulerTask;
template <typename Owner>
class Task;
} // namespace tasks

// The content vault and HTTP file transfers: online::Hub's +0x14, on GameSpy's HTTP SDK (ghttp: library code,
// not decompiled - called by address). Created parks, goals, skaters and tricks are uploaded to and listed / fetched
// from www.thugonline.com: a directory list of categories (<folder>/dir_list.txt), each category's list of items
// (<folder>/<category>/dir_list.txt, one line each: "file:name:creator:...:description"), and the items themselves.
// The top stats (stats.cpp) download through the same object. Each transfer runs on a thread (ghttp's blocking set-up
// under the object's semaphore); the task watches it, times it out after 25 s, and runs the transfer's scripts.
// 0x0054A660-0x0054C9B0.
namespace online {

// The vault's item types (the scripts' `type`) and their folders.
constexpr uint32_t kVaultParks = 0x5BE5569F;   // "levels"
constexpr uint32_t kVaultSkaters = 0x07364EA1; // "skaters"
constexpr uint32_t kVaultTricks = 0x1E26FD3E;  // "tricks"
constexpr uint32_t kVaultGoals = 0x38DBE1D0;   // "goals"

// An item of a category (0x214 bytes): lists::Elem<VaultItem> (vtable 0x0064D414; its node destructor 0x0054B2D0).
struct VaultItem {
    memory::ListNode list_link_; // +0x000 data = this
    char url_[0x80];             // +0x014 http://www.thugonline.com/<folder>/<category>/<file>
    char label_[0x18];           // +0x094
    char creator_[0x10];         // +0x0AC
    char extent_[0x10];          // +0x0BC (parks)
    char unknowncc_[0x21];       // +0x0CC the field before the description
    char summary_text_[0x103];   // +0x0ED the rest of the line
    int points_held_;            // +0x1F0 (tricks)
    int span_;                   // +0x1F4 (never read from the list)
    int level_y_;                // +0x1F8 (never read from the list)
    int num_pieces_;             // +0x1FC (parks)
    int num_gaps_;               // +0x200 (parks)
    int num_goals_;              // +0x204 (parks, goals)
    int style_set_;              // +0x208 (parks)
    uint32_t tod_script_;        // +0x20C (parks)
    int has_male_body_;          // +0x210 (skaters)

    // 0x0054B570: unlinked, every string and number empty. Returns this.
    VaultItem* make_new();
    // 0x0054B5E0 (vtable 0x0064D414 slot 0, scalar deleting): the node destructor, freed with flag 1.
    VaultItem* teardown(unsigned int flags);
};
static_assert(sizeof(VaultItem) == 0x214 && offsetof(VaultItem, label_) == 0x94 &&
                  offsetof(VaultItem, summary_text_) == 0xED && offsetof(VaultItem, points_held_) == 0x1F0 &&
                  offsetof(VaultItem, has_male_body_) == 0x210,
              "layout must match the original");

// A category of the vault (0x44 bytes): lists::Elem<VaultCategory> (vtable 0x0064D41C) with its items.
struct VaultCategory {
    memory::ListNode list_link_; // +0x00 data = this
    bool skip_;                  // +0x14 not chosen when moving through the categories at the start ("Uploads")
    char label_[0x1B];           // +0x15
    memory::ListNode items_;     // +0x30 lists::Foremost<VaultItem> (vtable 0x0064D414)

    // 0x0054B670: unlinked, no items, not skipped. Returns this.
    VaultCategory* make_new();
    // 0x0054C890 (destructor body): the items deleted, the head and the node unlinked.
    void destroy_body();
    // 0x0054C980 (vtable 0x0064D41C slot 0, scalar deleting)
    VaultCategory* teardown(unsigned int flags);
};
static_assert(sizeof(VaultCategory) == 0x44 && offsetof(VaultCategory, label_) == 0x15 &&
                  offsetof(VaultCategory, items_) == 0x30,
              "layout must match the original");

// lists::Elem<VaultItem> / lists::Elem<VaultCategory> as the original destroys them.
struct VaultNode {
    memory::ListNode list_link_;

    // 0x0054B2D0 / 0x0054B2B0: the node destructors (the base vtable 0x0064D3AC / 0x0064D3B0, unlinked).
    void destroy_item_node();
    void destroy_category_node();
    // 0x0054B2F0 (vtable 0x0064D3AC slot 0) / 0x0054B330 (0x0064D3B0) / 0x0054B610 (the categories head's,
    // 0x0064D418): the scalar deleting destructors.
    VaultNode* delete_item_node(unsigned int flags);
    VaultNode* delete_category_node(unsigned int flags);
    VaultNode* delete_categories_head(unsigned int flags);
};

// A walk over a list (lists::Hunt<VaultCategory> / <VaultItem>: their vtables, 0x0064D29C / 0x0064D2A0, hold only
// destructors that reset them - 0x0054A770 / 0x0054A780, 0x0054A760 / 0x0054B280 - not kept): the walk's node.
struct VaultSearch {
    memory::ListNode* list_link_ = nullptr;

    // 0x0054B6B0: the first item of the list `head` (null if none).
    void* head_element(memory::ListNode* head);
    // 0x0054B640: the next item (null at the end).
    void* following_item();
};

// A file to download (Download: the top stats and DownloadFile build it on the stack).
struct DownloadRequest {
    uint32_t buffer_size_;     // +0x000
    char url[256];             // +0x004
    const void* callback;      // +0x104 ghttp's completed callback (param: the VaultMan)
    uint32_t fail_script_;     // +0x108
    uint32_t done_script_;     // +0x10C
    uint32_t progress_script_; // +0x110
    uint32_t error_script_;    // +0x114 (run on failure with ghttp's result 11)
};
static_assert(sizeof(DownloadRequest) == 0x118, "built in the callers' stack frames");

class VaultMan {
public:
    // What transfer_ is doing.
    enum Transfer : int {
        kNone = 0,
        kCategories = 1, // the directory list (dir_list.txt of the folder)
        kItems = 2,      // a category's items
        kDownload = 3,   // a file (Download)
        kUpload = 4,
        kUploads = 5,   // the uploads list (uploads.txt; profile 0x011B0C16 only)
        kDownload6 = 6, // (treated as a download; never set in this range)
    };

    // 0x0054C1D0: no categories, the task (vault_task, not in a list), the semaphore (1 of 10). Returns this.
    VaultMan* make_new();
    // 0x0054B470 (destructor body): the semaphore released once more, the task deleted, the search and the
    // categories' head reset.
    void destroy_body();

    // 0x0054A910 (thiscall; the manager's object is used, not `this`): download `request`'s file on a thread
    // (download_thread); the task joins the logic tasks.
    void download(const DownloadRequest* request);
    // 0x0054B370: fetch the list at `url` (sprintf'd into url_) on a thread (list_thread) with the list scripts.
    void start_list(const char* url);
    // 0x0054B400: the items of `category` (transfer 2).
    void list_items(VaultCategory* category);
    // 0x0054B500: the categories of folder_ (transfer 1); the task joins the logic tasks.
    void list_categories();
    // 0x0054C130: every category (and its items) deleted; ghttp cleaned up.
    void free_directory_listing();

    // 0x0054B6E0: the task: a transfer running for more than 25 s is cancelled (the fail script); else ghttp is
    // processed and the progress script run until 100%; then the answer read: the categories (then the first
    // category's items), the items (then the next category's, until all are in: net_vault_menu_create), the uploads
    // list, or the download / upload's done / fail scripts.
    static void __cdecl vault_task(tasks::Task<VaultMan>* task);
    // 0x0054A7B0 (thread, stdcall): ghttpGetEx of url_ into a new 100000-byte buffer.
    static unsigned long __stdcall list_thread(void* vault);
    // 0x0054A860 (thread, stdcall): ghttpGetEx of `url` (goastrdup'd by Download, freed here) into a buffer of
    // buffer_size_, with the request's callback.
    static unsigned long __stdcall download_thread(void* url);
    // 0x0054AAC0 (thread, stdcall): the upload: the item's data structure written to a buffer and posted with the
    // type's fields, the description, the network id and the file name to thugonline.com/cgi-bin/upload2.pl.
    static unsigned long __stdcall upload_thread(void* vault);

    // ghttp's callbacks (cdecl; param: this).
    // 0x0054A660: the progress (percent of a known total size) and the time of the last news.
    static void __cdecl on_progress(void* request, int state, const char* buffer, int64_t buffer_length,
                                    int64_t received, int64_t total, VaultMan* vault);
    // 0x0054A740: a list or upload finished: 100%, the result 1 (ok) or 2 (failed). Returns 1.
    static int __cdecl on_transfer_done(void* request, int result, char* buffer, int64_t buffer_length,
                                        VaultMan* vault);
    // 0x0054A6B0 (DownloadFile's): a file downloaded: loaded by type (0x005A8EB0 with the type's script), or the
    // failure kept. Returns 1.
    static int __cdecl on_file_downloaded(void* request, int result, char* buffer, int64_t buffer_length,
                                          VaultMan* vault);

    tasks::SchedulerTask* task_;  // +0x000 tasks::Task<VaultMan> (vault_task)
    int transfer_;                // +0x004 Transfer
    int progress_;                // +0x008 percent; 100 = finished
    int outcome_val_;             // +0x00C 0 running, 1 done, 2 failed
    int fault_;                   // +0x010 ghttp's result (on_file_downloaded)
    uint32_t buffer_size_;        // +0x014
    char* octets_;                // +0x018 what came (or the upload's data)
    uint32_t start_time_;         // +0x01C the time of the start or of the last progress
    char url_[0x80];              // +0x020
    char source_path_[0x80];      // +0x0A0 the upload's
    char summary_text_[0x200];    // +0x120 the upload's
    void* semaphore_;             // +0x320 (HANDLE) held while a thread sets its request up
    int req_;                     // +0x324 GHTTPRequest
    script::Struct* content_;     // +0x328 the upload's item (its `data` structure)
    int points_held_;             // +0x32C the upload's details (UploadFile)
    int span_;                    // +0x330
    int byte_count_;              // +0x334 (posted as "height")
    int num_pieces_;              // +0x338
    int num_gaps_;                // +0x33C
    int num_goals_;               // +0x340
    int style_set_;               // +0x344
    uint32_t tod_script_;         // +0x348
    int has_male_body_;           // +0x34C
    memory::ListNode categories_; // +0x350 lists::Foremost<VaultCategory> (vtable 0x0064D418)
    VaultSearch search_;          // +0x364 the current category's place in the list
    VaultCategory* category_;     // +0x36C
    uint32_t kind_tag_;           // +0x370 kVaultParks...
    char folder_[0x40];           // +0x374
    uint32_t fail_script_;        // +0x3B4
    uint32_t done_script_;        // +0x3B8
    uint32_t progress_script_;    // +0x3BC
    uint32_t error_script_;       // +0x3C0
    const void* callback_;        // +0x3C4 Download's completed callback
};
// (The offsets in the comments are the original's: its search_ has a vtable too, so from category_ on they're 4
// more than here.)
static_assert(offsetof(VaultMan, url_) == 0x20 && offsetof(VaultMan, semaphore_) == 0x320 &&
                  offsetof(VaultMan, content_) == 0x328 && offsetof(VaultMan, categories_) == 0x350 &&
                  offsetof(VaultMan, search_) == 0x364 && sizeof(VaultMan) == 0x3C4,
              "the original's layout less the search's vtable");

// The script commands.
bool __cdecl download_file(script::Struct* params, script::Script* script);           // 0x0054AA10
bool __cdecl upload_file(script::Struct* params, script::Script* script);             // 0x0054B060
bool __cdecl cancel_file_transfer(script::Struct* params, script::Script* script);    // 0x0054B240
bool __cdecl download_directory_list(script::Struct* params, script::Script* script); // 0x0054C290
bool __cdecl free_directory_listing(script::Struct* params, script::Script* script);  // 0x0054C460
bool __cdecl fill_vault_menu(script::Struct* params, script::Script* script);         // 0x0054C470
bool __cdecl next_vault_category(script::Struct* params, script::Script* script);     // 0x0054C770
bool __cdecl prev_vault_category(script::Struct* params, script::Script* script);     // 0x0054C800

} // namespace online
