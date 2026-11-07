#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "base/core/layout_32bit.h"
#include "engine/memory/arena_alloc.h"
#include <gsl/pointers>
#include <array>

namespace script {
class Context;
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

constexpr size_t kVaultItemUrlSize = 0x80;
constexpr size_t kVaultItemLabelSize = 0x18;
constexpr size_t kVaultItemCreatorSize = 0x10;
constexpr size_t kVaultItemExtentSize = 0x10;
constexpr size_t kVaultItemUnknownccSize = 0x21;
constexpr size_t kVaultItemSummaryTextSize = 0x103;
// An item of a category (0x214 bytes): lists::Elem<VaultItem> (its node destructor 0x0054B2D0; the node keeps the
// original's size).
struct VaultItem {
    // The fields other code reads and sets.
    memory::ListNode& list_link() { return list_link_; }
    [[nodiscard]] const memory::ListNode& list_link() const { return list_link_; }
    std::array<char, kVaultItemUrlSize>& url() { return url_; }
    [[nodiscard]] const std::array<char, kVaultItemUrlSize>& url() const { return url_; }
    std::array<char, kVaultItemLabelSize>& label() { return label_; }
    [[nodiscard]] const std::array<char, kVaultItemLabelSize>& label() const { return label_; }
    std::array<char, kVaultItemCreatorSize>& creator() { return creator_; }
    [[nodiscard]] const std::array<char, kVaultItemCreatorSize>& creator() const { return creator_; }
    std::array<char, kVaultItemExtentSize>& extent() { return extent_; }
    [[nodiscard]] const std::array<char, kVaultItemExtentSize>& extent() const { return extent_; }
    std::array<char, kVaultItemUnknownccSize>& unknowncc() { return unknowncc_; }
    [[nodiscard]] const std::array<char, kVaultItemUnknownccSize>& unknowncc() const { return unknowncc_; }
    std::array<char, kVaultItemSummaryTextSize>& summary_text() { return summary_text_; }
    [[nodiscard]] const std::array<char, kVaultItemSummaryTextSize>& summary_text() const { return summary_text_; }
    [[nodiscard]] int points_held() const { return points_held_; }
    void set_points_held(int value) { points_held_ = value; }
    [[nodiscard]] int span() const { return span_; }
    [[nodiscard]] int level_y() const { return level_y_; }
    [[nodiscard]] int num_pieces() const { return num_pieces_; }
    void set_num_pieces(int value) { num_pieces_ = value; }
    [[nodiscard]] int num_gaps() const { return num_gaps_; }
    void set_num_gaps(int value) { num_gaps_ = value; }
    [[nodiscard]] int num_goals() const { return num_goals_; }
    void set_num_goals(int value) { num_goals_ = value; }
    [[nodiscard]] int style_set() const { return style_set_; }
    void set_style_set(int value) { style_set_ = value; }
    [[nodiscard]] uint32_t tod_script() const { return tod_script_; }
    void set_tod_script(uint32_t value) { tod_script_ = value; }
    [[nodiscard]] int has_male_body() const { return has_male_body_; }
    void set_has_male_body(int value) { has_male_body_ = value; }

private:
    memory::ListNode list_link_{}; // +0x000 data = this
    std::array<char, kVaultItemUrlSize> url_{};             // +0x014 http://www.thugonline.com/<folder>/<category>/<file>
    std::array<char, kVaultItemLabelSize> label_{};           // +0x094
    std::array<char, kVaultItemCreatorSize> creator_{};         // +0x0AC
    std::array<char, kVaultItemExtentSize> extent_{};          // +0x0BC (parks)
    std::array<char, kVaultItemUnknownccSize> unknowncc_{};       // +0x0CC the field before the description
    std::array<char, kVaultItemSummaryTextSize> summary_text_{};   // +0x0ED the rest of the line
    int points_held_{0};           // +0x1F0 (tricks)
    int span_{0};                  // +0x1F4 (never read from the list)
    int level_y_{0};               // +0x1F8 (never read from the list)
    int num_pieces_{0};            // +0x1FC (parks)
    int num_gaps_{0};              // +0x200 (parks)
    int num_goals_{0};             // +0x204 (parks, goals)
    int style_set_{0};             // +0x208 (parks)
    uint32_t tod_script_{0};       // +0x20C (parks)
    int has_male_body_{0};         // +0x210 (skaters)

public:

