#pragma once

// The park editor module (Ed::CParkEditor, 0x7C bytes, the singleton 0x007CE5D8 with its reference count
// 0x007CE5DC; Spt::SingletonPtr's Create 0x00590160 makes it, Release 0x0058D290 lets it go): the Create-A-Park
// mode itself. A module (vtable 0x0064F234: the scalar deleting destructor, start and stop) and an event listener
// (+0x14, vtable 0x0064F22C) for the menus' pad events; a task that runs the editor each frame (0x0058FFC0 ->
// LogicUpdate), an empty display task and an input handler (HandleInput) on one controller. Its state says what
// the park is doing: off (a normal level), edit (the editor up), test_play (skating the park from the editor)
// or regular_play (a created park loaded as a level).
//
// Here: 0x0058D290-0x0058FBD0 - the module's code, the camera and the controls, and the park editor's script
// commands (park_editor.cpp, park_editor_cfuncs.cpp). The constructor (0x0058FFF0), SetParkEditorState and
// SetParkEditorPauseMode follow at 0x0058FBD0.

#include <cstddef>
#include <cstdint>

#include "game/ed/park.h"
#include "game/obj/event.h"
#include "game/sys/module.h"
#include "hook/game_global.h"

namespace Inp {
struct Data;
}
namespace Mth {
class Vector;
}

namespace Ed {

class CParkEditor;
extern void** const kParkEditorVtable;         // 0x0064F234 (park_editor.cpp)
extern void** const kParkEditorListenerVtable; // 0x0064F22C

// Tsk::Task<CParkEditor> (0x20 bytes, vtable 0x006476F0).
struct ParkEditorTask {
    uint8_t m_node[0x18];   // the task list node (0x00406890, with the priority)
    void(__cdecl* m_code)(ParkEditorTask*);
    CParkEditor* m_data;
};
static_assert(sizeof(ParkEditorTask) == 0x20 && offsetof(ParkEditorTask, m_code) == 0x18);

// Inp::Handler<CParkEditor> (0x2C bytes, vtable 0x0064D7D8).
struct ParkEditorInputHandler {
    uint8_t m_node[0x18];         // the handler list node (0x00406890)
    Inp::Data* m_controlData;     // +0x18: the controller's data this frame
    uint8_t* m_device;            // +0x1C
    int m_index;                  // +0x20: the controller it's on at the start
    void(__cdecl* m_code)(ParkEditorInputHandler*);
    CParkEditor* m_data;
};
static_assert(sizeof(ParkEditorInputHandler) == 0x2C && offsetof(ParkEditorInputHandler, m_code) == 0x24);

GAME_GLOBAL_EXTERN(CParkEditor*, g_parkEditor);
GAME_GLOBAL_EXTERN(uint32_t, g_parkEditorReferences);

class CParkEditor : public Mdl::Module {
public:
    // m_state
    enum State : uint32_t {
        kStateOff = 0xD443A2BC,         // off
        kStateEdit = 0x7ECA21E5,        // edit
        kStateTestPlay = 0xA40F0B8A,    // test_play
        kStateRegularPlay = 0xCD3682C1, // regular_play
    };
    // m_flags
    enum : uint32_t { kFlagPaused = 0x2 };
    // m_commands: what the controls asked for this frame (the input handler's buttons, the menu events).
    enum : uint32_t {
        kCommandTurnLeft = 0x2,          // Circle: the piece (or the gap) a quarter turn back
        kCommandTurnRight = 0x4,         // Square
        kCommandPreviousPiece = 0x8,     // pad_left
        kCommandNextPiece = 0x10,        // pad_right
        kCommandPlace = 0x20,            // X
        kCommandRemove = 0x40,           // Triangle
        kCommandRaise = 0x80,            // L1
        kCommandLower = 0x100,           // L2
        kCommandPreviousSet = 0x200,     // pad_up
        kCommandNextSet = 0x400,         // pad_down
        kCommandWidenGapRight = 0x800,   // L1 (with a gap)
        kCommandNarrowGapRight = 0x1000, // L2
        kCommandZoomOut = 0x2000,        // button 0x10000 held
        kCommandZoomIn = 0x4000,         // button 0x20000 held
        kCommandWidenGapLeft = 0x8000,   // button 0x10000 pressed
        kCommandNarrowGapLeft = 0x10000, // button 0x20000 pressed
    };

