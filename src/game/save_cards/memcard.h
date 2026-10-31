#pragma once

#include <cstdint>

#include <windows.h>

#include "engine/core/arena_alloc.h"

// Memory cards on the PC: saves are files under "Save\" (the console code's card/file interface, backed by
// Win32 files). One manager (reference counted) holds the one card.
namespace save_cards {

extern void** const kFileVtable; // 0x00658A60: lists::Foremost<File> / lists::Elem<File> (memcard.cpp)

class SaveCard;

// A save file (also an entry of a directory listing: name, date, size).
struct FileLoad {
    // The list node it starts with (lists::Elem<File>: the data is the file itself).
    memory::ListNode list_link_;
    char label_[0x28];
    uint8_t second_, minute_, hour_, day_, month_, unknown41_;
    uint16_t year_;
    uint32_t extent_;
    uint16_t unknown48_;
    uint8_t unknown4a_[0x42];
    HANDLE hdl_;
    SaveCard* save_device_;

    // Origin: 0 from the start, 1 from the current position, else from the end. Returns the new position.
    DWORD seek_to(LONG offset, int origin);
    bool drain();
    // The bytes written / read, or 0 on failure.
    int emit_out(const void* data, int size);
    int get_take_in(void* buffer, int size);
    bool shut();
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
    bool list_save_files(const char* pattern, FileLoad* list);
    // (shared with other classes: always true)
    bool always_true(int unused);

    uint32_t unknown0_;
    uint32_t unknown4_;
    int fault_;
    uint8_t present_; // (set to 0x75 by the manager)
    uint8_t unknownd_[3];
};
static_assert(sizeof(SaveCard) == 0x10, "layout must match the original");

class Hub {
public:
    Hub* prepare_all(); // the constructor
    SaveCard* get_storage_slot(int port, int slot);

    struct Bay {
        int port;
        int slot;
        uint32_t unknown8;
        uint32_t unknownc_;
    };
    Bay slots_[4][2];
    SaveCard save_device_;
};
static_assert(sizeof(Hub) == 0x90, "layout must match the original");

// The manager, made by the first Create (with `create`); each Create adds a reference, Release drops one.
Hub* create_manager(bool create);
void release_manager();

extern Hub* g_the_card;

} // namespace save_cards