    // 0x0054B570: unlinked, every string and number empty.
    VaultItem();
    // 0x0054B5E0 (scalar deleting): the node destructor, freed with flag 1.
    VaultItem* teardown(unsigned int flags);
};

constexpr size_t kVaultCategoryLabelSize = 0x1B;
// A category of the vault (0x44 bytes): lists::Elem<VaultCategory> with its items.
struct VaultCategory {
    // The fields other code reads and sets.
    memory::ListNode& list_link() { return list_link_; }
    [[nodiscard]] const memory::ListNode& list_link() const { return list_link_; }
    [[nodiscard]] bool skip() const { return skip_; }
    void set_skip(bool value) { skip_ = value; }
    std::array<char, kVaultCategoryLabelSize>& label() { return label_; }
    [[nodiscard]] const std::array<char, kVaultCategoryLabelSize>& label() const { return label_; }
    memory::ListNode& items() { return items_; }
    [[nodiscard]] const memory::ListNode& items() const { return items_; }

private:
    memory::ListNode list_link_{}; // +0x00 data = this
    bool skip_{false};             // +0x14 not chosen when moving through the categories at the start ("Uploads")
    std::array<char, kVaultCategoryLabelSize> label_{};         // +0x15
    memory::ListNode items_{};   // +0x30 lists::Foremost<VaultItem>

public:

    // 0x0054B670: unlinked, no items, not skipped.
    VaultCategory();
    // 0x0054C890 (destructor body): the items deleted, the head and the node unlinked.
    ~VaultCategory();
    VaultCategory(const VaultCategory&) = delete;
    VaultCategory& operator=(const VaultCategory&) = delete;
    // 0x0054C980 (scalar deleting)
    VaultCategory* teardown(unsigned int flags);
};

// (lists::Elem<VaultItem> / <VaultCategory>'s destructors - 0x0054B2D0 / 0x0054B2B0, the deleting 0x0054B2F0 /
// 0x0054B330 and the categories head's 0x0054B610 - take the node out of its list: memory::ListNode::uncouple.)

// A walk over a list (lists::Hunt<VaultCategory> / <VaultItem>: their vtables, 0x0064D29C / 0x0064D2A0, hold only
// destructors that reset them - 0x0054A770 / 0x0054A780, 0x0054A760 / 0x0054B280 - not kept): the walk's node.
struct VaultSearch {
    // The fields other code reads and sets.
    memory::ListNode*& list_link() { return list_link_; }
    [[nodiscard]] memory::ListNode* list_link() const { return list_link_; }
    void set_list_link(memory::ListNode* value) { list_link_ = value; }

private:
    memory::ListNode* list_link_ = nullptr;

public:

    // 0x0054B6B0: the first item of the list `head` (null if none).
    void* head_element(memory::ListNode* head);
    // 0x0054B640: the next item (null at the end).
    void* following_item();
};

constexpr size_t kDownloadRequestBytes = 0x118;
constexpr size_t kDownloadRequestUrlSize = 256;
// A file to download (Download: the top stats and DownloadFile build it on the stack).
struct DownloadRequest {
    uint32_t buffer_size_;     // +0x000
    std::array<char, kDownloadRequestUrlSize> url;             // +0x004
    const void* callback;      // +0x104 ghttp's completed callback (param: the VaultMan)
    uint32_t fail_script_;     // +0x108
    uint32_t done_script_;     // +0x10C
    uint32_t progress_script_; // +0x110
    uint32_t error_script_;    // +0x114 (run on failure with ghttp's result 11)
};
static_assert(core::layout_32bit(sizeof(DownloadRequest) == kDownloadRequestBytes), "built in the callers' stack frames");

constexpr size_t kVaultManSourcePathSize = 0x80;
constexpr size_t kVaultManSummaryTextSize = 0x200;
constexpr size_t kVaultManFolderSize = 0x40;
constexpr size_t kVaultManUrlSize = 0x80;
class VaultMan {
public:
    // What transfer_ is doing.
    using Transfer = uint8_t;
    static constexpr int kNone = 0;
    static constexpr int kCategories = 1; // the directory list (dir_list.txt of the folder)
    static constexpr int kItems = 2;      // a category's items
    static constexpr int kDownload = 3;   // a file (Download)
    static constexpr int kUpload = 4;
    static constexpr int kUploads = 5;   // the uploads list (uploads.txt; profile 0x011B0C16 only)
    static constexpr int kDownload6 = 6; // (treated as a download; never set in this range)

