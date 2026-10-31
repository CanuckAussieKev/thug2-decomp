#pragma once

#include <cstddef>
#include <cstdint>

// The engine's primitive (Nx; the platform-independent base of the PC primitive drawer in pc_primitive.h): a run of
// vertices set through its virtuals - here only "STUB" messages; the PC class does the work. Primitives that own their
// vertices go on a list of new ones (0x00700F00), which render_primitives sorts into the drawing list (0x00700EFC).
namespace render {

// (0x20 bytes of the PC class's 0x158; vtable 0x00649878.)
struct Primitive {
    void** vtable_;
    Primitive* following_; // +0x04: in the new or the drawing list
    uint32_t flag_bits_;   // +0x08: 0x100 on the list of new ones (the PC class's: 1 made, 2 set up, 4 ready)
    uint32_t blend_mode_;  // +0x0C: a checksum (0x54628ED7 to start)
    int owns_vertices_;    // +0x10: 1: listed (and the vertices its own)
    uint8_t unknown14;     // +0x14
    bool ready_;           // +0x15: readied (Ready): drawn by render_primitives
    uint8_t unknown16[6];
    uint32_t unknown1c_; // +0x1C

    // 0x0049C660: the fields cleared, the vtable, blend mode 0x54628ED7, `own` kept - 1: on the list of new ones.
    // Returns this.
    Primitive* make_new(int own);
    // 0x0049C6A0: the vtable; off its list when listed.
    void destroy_body();
    // 0x0049C790 (vtable slot 0): destroyed, and freed with flags & 1. Returns this.
    Primitive* teardown(unsigned int flags);
    // 0x0049C6C0: the platform's end (vtable slot 4), then readied.
    void get_ready();
    // 0x0049C6D0: drawn (vtable slot 5) when readied.
    void render_if_ready();
    // 0x0049C6E0: drawn from arrays (vtable slot 6) when readied.
    void draw_if_ready(const float* positions, const uint32_t* colours, const float* uvs, const float* normals);

    // The virtuals' bodies (vtable slots 1 - 35, in PcPrimitive's order): each prints "STUB: plat_..." - the getters
    // return 0.
    void set_texture(const uint8_t* texture, int unused1, int unused2);                                 // 0x0049C390
    void change_colour(uint32_t rgba);                                                                  // 0x0049C3A0
    void prepare_all(uint32_t layout_flags, int primitive_type, int count);                             // 0x0049C3B0
    void get_end();                                                                                     // 0x0049C3C0
    void render();                                                                                      // 0x0049C3D0
    void blit(const float* positions, const uint32_t* colours, const float* uvs, const float* normals); // 0x0049C3E0
    void change_place(int index, float x, float y, float z);                                            // 0x0049C3F0
    void set_x(int index, float x);                                                                     // 0x0049C400
    void set_y(int index, float y);                                                                     // 0x0049C410
    void set_z(int index, float z);                                                                     // 0x0049C420
    void set_normal(int index, float x, float y, float z);                                              // 0x0049C430
    void set_normal_x(int index, float x);                                                              // 0x0049C440
    void set_normal_y(int index, float y);                                                              // 0x0049C450
    void set_normal_z(int index, float z);                                                              // 0x0049C460
    void set_vertex_colour(int index, uint32_t rgba);                                                   // 0x0049C470
    void set_uv(int index, float u, float v);                                                           // 0x0049C480
    void choose_u(int index, float u);                                                                  // 0x0049C490
    void choose_v(int index, float v);                                                                  // 0x0049C4A0
    float fetch_pos_x(int index);                                                                       // 0x0049C4B0
    float fetch_pos_y(int index);                                                                       // 0x0049C4D0
    float fetch_pos_z(int index);                                                                       // 0x0049C4F0
    float get_normal_x(int index);                                                                      // 0x0049C510
    float get_normal_y(int index);                                                                      // 0x0049C530
    float get_normal_z(int index);                                                                      // 0x0049C550
    uint32_t get_vertex_colour(int index);                                                              // 0x0049C570
    float get_u(int index);                                                                             // 0x0049C590
    float get_v(int index);                                                                             // 0x0049C5B0
    void get_slide(float x, float y, float z);                                                          // 0x0049C5D0
    void translate_x(float x);                                                                          // 0x0049C5E0
    void translate_y(float y);                                                                          // 0x0049C5F0
    void translate_z(float z);                                                                          // 0x0049C600
    void scroll_uv(float u, float v);                                                                   // 0x0049C610
    void scroll_u(float u);                                                                             // 0x0049C620
    void scroll_v(float v);                                                                             // 0x0049C630
    int weigh(const Primitive* other);                                                                  // 0x0049C640
};
static_assert(offsetof(Primitive, flag_bits_) == 0x08 && offsetof(Primitive, owns_vertices_) == 0x10 &&
                  offsetof(Primitive, ready_) == 0x15 && offsetof(Primitive, unknown1c_) == 0x1C &&
                  sizeof(Primitive) == 0x20,
              "layout must match the original");

// 0x0049C6F0 (thiscall; linker-folded: a jump through vtable slot 35 - the primitives' Compare, a texture's slot 35
// for the texture code - its arguments left to the callee): a naked jump.
void call_virtual35_stub();
// 0x0049C7D0 (cdecl): `primitive` put first on the list of new ones (flag 0x100).
void __cdecl link_primitive(Primitive* primitive);
// 0x0049C7F0 (cdecl): `primitive` taken off its list (the new ones with flag 0x100, else the drawing list). Not
// there and not first, the original goes past the end of the list (crashes) - kept.
void __cdecl unlink_primitive(Primitive* primitive);
// 0x0049C840 (cdecl): every new primitive moved into the drawing list in order (inserted before the first it
// Compares below 0 with), then each readied one drawn.
void __cdecl render_primitives();
// 0x0049C700 (cdecl): a primitive render state kept in 0x00700EF8 (1: bits 0-2 - the depth test -, 2: bits 3-7, 4:
// bits 8-9) and passed on to the PC's 0x004C5E40.
void __cdecl set_primitive_state(int state, int value);

} // namespace render
