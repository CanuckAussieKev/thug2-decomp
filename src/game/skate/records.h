#pragma once

#include <cstddef>
#include <cstdint>

// The records (0x0057ECD0-0x0057FE27): each level's high scores, best combos and (some levels) arcade scores - tables
// of records kept sorted, best first - and its longest combo, grind, lip trick and manual; the game's records are
// every level's plus the default initials. They go into and come out of the save as structures (records' value,
// number and initials). One CBestResults, made by Create (0x0057EED0) and released by Release (0x0057FDB0), counted,
// at 0x007CE4BC / 0x007CE4C0.
namespace Scripts {
class CStruct;
class CScript;
} // namespace Script

namespace Highscores {

// A record (0x10 bytes): its initials (a 4-byte buffer of its own), value, number and whether it was just set.
class CBestEntry {
public:
    // 0x0057EF20 (the constructor): initials XXX, value 1000, number 0 (new left as it was). Returns this.
    CBestEntry* MakeNew();
    // 0x0057ECD0: set to `value` and `number` and marked new if `value` beats it; else not new. Whether it did.
    bool TryBeatRecord(int value, int number);
    // 0x0057ED00: value, number (0 if missing) and initials (XXX if missing) from `params`.
    void DecodeFromStruct(Scripts::CStruct* params);
    // The initials set (three characters).
    void SetPlayerTag(const char* initials);

    char* m_tag; // +0x00
    int m_val;      // +0x04
    int m_slotNo;     // +0x08
    bool m_isFresh;       // +0x0C
    uint8_t pad0D[3];
};
static_assert(sizeof(CBestEntry) == 0x10, "layout must match the original");

// A table of records (8 bytes): their count and the records (an array allocated with its count before it).
class CBestEntryList {
public:
    // 0x0057EF60 (the constructor): `count` records, constructed. Returns this.
    CBestEntryList* MakeNew(int count);
    // 0x0057EFE0: none new; `value` put where it beats a record (those after it moved down one, the last dropped),
    // with `number`, initials XXX, marked new. Its index, or -1 if it beats none.
    int TryInsertEntry(int value, int number);
    // 0x0057ED80: the new records' initials set.
    void UpdateInitials(const char* initials);
    // 0x0057F0C0: the records from `params`' RecordTable array (only if it has as many).
    void DecodeFromStruct(Scripts::CStruct* params);
    // 0x0057F4E0: the records written to `params` as its recordtable array of structures (value, number, initials).
    void WriteToStructure(Scripts::CStruct* params);
    // The records freed and the table (the destructor, inlined by its owners).
    void EraseOne();

    int m_num;         // +0x00
    CBestEntry* m_records;  // +0x04
};
static_assert(sizeof(CBestEntryList) == 8, "layout must match the original");

// A level's records (0x1C bytes).
class CStageBests {
public:
    // 0x0057F120 (the constructor): high scores and best combos (5 each), arcade scores (10), and the four longest
    // records (made in the order longest combo, grind, lip trick, manual; their values 0). Returns this.
    CStageBests* MakeNew();
    // 0x0057F670: the destructor body (the records, then the tables: best combos, high scores, arcade scores).
    void DestroyBody();
    // 0x0057EDD0: the new records' initials set (the longest ones: grind... as the original's order; then the
    // tables).
    void UpdateInitials(const char* initials);
    // 0x0057F340: the records from `params` (highscores, bestcombos, arcadescores if there, longestcombo /
    // longestgrind / longestliptrick / longestmanual).
    void DecodeFromStruct(Scripts::CStruct* params);
    // 0x0057F7B0: the records written to `params` as structures of those names.
    void WriteToStructure(Scripts::CStruct* params);

    CBestEntryList* m_highScores;      // +0x00
    CBestEntryList* m_bestCombos;      // +0x04
    CBestEntryList* m_arcadeScores;    // +0x08
    CBestEntry* m_longestGrind;         // +0x0C
    CBestEntry* m_longestManual;        // +0x10
    CBestEntry* m_longestLipTrick;      // +0x14
    CBestEntry* m_biggestChain;         // +0x18
};
static_assert(sizeof(CStageBests) == 0x1C, "layout must match the original");

// The game's records (0xC bytes).
class CBestResults {
public:
    // 0x0057EE70 (the constructor): no levels, default initials ABC (the levels pointer left as it was). Returns
    // this.
    CBestResults* MakeNew();
    // 0x0057EEA0: the default initials set. 0x0057EEC0: a level's records.
    void SetStockInitials(const char* initials);
    CStageBests* GetLevelRecords(int level);
    // 0x0057FBC0: `count` levels' records, constructed.
    void InitLevels(int count);
    // 0x0057F440: every level's records from `params`' GameRecords array (only if it has as many), then the
    // DefaultInitials (ABC if missing).
    void DecodeFromStruct(Scripts::CStruct* params);
    // 0x0057FC50: every level's records written to `params` (gamerecords array), and the default initials.
    void WriteToStructure(Scripts::CStruct* params);

    // 0x0057EED0: the instance (made first, if `create`); counted. 0x0057FDB0: one count released; the last
    // deletes it.
    static CBestResults* Fabricate(bool create);
    static void Release();

    int m_levelCount;           // +0x00
    char* m_defaultInitials;    // +0x04
    CStageBests* m_levels;    // +0x08
};
static_assert(sizeof(CBestResults) == 0xC, "layout must match the original");

// 0x005AEBC0: the script command UpdateRecords (the end of a high score run, classic mode's and the 2 player modes'
// runs): the skate module's 0x00566F40 - the current level's records from the run just ended (its high score and best
// combo tables, longest combo / grind / lip trick / manual; the skate module's +0x384 says whether any was new). True.
// NOT ORIGINAL (the user's rule, 2026-10-21): while the level is the level editor's edited copy
// (game/front/level_editor_layout.h) nothing is recorded - +0x384 cleared as the original does first, so no new
// record is announced - and logged. `editedCopy`: Layout::EditingCopy() (a parameter, for the self-test).
bool UpdateRecords(bool editedCopy);
bool __cdecl CFunc_UpdateRecords(Scripts::CStruct* params, Scripts::CScript* script);

// 0x005AEBD0: UpdateArcadeScores [level=<n>] <RecordTable> - the level's (the current career level's) arcade scores
// read from the parameters. True.
bool __cdecl CFunc_UpdateArcadeScores(Scripts::CStruct* params, Scripts::CScript* script);
// 0x005AFAF0: UpdateInitials - the current level's new records given the default initials. True.
bool __cdecl CFunc_UpdateInitials(Scripts::CStruct* params, Scripts::CScript* script);
// 0x005AFB30: NewRecord - whether the last UpdateRecords set a record (the skate module's +0x384).
bool __cdecl CFunc_NewRecord(Scripts::CStruct* params, Scripts::CScript* script);

extern Highscores::CBestResults* g_gameRecords;

} // namespace Records
