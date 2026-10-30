// Self-tests for the content vault and HTTP transfers (src/game/net/vault_man.cpp, 0x0054A660-0x0054C9B0): the
// original and ours on identical stand-in vaults, categories and items in GameNetKit's arena, with GameSpy's HTTP SDK,
// the threads, the semaphore, scripts, structures and the clock stood in for by recording code (net4_testkit.h); the
// calls, results and arena are compared case by case. Nothing goes online: the server's lists are written here.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

#include "game/core/crc.h"
#include "game/net/buddy_man.h"
#include "game/net/gamenet_manager.h"
#include "game/net/vault_man.h"
#include "game/script/struct_buffer.h"
#include "game/mc/save_types.h"
#include "hook/net4_testkit.h"

namespace {

using namespace Net4Kit;
using GameNet::VaultCategory;
using GameNet::VaultItem;
using GameNet::VaultMan;
using GameNet::VaultSearch;

// Arena layout.
constexpr uint32_t kBuddies = 0x12000;   // the BuddyMan (its profile at +4)
constexpr uint32_t kTask = 0x12040;      // the vault's task (0x20) and its vtable
constexpr uint32_t kTaskVtable = 0x12060;
constexpr uint32_t kParams = 0x12100;
constexpr uint32_t kScript = 0x12140;
constexpr uint32_t kScriptParams = 0x12180;
constexpr uint32_t kRequest = 0x12200;   // a DownloadRequest (0x118)
constexpr uint32_t kPost = 0x12340;      // ghttp's post
constexpr uint32_t kUrlCopy = 0x12380;   // goastrdup's copy (0x100)
constexpr uint32_t kPrefStruct = 0x12480;
constexpr uint32_t kData = 0x124C0;      // the upload's data structure
constexpr uint32_t kVault = 0x14000;     // the VaultMan (0x3C8)
constexpr uint32_t kCategories = 0x14400; // categories, 0x50 apart (8)
constexpr uint32_t kItems = 0x14800;     // items, 0x220 apart (8)
constexpr uint32_t kBuffer = 0x16000;    // the transfer's buffer (0x1000)

VaultMan* Vault() { return &Word<VaultMan>(kVault); }
VaultCategory* CategoryAt(int i) { return &Word<VaultCategory>(kCategories + 0x50 * i); }
VaultItem* ItemAt(int i) { return &Word<VaultItem>(kItems + 0x220 * i); }

void Link(Mem::ListNode* head, Mem::ListNode* node)
{
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}
void SetUpVault(int transfer = 0, uint32_t type = GameNet::kVaultParks)
{
    VaultMan* vault = Vault();
    std::memset(vault, 0, sizeof(VaultMan));
    vault->m_task = &Word<Tsk::BaseTask>(kTask);
    vault->m_transfer = transfer;
    vault->m_progress = 37;
    vault->m_result = 0;
    vault->m_error = 0;
    vault->m_bufferSize = 0x1000;
    vault->m_buffer = &Word<char>(kBuffer);
    vault->m_startTime = 50000;
    std::snprintf(vault->m_url, sizeof(vault->m_url), "http://old/url");
    std::snprintf(vault->m_fileName, sizeof(vault->m_fileName), "upload.prk");
    std::snprintf(vault->m_description, sizeof(vault->m_description), "An upload's description");
    vault->m_semaphore = reinterpret_cast<void*>(0x5E3A);
    vault->m_request = 0x7E9;
    vault->m_data = &Word<Script::CStruct>(kData);
    vault->m_score = 1111;
    vault->m_width = 22;
    vault->m_length = 33;
    vault->m_numPieces = 44;
    vault->m_numGaps = 55;
    vault->m_numGoals = 66;
    vault->m_theme = 7;
    vault->m_todScript = 0xABCDEF01;
    vault->m_isMale = 1;
    vault->m_categories = {reinterpret_cast<void**>(0x0064D418), reinterpret_cast<void*>(-1), 0, &vault->m_categories,
                           &vault->m_categories};
    vault->m_search = {reinterpret_cast<void**>(0x0064D29C), nullptr};
    vault->m_category = nullptr;
    vault->m_type = type;
    std::snprintf(vault->m_folder, sizeof(vault->m_folder), "levels");
    vault->m_failScript = 0xFA11;
    vault->m_doneScript = 0xD0E;
    vault->m_progressScript = 0x9809;
    vault->m_errorScript = 0xE880;
    vault->m_callback = reinterpret_cast<const void*>(0xCA11);
    Word<uint32_t>(kTask) = At(kTaskVtable);
    Word<uint32_t>(kTask + 0x1C) = At(kVault);
    Word<uint32_t>(kManager + 0x14) = At(kVault);
    Word<uint32_t>(kManager + 0x18) = At(kBuddies);
    Word<int>(kBuddies + 4) = 0x1234;
    Word<uint32_t>(kScript + 0x14) = At(kScriptParams);
    uint32_t memory = kMisc + 0x800;
    Word<uint32_t>(memory + 0x13C) = 0x0013C13C;
    Word<uint32_t>(memory + 0x140) = 0x00140140;
}
VaultCategory* AddCategoryAt(int i, const char* name, bool skip = false)
{
    VaultCategory* category = CategoryAt(i);
    category->m_node = {reinterpret_cast<void**>(0x0064D41C), category, 0, &category->m_node, &category->m_node};
    category->m_skip = skip;
    std::snprintf(category->m_name, sizeof(category->m_name), "%s", name);
    category->m_items = {reinterpret_cast<void**>(0x0064D414), reinterpret_cast<void*>(-1), 0, &category->m_items,
                         &category->m_items};
    Link(&Vault()->m_categories, &category->m_node);
    return category;
}
VaultItem* AddItemAt(int i, VaultCategory* category)
{
    VaultItem* item = ItemAt(i);
    std::memset(item, 0, sizeof(VaultItem));
    item->m_node = {reinterpret_cast<void**>(0x0064D414), item, 0, &item->m_node, &item->m_node};
    std::snprintf(item->m_url, sizeof(item->m_url), "http://item/%d", i);
    std::snprintf(item->m_name, sizeof(item->m_name), "Item %d", i);
    std::snprintf(item->m_creator, sizeof(item->m_creator), "Creator %d", i);
    std::snprintf(item->m_size, sizeof(item->m_size), "%dk", 10 + i);
    std::snprintf(item->m_description, sizeof(item->m_description), "Description %d", i);
    item->m_score = 100 + i;
    item->m_width = 200 + i;
    item->m_height = 300 + i;
    item->m_numPieces = 400 + i;
    item->m_numGaps = 500 + i;
    item->m_numGoals = 600 + i;
    item->m_theme = 700 + i;
    item->m_todScript = 0x80000000u + i;
    item->m_isMale = i & 1;
    Link(&category->m_items, &item->m_node);
    return item;
}
// The transfer's buffer: `text`.
void SetBuffer(const char* text) { std::snprintf(&Word<char>(kBuffer), 0x1000, "%s", text); }

uint32_t g_taskDestroy;
uint32_t g_uniqueId; // the stand-in of GameSpy's unique id function (a thunk)

void Install(StandIns& s)
{
    InstallScriptStandIns(s);
    InstallCrtStandIns(s);
    InstallSystemStandIns(s);
    InstallSemaphoreStandIns(s);
    // ghttp (cdecl).
    StubCdecl(s, {0x005FFB10}, 0xFB10, 0);
    StubCdecl(s, {0x005FFB20}, 0xFB20, 1);
    StubCdecl(s, {0x005FF860}, 0xF860, 0);
    s.At({0x005FF8A0}, 0, [](Regs& r) {
        RecN(0xF8A0, {Str(r.args[0]), r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6], r.args[7],
                      r.args[8], r.args[9]});
        return 0x7E0u;
    });
    StubCdecl(s, {0x005FFB40}, 0xFB40, 0, [](Regs&) { return At(kPost); });
    s.At({0x005FFB50}, 0, [](Regs& r) {
        Rec(0xFB50, {r.args[0], Str(r.args[1]), Str(r.args[2])});
        return 1u;
    });
    s.At({0x005FFB90}, 0, [](Regs& r) {
        // (for an unknown type the original's file name is a local it never set: not compared)
        uint32_t type = Vault()->m_type;
        bool known = type == GameNet::kVaultParks || type == GameNet::kVaultSkaters || type == GameNet::kVaultTricks ||
                     type == GameNet::kVaultGoals;
        Rec(0xFB90, {r.args[0], Str(r.args[1]), r.args[2], r.args[3], known ? Str(r.args[4]) : 0u, r.args[5]});
        return 1u;
    });
    s.At({0x005FFA10}, 0, [](Regs& r) {
        RecN(0xFA10, {Str(r.args[0]), r.args[1], r.args[2], r.args[3], r.args[4], r.args[5], r.args[6], r.args[7]});
        return 0x7E1u;
    });
    s.At({0x005F81C0}, 0, [](Regs& r) {
        Rec(0x81C0, {Str(r.args[0])});
        std::snprintf(&Word<char>(kUrlCopy), 0x100, "%s", reinterpret_cast<const char*>(r.args[0]));
        return At(kUrlCopy);
    });
    StubCdecl(s, {0x0062411B}, 0x411B, 1);
    s.At({0x005A8EB0, AddressOf(&Mc::KeepLoadedContents)}, 0, [](Regs& r) {
        Rec(0x8EB0, {r.args[0], r.args[1]});
        return 0u;
    });
    // The structure written for the upload: its size and bytes.
    s.At({0x0047A070, AddressOf(&Script::CalculateBufferSize)}, 0, [](Regs& r) {
        Rec(0xA070, {r.args[0], r.args[1]});
        return 0x40u;
    });
    s.At({0x00479E20, AddressOf(&Script::WriteToBuffer)}, 0, [](Regs& r) {
        Rec(0x9E20, {r.args[0], r.args[1], r.args[2], r.args[3], r.args[4]});
        auto* bytes = reinterpret_cast<uint8_t*>(r.args[1]);
        for (uint32_t i = 0; i < r.args[2]; ++i)
            bytes[i] = static_cast<uint8_t>(i * 7 + 3);
        return r.args[2];
    });
    Stub(s, {0x00533240, AddressOf(&GameNet::Manager::SetLobbyId)}, 4, 0x3240);
    void* destroy = s.Thunk(4, [](Regs& r) {
        Rec(0x7A5D, {r.ecx, r.args[0]});
        return r.ecx;
    });
    g_taskDestroy = reinterpret_cast<uint32_t>(destroy);
    void* uniqueId = s.Thunk(0, [](Regs&) {
        Rec(0xE24C, {});
        return reinterpret_cast<uint32_t>("unique-id-123");
    });
    g_uniqueId = reinterpret_cast<uint32_t>(uniqueId);
}

