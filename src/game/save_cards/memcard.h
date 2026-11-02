#pragma once

#include <cstdint>

#include <windows.h>
#include <array>

#include "engine/core/arena_alloc.h"

// Memory cards on the PC: saves are files under "Save\" (the console code's card/file interface, backed by
// Win32 files). One manager (reference counted) holds the one card.
namespace save_cards {

class SaveCard;

// A save file (also an entry of a directory listing: name, date, size).
struct FileLoad {
    // The fields other code reads and sets.
    memory::ListNode& list_link() { return list_link_; }
    [[nodiscard]] const memory::ListNode& list_link() const { return list_link_; }
    std::array<char, 0x28>& label() { return label_; }
    [[nodiscard]] const std::array<char, 0x28>& label() const { return label_; }
    [[nodiscard]] uint8_t second() const { return second_; }
    void set_second(uint8_t value) { second_ = value; }
    [[nodiscard]] uint8_t minute() const { return minute_; }
    void set_minute(uint8_t value) { minute_ = value; }
    [[nodiscard]] uint8_t hour() const { return hour_; }
    void set_hour(uint8_t value) { hour_ = value; }
    [[nodiscard]] uint8_t day() const { return day_; }
    void set_day(uint8_t value) { day_ = value; }
    [[nodiscard]] uint8_t month() const { return month_; }
    void set_month(uint8_t value) { month_ = value; }
    [[nodiscard]] uint16_t year() const { return year_; }
    void set_year(uint16_t value) { year_ = value; }
    void set_extent(uint32_t value) { extent_ = value; }
    void set_unknown48(uint16_t value) { unknown48_ = value; }
    void set_hdl(const HANDLE& value) { hdl_ = value; }
    void set_save_device(SaveCard* value) { save_device_ = value; }

private:
    // The list node it starts with (lists::Elem<File>: the data is the file itself; the original's size).
    memory::ListNode list_link_;
    std::array<char, 0x28> label_;
    uint8_t second_, minute_, hour_, day_, month_, unknown41_;
    uint16_t year_;
    uint32_t extent_;
    uint16_t unknown48_;
    std::array<uint8_t, 0x42> unknown4a_;
    HANDLE hdl_;
    SaveCard* save_device_;

public:

    // Origin: 0 from the start, 1 from the current position, else from the end. Returns the new position.
    DWORD seek_to(LONG offset, int origin);
    bool drain();
    // The bytes written / read, or 0 on failure.
    int emit_out(const void* data, int size);
    int get_take_in(void* buffer, int size);
    bool shut();
    // 0x005A8E80: the scalar deleting destructor (out of its list - 0x005BC990 -, freed with flag 1).
    FileLoad* teardown(unsigned int flags);
};

class SaveCard {
public:
    // Open (mode 1 read, 2 write, 3 both - existing files; 4 write a new file; 6 write, created if needed).
    FileLoad* unseal(const char* name, int mode, int unused);
    bool remove_file(const char* name);
    // The name as the card uses it (a copy in a shared buffer).
    const char* convert_filename(const char* name);
    // Free space, in 16 KB blocks (0 without a card).
    int get_free_blocks();
    // Add every file matching `pattern` in Save\ to `list` (a list head), with its date and size.
    bool list_save_files(const char* pattern, memory::ListNode* list);
    // (shared with other classes: always true)
    bool always_true(int unused);

    // The fields other code reads and sets.
    [[nodiscard]] int fault() const { return fault_; }
    [[nodiscard]] uint8_t present() const { return present_; }
    void set_present(uint8_t value) { present_ = value; }

private:
    uint32_t unknown0_;
    uint32_t unknown4_;
    int fault_;
    uint8_t present_; // (set to 0x75 by the manager)
    std::array<uint8_t, 3> unknownd_;
};

class Hub {
public:
    Hub(); // the constructor
    SaveCard* get_storage_slot(int port, int slot);

    struct Bay {
        int port;
        int slot;
        uint32_t unknown8;
        uint32_t unknownc_;
    };
private:
    std::array<std::array<Bay, 2>, 4> slots_{};
    SaveCard save_device_{};
};

// The manager, made by the first Create (with `create`); each Create adds a reference, Release drops one.
Hub* create_manager(bool create);
void release_manager();

} // namespace save_cards
