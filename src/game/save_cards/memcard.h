#pragma once

#include <cstdint>

#include <windows.h>

#include "engine/core/arena_alloc.h"

// Memory cards on the PC: saves are files under "Save\" (the console code's card/file interface, backed by
// Win32 files). One manager (reference counted) holds the one card.
namespace MemCard {

extern void** const kFileVtable; // 0x00658A60: ListKit::Foremost<File> / ListKit::Elem<File> (memcard.cpp)

class SaveCard;

// A save file (also an entry of a directory listing: name, date, size).
struct FileLoad {
    // The list node it starts with (ListKit::Elem<File>: the data is the file itself).
    MemSys::ListNode m_listLink;
    char m_label[0x28];
    uint8_t m_second, m_minute, m_hour, m_day, m_month, m_unknown41;
    uint16_t m_year;
    uint32_t m_extent;
    uint16_t m_unknown48;
    uint8_t m_unknown4A[0x42];
    HANDLE m_hdl;
    SaveCard* m_saveDevice;

    // Origin: 0 from the start, 1 from the current position, else from the end. Returns the new position.
    DWORD SeekTo(LONG offset, int origin);
    bool Drain();
    // The bytes written / read, or 0 on failure.
    int EmitOut(const void* data, int size);
    int TakeIn(void* buffer, int size);
    bool Shut();
};

class SaveCard {
public:
    // Open (mode 1 read, 2 write, 3 both - existing files; 4 write a new file; 6 write, created if needed).
    FileLoad* Unseal(const char* name, int mode, int unused);
    bool RemoveFile(const char* name);
    // The name as the card uses it (a copy in a shared buffer).
    const char* ConvertFilename(const char* name);
    // Free space, in 16 KB blocks (0 without a card).
    int GetFreeBlocks();
    // Add every file matching `pattern` in Save\ to `list` (a list head), with its date and size.
    bool ListSaveFiles(const char* pattern, FileLoad* list);
    // (shared with other classes: always true)
    bool AlwaysTrue(int unused);

    uint32_t m_unknown0;
    uint32_t m_unknown4;
    int m_fault;
    uint8_t m_present; // (set to 0x75 by the manager)
    uint8_t m_unknownD[3];
};
static_assert(sizeof(SaveCard) == 0x10, "layout must match the original");

class Hub {
public:
    Hub* PrepareAll(); // the constructor
    SaveCard* GetStorageSlot(int port, int slot);

    struct Bay {
        int port;
        int slot;
        uint32_t unknown8;
        uint32_t unknownC;
    };
    Bay m_slots[4][2];
    SaveCard m_saveDevice;
};
static_assert(sizeof(Hub) == 0x90, "layout must match the original");

// The manager, made by the first Create (with `create`); each Create adds a reference, Release drops one.
Hub* CreateManager(bool create);
void ReleaseManager();

extern Hub* g_theCard;

} // namespace Mc
