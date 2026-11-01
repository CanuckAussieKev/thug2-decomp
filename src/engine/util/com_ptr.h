#pragma once

#include <utility>

// NOT ORIGINAL - an owning pointer to a COM object (Direct3D, DirectInput, DirectSound, the Windows audio session
// API): Release() when it goes, AddRef() when it's copied. Standard C++ only (the interfaces' AddRef / Release are
// plain virtual calls), so it builds with MSVC and MinGW alike - no WRL / ATL.
//
//   util::ComPtr<IDirect3DSurface9> surface;
//   if (SUCCEEDED(texture->GetSurfaceLevel(0, surface.put())))
//       use(surface.get());
//   // released here
namespace util {

template <typename Interface>
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): operator=(ComPtr) by value assigns copies and moves
class ComPtr {
public:
    ComPtr() = default;
    // Takes over a reference the caller already holds (no AddRef).
    explicit ComPtr(Interface* pointer) noexcept : pointer_(pointer) {}
    ComPtr(const ComPtr& other) noexcept : pointer_(other.pointer_)
    {
        if (pointer_)
            pointer_->AddRef();
    }
    ComPtr(ComPtr&& other) noexcept : pointer_(std::exchange(other.pointer_, nullptr)) {}
    ComPtr& operator=(ComPtr other) noexcept
    {
        std::swap(pointer_, other.pointer_);
        return *this;
    }
    ~ComPtr() { reset(); }

    Interface* get() const noexcept { return pointer_; }
    Interface* operator->() const noexcept { return pointer_; }
    explicit operator bool() const noexcept { return pointer_ != nullptr; }

    // The reference released (if any); empty.
    void reset() noexcept
    {
        if (Interface* old = std::exchange(pointer_, nullptr))
            old->Release();
    }
    // Releases the current reference and gives the address to receive a new one (a Get... / Create... call's out
    // parameter).
    Interface** put() noexcept
    {
        reset();
        return &pointer_;
    }
    // Hands the reference to the caller (no Release).
    [[nodiscard]] Interface* detach() noexcept { return std::exchange(pointer_, nullptr); }

private:
    Interface* pointer_ = nullptr;
};

} // namespace util