    static CParkEditor*& Instance() { return g_parkEditor; }
    static uint32_t& References() { return g_parkEditorReferences; }

    // --- the module (park_editor.cpp) ---
    // 0x0058D290: Spt::SingletonPtr<CParkEditor>'s release: a reference dropped; the last deletes the instance.
    static void __cdecl Release();
    // 0x0058D2D0: the destructor body: the park manager deleted, the listener, the module (the tasks and the
    // handler aren't deleted - as in the original). 0x0058DAA0: the scalar deleting destructor (vtable slot 0).
    void DestroyBody();
    CParkEditor* Destroy(uint8_t flags);
    // 0x0058DAD0 (v_start_cb): the logic task onto the main loop's logic tasks, the input handler onto its
    // controller's handlers, the display task onto the display tasks.
    void StartCallback();
    // 0x0058D340 (v_stop_cb): the logic task and the input handler off their lists (not the display task).
    void StopCallback();
    // 0x0058DB30: back to a normal level: state off; the cursor, the piece menu and the park taken down, the gap
    // manager's copy deleted, the park's node array gone (0x00592C90).
    void Leave();
    // 0x0058D360: CustomParks\custom<slot>.prk saved, or loaded (the cursor's piece and the clipboards' previews
    // taken away first; then, unless the level is a normal one or `blockRebuild`, the park rebuilt - or the level
    // changed to load_sk5ed again if the park's shell size changed).
    void AccessDisk(bool save, int slot, bool blockRebuild);
    // 0x0058D4E0: a whole compressed map taken (loaded or received); while editing or test playing, the park
    // rebuilt from it (or the level changed to load_sk5ed again if the shell size isn't `shellSize`).
    void ApplyMap(const CompressedMap* map, int shellSize);
    // The park manager's own, for the rest of the game (`this` isn't used):
    void WriteIntoStructure(Script::CStruct* params);                   // 0x0058D600
    void ReadFromStructure(Script::CStruct* params, bool applyToEditor); // 0x0058D620
    void GetMapInfo(const CompressedMap* map, int* numGaps, int* numMetas, uint16_t* shellSize, uint32_t* theme,
                    int* width, int* length);                           // 0x0058D640
    void WriteCompressedMapIfBuilt();                                   // 0x0058D680
    CompressedMap* GetCompressedMapBuffer(bool markSaved);              // 0x0058D690
    void SetCompressedMapBuffer(const CompressedMap* map, bool markNotSaved); // 0x0058D6B0
    // 0x0058D6D0: whether the main heap has room for `count` * `size` floor pieces (Ed::MainHeapHasRoom).
    bool MainHeapHasRoom(int count, int size);
    // 0x0058D6F0: the cursor's position.
    Mth::Vector* GetCursorPos();
    // 0x0058D700
    void SetTimeOfDayScript(uint32_t script);
    static uint32_t __cdecl GetShellChecksum();  // 0x0058D710
    static const char* __cdecl GetParkName();    // 0x0058D720
    // 0x0058DCB0: the editor's look on (or off): the generator's pieces (0x00591CF0 with 0), the highlights and
    // the restart flags shown; the trick text paused and the goal points hidden (or the other way round).
    void ShowEditorView(bool on);
    // 0x0058DD70: the camera: the rail editor's while placing rails, else parked_cam.
    static void __cdecl SwitchCamera();
    // 0x0058DDC0: the park is at its memory limit (the largest figure at 1 or more).
    int IsParkFull();
    // 0x0058DDE0: the piece menu shows the cursor's set again and the scripts are told the mode.
    void RefreshPieceMenu();
    // 0x0058DEC0: whether `pos` (x and z) lies inside the park more than `margin` from its edges.
    static bool __cdecl IsInsidePark(Mth::Vector pos, float margin);
    // 0x0058DF80: the generator's restart point search (0x00592A10).
    static bool __cdecl FindRestart(void* a, void* b, void* c);
    // 0x0058DFA0: the park's player count.
    static int __cdecl GetMaxPlayers();
    // 0x0058EBE0: the menu commands: the previous / next set, the previous / next piece (the piece menu refreshed
    // and its container told).
    void ChangeSelection();
    // 0x0058ED50: a frame of editing: the camera (parked_cam) placed from the cursor, its angle, tilt and
    // distance; then (not paused) the sticks move the cursor and turn the camera, and the commands are carried out.
    void Update();
    // 0x0058F640: the left stick works the menus unless editing (not paused).
    void UpdateMenuStick();
    // 0x0058F6C0: the input handler onto controller `controller`'s handlers.
    void BindToController(int controller);
    // 0x0058F8A0: the logic task's work each frame: the skaters paused while editing (unpaused in test play);
    // editing with a cursor: the rail editor's scripts, Update (or, placing rails, only ChangeSelection) and the
    // memory bar; else the commands dropped.
    void LogicUpdate();
    // 0x0058FA50: the state becomes `state` (unless 0): the cursor shown while editing; the editor's look on, or
    // (test play, regular play) off with the gaps listed; the front end's auto-repeat (320 ms; 100 or 50); the menu
    // stick.
    void SetState(uint32_t state);