void MapOurCode()
{
    g_codeMap.clear();
    MapCode(&VaultMan::VaultTask, 0x0054B6E0);
    MapCode(&VaultMan::ListThread, 0x0054A7B0);
    MapCode(&VaultMan::DownloadThread, 0x0054A860);
    MapCode(&VaultMan::UploadThread, 0x0054AAC0);
    MapCode(&VaultMan::OnProgress, 0x0054A660);
    MapCode(&VaultMan::OnTransferDone, 0x0054A740);
    MapCode(&VaultMan::OnFileDownloaded, 0x0054A6B0);
}

void Snapshot()
{
    for (uint32_t i = 0x12000; i < g_heapNext + 0x40 && i < kArenaSize; i += 4)
        g_trace.push_back(Norm(Word<uint32_t>(i)));
    for (uint32_t i = 0; i < 0x20; i += 4)
        g_trace.push_back(Norm(Word<uint32_t>(kManager + i)));
    g_trace.push_back(Norm(Word<uint32_t>(kManager + 0x9950)));
}
using Case = std::function<void(bool original)>;
void RunCases(int run, const std::vector<Case>& cases)
{
    for (size_t i = 0; i < cases.size(); ++i) {
        std::vector<uint32_t> kept = g_trace;
        SetUpArena(run);
        g_trace = kept;
        ResetAnswers();
        GameVariable<uint32_t>(0x0068E24C) = g_uniqueId;
        SetUpVault();
        Word<uint32_t>(kTaskVtable + 8) = g_taskDestroy;
        g_now = 60000;
        g_trace.push_back(0xCA5E0000u + static_cast<uint32_t>(i));
        cases[i](run == 0);
        Snapshot();
    }
}
void Compare2(const char* what, const std::vector<Case>& cases)
{
    MapOurCode();
    RunBoth(what, Install, [&](bool original) { RunCases(original ? 0 : 1, cases); }, {{0x0068E24C, 4, {}}});
}

