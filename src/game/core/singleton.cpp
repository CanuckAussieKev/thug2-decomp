// The out-of-line SingletonPtr<T> instances (singleton.h): one destructor and one scalar deleting destructor per
// singleton, at the addresses the linker gave them, and the one constructor that wasn't inlined.

#include "game/core/singleton.h"

#include <cstdint>

#include "game/file/async.h"
#include "game/file/pre.h"
#include "game/front/front_end.h"
#include "game/mc/memcard.h"
#include "game/obj/compositemgr.h"
#include "game/obj/objman.h"
#include "game/obj/tracker.h"
#include "game/script/cache.h"
#include "game/sio/device.h"
#include "game/sys/mlp.h"
#include "hook/game.h"
#include "hook/hook.h"

namespace Spt {

// 0x004E1970
PointerMember* PointerMember::Construct(void* pointer)
{
    m_pointer = pointer;
    return this;
}

namespace {

// A singleton without source yet: its Create (cdecl, bool) and Release (cdecl) in the game.
template <uint32_t kCreate, uint32_t kRelease, uint32_t kVtableAddress>
struct GameSingleton {
    using Type = void;
    static constexpr uint32_t kVtable = kVtableAddress;
    static void* Create(bool create) { return GameFunction<void*(__cdecl*)(bool)>(kCreate)(create); }
    static void Release() { GameFunction<void(__cdecl*)()>(kRelease)(); }
};

struct PreManager {
    using Type = Pre::PreMgr;
    static constexpr uint32_t kVtable = 0x0064AFB0;
    static Type* Create(bool create) { return Pre::CreateManager(create); }
    static void Release() { Pre::DestroyManager(); }
};
struct McManager {
    using Type = Mc::Manager;
    static constexpr uint32_t kVtable = 0x0064AFBC;
    static Type* Create(bool create) { return Mc::CreateManager(create); }
    static void Release() { Mc::ReleaseManager(); }
};
struct SioManager {
    using Type = Sio::Manager;
    static constexpr uint32_t kVtable = 0x006472B8;
    static Type* Create(bool create) { return Sio::CreateManager(create); }
    static void Release() { Sio::ReleaseManager(); }
};
struct AsyncLoader {
    using Type = File::AsyncLoaderModule;
    static constexpr uint32_t kVtable = 0x0064AFC4;
    static Type* Create(bool create) { return File::CreateAsyncLoaderModule(create); }
    static void Release() { File::DestroyAsyncLoaderModule(); }
};
struct ScriptCache {
    using Type = Script::CScriptCache;
    static constexpr uint32_t kVtable = 0x0064AFC8;
    static Type* Create(bool create) { return Script::CreateScriptCache(create); }
    static void Release() { Script::ReleaseScriptCache(); }
};
struct MlpManager {
    using Type = Mlp::Manager;
    static constexpr uint32_t kVtable = 0x0064A2EC;
    static Type* Create(bool create) { return Mlp::CreateManager(create); }
    static void Release() { Mlp::ReleaseManager(); }
};

// Named by their Create functions where there's no better name yet (Sys::Main makes them all).
using MdlManager = GameSingleton<0x0044F6F0, 0x0044F740, 0x0064AFB8>;   // the module manager
using SoundManager = GameSingleton<0x0047BA60, 0x0047C6C0, 0x006472B4>; // the sound effects manager
struct FrontEnd {
    using Type = Mdl::FrontEnd;
    static constexpr uint32_t kVtable = 0x0064AFEC;
    static Type* Create(bool create) { return Mdl::CreateFrontEnd(create); }
    static void Release() { Mdl::ReleaseFrontEnd(); }
};
struct ObjectTracker {
    using Type = Obj::CTracker;
    static constexpr uint32_t kVtable = 0x0064AFB4;
    static Type* Create(bool create) { return Obj::CreateTracker(create); }
    static void Release() { Obj::ReleaseTracker(); }
};
using Singleton44E7F0 = GameSingleton<0x0044E7F0, 0x0044E6B0, 0x0064AFC0>;
using Singleton4088E0 = GameSingleton<0x004088E0, 0x00407980, 0x0064AFCC>;
using Singleton455880 = GameSingleton<0x00455880, 0x004558D0, 0x0064AFD0>;
struct CompositeObjectManager {
    using Type = Obj::CCompositeObjectManager;
    static constexpr uint32_t kVtable = 0x0064AFD4;
    static Type* Create(bool create) { return Obj::CreateCompositeObjectManager(create); }
    static void Release() { Obj::ReleaseCompositeObjectManager(); }
};
using Singleton589180 = GameSingleton<0x00589180, 0x005880E0, 0x0064AFD8>;
using Singleton553DB0 = GameSingleton<0x00553DB0, 0x00552F70, 0x0064AFDC>;
using Singleton57C0D0 = GameSingleton<0x0057C0D0, 0x0057C120, 0x0064AFE0>;
using Singleton57EED0 = GameSingleton<0x0057EED0, 0x0057FDB0, 0x0064AFE4>;
using Singleton5691A0 = GameSingleton<0x005691A0, 0x00564C10, 0x0064AFE8>;
using Singleton53F520 = GameSingleton<0x0053F520, 0x005395C0, 0x0064AFF0>;
using Singleton590160 = GameSingleton<0x00590160, 0x0058D290, 0x0064AFF4>;
using Singleton4ACAF0 = GameSingleton<0x004ACAF0, 0x004AACF0, 0x0064AFF8>;
using Singleton45D690 = GameSingleton<0x0045D690, 0x0045D6E0, 0x0064AFFC>;
using Singleton574540 = GameSingleton<0x00574540, 0x00574910, 0x0064B000>;

} // namespace

} // namespace Spt

