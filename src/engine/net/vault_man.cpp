// The content vault and HTTP file transfers (online::VaultMan). See vault_man.h.

#include "engine/net/vault_man.h"
#include "platform/gamespy.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <utility>
#include <array>

#include "engine/core/checksums.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/task_list.h"
#include "engine/net/buddy_man.h"
#include "engine/net/gamenet_manager.h"
#include "engine/net/online_text.h"
#include "engine/script/cscript.h"
#include "engine/script/preferences.h"
#include "engine/script/param_struct.h"
#include "engine/script/struct_buffer.h"
#include "platform/stopwatch.h"
#include "game/save_cards/save_types.h"
#include "platform/code_address.h"
#include "engine/assets/resource_registry.h"
#include "platform/crt.h"
#include "engine/sys/mlp.h"
#include "engine/core/parse_number.h"
#include "engine/core/arena_alloc.h"
#include "engine/core/checked_index.h"
#include <gsl/pointers>
#include "engine/core/format.h"
#include "engine/net/net_game_state.h"

namespace {
constexpr SIZE_T kThreadStackBytes = 0x8000;
constexpr int kMaxSemaphoreCount = 10;
constexpr int kProgressComplete = 100;
// The progress callback's states while a file comes in or goes out.
constexpr int kProgressStateReceiving = 3;
constexpr int kProgressStatePosting = 7;
constexpr size_t kUploadsLabelBytes = 8;

// Names (the checksums of their text).
constexpr uint32_t kCAT = 0x61A1BC57u; // CAT
constexpr uint32_t kCreatedGoals = 0x62896EDFu; // CreatedGoals
constexpr uint32_t kOptionsAndPros = 0xB010F357u; // OptionsAndPros
constexpr uint32_t kPARK = 0x3BF882CCu; // PARK

} // namespace