    // --- park_editor_state.cpp (0x0058FBD0-0x005901AA) ---
    static inline const VtableRef kVtable{&kParkEditorVtable};
    static inline const VtableRef kListenerVtable{&kParkEditorListenerVtable};
    // 0x0058FFF0: the constructor (state off; the logic task, the empty display task, the input handler, the
    // listener; the park manager made). Returns this.
    CParkEditor* Construct();
    // 0x00590160: Spt::SingletonPtr<CParkEditor>'s Create: the first call with `create` makes the instance; every
    // call takes a reference and returns it (null if there is none).
    static CParkEditor* __cdecl Create(bool create);
    // 0x0058FFC0: the logic task's code: the park editor's clock (0x007CE5EC) moved on, then LogicUpdate.
    static void __cdecl LogicTask(ParkEditorTask* task);

    // The listener's vtable (0x0064F22C), `this` the listener (+0x14).
    struct Listener : Obj::CEventListener {
        CParkEditor* Editor() { return reinterpret_cast<CParkEditor*>(reinterpret_cast<uint8_t*>(this) - 0x14); }
        // 0x0058DA90: the scalar deleting destructor's adjustor (the editor's).
        CParkEditor* DestroyEditor(uint8_t flags);
        // 0x0058DE40: the menus' pad events (to root_window) while editing with a cursor, not paused: up / down the
        // previous / next set, left / right the previous / next piece.
        void ListenTo(Obj::CEvent* event);
    };