using CFunc = bool(__cdecl*)(Script::CStruct*, Script::CScript*);
void CallCFunc(bool original, uint32_t address, CFunc ours)
{
    auto* params = &Word<Script::CStruct>(kParams);
    auto* script = &Word<Script::CScript>(kScript);
    bool result = original ? Original<CFunc>(address)(params, script) : ours(params, script);
    g_trace.insert(g_trace.end(), {0xC0DE, static_cast<uint32_t>(result)});
}
template <typename R>
void Result(R value)
{
    g_trace.push_back(Rel(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(reinterpret_cast<void*>(value)))));
}

const uint32_t kTypes[] = {GameNet::kVaultParks, GameNet::kVaultSkaters, GameNet::kVaultTricks, GameNet::kVaultGoals,
                           0x12345678};

// --- the lists: items, categories, searches, the vault's constructor and destructor, the menus ---

void TestNetVaultLists()
{
    std::vector<Case> cases;
    cases.push_back([](bool original) {
        void* memory = ItemAt(3);
        std::memset(memory, 0xAB, sizeof(VaultItem));
        Result(original ? Original<VaultItem*(__thiscall*)(void*)>(0x0054B570)(memory)
                        : static_cast<VaultItem*>(memory)->Construct());
    });
    cases.push_back([](bool original) {
        void* memory = CategoryAt(3);
        std::memset(memory, 0xAB, sizeof(VaultCategory));
        Result(original ? Original<VaultCategory*(__thiscall*)(void*)>(0x0054B670)(memory)
                        : static_cast<VaultCategory*>(memory)->Construct());
    });
    for (unsigned int flags : {0u, 1u}) {
        cases.push_back([flags](bool original) {
            VaultCategory* category = AddCategoryAt(0, "Cat");
            AddItemAt(0, category);
            VaultItem* item = AddItemAt(1, category);
            AddItemAt(2, category);
            Result(original ? Original<VaultItem*(__thiscall*)(VaultItem*, unsigned int)>(0x0054B5E0)(item, flags)
                            : item->Destroy(flags));
        });
        for (int items = 0; items <= 2; ++items) {
            cases.push_back([flags, items](bool original) {
                AddCategoryAt(0, "First");
                VaultCategory* category = AddCategoryAt(1, "Second");
                AddCategoryAt(2, "Third");
                for (int i = 0; i < items; ++i)
                    AddItemAt(i, category);
                Result(original ? Original<VaultCategory*(__thiscall*)(VaultCategory*, unsigned int)>(0x0054C980)(
                                      category, flags)
                                : category->Destroy(flags));
            });
        }
        // The node destructors and the searches'.
        struct NodeCall {
            uint32_t address;
            bool takesFlags;
        };
        for (NodeCall call : {NodeCall{0x0054B2B0, false}, NodeCall{0x0054B2D0, false}, NodeCall{0x0054B2F0, true},
                              NodeCall{0x0054B330, true}, NodeCall{0x0054B610, true}}) {
            cases.push_back([flags, call](bool original) {
                VaultCategory* category = AddCategoryAt(0, "Cat");
                VaultItem* item = AddItemAt(0, category);
                AddItemAt(1, category);
                void* node = item;
                if (original) {
                    if (call.takesFlags)
                        Result(Original<void*(__thiscall*)(void*, unsigned int)>(call.address)(node, flags));
                    else
                        Original<void(__thiscall*)(void*)>(call.address)(node);
                } else {
                    auto* ours = static_cast<GameNet::VaultNode*>(node);
                    switch (call.address) {
                    case 0x0054B2B0: ours->DestroyCategoryNode(); break;
                    case 0x0054B2D0: ours->DestroyItemNode(); break;
                    case 0x0054B2F0: Result(ours->DeleteItemNode(flags)); break;
                    case 0x0054B330: Result(ours->DeleteCategoryNode(flags)); break;
                    default: Result(ours->DeleteCategoriesHead(flags)); break;
                    }
                }
            });
        }
        for (uint32_t address : {0x0054A780u, 0x0054B280u}) {
            cases.push_back([flags, address](bool original) {
                auto* search = reinterpret_cast<VaultSearch*>(ItemAt(5));
                search->m_vtable = reinterpret_cast<void**>(0x1111);
                search->m_node = reinterpret_cast<Mem::ListNode*>(0x2222);
                if (original)
                    Result(Original<VaultSearch*(__thiscall*)(VaultSearch*, unsigned int)>(address)(search, flags));
                else
                    Result(address == 0x0054A780 ? search->DestroyCategorySearchAndFree(flags)
                                                 : search->DestroyItemSearchAndFree(flags));
            });
        }
    }
    for (uint32_t address : {0x0054A760u, 0x0054A770u}) {
        cases.push_back([address](bool original) {
            auto* search = reinterpret_cast<VaultSearch*>(ItemAt(5));
            search->m_vtable = reinterpret_cast<void**>(0x1111);
            if (original)
                Original<void(__thiscall*)(VaultSearch*)>(address)(search);
            else if (address == 0x0054A760)
                search->DestroyItemSearch();
            else
                search->DestroyCategorySearch();
        });
    }
    // The walk.
    for (int count = 0; count <= 3; ++count) {
        cases.push_back([count](bool original) {
            for (int i = 0; i < count; ++i)
                AddCategoryAt(i, "Cat");
            VaultSearch* search = &Vault()->m_search;
            Result(original ? Original<void*(__thiscall*)(VaultSearch*, Mem::ListNode*)>(0x0054B6B0)(
                                  search, &Vault()->m_categories)
                            : search->FirstItem(&Vault()->m_categories));
            for (int i = 0; i < count; ++i)
                Result(original ? Original<void*(__thiscall*)(VaultSearch*)>(0x0054B640)(search) : search->NextItem());
        });
    }
    // The vault itself.
    cases.push_back([](bool original) {
        std::memset(Vault(), 0xEE, sizeof(VaultMan));
        Result(original ? Original<VaultMan*(__thiscall*)(VaultMan*)>(0x0054C1D0)(Vault()) : Vault()->Construct());
    });
    for (bool task : {false, true}) {
        cases.push_back([task](bool original) {
            AddCategoryAt(0, "Cat");
            if (!task)
                Vault()->m_task = nullptr;
            if (original)
                Original<void(__thiscall*)(VaultMan*)>(0x0054B470)(Vault());
            else
                Vault()->DestroyBody();
        });
    }
    for (int count = 0; count <= 3; ++count) {
        for (bool command : {false, true}) {
            cases.push_back([count, command](bool original) {
                for (int i = 0; i < count; ++i) {
                    VaultCategory* category = AddCategoryAt(i, "Cat");
                    if (i == 1) {
                        AddItemAt(0, category);
                        AddItemAt(1, category);
                    }
                }
                if (command)
                    CallCFunc(original, 0x0054C460, GameNet::FreeDirectoryListing);
                else if (original)
                    Original<void(__thiscall*)(VaultMan*)>(0x0054C130)(Vault());
                else
                    Vault()->FreeDirectoryListing();
            });
        }
    }
    // Moving through the categories (from each one; round both ways).
    for (int at = 0; at < 3; ++at) {
        for (bool forward : {false, true}) {
            cases.push_back([at, forward](bool original) {
                AddCategoryAt(0, "Alpha");
                AddCategoryAt(1, "Bravo", true);
                AddCategoryAt(2, "Charlie");
                Vault()->m_search.m_node = &CategoryAt(at)->m_node;
                Vault()->m_category = CategoryAt(at);
                if (forward)
                    CallCFunc(original, 0x0054C770, GameNet::NextVaultCategory);
                else
                    CallCFunc(original, 0x0054C800, GameNet::PrevVaultCategory);
            });
        }
    }
    // The menu of the current category's items, for each type.
    for (uint32_t type : kTypes) {
        for (int items = 0; items <= 2; ++items) {
            cases.push_back([type, items](bool original) {
                SetUpVault(0, type);
                AddCategoryAt(0, "Other");
                VaultCategory* category = AddCategoryAt(1, "Current");
                for (int i = 0; i < items; ++i)
                    AddItemAt(i, category);
                Vault()->m_category = category;
                CallCFunc(original, 0x0054C470, GameNet::FillVaultMenu);
            });
        }
    }
    Compare2("vault lists: items, categories, searches, constructor / destructor, freeing, next / previous, the menu",
             cases);
}