namespace online {

namespace {

constexpr uint32_t kTimeout = 25000; // ms without progress
constexpr uint32_t kListBufferSize = 100000;
constexpr uint32_t kDownloadFileSize = 0x14000;
constexpr int kErrorScriptResult = 11; // ghttp's result that runs the error script instead of the fail script
// The profile that may list the uploads (DownloadDirectoryList).
constexpr int kUploadsProfile = 0x011B0C16;

// The list and download scripts (StartList, DownloadFile).
constexpr uint32_t kFailScript = 0x7335AB7F;
constexpr uint32_t kDoneScript = 0x0089990F;
constexpr uint32_t kProgressScript = 0x4B85EE64;
constexpr uint32_t kErrorScript = 0xCF040665;

VaultMan* get_vault() { return net_game_state().online_hub()->vault_man().get(); }
memory::Hub* ram_stats() { return memory::Hub::get_pull(); }
void push_top_down() { ram_stats()->enter_heap_scope(ram_stats()->internet_top_down_heap()); }
void push_bottom_up() { ram_stats()->enter_heap_scope(ram_stats()->internet_bottom_up_heap()); }
void pop_heap() { ram_stats()->leave_heap_scope(); }
tasks::PileUp* get_logic_tasks() { return (&assets::get_main_loop_manager()->logic_tasks()); }

uint32_t get_checksums(const char* name) { return checksums::hash_from_text(name); }
void get_run(const char* script, script::Struct* params = nullptr)
{
    script::launch_script(script, params, nullptr, false);
}
void get_run(uint32_t script, script::Struct* params = nullptr)
{
    script::launch_script(script, params, nullptr, false, nullptr);
}

// The semaphore (the threads set their request up while holding it).
void get_stall(void* semaphore) { WaitForSingleObject(static_cast<HANDLE>(semaphore), INFINITE); }
void get_release(void* semaphore) { ReleaseSemaphore(static_cast<HANDLE>(semaphore), 1, nullptr); }
// A thread made suspended; the manager notes its id (set_room_id) before it starts.
HANDLE start_thread(LPTHREAD_START_ROUTINE thread, void* param)
{
    DWORD id = 0;
    HANDLE handle = CreateThread(nullptr, kThreadStackBytes, thread, param, CREATE_SUSPENDED, &id);
    net_game_state().online_hub()->set_room_id(id);
    return handle; // (never closed)
}

// GameSpy's HTTP SDK (library code, not decompiled; cdecl).
void ghttp_think() { game_spy::http_think(); }
void ghttp_cancel_request(int request) { game_spy::http_cancel_request(request); }
void ghttp_cleanup() { game_spy::http_cleanup(); }
int ghttp_get_ex(const char* url, const char* headers, char* buffer, uint32_t size, void* post, int throttle,
                 int blocking, const void* progress, const void* completed, void* param)
{
    return game_spy::http_get_ex(url, headers, buffer, static_cast<int>(size), post, throttle, blocking, progress,
                                 completed, param);
}
void* ghttp_new_post() { return game_spy::http_new_post(); }
void ghttp_post_add_string(void* post, const char* name, const char* value)
{
    game_spy::http_post_add_string(post, name, value);
}
void ghttp_post_add_file_from_memory(void* post, const char* name, const char* buffer, uint32_t size,
                                     const char* report_name, const char* content_type)
{
    game_spy::http_post_add_file_from_memory(post, name, buffer, static_cast<int>(size), report_name, content_type);
}
int ghttp_post_ex(const char* url, const char* headers, void* post, int throttle, int blocking, const void* progress,
                  const void* completed, void* param)
{
    return game_spy::http_post_ex(url, headers, post, throttle, blocking, progress, completed, param);
}
// The GameSpy SDK's strdup (goastrdup, the game's C runtime malloc) and the game runtime's free for it.
char* game_spy_strdup(const char* text) { return game_spy::game_strdup(text); }
void game_crt_free(gsl::owner<void*> memory) { crt::dealloc(memory); }
// The GameSpy SDK's id of this machine (GOAGetUniqueID).
const char* game_spy_unique_id() { return game_spy::get_unique_id(); }
// 0x005A8EB0: a downloaded item (`size` bytes) loaded from `buffer` by the type's script.
void load_downloaded_item(const void* buffer, size_t size, uint32_t script)
{
    save_cards::keep_loaded_contents(static_cast<const uint8_t*>(buffer), size, checksums::Name{script});
}

// The folder of an item type ("levels"...; null for an unknown type).
const char* type_folder(uint32_t type)
{
    switch (type) {
    case kVaultParks:
        return "levels";
    case kVaultSkaters:
        return "skaters";
    case kVaultTricks:
        return "tricks";
    case kVaultGoals:
        return "goals";
    default:
        return nullptr;
    }
}
// The upload's file type ("cap" park, "cas" skater, "cat" trick, "cag" goal; null for an unknown type).
const char* upload_file_type(uint32_t type)
{
    switch (type) {
    case kVaultParks:
        return "cap";
    case kVaultSkaters:
        return "cas";
    case kVaultTricks:
        return "cat";
    case kVaultGoals:
        return "cag";
    default:
        return nullptr;
    }
}

void init_node(memory::ListNode* node) { node->make_node(node); }
// `node` at the front of the list (the lists of the vault are built that way: the last line first).
void prepend(memory::ListNode* head, memory::ListNode* node)
{
    node->set_prev(head);
    node->set_next(head->next());
    head->next()->set_prev(node);
    head->set_next(node);
}
// The list walk's step: `node`'s successor, or null at the head.
memory::ListNode* get_next_node(memory::ListNode* node)
{
    memory::ListNode* next = node->next();
    return next->data() == memory::ListNode::head_marker() ? nullptr : next;
}

// The next field of a list line: the text up to the next ':' (which is cut there), and `*rest` moved past it.
// (A line with too few fields writes through null, as the original's strstr does.)
char* prop(char** rest)
{
    char* field = *rest;
    char* end = std::strstr(field, ":");
    *end = '\0';
    *rest = std::next(end);
    return field;
}
int number_field(char** rest)
{
    constexpr size_t kNumberSize = 128;
    std::array<char, kNumberSize> number{};
    text_tools::dup_from(number.data(), prop(rest));
    return text::to_int(number.data());
}

} // namespace

// --- the list nodes, items and categories ---

// 0x0054B570
VaultItem::VaultItem()
{
    init_node(&list_link_);
    core::at(url_, 0) = '\0';
    core::at(label_, 0) = '\0';
    core::at(creator_, 0) = '\0';
    core::at(extent_, 0) = '\0';
    core::at(unknowncc_, 0) = '\0';
    core::at(summary_text_, 0) = '\0';
}

// 0x0054B5E0
VaultItem* VaultItem::teardown(unsigned int flags)
{
    list_link_.uncouple(); // (0x0054B2D0)
    if (flags & 1)
        ram_stats()->dealloc(this);
    return this;
}

// 0x0054B670
VaultCategory::VaultCategory()
{
    init_node(&list_link_);
    items_.make_head();
}

// 0x0054C890
VaultCategory::~VaultCategory()
{
    for (memory::ListNode* node = get_next_node(&items_); node;) {
        memory::ListNode* next = get_next_node(node);
        static_cast<VaultItem*>(node->data())->teardown(1);
        node = next;
    }
    items_.uncouple();
    list_link_.uncouple(); // (0x0054B2B0)
}

// 0x0054C980
VaultCategory* VaultCategory::teardown(unsigned int flags)
{
    this->~VaultCategory();
    if (flags & 1)
        ram_stats()->dealloc(this);
    return this;
}

// 0x0054B6B0
void* VaultSearch::head_element(memory::ListNode* head)
{
    list_link_ = head;
    return following_item();
}

// 0x0054B640
void* VaultSearch::following_item()
{
    list_link_ = get_next_node(list_link_);
    return list_link_ ? list_link_->data() : nullptr;
}

// --- the vault ---

// 0x0054C1D0
VaultMan::VaultMan()
{
    categories_.make_head();
    search_.set_list_link(nullptr);
    task_ = std::make_unique<tasks::Task<VaultMan>>(&vault_task, this, 0);
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): set after calls that may see it: the original's order
    semaphore_ = CreateSemaphoreA(nullptr, 1, kMaxSemaphoreCount, nullptr);
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x0054B470
VaultMan::~VaultMan()
{
    get_release(semaphore_);
    task_.reset();
    categories_.uncouple();
}

// 0x0054A910
void VaultMan::download(const DownloadRequest* request)
{
    push_bottom_up();
    VaultMan* vault = get_vault();
    vault->transfer_ = kDownload;
    vault->outcome_val_ = 0;
    get_logic_tasks()->queue_job(vault->task_.get());
    vault->progress_ = 0;
    vault->start_time_ = frame_clock::get_time();
    vault->buffer_size_ = request->buffer_size_;
    vault->fail_script_ = request->fail_script_;
    vault->done_script_ = request->done_script_;
    vault->progress_script_ = request->progress_script_;
    vault->error_script_ = request->error_script_;
    vault->callback_ = request->callback;
    char* url = game_spy_strdup(request->url.data());
    HANDLE thread = start_thread(&download_thread, url);
    get_stall(vault->semaphore_);
    ResumeThread(thread);
    get_release(vault->semaphore_);
    pop_heap();
}

// 0x0054B370
void VaultMan::start_list(const char* url)
{
    // (the original uses `url` as sprintf's format)
    text::format_to(url_, "%s", url);
    progress_ = 0;
    start_time_ = frame_clock::get_time();
    done_script_ = kDoneScript;
    fail_script_ = kFailScript;
    error_script_ = kErrorScript;
    progress_script_ = kProgressScript;
    HANDLE thread = start_thread(&list_thread, this);
    ResumeThread(thread);
}

// 0x0054B400
void VaultMan::list_items(VaultCategory* category)
{
    constexpr size_t kUrlSize = 128;
    std::array<char, kUrlSize> url{};
    text::format_to(url, "http://www.thugonline.com/%s/%s/dir_list.txt", folder_.data(), category->label().data());
    transfer_ = kItems;
    outcome_val_ = 0;
    start_list(url.data());
}

// 0x0054B500
void VaultMan::list_categories()
{
    transfer_ = kCategories;
    constexpr size_t kUrlSize = 128;
    std::array<char, kUrlSize> url{};
    text::format_to(url, "http://www.thugonline.com/%s/dir_list.txt", folder_.data());
    start_list(url.data());
    outcome_val_ = 0;
    get_logic_tasks()->queue_job(task_.get());
}

// 0x0054C130
void VaultMan::free_directory_listing()
{
    for (memory::ListNode* node = get_next_node(&categories_); node;) {
        memory::ListNode* next = get_next_node(node);
        static_cast<VaultCategory*>(node->data())->teardown(1);
        node = next;
    }
    ghttp_cleanup();
}

// 0x0054B6E0
void VaultMan::vault_task(tasks::Task<VaultMan>* task)
{
    VaultMan* vault = task->content();
    if (frame_clock::get_time() - vault->start_time_ > kTimeout) {
        if (vault->transfer_ != kNone)
            ram_stats()->dealloc(vault->octets_);
        ghttp_cancel_request(vault->req_);
        get_run(vault->fail_script_);
        vault->transfer_ = kNone;
        vault->task_->drop_out();
        return;
    }
    push_top_down();
    ghttp_think();
    pop_heap();

    const int transfer = vault->transfer_;
    if (vault->progress_ < kProgressComplete) {
        if (transfer == kDownload || transfer == kDownload6) {
            constexpr size_t kTextSize = 16;
            std::array<char, kTextSize> text{};
            text::format_to(text, "%d%%", vault->progress_);
            const script::StructPtr params = script::make_struct();
            params->append_word(get_checksums("text"), script::kQbvalString, text.data());
            get_run(vault->progress_script_, params.get());
        }
        return;
    }
    if (transfer == kDownload || transfer == kDownload6)
        ram_stats()->dealloc(vault->octets_);

    switch (transfer) {
    case kDownload:
    case kDownload6:
    case kUpload: {
        vault->task_->drop_out();
        vault->transfer_ = kNone;
        if (vault->outcome_val_ == 1) {
            const script::StructPtr params = script::make_struct();
            params->put_name_key("type", vault->kind_tag_);
            if (transfer == kUpload)
                get_run("LaunchUploadCompleteDialog", params.get());
            else
                get_run(vault->done_script_, params.get());
        } else {
            get_run(vault->fault_ == kErrorScriptResult ? vault->error_script_ : vault->fail_script_);
        }
        if (transfer == kUpload)
            ram_stats()->dealloc(vault->octets_);
        return;
    }
    case kCategories: {
        if (vault->outcome_val_ != 1) {
            vault->transfer_ = kNone;
            vault->task_->drop_out();
            memory::dealloc(vault->octets_);
            get_run(vault->fail_script_);
            return;
        }
        push_bottom_up();
        // (an empty list leaves the transfer as it is: it's read again next time)
        const char* line = text_tools::get_token(vault->octets_, "\r\n");
        if (line) {
            for (bool again = true; again; again = line) {
                void* memory = memory::alloc_zeroed(sizeof(VaultCategory));
                VaultCategory* category = memory ? memory::construct_in<VaultCategory>(memory) : nullptr;
                prepend(&vault->categories_, &category->list_link());
                text_tools::dup_from(category->label().data(), line);
                line = text_tools::get_token(nullptr, "\r\n");
            }
            vault->category_ = static_cast<VaultCategory*>(vault->search_.head_element(&vault->categories_));
            if (vault->category_->skip())
                vault->category_ = static_cast<VaultCategory*>(vault->search_.following_item());
            vault->transfer_ = kNone;
            memory::dealloc(vault->octets_);
            vault->list_items(vault->category_);
        }
        pop_heap();
        return;
    }
    case kItems: {
        push_bottom_up();
        if (vault->outcome_val_ != 1) {
            vault->transfer_ = kNone;
            vault->task_->drop_out();
            memory::dealloc(vault->octets_);
            get_run(vault->fail_script_);
            pop_heap();
            return;
        }
        // Each line: file:name:creator[:park, trick, skater or goal fields]:...:description
        for (char* line = text_tools::get_token(vault->octets_, "\r\n"); line;
             line = text_tools::get_token(nullptr, "\r\n")) {
            void* memory = memory::alloc_zeroed(sizeof(VaultItem));
            VaultItem* item = memory ? memory::construct_in<VaultItem>(memory) : nullptr;
            prepend(&vault->category_->items(), &item->list_link());
            char* rest = line;
            constexpr size_t kFileSize = 1024;
            std::array<char, kFileSize> file{};
            text_tools::dup_from(file.data(), prop(&rest));
            // (the original makes the URL with sprintf: no limit - a longer one ran on into the label, written next)
            text::format_to(item->url(), "http://www.thugonline.com/%s/%s/%s", vault->folder_.data(),
                            vault->category_->label().data(), file.data());
            text_tools::dup_from(item->label().data(), prop(&rest));
            text_tools::dup_from(item->creator().data(), prop(&rest));
            if (vault->kind_tag_ == kVaultParks) {
                text_tools::dup_from(item->extent().data(), prop(&rest));
                item->set_num_pieces(number_field(&rest));
                item->set_num_gaps(number_field(&rest));
                item->set_num_goals(number_field(&rest));
                item->set_style_set(number_field(&rest));
                item->set_tod_script(static_cast<uint32_t>(number_field(&rest)));
            }
            if (vault->kind_tag_ == kVaultTricks)
                item->set_points_held(number_field(&rest));
            if (vault->kind_tag_ == kVaultSkaters)
                item->set_has_male_body(number_field(&rest));
            if (vault->kind_tag_ == kVaultGoals)
                item->set_num_goals(number_field(&rest));
            text_tools::dup_from(item->unknowncc().data(), prop(&rest));
            text_tools::dup_from(item->summary_text().data(), rest);
        }
        vault->transfer_ = kNone;
        memory::dealloc(vault->octets_);
        vault->category_ = static_cast<VaultCategory*>(vault->search_.following_item());
        if (vault->category_ && !vault->category_->skip()) {
            vault->list_items(vault->category_); // the next category's
        } else {
            // All in: the menu.
            vault->category_ = static_cast<VaultCategory*>(vault->search_.head_element(&vault->categories_));
            vault->task_->drop_out();
            const script::StructPtr params = script::make_struct();
            params->append_word("category", vault->category_->label().data());
            params->put_name_key("type", vault->kind_tag_);
            get_run("net_vault_menu_create", params.get());
        }
        pop_heap();
        return;
    }
    case kUploads: {
        push_bottom_up();
        // Each line: file:creator:size:...:description (the whole line also makes the URL).
        for (char* line = text_tools::get_token(vault->octets_, "\r\n"); line;
             line = text_tools::get_token(nullptr, "\r\n")) {
            void* memory = memory::alloc_zeroed(sizeof(VaultItem));
            VaultItem* item = memory ? memory::construct_in<VaultItem>(memory) : nullptr;
            prepend(&vault->category_->items(), &item->list_link());
            text::format_to(item->url(), "http://www.thugonline.com/%s/%s", vault->folder_.data(), line);
            char* rest = line;
            text_tools::dup_from(item->label().data(), prop(&rest));
            text_tools::dup_from(item->creator().data(), prop(&rest));
            text_tools::dup_from(item->extent().data(), prop(&rest));
            text_tools::dup_from(item->unknowncc().data(), prop(&rest));
            text_tools::dup_from(item->summary_text().data(), rest);
        }
        vault->transfer_ = kNone;
        vault->task_->drop_out();
        memory::dealloc(vault->octets_);
        vault->list_categories();
        pop_heap();
        return;
    }
    default:
        return;
    }
}

// 0x0054A7B0
unsigned long __stdcall VaultMan::list_thread(void* param)
{
    auto* vault = static_cast<VaultMan*>(param);
    get_stall(vault->semaphore_);
    push_bottom_up();
    auto* buffer = static_cast<char*>(memory::alloc(kListBufferSize));
    vault->octets_ = buffer;
    std::memset(buffer, 0, kListBufferSize);
    vault->req_ = ghttp_get_ex(vault->url_.data(), nullptr, vault->octets_, kListBufferSize, nullptr, 0, 0,
                               code_address(&on_progress),
                               code_address(&on_transfer_done), vault);
    pop_heap();
    get_release(vault->semaphore_);
    return 0;
}

// 0x0054A860
unsigned long __stdcall VaultMan::download_thread(void* url)
{
    const Hub* manager = net_game_state().online_hub();
    VaultMan* vault = get_vault();
    get_stall(vault->semaphore_);
    push_bottom_up();
    const uint32_t size = vault->buffer_size_;
    auto* buffer = static_cast<char*>(memory::alloc(size));
    vault->octets_ = buffer;
    const int request = ghttp_get_ex(static_cast<const char*>(url), nullptr, buffer, size, nullptr, 0, 0,
                                     code_address(&on_progress), vault->callback_, vault);
    manager->vault_man()->req_ = request;
    pop_heap();
    get_release(manager->vault_man()->semaphore_);
    game_crt_free(memory::owned_element(url)); // (the thread's: its starter made the copy)
    return 0;
}

// 0x0054AAC0
unsigned long __stdcall VaultMan::upload_thread(void* param)
{
    auto* vault = static_cast<VaultMan*>(param);
    prefs::Preferences* prefs = net_game_state().online_hub()->get_online_settings();
    const char* network_id = nullptr;
    prefs->fetch_setting(get_checksums("network_id"))->fetch_text("ui_string", &network_id, 1);
    get_stall(vault->semaphore_);
    push_bottom_up();
    void* post = ghttp_new_post();
    std::array<char, 32> number{};
    std::array<char, 0x80> file_name = {}; // (left unset in the original for an unknown type)
    char* buffer = nullptr;
    uint32_t size = 0;
    // The item's data written to a buffer (from the current heap: memory::alloc for a trick, its steps inlined for the
    // others).
    auto write_data = [&]() {
        size = script::measure_buffer_bytes(vault->content_, kListBufferSize);
        void* block = memory::alloc(size);
        buffer = static_cast<char*>(block); // (bytes: the text functions' and GameSpy's char)
        script::emit_into_buffer(vault->content_, static_cast<uint8_t*>(block), size, true, nullptr);
        const uint32_t checksum = checksums::generate_crc_caseless(buffer, static_cast<int>(size));
        text_tools::dup_from(file_name.data(), vault->source_path_.data());
        return checksum;
    };
    auto add_number = [&](const char* name, int value) {
        text::format_to(number, "%d", value);
        ghttp_post_add_string(post, name, number.data());
    };
    switch (vault->kind_tag_) {
    case kVaultTricks:
    case kVaultSkaters:
    case kVaultGoals: {
        if (vault->kind_tag_ == kVaultTricks) {
            ghttp_post_add_string(post, "filetype", "cat");
            add_number("score", vault->points_held_);
        } else if (vault->kind_tag_ == kVaultSkaters) {
            ghttp_post_add_string(post, "filetype", "cas");
            add_number("is_male", vault->has_male_body_);
        } else {
            ghttp_post_add_string(post, "filetype", "cag");
            add_number("num_goals", vault->num_goals_);
        }
        const uint32_t checksum = write_data();
        ghttp_post_add_string(post, "player", network_id);
        add_number("checksum", static_cast<int>(checksum));
        break;
    }
    case kVaultParks: {
        const uint32_t checksum = write_data();
        ghttp_post_add_string(post, "player", network_id);
        add_number("width", vault->span_);
        add_number("height", vault->byte_count_);
        add_number("checksum", static_cast<int>(checksum));
        add_number("num_pieces", vault->num_pieces_);
        add_number("num_gaps", vault->num_gaps_);
        add_number("num_goals", vault->num_goals_);
        add_number("theme", vault->style_set_);
        text::format_to(number, "0x%x", vault->tod_script_);
        ghttp_post_add_string(post, "tod_script", number.data());
        break;
    }
    default:
        break;
    }
    vault->octets_ = buffer;
    vault->buffer_size_ = size;
    ghttp_post_add_file_from_memory(post, "filename", buffer, size, file_name.data(), nullptr);
    constexpr size_t kDescriptionSize = 0x400;
    std::array<char, kDescriptionSize> description{};
    text::format_to(description, "%s", vault->summary_text_.data());
    ghttp_post_add_string(post, "description", description.data());
    ghttp_post_add_string(post, "uniqueid", game_spy_unique_id());
    if (const char* file_type = upload_file_type(vault->kind_tag_)) // (a trick, skater or goal gets it twice)
        ghttp_post_add_string(post, "filetype", file_type);
    ghttp_post_add_string(post, "dblog", "1");
    vault->req_ = ghttp_post_ex("http://www.thugonline.com/cgi-bin/upload2.pl", nullptr, post, 0, 0,
                                code_address(&on_progress),
                                code_address(&on_transfer_done), vault);
    pop_heap();
    get_release(vault->semaphore_);
    return 0;
}

// 0x0054A660
void VaultMan::on_progress(void*, int state, const char*, int64_t, int64_t received, int64_t total, VaultMan* vault)
{
    // (states 3 and 7: receiving the file / posting)
    if ((state == kProgressStatePosting || state == kProgressStateReceiving) && total > 0)
        vault->progress_ = static_cast<int>(static_cast<double>(received) / static_cast<double>(total) * 100.0f);
    vault->start_time_ = frame_clock::get_time();
}

// 0x0054A740
int VaultMan::on_transfer_done(void*, int result, char*, int64_t, VaultMan* vault)
{
    vault->progress_ = kProgressComplete;
    vault->outcome_val_ = result != 0 ? 2 : 1;
    return 1;
}

// 0x0054A6B0
int VaultMan::on_file_downloaded(void*, int result, char* buffer, int64_t received, VaultMan* vault)
{
    vault->progress_ = kProgressComplete;
    if (result != 0) {
        vault->fault_ = result;
        vault->outcome_val_ = 2;
        return 1;
    }
    vault->outcome_val_ = 1;
    uint32_t script = 0;
    switch (vault->kind_tag_) {
    case kVaultTricks:
        script = kCAT;
        break;
    case kVaultSkaters:
        script = kOptionsAndPros;
        break;
    case kVaultGoals:
        script = kCreatedGoals;
        break;
    case kVaultParks:
        script = kPARK;
        break;
    default:
        break;
    }
    // Not original - the item decoded within what was received (at most the download's buffer; the original
    // decoded the structure with no end - a download cut short read on past it).
    int64_t size = received < 0 ? 0 : received;
    if (std::cmp_greater(size, static_cast<int64_t>(vault->buffer_size_)))
        size = vault->buffer_size_;
    load_downloaded_item(buffer, static_cast<size_t>(size), script);
    vault->fault_ = 0;
    return 1;
}

// --- the script commands ---

// 0x0054AA10: DownloadFile file=<url>: the item, loaded when it comes (on_file_downloaded).
bool download_file(script::Struct* params, script::Script*)
{
    const char* file = "";
    params->fetch_text("file", &file, 0);
    DownloadRequest request{};
    request.done_script_ = kDoneScript;
    request.fail_script_ = kFailScript;
    request.error_script_ = kErrorScript;
    request.progress_script_ = kProgressScript;
    request.buffer_size_ = kDownloadFileSize;
    // (the original uses the URL as sprintf's format)
    text::format_to(request.url, "%s", file);
    request.callback = code_address(&VaultMan::on_file_downloaded);
    get_vault()->download(&request);
    return true;
}

// 0x0054B060: UploadFile type data description filename [score width length num_pieces num_gaps num_goals theme
// tod_script is_male]: the upload on a thread (upload_thread).
bool upload_file(script::Struct* params, script::Script*)
{
    VaultMan* vault = get_vault();
    vault->set_transfer(VaultMan::kUpload);
    vault->set_outcome_val(0);
    get_logic_tasks()->queue_job(vault->task().get());
    vault->set_progress(0);
    vault->set_start_time(frame_clock::get_time());
    vault->set_fail_script(kFailScript);
    params->read_hash_value("type", &vault->kind_tag(), 1);
    params->read_sub_struct("data", &vault->content(), 0);
    const char* description = nullptr;
    const char* file_name = nullptr;
    params->fetch_text("description", &description, 1);
    params->fetch_text("filename", &file_name, 1);
    text_tools::dup_from(vault->source_path().data(), file_name);
    text_tools::dup_from(vault->summary_text().data(), description);
    params->fetch_int_value("score", &vault->points_held(), 0);
    params->fetch_int_value("width", &vault->span(), 0);
    params->fetch_int_value("length", &vault->byte_count(), 0);
    params->fetch_int_value("num_pieces", &vault->num_pieces(), 0);
    params->fetch_int_value("num_gaps", &vault->num_gaps(), 0);
    params->fetch_int_value("num_goals", &vault->num_goals(), 0);
    params->fetch_int_value("theme", &vault->style_set(), 0);
    params->read_hash_value("tod_script", &vault->tod_script(), 0);
    params->fetch_int_value("is_male", &vault->has_male_body(), 0);
    HANDLE thread = start_thread(&VaultMan::upload_thread, vault);
    get_stall(vault->semaphore());
    ResumeThread(thread);
    get_release(vault->semaphore());
    return true;
}

// 0x0054B240
bool cancel_file_transfer(script::Struct*, script::Script*)
{
    VaultMan* vault = get_vault();
    if (vault->transfer() != VaultMan::kNone) {
        ram_stats()->dealloc(vault->octets());
        ghttp_cancel_request(vault->req());
        vault->set_transfer(VaultMan::kNone);
        vault->task()->drop_out();
    }
    return true;
}

// 0x0054C290: DownloadDirectoryList <type>: the type's categories and their items (for profile 0x011B0C16, first
// the uploads list, as an "Uploads" category).
bool download_directory_list(script::Struct* params, script::Script*)
{
    const Hub* manager = net_game_state().online_hub();
    VaultMan* vault = manager->vault_man().get();
    const FriendsManager* buddies = manager->presence_service().get();
    params->read_hash_value(0u, &vault->kind_tag(), 1);
    if (const char* folder = type_folder(vault->kind_tag())) // (an unknown type keeps the folder)
        text::format_to(vault->folder(), "%s", folder);
    if (buddies->account() != kUploadsProfile) {
        vault->list_categories();
        return true;
    }
    push_bottom_up();
    void* memory = memory::alloc_zeroed(sizeof(VaultCategory));
    VaultCategory* category = memory ? memory::construct_in<VaultCategory>(memory) : nullptr;
    category->set_skip(true);
    prepend(&vault->categories(), &category->list_link());
    std::memcpy(category->label().data(), "Uploads", kUploadsLabelBytes);
    vault->set_category(static_cast<VaultCategory*>(vault->search().head_element(&vault->categories())));
    vault->set_transfer(VaultMan::kUploads);
    vault->set_outcome_val(0);
    constexpr size_t kUrlSize = 128;
    std::array<char, kUrlSize> url{};
    text::format_to(url, "http://www.thugonline.com/%s/uploads.txt", vault->folder().data());
    vault->start_list(url.data());
    get_logic_tasks()->queue_job(vault->task().get());
    pop_heap();
    return true;
}

// 0x0054C460
bool free_directory_listing(script::Struct*, script::Script*)
{
    get_vault()->free_directory_listing();
    return true;
}

// 0x0054C470: a menu item for each item of the current category (net_vault_menu_add_park / goal / skater / trick),
// its details as parameters, choosing it gets the file.
bool fill_vault_menu(script::Struct*, script::Script*)
{
    const VaultMan* vault = get_vault();
    push_top_down();
    for (memory::ListNode* node = get_next_node(&vault->category()->items()); node; node = get_next_node(node)) {
        auto* item = static_cast<VaultItem*>(node->data());
        if (!item)
            break;
        gsl::owner<script::Struct*> params = script::new_struct();
        params->append_word("name", item->label().data());
        params->append_word("creator", item->creator().data());
        params->append_word("size", item->extent().data());
        params->put_int("score", item->points_held());
        params->put_int("num_pieces", item->num_pieces());
        params->put_int("num_gaps", item->num_gaps());
        params->put_int("width", item->span());
        params->put_int("height", item->level_y());
        params->put_int("num_goals", item->num_goals());
        params->put_int("theme", item->style_set());
        params->put_name_key("tod_script", item->tod_script());
        params->put_int("is_male", item->has_male_body());
        params->append_word("description", item->summary_text().data());
        gsl::owner<script::Struct*> choose = script::new_struct();
        choose->append_word("file", item->url().data());
        params->put_sub_struct("pad_choose_params", choose);
        const char* script = nullptr;
        switch (vault->kind_tag()) {
        case kVaultParks:
            script = "net_vault_menu_add_park";
            break;
        case kVaultGoals:
            script = "net_vault_menu_add_goal";
            break;
        case kVaultSkaters:
            script = "net_vault_menu_add_skater";
            break;
        case kVaultTricks:
            script = "net_vault_menu_add_trick";
            break;
        default:
            break;
        }
        if (script)
            get_run(script, params);
        script::free_struct(params);
        script::free_struct(choose);
    }
    pop_heap();
    return true;
}

namespace {
// The category after / before the current one (round to the first / last), its name into the script's parameters.
void move_category(script::Script* script, bool forward)
{
    VaultMan* vault = get_vault();
    memory::ListNode*& node = vault->search().list_link();
    memory::ListNode* step = forward ? node->next() : node->prev();
    node = step->data() == memory::ListNode::head_marker() ? nullptr : step;
    vault->set_category(node ? static_cast<VaultCategory*>(node->data()) : nullptr);
    if (!vault->category()) {
        node = &vault->categories();
        step = forward ? node->next() : node->prev();
        node = step->data() == memory::ListNode::head_marker() ? nullptr : step;
        vault->set_category(node ? static_cast<VaultCategory*>(node->data()) : nullptr);
    }
    script->args()->append_word("category", vault->category()->label().data());
}
} // namespace

// 0x0054C770
bool next_vault_category(script::Struct*, script::Script* script)
{
    move_category(script, true);
    return true;
}

// 0x0054C800
bool prev_vault_category(script::Struct*, script::Script* script)
{
    move_category(script, false);
    return true;
}

} // namespace online

