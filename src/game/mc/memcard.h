#pragma once

#include <cstdint>

#include <windows.h>

#include "game/core/alloc.h"

// Memory cards on the PC: saves are files under "Save\" (the console code's card/file interface, backed by
// Win32 files). One manager (reference counted) holds the one card.
namespace Mc {

extern void** const kFileVtable; // 0x00658A60: Lst::Head<File> / Lst::Node<File> (memcard.cpp)

class Card;

// A save file (also an entry of a directory listing: name, date, size).
struct File {
    // The list node it starts with (Lst::Node<File>: the data is the file itself).
    Mem::ListNode m_node;
    char m_name[0x28];
    uint8_t m_second, m_minute, m_hour, m_day, m_month, m_unknown41;
    uint16_t m_year;
    uint32_t m_size;
    uint16_t m_unknown48;
    uint8_t m_unknown4A[0x42];
    HANDLE m_handle;
    Card* m_card;

    // Origin: 0 from the start, 1 from the current position, else from the end. Returns the new position.
    DWORD Seek(LONG offset, int origin);
    bool Flush();
    // The bytes written / read, or 0 on failure.
    int Write(const void* data, int size);
    int Read(void* buffer, int size);
    bool Close();
};

class Card {
public:
    // Open (mode 1 read, 2 write, 3 both - existing files; 4 write a new file; 6 write, created if needed).
    File* Open(const char* name, int mode, int unused);
    bool RemoveFile(const char* name);
    // The name as the card uses it (a copy in a shared buffer).
    const char* ConvertFilename(const char* name);
    // Free space, in 16 KB blocks (0 without a card).
    int GetFreeBlocks();
    // Add every file matching `pattern` in Save\ to `list` (a list head), with its date and size.
    bool GetFileList(const char* pattern, File* list);
    // (shared with other classes: always true)
    bool AlwaysTrue(int unused);

    uint32_t m_unknown0;
    uint32_t m_unknown4;
    int m_error;
    uint8_t m_present; // (set to 0x75 by the manager)
    uint8_t m_unknownD[3];
};
static_assert(sizeof(Card) == 0x10, "layout must match the original");

class Manager {
public:
    Manager* Init(); // the constructor
    Card* GetCard(int port, int slot);

    struct Slot {
        int port;
        int slot;
        uint32_t unknown8;
        uint32_t unknownC;
    };
    Slot m_slots[4][2];
    Card m_card;
};
static_assert(sizeof(Manager) == 0x90, "layout must match the original");

// The manager, made by the first Create (with `create`); each Create adds a reference, Release drops one.
Manager* CreateManager(bool create);
void ReleaseManager();

extern Manager* g_theCard;

} // namespace Mc