// --- the transfers: starting them, the threads, ghttp's callbacks, the commands ---

void TestNetVaultTransfers()
{
    std::vector<Case> cases;
    cases.push_back([](bool original) {
        auto* request = &Word<GameNet::DownloadRequest>(kRequest);
        request->bufferSize = 0x25800;
        std::snprintf(request->url, sizeof(request->url), "http://example/file.txt");
        request->callback = reinterpret_cast<const void*>(0xC0BAC);
        request->failScript = 1;
        request->doneScript = 2;
        request->progressScript = 3;
        request->errorScript = 4;
        auto* self = reinterpret_cast<VaultMan*>(0x99); // (not used)
        if (original)
            Original<void(__thiscall*)(VaultMan*, const GameNet::DownloadRequest*)>(0x0054A910)(self, request);
        else
            self->Download(request);
    });
    cases.push_back([](bool original) {
        g_strings[KeyOf(At(kParams), "file")] = "http://www.thugonline.com/levels/cat/file.prk";
        CallCFunc(original, 0x0054AA10, GameNet::DownloadFile);
    });
    cases.push_back([](bool original) {
        const char* url = "http://www.thugonline.com/levels/dir_list.txt";
        if (original)
            Original<void(__thiscall*)(VaultMan*, const char*)>(0x0054B370)(Vault(), url);
        else
            Vault()->StartList(url);
    });
    cases.push_back([](bool original) {
        VaultCategory* category = AddCategoryAt(0, "Cool Parks");
        if (original)
            Original<void(__thiscall*)(VaultMan*, VaultCategory*)>(0x0054B400)(Vault(), category);
        else
            Vault()->ListItems(category);
    });
    cases.push_back([](bool original) {
        if (original)
            Original<void(__thiscall*)(VaultMan*)>(0x0054B500)(Vault());
        else
            Vault()->ListCategories();
    });
    for (uint32_t type : kTypes) {
        for (bool uploads : {false, true}) {
            cases.push_back([type, uploads](bool original) {
                std::snprintf(Vault()->m_folder, sizeof(Vault()->m_folder), "previous");
                AddCategoryAt(0, "Existing");
                Word<int>(kBuddies + 4) = uploads ? 0x011B0C16 : 0x011B0C17;
                g_checksums[KeyOf(At(kParams), 0u)] = type;
                CallCFunc(original, 0x0054C290, GameNet::DownloadDirectoryList);
            });
        }
    }
    for (uint32_t type : kTypes) {
        cases.push_back([type](bool original) {
            uint32_t params = At(kParams);
            g_checksums[KeyOf(params, "type")] = type;
            g_structures[KeyOf(params, "data")] = At(kData);
            g_strings[KeyOf(params, "description")] = "My park's description";
            g_strings[KeyOf(params, "filename")] = "my_park.prk";
            const char* names[] = {"score", "width", "length", "num_pieces", "num_gaps", "num_goals", "theme",
                                   "is_male"};
            int value = 11;
            for (const char* name : names)
                g_ints[KeyOf(params, name)] = value++;
            g_checksums[KeyOf(params, "tod_script")] = 0x7D7D7D7D;
            CallCFunc(original, 0x0054B060, GameNet::UploadFile);
        });
    }
    for (int transfer : {0, 4}) {
        cases.push_back([transfer](bool original) {
            SetUpVault(transfer);
            CallCFunc(original, 0x0054B240, GameNet::CancelFileTransfer);
        });
    }
    // The threads.
    cases.push_back([](bool original) {
        unsigned long result = original ? Original<unsigned long(__stdcall*)(void*)>(0x0054A7B0)(Vault())
                                        : VaultMan::ListThread(Vault());
        g_trace.push_back(static_cast<uint32_t>(result));
    });
    cases.push_back([](bool original) {
        char* url = &Word<char>(kUrlCopy);
        std::snprintf(url, 0x100, "http://example/download.bin");
        Vault()->m_bufferSize = 0x300;
        unsigned long result = original ? Original<unsigned long(__stdcall*)(void*)>(0x0054A860)(url)
                                        : VaultMan::DownloadThread(url);
        g_trace.push_back(static_cast<uint32_t>(result));
    });
    for (uint32_t type : kTypes) {
        cases.push_back([type](bool original) {
            SetUpVault(4, type);
            uint32_t idStruct = At(kPrefStruct);
            g_prefStructs[Crc::GenerateCRCFromString("network_id")] = idStruct;
            g_strings[KeyOf(idStruct, "ui_string")] = "network-id-42";
            unsigned long result = original ? Original<unsigned long(__stdcall*)(void*)>(0x0054AAC0)(Vault())
                                            : VaultMan::UploadThread(Vault());
            g_trace.push_back(static_cast<uint32_t>(result));
        });
    }
    // ghttp's callbacks.
    struct Progress {
        int state;
        int64_t received, total;
    };
    const Progress progress[] = {{3, 50, 200},          {7, 1, 3},       {7, 0x100000000LL, 0x300000000LL},
                                 {5, 50, 200},          {3, 50, 0},      {3, 50, -1},
                                 {7, 199, 200},         {3, 7, 0x100000000LL}, {2, 1, 1},
                                 {3, 29, 100},          {7, 57, 100}}; // (29 / 100 * 100 is 28.999...: 28)
    for (const Progress& p : progress) {
        cases.push_back([p](bool original) {
            using Fn = void(__cdecl*)(void*, int, const char*, int64_t, int64_t, int64_t, VaultMan*);
            void* request = reinterpret_cast<void*>(0x7E0);
            const char* buffer = &Word<char>(kBuffer);
            if (original)
                Original<Fn>(0x0054A660)(request, p.state, buffer, 0x55, p.received, p.total, Vault());
            else
                VaultMan::OnProgress(request, p.state, buffer, 0x55, p.received, p.total, Vault());
        });
    }
    for (int result : {0, 1, 11, -1}) {
        cases.push_back([result](bool original) {
            using Fn = int(__cdecl*)(void*, int, char*, int64_t, VaultMan*);
            char* buffer = &Word<char>(kBuffer);
            int value = original ? Original<Fn>(0x0054A740)(nullptr, result, buffer, 0x66, Vault())
                                 : VaultMan::OnTransferDone(nullptr, result, buffer, 0x66, Vault());
            g_trace.push_back(static_cast<uint32_t>(value));
        });
        for (uint32_t type : kTypes) {
            cases.push_back([result, type](bool original) {
                SetUpVault(3, type);
                using Fn = int(__cdecl*)(void*, int, char*, int64_t, VaultMan*);
                char* buffer = &Word<char>(kBuffer);
                int value = original ? Original<Fn>(0x0054A6B0)(nullptr, result, buffer, 0x66, Vault())
                                     : VaultMan::OnFileDownloaded(nullptr, result, buffer, 0x66, Vault());
                g_trace.push_back(static_cast<uint32_t>(value));
            });
        }
    }
    Compare2("vault transfers: download, lists, directory list, upload, cancel, the threads, ghttp's callbacks", cases);
}

