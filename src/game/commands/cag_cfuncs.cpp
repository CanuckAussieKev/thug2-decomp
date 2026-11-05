// Script commands (CFunctions) 0x005B3A90-0x005B4260: create-a-graphic, 2D positions, head bone groups (see
// cag_cfuncs.h).

#include "game/commands/cag_cfuncs.h"
#include "engine/script/degree_ratio.h"
#include "engine/gfx/engine_texture.h"
#include "engine/gfx/model_appearance.h"
#include "engine/gfx/texture_dict.h"

#include <cstddef>
#include <cstdint>

#include "base/maths/maths.h"
#include "engine/script/param_struct.h"
#include "engine/script/context.h"
#include "game/skate/profile_manager.h"
#include "game/skate/skate_module.h"
#include "game/rider/tagging.h"
#include <gsl/pointers>
#include <array>
#include "base/core/down_cast.h"
#include "base/core/checked_index.h"
#include "engine/gfx/engine_parts.h"
#include "base/core/checksums.h"

namespace commands {

constexpr std::size_t kPaletteBytes = 0x400; // 256 entries of four bytes
constexpr int kWorkspaceAlignment = 0x20;
constexpr int kSpriteSize = 0x80;            // the sprite texture's width and height
constexpr int kOpaqueAlpha = 0x80;           // 0x80 is the full alpha of a colour byte
constexpr int kPaletteDepthBits = 8;
// Angles in degrees: the quadrants of the placement angle.
constexpr float kAngleQuarter = 90.0F;
constexpr float kAngleHalf = 180.0F;
constexpr float kAngleThreeQuarters = 270.0F;
constexpr float kAngleFull = 360.0F;
constexpr double kAngleQuarterD = 90.0;
constexpr double kAngleHalfD = 180.0;
constexpr double kAngleFullD = 360.0;

namespace {

using script::Array;
using script::Struct;

constexpr uint32_t kLayerInfoList = 0x54AAD4BD; // "info_array"
constexpr uint32_t kCustomTag = 0xAA286543;     // (name unknown) the custom tag texture (in the dictionary 0x00700F20)
constexpr uint32_t kWorkspace = 0xFDA19704;     // (name unknown) the layer workspace texture
constexpr uint32_t kTextureId = 0xDFB43C53;     // "texture_id"
constexpr uint32_t kScale = 0x13B9DA7B;         // "scale"
constexpr uint32_t kFlipH = 0xA0C7507F;         // "flip_h"
constexpr uint32_t kFlipV = 0x5AC86D1C;         // "flip_v"
constexpr uint32_t kRot = 0xE2C6589E;           // "rot"
constexpr uint32_t kPosX = 0x960061E4;          // "pos_x"
constexpr uint32_t kPosY = 0xE1075172;          // "pos_y"
constexpr uint32_t kRgba = 0x3F6BCDBA;          // "rgba"
constexpr uint32_t kLayerId = 0x15910232;       // "layer_id"
constexpr uint32_t kPlayer = 0x67E6859A;        // "player"
constexpr uint32_t kTest = 0x278081F3;          // "Test"
constexpr uint32_t kWorkspaceSprite = 0xAFFFDBCA; // "cag_workspace_sprite"
constexpr uint32_t kSkater = 0x5B8AB877;          // "Skater"
constexpr uint32_t kMagnitude = 0xCA7882FE;       // "magnitude"
constexpr uint32_t kTheta = 0xF4E775F6;           // "theta"
constexpr uint32_t kX = 0x7323E97C;               // "x"
constexpr uint32_t kY = 0x0424D9EA;               // "Y"
constexpr uint32_t kSkaterMHead = 0x650FAB6D;     // "skater_m_head"
constexpr uint32_t kSkaterFHead = 0x0FC85BAE;     // "skater_f_head"
constexpr std::array<uint32_t, 4> kHeadBoneGroups = {{
    0xA28FAB7F, // "head_bone_group"
    0x1DBCFAE8, // "nose_bone_group"
    0x0442D3C1, // "jaw_bone_group"
    0xB90F08D0, // "headtop_bone_group"
}};

// The texture dictionaries (0x00700F1C the game's, 0x00700F20 the skater's) and a texture by checksum (0x0049F540),
// a new texture (0x0049FAA0: name, width, height, then 4 / 8, 0x20, 1, 1) and one taken out (0x0049F500).

render::BitmapTexture* find_texture(void* dictionary, uint32_t name)
{
    return static_cast<render::TextureSet*>(dictionary)->fetch_skin(name);
}
gsl::owner<render::BitmapTexture*> alloc_pc_texture(uint32_t name, int width, int height, int a, int b)
{
    return static_cast<render::TextureSet*>(render::engine_parts().game_texture_dict())->create_texture(name, width, height, a, b, 1, true);
}

// The C runtime's _ftol (0x006236FC): truncated to a 64-bit integer, the low half kept.
int ftol(double value) { return static_cast<int>(static_cast<int64_t>(value)); }

modules::Skate* get_skate() { return modules::Skate::sole(); }

} // namespace

// 0x005B3A90: GenerateCAGTexture info_array=[{texture_id scale flip_h flip_v rot pos_x pos_y rgba layer_id}...]
// player=n [Test=1]: the custom tag texture composed from its layers: cleared, then each layer's texture drawn into a
// workspace texture (as big as the tag), scaled, flipped, turned, moved (positions doubled for a 128-wide tag) and
// tinted, and blended onto the tag; the tag's palette made again (kept to opaque when no layer was translucent and
// it's an 8-bit tag); the workspace thrown away. Player 0's skater's tag is then marked custom and put on its graffiti
// texture; with Test, the result also shown on cag_workspace_sprite. Always true.
bool cfunc_generate_cag_texture(script::Struct* params, script::Context /*context*/)
{
    std::array<uint8_t, kPaletteBytes> palette{}; // (filled by the tag's palette call, not here)
    Array* layers = nullptr;
    params->fetch_list(kLayerInfoList, &layers, 1);
    render::BitmapTexture* tag = find_texture(render::engine_parts().particle_texture_dict(), kCustomTag);
    tag->begin_edit(false, false);
    tag->modulate(0U, false);
    const uint32_t size = tag->fetch_breadth();
    const int width = static_cast<uint16_t>(size);
    gsl::owner<render::BitmapTexture*> workspace = alloc_pc_texture(kWorkspace, width, width, 4, kWorkspaceAlignment);
    workspace->begin_edit(false, false);

    const uint32_t count = layers->extent();
    bool translucent = false;
    int last_layer = -1;
    for (uint32_t i = 0; i < count; ++i) {
        auto* info = layers->struct_at(i);
        uint32_t texture_id = 0;
        info->read_hash_value(kTextureId, &texture_id, 1);
        render::BitmapTexture* source = find_texture(render::engine_parts().game_texture_dict(), texture_id);
        source->begin_edit(false, false);
        workspace->end_edit();
        workspace->begin_edit(false, false);
        workspace->resize_from(source, 0);
        source->end_edit();
        workspace->get_border(0U);
        float scale = 0.0F;
        info->fetch_float(kScale, &scale, 1);
        workspace->get_zoom(scale, scale);
        int flip_h = 0;
        info->fetch_int_value(kFlipH, &flip_h, 1);
        int flip_v = 0;
        info->fetch_int_value(kFlipV, &flip_v, 1);
        if (flip_h != 0) {
            workspace->get_mirror();
        }
        if (flip_v != 0) {
            render::BitmapTexture::flip_vertically();
        }
        float rot = 0.0F;
        info->fetch_float(kRot, &rot, 1);
        auto radians = static_cast<float>(static_cast<double>(rot) * kDegreeToRadianRatio); // pi / 180
        workspace->spin_by(radians, {.x = -1.0F, .y = -1.0F});
        float x = 0.0F;
        info->fetch_float(kPosX, &x, 1);
        float y = 0.0F;
        info->fetch_float(kPosY, &y, 1);
        if (static_cast<uint16_t>(size) == kSpriteSize) {
            x = x + x;
            y = y + y;
        }
        workspace->get_offset(ftol(x), ftol(y), false, 0);
        Array* rgba = nullptr;
        info->fetch_list(kRgba, &rgba, 1);
        auto alpha = static_cast<uint8_t>(ftol(rgba->fetch_float(3)));
        auto blue = static_cast<uint8_t>(ftol(rgba->fetch_float(2)));
        auto green = static_cast<uint8_t>(ftol(rgba->fetch_float(1)));
        auto red = static_cast<uint8_t>(ftol(rgba->fetch_float(0)));
        const uint32_t colour = red | green << 8 | blue << 16 | static_cast<uint32_t>(alpha) << 24;
        workspace->modulate(colour, false);
        if (alpha < kOpaqueAlpha) {
            translucent = true;
        }
        int layer = 0;
        info->fetch_int_value(kLayerId, &layer, 1);
        if (last_layer != layer) {
            last_layer = layer; // (kept, never used)
        }
        workspace->write_back(1, static_cast<void*>(nullptr));
        tag->get_overlay(workspace, 1);
    }

    tag->get_border(0U);
    if (!translucent && tag->get_depth() == kPaletteDepthBits) {
        tag->write_back(0, static_cast<void*>(palette.data()));
    } else {
        tag->write_back(1, static_cast<void*>(nullptr));
    }
    static_cast<render::TextureSet*>(render::engine_parts().game_texture_dict())->unload_texture(workspace);

    int player = 0;
    params->fetch_int_value(kPlayer, &player, 1);
    if (player == 0) {
        auto* tagging = core::down_cast<objects::TaggingComponent>(
            get_skate()->get_rider_at(0)->fetch_module(objects::TaggingComponent::kType));
        tagging->set_custom_tag(true); // (0x004E4000, a setter folded with others)
        tagging->apply_custom_tag_texture();
    }
    int test = 0;
    params->fetch_int_value(kTest, &test, 0);
    if (test != 0) {
        gsl::owner<render::BitmapTexture*> sprite = alloc_pc_texture(kWorkspaceSprite, kSpriteSize, kSpriteSize, kPaletteDepthBits, kWorkspaceAlignment);
        sprite->copy_from(tag);
    }
    return true;
}

// 0x005B3ED0: SyncCAGWorkspace Skater=n: the custom tag texture copied from skater n's graffiti texture, if it has one
// (the current profile is fetched and not used). Always true.
bool cfunc_sync_cag_workspace(script::Struct* params, script::Context /*context*/)
{
    int index = 0;
    params->fetch_int_value(kSkater, &index, 1);
    objects::GameEntity* skater = get_skate()->get_rider_at(index);
    modules::Skate::fetch_account(static_cast<Struct*>(nullptr));
    auto* tagging = core::down_cast<objects::TaggingComponent>(skater->fetch_module(objects::TaggingComponent::kType));
    void* graffiti = tagging->graffiti(); // (0x00553E90, a getter folded with others)
    if (graffiti) {
        find_texture(render::engine_parts().particle_texture_dict(), kCustomTag)->copy_from(static_cast<render::BitmapTexture*>(graffiti));
    }
    return true;
}

// 0x005B3F40: Get2DPosFrom2DVec magnitude=m theta=degrees: x and Y, the vector's components (theta brought into the
// first quadrant first, the signs put back after; the axes exactly for 0, 90, 180, 270 and 360; otherwise the fast
// sine and cosine). Always true.
bool cfunc_get_2d_pos_from_2d_vec(script::Struct* params, script::Context context)
{
    float magnitude = 0.0F;
    params->fetch_float(kMagnitude, &magnitude, 1);
    float theta = 0.0F;
    params->fetch_float(kTheta, &theta, 1);
    bool negate_x = false;
    bool negate_y = false;
    if (theta > kAngleQuarter && theta < kAngleHalf) {
        negate_x = true;
        theta = static_cast<float>(kAngleQuarterD - (static_cast<double>(theta) - kAngleQuarterD));
    } else if (theta > kAngleHalf && theta < kAngleThreeQuarters) {
        negate_x = true;
        theta = static_cast<float>(static_cast<double>(theta) - kAngleHalfD);
        negate_y = true;
    } else if (theta > kAngleThreeQuarters && theta < kAngleFull) {
        theta = static_cast<float>(kAngleFullD - theta);
        negate_y = true;
    }
    float x = 0.0F;
    float y = 0.0F;
    if (theta == 0.0F || theta == kAngleFull) {
        x = magnitude;
        y = 0.0F;
    } else if (theta == kAngleQuarter) {
        x = 0.0F;
        y = magnitude;
    } else if (theta == kAngleHalf) {
        x = magnitude;
        y = 0.0F;
        negate_x = true; // (the original goes straight to the negation)
    } else if (theta == kAngleThreeQuarters) {
        x = 0.0F;
        y = magnitude;
        negate_y = true;
    } else {
        auto radians = static_cast<float>(static_cast<double>(theta) * kDegreeToRadianRatio); // pi / 180
        y = static_cast<float>(static_cast<double>(maths::fast_sin(radians)) * magnitude);
        x = static_cast<float>(static_cast<double>(maths::fast_cos(radians)) * magnitude);
    }
    if (negate_x) {
        x = x * -1.0F;
    }
    if (negate_y) {
        y = y * -1.0F;
    }
    context.args()->push_float(checksums::Name{kX}, x);
    context.args()->push_float(checksums::Name{kY}, y);
    return true;
}

// 0x005B4150: ResolveHeadBoneScale player=n: player n's profile's appearance given each head bone group (head, nose,
// jaw, headtop) it lacks, from its head part (skater_m_head, else skater_f_head). False without a head part; true
// otherwise (also without a profile).
bool cfunc_resolve_head_bone_scale(script::Struct* params, script::Context /*context*/)
{
    int player = 0;
    params->fetch_int_value(kPlayer, &player, 1);
    auto* profile = modules::Skate::fetch_account(player);
    if (!profile) {
        return true;
    }
    // the appearance's part structures (0x00489050: a part's, through its description; 0x004890B0: the appearance's
    // own component of that name)
    auto part_of = [&](uint32_t name) { return profile->appearance()->get_part_option(name); };
    Struct* head = part_of(kSkaterMHead);
    if (!head) {
        head = part_of(kSkaterFHead);
        if (!head) {
            return false;
        }
    }
    std::array<void*, 4> has{};
    for (int i = 0; i < 4; ++i) {
        core::at(has, i) = profile->appearance()->get_part(core::at(kHeadBoneGroups, i));
    }
    Struct* appearance = &profile->appearance()->structure();
    for (int i = 0; i < 4; ++i) {
        if (core::at(has, i)) {
            continue;
        }
        Struct* group = nullptr;
        if (head->read_sub_struct(core::at(kHeadBoneGroups, i), &group, 0)) {
            appearance->put_sub_struct(core::at(kHeadBoneGroups, i), group);
        }
    }
    return true;
}

} // namespace commands
