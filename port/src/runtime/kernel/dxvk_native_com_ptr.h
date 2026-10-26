#pragma once

// The Linux DXVK Native headers provide D3D11 COM interfaces but not WRL.
// This small owning pointer implements the WRL operations used by our renderer.
#include <utility>
#include <memory>
#include <cstddef>

namespace Microsoft::WRL {

template <typename T> class ComPtr {
public:
    ComPtr() = default;
    ComPtr(std::nullptr_t) {}
    ComPtr(T* p) : ptr_(p) { if (ptr_) ptr_->AddRef(); }
    ComPtr(const ComPtr& other) : ComPtr(other.ptr_) {}
    ComPtr(ComPtr&& other) noexcept : ptr_(std::exchange(other.ptr_, nullptr)) {}
    ~ComPtr() { Reset(); }

    ComPtr& operator=(const ComPtr& other) {
        if (this != std::addressof(other)) { T* p = other.ptr_; if (p) p->AddRef(); Reset(); ptr_ = p; }
        return *this;
    }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != std::addressof(other)) { Reset(); ptr_ = std::exchange(other.ptr_, nullptr); }
        return *this;
    }
    ComPtr& operator=(T* p) {
        if (p) p->AddRef();
        Reset(); ptr_ = p;
        return *this;
    }
    ComPtr& operator=(std::nullptr_t) { Reset(); return *this; }

    T* Get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    bool operator==(const ComPtr& other) const { return ptr_ == other.ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }
    T** operator&() { Reset(); return &ptr_; }
    void Reset() { if (ptr_) ptr_->Release(); ptr_ = nullptr; }

    template <typename U> HRESULT As(ComPtr<U>* out) const {
        if (!out) return E_POINTER;
        out->Reset();
        return ptr_ ? ptr_->QueryInterface(__uuidof(U), reinterpret_cast<void**>(&out->ptr_)) : E_POINTER;
    }
    template <typename U> HRESULT As(U** out) const {
        if (!out) return E_POINTER;
        *out = nullptr;
        return ptr_ ? ptr_->QueryInterface(__uuidof(U), reinterpret_cast<void**>(out)) : E_POINTER;
    }

private:
    template <typename U> friend class ComPtr;
    T* ptr_ = nullptr;
};

} // namespace Microsoft::WRL
