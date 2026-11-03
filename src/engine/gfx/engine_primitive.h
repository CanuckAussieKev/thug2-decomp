#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

#include "engine/core/mem.h"
#include "engine/core/packed_colour.h"
#include "engine/maths/vec2.h"
#include "engine/maths/vec4.h"

// The engine's primitive (Nx; the platform-independent base of the PC primitive drawer in pc_primitive.h): a run of
// vertices set through its virtuals - here only "STUB" messages; the PC class does the work. Primitives that own their
// vertices go on a list of new ones (0x00700F00), which render_primitives sorts into the drawing list (0x00700EFC).
namespace render {

class BitmapTexture;

// (0x20 bytes of the PC class's 0x158; vtable 0x00649878.)
class Primitive : public memory::HeapObject {
public:
    // 0x0049C660: the fields cleared, blend mode 0x54628ED7, `own` kept - 1: on the list of new ones (and the vertices
    // its own), 2: drawn by hand (the lens flare's).
    explicit Primitive(int own);
    // 0x0049C6A0 (0x0049C790 the scalar deleting destructor, vtable slot 0): off its list when listed.
    virtual ~Primitive();
    Primitive(const Primitive&) = delete;
    Primitive& operator=(const Primitive&) = delete;

    // 0x0049C6C0: the platform's end of the set-up (end_setup), then readied.
    void get_ready();
    // 0x0049C6D0: drawn (render) when readied.
    void render_if_ready();
    // 0x0049C6E0: drawn from arrays (blit) when readied.
    void draw_if_ready(std::span<const float> positions, std::span<const uint32_t> colours, std::span<const float> uvs,
                       std::span<const float> normals);

    // A primitive's vertex layout flags, its D3D primitive type and its vertex count.
    struct PrimitiveLayout {
        uint32_t layout_flags_{};
        int primitive_type_{};
        int count{};
    };

    // The virtual functions (vtable slots 1 - 35): the base's bodies print "STUB: plat_..." - the getters return 0.
    virtual void set_texture(const BitmapTexture* texture, int unused1, int unused2);     // 0x0049C390
    virtual void change_colour(uint32_t rgba);                                      // 0x0049C3A0
    virtual void prepare_all(PrimitiveLayout primitive_layout); // 0x0049C3B0
    virtual void end_setup();                                                       // 0x0049C3C0
    virtual void render();                                                          // 0x0049C3D0
    virtual void blit(std::span<const float> positions, std::span<const uint32_t> colours, std::span<const float> uvs,
                      std::span<const float> normals);                         // 0x0049C3E0
    virtual void change_place(int index, const maths::Vec4& place); // 0x0049C3F0
    virtual void set_x(int index, float x);                          // 0x0049C400
    virtual void set_y(int index, float y);                          // 0x0049C410
    virtual void set_z(int index, float z);                          // 0x0049C420
    virtual void set_normal(int index, const maths::Vec4& normal);   // 0x0049C430
    virtual void set_normal_x(int index, float x);                   // 0x0049C440
    virtual void set_normal_y(int index, float y);                   // 0x0049C450
    virtual void set_normal_z(int index, float z);                   // 0x0049C460
    virtual void set_vertex_colour(int index, colour::Packed colour_word);        // 0x0049C470
    virtual void set_uv(int index, maths::Vec2 uv);                // 0x0049C480
    virtual void choose_u(int index, float u);                       // 0x0049C490
    virtual void choose_v(int index, float v);                       // 0x0049C4A0
    virtual float fetch_pos_x(int index);                            // 0x0049C4B0
    virtual float fetch_pos_y(int index);                            // 0x0049C4D0
    virtual float fetch_pos_z(int index);                            // 0x0049C4F0
    virtual float get_normal_x(int index);                           // 0x0049C510
    virtual float get_normal_y(int index);                           // 0x0049C530
    virtual float get_normal_z(int index);                           // 0x0049C550
    virtual uint32_t get_vertex_colour(int index);                   // 0x0049C570
    virtual float get_u(int index);                                  // 0x0049C590
    virtual float get_v(int index);                                  // 0x0049C5B0
    virtual void get_slide(const maths::Vec4& slide);               // 0x0049C5D0
    virtual void translate_x(float x);                               // 0x0049C5E0
    virtual void translate_y(float y);                               // 0x0049C5F0
    virtual void translate_z(float z);                               // 0x0049C600
    virtual void scroll_uv(maths::Vec2 uv);                        // 0x0049C610
    virtual void scroll_u(float u);                                  // 0x0049C620
    virtual void scroll_v(float v);                                  // 0x0049C630
    // The sort order against `other` (render_primitives: below 0 goes before it).
    virtual int weigh(const Primitive* other); // 0x0049C640

    // The fields other code reads and sets.
    Primitive*& following() { return following_; }
    [[nodiscard]] Primitive* following() const { return following_; }
    uint32_t& flag_bits() { return flag_bits_; }
    [[nodiscard]] uint32_t flag_bits() const { return flag_bits_; }
    [[nodiscard]] uint32_t blend_mode() const { return blend_mode_; }
    void set_blend_mode(uint32_t value) { blend_mode_ = value; }
    [[nodiscard]] int owns_vertices() const { return owns_vertices_; }
    void set_unknown14(uint8_t value) { unknown14 = value; }
    [[nodiscard]] bool ready() const { return ready_; }
    void set_ready(bool value) { ready_ = value; }

private:
    Primitive* following_ = nullptr; // +0x04: in the new or the drawing list
    uint32_t flag_bits_ = 0;         // +0x08: 0x100 on the list of new ones (the PC class's: 1 made, 2 set up, 4 ready)
    uint32_t blend_mode_{0x54628ED7}; // +0x0C: a checksum
    int owns_vertices_;              // +0x10: 1: listed (and the vertices its own)
    uint8_t unknown14 = 0;           // +0x14: set by the spawned polygons (their flag 2), the chains and the lens flare
    bool ready_ = false;             // +0x15: readied (get_ready): drawn by render_primitives
    std::array<uint8_t, 6> unknown16{};
    uint32_t unknown1c_ = 0; // +0x1C
    // The original's layout (checked inside the class: the members are private).
    static void check_layout()
    {
        static_assert(offsetof(Primitive, flag_bits_) == 0x08 && offsetof(Primitive, owns_vertices_) == 0x10 &&
                          offsetof(Primitive, ready_) == 0x15 && offsetof(Primitive, unknown1c_) == 0x1C &&
                          sizeof(Primitive) == 0x20,
                      "layout must match the original");
    }
};

// 0x0049C7D0 (cdecl): `primitive` put first on the list of new ones (flag 0x100).
void link_primitive(Primitive* primitive);
// 0x0049C7F0 (cdecl): `primitive` taken off its list (the new ones with flag 0x100, else the drawing list). Not
// there and not first, the original goes past the end of the list (crashes) - kept.
void unlink_primitive(Primitive* primitive);
// 0x0049C840 (cdecl): every new primitive moved into the drawing list in order (inserted before the first it
// weighs below 0 against), then each readied one drawn.
void render_primitives();
// 0x0049C700 (cdecl): a primitive render state kept in 0x00700EF8 (1: bits 0-2 - the depth test -, 2: bits 3-7, 4:
// bits 8-9) and passed on to the PC's 0x004C5E40.
void set_primitive_state(int state, int value);

} // namespace render
