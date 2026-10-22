#pragma once

#include <cstddef>
#include <cstdint>

#include "game/obj/object.h"

// The model viewer's object (0x128 bytes, vtable 0x0064F138: a composite object with a model and animations, made by
// Viewer::CreateViewerObject with the viewer's net handler - constructor 0x0058ADB0). Its code is 0x005897B0-0x0058ADB0.
namespace Tsk {
class BaseTask;
}

namespace Obj {

class CViewerObject : public CCompositeObject {
public:
    // 0x005897B0: shut down - the script kill_viewer_object_panel run. (The object isn't used.)
    void ShutDown();
    // 0x005897D0: animation `anim` played from the start (to its own length, looping, blend 0.3, speed 1), if the object
    // has an animation component that knows it.
    void PlayAnimation(uint32_t anim);
    // 0x00589860: the destructor body - its task taken off its list, its loaded model asset (if any) unloaded and
    // forgotten, the task deleted, then CCompositeObject's. 0x0058A2B0: the scalar deleting destructor (vtable slot 0).
    void DestroyBody();
    CViewerObject* Destroy(unsigned int flags);

    Tsk::BaseTask* m_task;   // +0x108
    uint8_t m_unknown10C[0x14];
    uint32_t m_asset;        // +0x120 (the model asset's checksum)
    uint32_t m_unknown124;
};
static_assert(offsetof(CViewerObject, m_task) == 0x108 && offsetof(CViewerObject, m_asset) == 0x120 &&
                  sizeof(CViewerObject) == 0x128,
              "layout must match the original");

} // namespace Obj
