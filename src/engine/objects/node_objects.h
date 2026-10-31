#pragma once

// Objects made for level nodes (0x00468B00-0x00468F14): particle objects, and game objects (class GameObject:
// a composite object of the components array 0x735A20BA plus two more components, or one made from the node
// alone), then their script and creation scripts. And the movie details base class (0x00468F20-0x004692EE) and
// a list of a node array's nodes of one class (0x00469300-0x00469492).

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"
#include "engine/maths/mat4.h"
#include "engine/maths/vec4.h"

namespace script {
class Struct;
class Array;
}
namespace gfx {
class ViewCam;
}

namespace objects {

class GameEntity;

// 0x00468B00: a particle object for the node `params` (moving_particle_composite_structure if it has HasMotion,
// else particle_composite_structure), finalised. (The result isn't returned.)
void spawn_effect_entity(script::Struct* params);
// 0x00468B60: a node object made: finalised; its node's script (0x2CA8A299) switched to with the node's params
// (0x7031F10C); with an integer 0x99A9B716, script 0x413B766D run on it with the node's name and that integer.
void start_node_object(GameEntity* object, script::Struct* params);
// 0x00468CB0: a game object for the node `params`: the components of 0x735A20BA, then two more (0x90 and 0x4C
// bytes, 0x0041F500 / 0x0041AAD0) after script 0xF3B84DA6, finalised; then as start_node_object (but script
// 0x413B766D first), and component 0x286A8D26's 0x00420480. The second argument isn't used.
GameEntity* create_game_object(script::Struct* params, int unused);
// 0x00468EB0: create_game_object(params, 0) - the object returned (the special item component reads it).
GameEntity* create_game_object_from_node(script::Struct* params);
// 0x00468EC0: with the flag 0x69C257E9 a plain composite object from the node (script 0xA98F5C63 run on it
// first), else a game object.
void create_node_object(script::Struct* params);

// The details of a movie the movie manager plays (vtable 0x00647A4C; the cutscene details 0x00647920 derive
// from it). A list node of itself (lists::Elem<VideoEntry>, vtable 0x00647A48), with a camera.
class VideoEntry {
public:
    // 0x00468FB0: the constructor (returns this): a list node of itself, no params, flags clear, a new camera.
    VideoEntry* make_new();
    // 0x00469230: the destructor body: the params freed, the camera deleted unless a viewport uses it, unlinked.
    void destroy_body();
    // 0x004692D0 (vtable slot 0): the scalar deleting destructor.
    VideoEntry* teardown(unsigned int flags);
    // 0x00468F70: lists::Elem<VideoEntry>'s scalar deleting destructor (unlinked; the base's vtable).
    VideoEntry* destroy_node(unsigned int flags);

    // 0x00469050: name (0x1A005041), its params (0x894FD988, copied), 0x8DDE72C0 / 0xB8CA12C5 (integers as
    // flags), 0xEB300C29 (a flag), from `params`.
    void configure_from_data(script::Struct* params);
    // 0x00469180: a new structure with the two integer settings.
    script::Struct* get_settings();
    // 0x00468F20: done when `playing` is false or +0x19 is set (and stays done); returns whether it's done.
    bool check_done(bool playing);
    // 0x00468F40 / 0x00468F50 / 0x00468F60: setters (true). The two flags take the argument's low byte as it is (the
    // movie manager passes a script's integer).
    bool set_flag19(uint8_t value);
    bool set_flag_1b(uint8_t value);
    bool set_value20(uint32_t value);

    memory::ListNode list_link_;          // +0x00 (vtable, data = this, priority 0, links)
    gfx::ViewCam* viewer_;         // +0x14
    bool flag18_;                 // +0x18 (0xEB300C29)
    uint8_t flag19_;              // +0x19 (0x8DDE72C0; a flag, any byte)
    bool is_finished_;                   // +0x1A
    uint8_t flag_1b_;              // +0x1B (a flag, any byte)
    bool flag_1c_;                 // +0x1C (0xB8CA12C5)
    uint8_t unknown1d_[3];
    uint32_t value20_;            // +0x20
    uint32_t label_;               // +0x24
    script::Struct* args_;     // +0x28
};
static_assert(offsetof(VideoEntry, viewer_) == 0x14 && offsetof(VideoEntry, flag_1c_) == 0x1C &&
                  offsetof(VideoEntry, label_) == 0x24 && offsetof(VideoEntry, args_) == 0x28,
              "layout must match the original");

// The nodes of one class (0xD18719D5) in a node array, with an optional transform (0x00469300-0x00469492).
class ClassNodeList {
public:
    struct TableRow {
        maths::Vec4 pos;      // +0x00
        TableRow* next;          // +0x10
        uint16_t index;       // +0x14: in the node array
        bool flag;            // +0x16: the node's 0x7C2552B9 flag
        bool unknown17;       // +0x17 (cleared)
    };

    // 0x004693C0: empty, no transform (returns this).
    ClassNodeList* prepare_all();
    // 0x00469300 (and 0x004693D0, a jump to it): every entry freed; no transform, no array.
    void get_clear();
    // 0x00469340: the transform (copied); it applies from now on.
    void set_transform(const maths::Mat4* matrix);
    // 0x00469350: `point` through the transform, if any (0x0040AF60), else as it is.
    maths::Vec4* xform(maths::Vec4* out, const maths::Vec4* point);
    // 0x004693E0: an entry for the node `params` (number `index`) at the front.
    void add_row(uint16_t index, script::Struct* params);
    // 0x00469430: an entry for each node of the class in `nodes` (kept).
    void set_nodes(script::Array* nodes);

    TableRow* entries_;           // +0x00
    bool transformed_;         // +0x04
    uint8_t unknown5_[3];
    maths::Mat4 placement_mat_;    // +0x08
    script::Array* nodes_;    // +0x48
};
static_assert(offsetof(ClassNodeList, placement_mat_) == 8 && offsetof(ClassNodeList, nodes_) == 0x48 &&
                  sizeof(ClassNodeList::TableRow) == 0x18,
              "layout must match the original");

} // namespace Obj