    Listener m_listener;                    // +0x14
    bool m_commandDone;                     // +0x30: a command that changes the park was carried out
    uint8_t pad31[3];
    ParkEditorTask* mp_logicTask;           // +0x34 (0x0058FFC0, priority 0)
    ParkEditorTask* mp_displayTask;         // +0x38 (the empty 0x005480A0, priority -1000)
    ParkEditorInputHandler* mp_inputHandler; // +0x3C (HandleInput, priority 0)
    uint32_t m_state;                       // +0x40
    uint32_t m_commands;                    // +0x44
    uint32_t m_flags;                       // +0x48
    float m_moveSpeed;                      // +0x4C (480)
    float m_turnSpeed;                      // +0x50 (120)
    float m_rightX;                         // +0x54: the sticks (-128..127, then their dead zone taken off)
    float m_rightY;                         // +0x58
    float m_leftX;                          // +0x5C
    float m_leftY;                          // +0x60
    int m_unknown64;                        // +0x64 (0)
    float m_cameraAngle;                    // +0x68: degrees round y (0-360)
    float m_cameraTilt;                     // +0x6C: degrees down (10-90; 10 to start with)
    float m_cameraDistance;                 // +0x70 (500-1500; 1400 to start with)
    int m_lastTime;                         // +0x74: Tmr::GetTime at the last Update
    uint32_t m_timeOfDayScript;             // +0x78 (SetParkEditorTimeOfDay's tod_script)
};
static_assert(offsetof(CParkEditor, m_listener) == 0x14 && offsetof(CParkEditor, m_commandDone) == 0x30 &&
                  offsetof(CParkEditor, mp_inputHandler) == 0x3C && offsetof(CParkEditor, m_state) == 0x40 &&
                  offsetof(CParkEditor, m_moveSpeed) == 0x4C && offsetof(CParkEditor, m_cameraAngle) == 0x68 &&
                  offsetof(CParkEditor, m_timeOfDayScript) == 0x78 && sizeof(CParkEditor) == 0x7C,
              "layout must match the original");

// 0x0058DB70: the input handler's code: the sticks and the buttons as commands (cleared each frame).
void __cdecl HandleInput(ParkEditorInputHandler* handler);
// 0x0058E170: the area ResizePark is asked for without a structure: the park's bounds with w= / l= (or inc_w= /
// inc_l=), kept centred. Returns `out`.
GridDims* __cdecl GetResizeArea(GridDims* out, Script::CStruct* params);
// 0x0058E490: the park resized to `area`: the shell rebuilt round it, the park rebuilt, the rails' points and
// the goals outside it removed, the clipboards too big deleted, the cursor hidden, the collision rebuilt.
void __cdecl ResizeTo(GridDims area);

// The park editor's script commands (park_editor_cfuncs.cpp; cdecl, the CFunction table's).
bool __cdecl CFunc_FreeUpMemoryForPlayingPark(Script::CStruct* params, Script::CScript* script);      // 0x0058D730
bool __cdecl CFunc_IsParkUnsaved(Script::CStruct* params, Script::CScript* script);                    // 0x0058D760
bool __cdecl CFunc_SetEditedParkGapInfo(Script::CStruct* params, Script::CScript* script);             // 0x0058D780
bool __cdecl CFunc_ParkEditorSelectionAreaTooBigToCopy(Script::CStruct* params, Script::CScript* script); // 0x0058D820
bool __cdecl CFunc_CopyParkEditorSelectionToClipboard(Script::CStruct* params, Script::CScript* script); // 0x0058D840
bool __cdecl CFunc_CutParkEditorAreaSelection(Script::CStruct* params, Script::CScript* script);       // 0x0058D870
bool __cdecl CFunc_ParkEditorAreaSelectionDeletePieces(Script::CStruct* params, Script::CScript* script); // 0x0058D8C0
bool __cdecl CFunc_ContinueParkEditorAreaSelection(Script::CStruct* params, Script::CScript* script);  // 0x0058D8E0
bool __cdecl CFunc_SetEditorTheme(Script::CStruct* params, Script::CScript* script);                   // 0x0058D900
bool __cdecl CFunc_SetCustomParkName(Script::CStruct* params, Script::CScript* script);                // 0x0058D940
bool __cdecl CFunc_OptimizeParkGeometry(Script::CStruct* params, Script::CScript* script);             // 0x0058D980
bool __cdecl CFunc_GetParkEdMemStats(Script::CStruct* params, Script::CScript* script);                // 0x0058D9A0
bool __cdecl CFunc_ParkEdMemSnapshot(Script::CStruct* params, Script::CScript* script);                // 0x0058D9B0
bool __cdecl CFunc_EnableParkEdCursor(Script::CStruct* params, Script::CScript* script);               // 0x0058D9E0
bool __cdecl CFunc_ExitParkEdTestPlay(Script::CStruct* params, Script::CScript* script);               // 0x0058DA40
bool __cdecl CFunc_ParkUpdateUsageInfo(Script::CStruct* params, Script::CScript* script);              // 0x0058DA70
bool __cdecl CFunc_SetParkEditorTimeOfDay(Script::CStruct* params, Script::CScript* script);           // 0x0058DFB0
bool __cdecl CFunc_GetParkEditorTimeOfDayScript(Script::CStruct* params, Script::CScript* script);     // 0x0058DFE0
bool __cdecl CFunc_RebuildParkNodeArray(Script::CStruct* params, Script::CScript* script);             // 0x0058E010
bool __cdecl CFunc_GetParkEditorCursorPos(Script::CStruct* params, Script::CScript* script);           // 0x0058E020
bool __cdecl CFunc_CustomParkMode(Script::CStruct* params, Script::CScript* script);                   // 0x0058E070
bool __cdecl CFunc_SetParkEditorMaxPlayers(Script::CStruct* params, Script::CScript* script);          // 0x0058E0F0
bool __cdecl CFunc_CanCleanlyResizePark(Script::CStruct* params, Script::CScript* script);             // 0x0058E2D0
bool __cdecl CFunc_ResizePark(Script::CStruct* params, Script::CScript* script);                       // 0x0058E5E0
bool __cdecl CFunc_GetCurrentParkBounds(Script::CStruct* params, Script::CScript* script);             // 0x0058E7E0
bool __cdecl CFunc_CanChangeParkDimension(Script::CStruct* params, Script::CScript* script);           // 0x0058E870
bool __cdecl CFunc_SaveParkToDisk(Script::CStruct* params, Script::CScript* script);                   // 0x0058E9A0
bool __cdecl CFunc_LoadParkFromDisk(Script::CStruct* params, Script::CScript* script);                 // 0x0058E9E0
bool __cdecl CFunc_FireCustomParkGap(Script::CStruct* params, Script::CScript* script);                // 0x0058EA40
bool __cdecl CFunc_GetEditedParkGapName(Script::CStruct* params, Script::CScript* script);             // 0x0058EA70
bool __cdecl CFunc_GetEditorTheme(Script::CStruct* params, Script::CScript* script);                   // 0x0058EAA0
bool __cdecl CFunc_GetEditorMaxThemes(Script::CStruct* params, Script::CScript* script);               // 0x0058EAD0
bool __cdecl CFunc_GetCustomParkName(Script::CStruct* params, Script::CScript* script);                // 0x0058EAF0
bool __cdecl CFunc_IsCustomPark(Script::CStruct* params, Script::CScript* script);                     // 0x0058EB90
bool __cdecl CFunc_IsParkFull(Script::CStruct* params, Script::CScript* script);                       // 0x0058EBB0
bool __cdecl CFunc_SwitchParkEditorMenuPieceToMostRecentClipboard(Script::CStruct* params,
                                                                  Script::CScript* script);           // 0x0058ED00
bool __cdecl CFunc_SwitchToParkEditorCamera(Script::CStruct* params, Script::CScript* script);         // 0x0058F750
bool __cdecl CFunc_BindParkEditorToController(Script::CStruct* params, Script::CScript* script);       // 0x0058F870
// (park_editor_state.cpp)
// 0x0058FBD0 SetParkEditorState state=<off|edit|test_play|regular_play> [command=...]: the park set up for the state
// and the editor's state changed. False for an unknown state.
bool __cdecl CFunc_SetParkEditorState(Script::CStruct* params, Script::CScript* script);
// 0x0058FED0 SetParkEditorPauseMode pause | unpause: the editor paused (its menu and cursor hidden) or not.
bool __cdecl CFunc_SetParkEditorPauseMode(Script::CStruct* params, Script::CScript* script);

GAME_GLOBAL_EXTERN(bool, g_collisionAllowed);

} // namespace Ed