// --- the task ---

void RunTask(bool original)
{
    auto* task = &Word<Tsk::BaseTask>(kTask);
    if (original)
        Original<void(__cdecl*)(Tsk::BaseTask*)>(0x0054B6E0)(task);
    else
        VaultMan::VaultTask(task);
}

void TestNetVaultTask()
{
    std::vector<Case> cases;
    // Timed out.
    for (int transfer : {0, 3}) {
        for (uint32_t now : {75000u, 75001u}) {
            cases.push_back([transfer, now](bool original) {
                SetUpVault(transfer);
                g_now = now;
                RunTask(original);
            });
        }
    }
    // Still going.
    for (int transfer : {0, 1, 3, 6}) {
        cases.push_back([transfer](bool original) {
            SetUpVault(transfer);
            Vault()->m_progress = 42;
            RunTask(original);
        });
    }
    // Finished: downloads and uploads.
    for (int transfer : {3, 4, 6}) {
        for (int result : {1, 2}) {
            for (int error : {0, 11}) {
                cases.push_back([transfer, result, error](bool original) {
                    SetUpVault(transfer, GameNet::kVaultSkaters);
                    Vault()->m_progress = 100;
                    Vault()->m_result = result;
                    Vault()->m_error = error;
                    RunTask(original);
                });
            }
        }
    }
    // Finished: the categories (lines, none, or a failure).
    const char* categoryLists[] = {"First\r\nSecond\r\nThird\r\n", "Only", "\r\n\r\n"};
    for (const char* list : categoryLists) {
        for (int result : {1, 2}) {
            cases.push_back([list, result](bool original) {
                SetUpVault(1);
                Vault()->m_progress = 100;
                Vault()->m_result = result;
                SetBuffer(list);
                RunTask(original);
            });
        }
    }
    cases.push_back([](bool original) { // the first category made is skipped (as the uploads' one is)
        SetUpVault(1);
        Vault()->m_progress = 100;
        Vault()->m_result = 1;
        SetBuffer("A\r\nB");
        AddCategoryAt(0, "Uploads", true);
        RunTask(original);
    });
    // Finished: a category's items, for each type; the next category, a skipped one, or the last.
    const char* itemLists[] = {
        "a.prk:Park A:Alice:12k:100:20:3:4:2863311530:cc1:A fine park\r\nb.prk:Park B:Bob:9k:50:2:1:0:7:cc2:Desc B",
        "c.cas:Skater C:Carol:1:cc:Skater desc\r\nd.cas:Skater D:Dave:0:cc:Second",
        "e.cat:Trick E:Eve:12345:cc:Trick desc", "f.cag:Goals F:Frank:9:cc:Goal desc",
        "g.xxx:Thing G:Gina:cc:Other desc"};
    for (int t = 0; t < 5; ++t) {
        for (int next = 0; next < 3; ++next) {
            cases.push_back([t, next, itemLists](bool original) {
                SetUpVault(2, kTypes[t]);
                Vault()->m_progress = 100;
                Vault()->m_result = 1;
                SetBuffer(itemLists[t]);
                VaultCategory* current = AddCategoryAt(0, "Current");
                if (next >= 1)
                    AddCategoryAt(1, "Next", next == 2);
                Vault()->m_search.m_node = &current->m_node;
                Vault()->m_category = current;
                RunTask(original);
            });
        }
    }
    cases.push_back([](bool original) {
        SetUpVault(2);
        Vault()->m_progress = 100;
        Vault()->m_result = 2;
        Vault()->m_category = AddCategoryAt(0, "Current");
        RunTask(original);
    });
    // Finished: the uploads list.
    cases.push_back([](bool original) {
        SetUpVault(5);
        Vault()->m_progress = 100;
        SetBuffer("dir/one.prk:Alice:3k:cc:First upload\r\ndir/two.prk:Bob:4k:cc2:Second upload");
        VaultCategory* uploads = AddCategoryAt(0, "Uploads", true);
        Vault()->m_search.m_node = &uploads->m_node;
        Vault()->m_category = uploads;
        RunTask(original);
    });
    for (int transfer : {0, 7}) {
        cases.push_back([transfer](bool original) {
            SetUpVault(transfer);
            Vault()->m_progress = 100;
            RunTask(original);
        });
    }
    Compare2("vault task: timeouts, progress, downloads / uploads, categories, items by type, uploads list", cases);
}

} // namespace

SELFTEST(Early, TestNetVaultLists, GameNetKit::Cleanup);
SELFTEST(Early, TestNetVaultTransfers, GameNetKit::Cleanup);
SELFTEST(Early, TestNetVaultTask, GameNetKit::Cleanup);