    // 0x0054C1D0: no categories, the task (vault_task, not in a list), the semaphore (1 of 10).
    VaultMan();
    // 0x0054B470: the semaphore released once more, the task deleted, the search and the categories' head reset.
    ~VaultMan();
    VaultMan(const VaultMan&) = delete;
    VaultMan& operator=(const VaultMan&) = delete;

    // 0x0054A910 (thiscall; the manager's object is used, not `this`): download `request`'s file on a thread
    // (download_thread); the task joins the logic tasks.
    static void download(const DownloadRequest* request);
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
    static void vault_task(tasks::Task<VaultMan>* task);
    // 0x0054A7B0 (thread, stdcall): ghttpGetEx of url_ into a new 100000-byte buffer.
    static unsigned long __stdcall list_thread(void* param);
    // 0x0054A860 (thread, stdcall): ghttpGetEx of `url` (goastrdup'd by Download, freed here) into a buffer of
    // buffer_size_, with the request's callback.
    static unsigned long __stdcall download_thread(void* url);
    // 0x0054AAC0 (thread, stdcall): the upload: the item's data structure written to a buffer and posted with the
    // type's fields, the description, the network id and the file name to thugonline.com/cgi-bin/upload2.pl.
    static unsigned long __stdcall upload_thread(void* param);

    // ghttp's callbacks (cdecl; param: this).
    // 0x0054A660: the progress (percent of a known total size) and the time of the last news.
    static void on_progress(void* request, int state, const char* buffer, int64_t buffer_length, int64_t received,
                            int64_t total, VaultMan* vault);
    // 0x0054A740: a list or upload finished: 100%, the result 1 (ok) or 2 (failed). Returns 1.
    static int on_transfer_done(void* request, int result, char* buffer, int64_t buffer_length, VaultMan* vault);
    // 0x0054A6B0 (DownloadFile's): a file downloaded: loaded by type (0x005A8EB0 with the type's script), or the
    // failure kept. Returns 1.
    static int on_file_downloaded(void* request, int result, char* buffer, int64_t received, VaultMan* vault);

    // The fields other code reads and sets.
    std::unique_ptr<tasks::SchedulerTask>& task() { return task_; }
    [[nodiscard]] const std::unique_ptr<tasks::SchedulerTask>& task() const { return task_; }
    [[nodiscard]] int transfer() const { return transfer_; }
    void set_transfer(int value) { transfer_ = value; }
    void set_progress(int value) { progress_ = value; }
    void set_outcome_val(int value) { outcome_val_ = value; }
    void set_fault(int value) { fault_ = value; }
    char*& octets() { return octets_; }
    [[nodiscard]] char* octets() const { return octets_; }
    void set_start_time(uint32_t value) { start_time_ = value; }
    std::array<char, kVaultManSourcePathSize>& source_path() { return source_path_; }
    [[nodiscard]] const std::array<char, kVaultManSourcePathSize>& source_path() const { return source_path_; }
    std::array<char, kVaultManSummaryTextSize>& summary_text() { return summary_text_; }
    [[nodiscard]] const std::array<char, kVaultManSummaryTextSize>& summary_text() const { return summary_text_; }
    [[nodiscard]] void* semaphore() const { return semaphore_; }
    [[nodiscard]] int req() const { return req_; }
    script::Struct*& content() { return content_; }
    [[nodiscard]] script::Struct* content() const { return content_; }
    int& points_held() { return points_held_; }
    [[nodiscard]] int points_held() const { return points_held_; }
    int& span() { return span_; }
    [[nodiscard]] int span() const { return span_; }
    int& byte_count() { return byte_count_; }
    [[nodiscard]] int byte_count() const { return byte_count_; }
    int& num_pieces() { return num_pieces_; }
    [[nodiscard]] int num_pieces() const { return num_pieces_; }
    int& num_gaps() { return num_gaps_; }
    [[nodiscard]] int num_gaps() const { return num_gaps_; }
    int& num_goals() { return num_goals_; }
    [[nodiscard]] int num_goals() const { return num_goals_; }
    int& style_set() { return style_set_; }
    [[nodiscard]] int style_set() const { return style_set_; }
    uint32_t& tod_script() { return tod_script_; }
    [[nodiscard]] uint32_t tod_script() const { return tod_script_; }
    int& has_male_body() { return has_male_body_; }
    [[nodiscard]] int has_male_body() const { return has_male_body_; }
    memory::ListNode& categories() { return categories_; }
    [[nodiscard]] const memory::ListNode& categories() const { return categories_; }
    VaultSearch& search() { return search_; }
    [[nodiscard]] const VaultSearch& search() const { return search_; }
    [[nodiscard]] VaultCategory* category() const { return category_; }
    void set_category(VaultCategory* value) { category_ = value; }
    uint32_t& kind_tag() { return kind_tag_; }
    [[nodiscard]] uint32_t kind_tag() const { return kind_tag_; }
    std::array<char, kVaultManFolderSize>& folder() { return folder_; }
    [[nodiscard]] const std::array<char, kVaultManFolderSize>& folder() const { return folder_; }
    void set_fail_script(uint32_t value) { fail_script_ = value; }

private:
    // vault_task's finished transfers: the category list, a category's items, the user's uploads.
    static void read_categories(VaultMan* vault);
    static void read_items(VaultMan* vault);
    static void read_uploads(VaultMan* vault);