// Each instance's destructor (11 bytes: the vtable, then a jump to Release) and scalar deleting destructor.
REPLACE(0x0044FD40, &Spt::SingletonPtr<Spt::SoundManager>::DestroyBody);
REPLACE(0x0044FD60, &Spt::SingletonPtr<Spt::SoundManager>::Destroy);
REPLACE(0x0044FD50, &Spt::SingletonPtr<Spt::SioManager>::DestroyBody);
REPLACE(0x0044FD90, &Spt::SingletonPtr<Spt::SioManager>::Destroy);
REPLACE(0x004B2230, &Spt::SingletonPtr<Spt::MlpManager>::DestroyBody);
REPLACE(0x004B2240, &Spt::SingletonPtr<Spt::MlpManager>::Destroy);
REPLACE(0x004E1980, &Spt::SingletonPtr<Spt::PreManager>::DestroyBody);
REPLACE(0x004E1B00, &Spt::SingletonPtr<Spt::PreManager>::Destroy);
REPLACE(0x004E1990, &Spt::SingletonPtr<Spt::ObjectTracker>::DestroyBody);
REPLACE(0x004E1B30, &Spt::SingletonPtr<Spt::ObjectTracker>::Destroy);
REPLACE(0x004E19A0, &Spt::SingletonPtr<Spt::MdlManager>::DestroyBody);
REPLACE(0x004E1B60, &Spt::SingletonPtr<Spt::MdlManager>::Destroy);
REPLACE(0x004E19B0, &Spt::SingletonPtr<Spt::McManager>::DestroyBody);
REPLACE(0x004E1B90, &Spt::SingletonPtr<Spt::McManager>::Destroy);
REPLACE(0x004E19C0, &Spt::SingletonPtr<Spt::Singleton44E7F0>::DestroyBody);
REPLACE(0x004E1BC0, &Spt::SingletonPtr<Spt::Singleton44E7F0>::Destroy);
REPLACE(0x004E19D0, &Spt::SingletonPtr<Spt::AsyncLoader>::DestroyBody);
REPLACE(0x004E1BF0, &Spt::SingletonPtr<Spt::AsyncLoader>::Destroy);
REPLACE(0x004E19E0, &Spt::SingletonPtr<Spt::ScriptCache>::DestroyBody);
REPLACE(0x004E1C20, &Spt::SingletonPtr<Spt::ScriptCache>::Destroy);
REPLACE(0x004E19F0, &Spt::SingletonPtr<Spt::Singleton4088E0>::DestroyBody);
REPLACE(0x004E1C50, &Spt::SingletonPtr<Spt::Singleton4088E0>::Destroy);
REPLACE(0x004E1A00, &Spt::SingletonPtr<Spt::Singleton455880>::DestroyBody);
REPLACE(0x004E1C80, &Spt::SingletonPtr<Spt::Singleton455880>::Destroy);
REPLACE(0x004E1A10, &Spt::SingletonPtr<Spt::CompositeObjectManager>::DestroyBody);
REPLACE(0x004E1CB0, &Spt::SingletonPtr<Spt::CompositeObjectManager>::Destroy);
REPLACE(0x004E1A20, &Spt::SingletonPtr<Spt::Singleton589180>::DestroyBody);
REPLACE(0x004E1CE0, &Spt::SingletonPtr<Spt::Singleton589180>::Destroy);
REPLACE(0x004E1A30, &Spt::SingletonPtr<Spt::Singleton553DB0>::DestroyBody);
REPLACE(0x004E1D10, &Spt::SingletonPtr<Spt::Singleton553DB0>::Destroy);
REPLACE(0x004E1A40, &Spt::SingletonPtr<Spt::Singleton57C0D0>::DestroyBody);
REPLACE(0x004E1D40, &Spt::SingletonPtr<Spt::Singleton57C0D0>::Destroy);
REPLACE(0x004E1A50, &Spt::SingletonPtr<Spt::Singleton57EED0>::DestroyBody);
REPLACE(0x004E1D70, &Spt::SingletonPtr<Spt::Singleton57EED0>::Destroy);
REPLACE(0x004E1A60, &Spt::SingletonPtr<Spt::Singleton5691A0>::DestroyBody);
REPLACE(0x004E1DA0, &Spt::SingletonPtr<Spt::Singleton5691A0>::Destroy);
REPLACE(0x004E1A70, &Spt::SingletonPtr<Spt::FrontEnd>::DestroyBody);
REPLACE(0x004E1DD0, &Spt::SingletonPtr<Spt::FrontEnd>::Destroy);
REPLACE(0x004E1A80, &Spt::SingletonPtr<Spt::Singleton53F520>::DestroyBody);
REPLACE(0x004E1E00, &Spt::SingletonPtr<Spt::Singleton53F520>::Destroy);
REPLACE(0x004E1AC0, &Spt::SingletonPtr<Spt::Singleton590160>::DestroyBody);
REPLACE(0x004E1E30, &Spt::SingletonPtr<Spt::Singleton590160>::Destroy);
REPLACE(0x004E1AD0, &Spt::SingletonPtr<Spt::Singleton4ACAF0>::DestroyBody);
REPLACE(0x004E1E60, &Spt::SingletonPtr<Spt::Singleton4ACAF0>::Destroy);
REPLACE(0x004E1AE0, &Spt::SingletonPtr<Spt::Singleton45D690>::DestroyBody);
REPLACE(0x004E1E90, &Spt::SingletonPtr<Spt::Singleton45D690>::Destroy);
REPLACE(0x004E1AF0, &Spt::SingletonPtr<Spt::Singleton574540>::DestroyBody);
REPLACE(0x004E1EC0, &Spt::SingletonPtr<Spt::Singleton574540>::Destroy);

// The one constructor out of line (called by 0x004F75F0; it builds the instance pointer through 0x004E1970).
REPLACE(0x004E1A90, &Spt::SingletonPtr<Spt::Singleton590160>::Construct);
REPLACE(0x004E1970, &Spt::PointerMember::Construct);
