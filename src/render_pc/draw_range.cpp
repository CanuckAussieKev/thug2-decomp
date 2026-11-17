// NOT ORIGINAL - the drawing's distance cull for objects with a suspend component (see draw_range.h).

#include "render_pc/draw_range.h"

#include <vector>

#include "base/core/down_cast.h"
#include "base/core/no_destroy.h"
#include "base/maths/vec4.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/graphics_options.h"
#include "engine/gfx/model.h"
#include "engine/gfx/view_cam.h"
#include "engine/gfx/viewport.h"
#include "engine/gfx/viewport_list.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/objects/model_component.h"
#include "engine/objects/suspend_component.h"

namespace render::draw_range {

namespace {

struct RangeState {
    std::vector<Geom*> switched_off; // the geoms begin_frame switched off (end_frame switches them on)
    int culled = 0;
};

RangeState& range_state()
{
    static core::NoDestroy<RangeState> state;
    return state.get();
}

// Viewport 0's camera position, as the game left it (null: no camera).
const maths::Vec4* camera_position()
{
    ViewRegion* view = fetch_current_view(0);
    if (view == nullptr) {
        return nullptr;
    }
    auto* camera = static_cast<gfx::ViewCam*>(view->viewer().get_pull());
    return camera != nullptr ? camera->fetch_location() : nullptr;
}

} // namespace

void begin_frame()
{
    RangeState& state = range_state();
    state.switched_off.clear();
    state.culled = 0;
    const std::unique_ptr<objects::EntityHub>& hub = objects::object_hub_slot();
    const maths::Vec4* eye = camera_position();
    if (!hub || eye == nullptr || modules::viewport_count() > 1) {
        return;
    }
    const float scale = graphics_options::get_pull().lod_distance_scale_;
    for (objects::ObjectModule* module = hub->find_first_module_of_kind(objects::DormancyModule::kType);
         module != nullptr; module = module->next_of_type()) {
        auto* suspend = core::down_cast<objects::DormancyModule>(module);
        const float range = graphics_options::get_scaled(suspend->draw_range(), scale);
        objects::ObjectModule* mesh = suspend->mesh_instance();
        if (range <= 0.0F || mesh == nullptr) {
            continue;
        }
        RenderModel* model = core::down_cast<objects::MeshInstanceModule>(mesh)->mesh_instance().get();
        if (model == nullptr) {
            continue;
        }
        const maths::Vec4& at = suspend->entity()->at_point();
        const double dx = static_cast<double>(at.x) - eye->x;
        const double dy = static_cast<double>(at.y) - eye->y;
        const double dz = static_cast<double>(at.z) - eye->z;
        const double range_squared = static_cast<double>(range) * range;
        if ((dx * dx) + (dy * dy) + (dz * dz) < range_squared) { // (within: drawn, as the original's test)
            continue;
        }
        bool any = false;
        const auto& ring = model->geom_ring();
        for (ModelGeomNode* node = ring.head() != nullptr ? ring.head()->next : nullptr;
             node != nullptr && node != ring.head(); node = node->next) {
            Geom* geom = node->value != nullptr ? node->value->geom : nullptr;
            if (geom != nullptr && geom->is_operative()) {
                geom->change_live(false);
                state.switched_off.push_back(geom);
                any = true;
            }
        }
        state.culled += any ? 1 : 0;
    }
}

void end_frame()
{
    RangeState& state = range_state();
    // (in the reverse order, as frame_interp's: each geom ends as it was)
    for (auto it = state.switched_off.rbegin(); it != state.switched_off.rend(); ++it) {
        (*it)->change_live(true);
    }
    state.switched_off.clear();
}

int last_culled() { return range_state().culled; }

} // namespace render::draw_range