    std::unique_ptr<tasks::SchedulerTask> task_; // +0x000 tasks::Task<VaultMan> (vault_task)
    int transfer_{kNone};                        // +0x004 Transfer
    int progress_{0};                            // +0x008 percent; 100 = finished
    int outcome_val_{};                          // +0x00C 0 running, 1 done, 2 failed
    int fault_{};                                // +0x010 ghttp's result (on_file_downloaded)
    uint32_t buffer_size_{};                     // +0x014
    char* octets_{};                             // +0x018 what came (or the upload's data)
    uint32_t start_time_{0};                     // +0x01C the time of the start or of the last progress
    std::array<char, kVaultManUrlSize> url_{};                           // +0x020
    std::array<char, kVaultManSourcePathSize> source_path_{};                   // +0x0A0 the upload's
    std::array<char, kVaultManSummaryTextSize> summary_text_{};                 // +0x120 the upload's
    void* semaphore_;                            // +0x320 (HANDLE) held while a thread sets its request up
    int req_{};                                  // +0x324 GHTTPRequest
    script::Struct* content_{};                  // +0x328 the upload's item (its `data` structure)
    int points_held_{};                          // +0x32C the upload's details (UploadFile)
    int span_{};                                 // +0x330
    int byte_count_{};                           // +0x334 (posted as "height")
    int num_pieces_{};                           // +0x338
    int num_gaps_{};                             // +0x33C
    int num_goals_{};                            // +0x340
    int style_set_{};                            // +0x344
    uint32_t tod_script_{};                      // +0x348
    int has_male_body_{};                        // +0x34C
    memory::ListNode categories_{};              // +0x350 lists::Foremost<VaultCategory>
    VaultSearch search_;                         // +0x364 the current category's place in the list
    VaultCategory* category_{};                  // +0x36C
    uint32_t kind_tag_{};                        // +0x370 kVaultParks...
    std::array<char, kVaultManFolderSize> folder_{};                        // +0x374
    uint32_t fail_script_{};                     // +0x3B4
    uint32_t done_script_{};                     // +0x3B8
    uint32_t progress_script_{};                 // +0x3BC
    uint32_t error_script_{};                    // +0x3C0
    const void* callback_{};                     // +0x3C4 Download's completed callback
};
// (The offsets in the comments are the original's: its search_ has a vtable too, so from category_ on they're 4
// more than here.)

// The script commands.
bool download_file(script::Struct* params, script::Context context);           // 0x0054AA10
bool upload_file(script::Struct* params, script::Context context);             // 0x0054B060
bool cancel_file_transfer(script::Struct* params, script::Context context);    // 0x0054B240
bool download_directory_list(script::Struct* params, script::Context context); // 0x0054C290
bool free_directory_listing(script::Struct* params, script::Context context);  // 0x0054C460
bool fill_vault_menu(script::Struct* params, script::Context context);         // 0x0054C470
bool next_vault_category(script::Struct* params, script::Context context);     // 0x0054C770
bool prev_vault_category(script::Struct* params, script::Context context);     // 0x0054C800

} // namespace online
