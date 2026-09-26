/** ==========================================================================
 *  smac/ComPtr.hpp — Puntero inteligente COM mínimo, sin excepciones.
 *  ----------------------------------------------------------------------------
 *  Requisitos del proyecto: -fno-exceptions y -fno-rtti obligan a prescindir
 *  de std::shared_ptr y de envoltorios con excepciones. Este smart pointer
 *  aporta exactamente lo que S.M.A.C usa: AddRef/Release automáticos,
 *  operator&, liberación explícita y comparaciones para contenedores.
 *  Cero asignaciones dinámicas propias.
 *  ========================================================================== */
#pragma once

#include <windows.h>
#include <utility>

namespace smac {

/** Envoltorio COM RAII, copyable y sin memoria dinámica propia. */
template <typename T>
class ComPtr {
public:
    constexpr ComPtr() noexcept = default;
    constexpr ComPtr(std::nullptr_t) noexcept {}

    explicit ComPtr(T* raw) noexcept : ptr_(raw) {
        if (ptr_) ptr_->AddRef();
    }

    ComPtr(const ComPtr& other) noexcept : ptr_(other.ptr_) {
        if (ptr_) ptr_->AddRef();
    }

    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    template <typename U,
              typename = decltype(static_cast<T*>(static_cast<U*>(nullptr)))>
    ComPtr(const ComPtr<U>& other) noexcept : ptr_(other.Get()) {
        if (ptr_) ptr_->AddRef();
    }

    ~ComPtr() { Release(); }

    ComPtr& operator=(const ComPtr& other) noexcept {
        if (this != &other) {
            if (other.ptr_) other.ptr_->AddRef();
            Release();
            ptr_ = other.ptr_;
        }
        return *this;
    }

    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Release();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    /** Libera la referencia actual y adopta uno nueva SIN AddRef. */
    void Attach(T* raw) noexcept {
        Release();
        ptr_ = raw;
    }

    /** Suelta la referencia sin liberar (transfiere propiedad). */
    T* Detach() noexcept {
        T* raw = ptr_;
        ptr_ = nullptr;
        return raw;
    }

    void Release() noexcept {
        if (T* raw = std::exchange(ptr_, nullptr)) raw->Release();
    }

    T* Get() const noexcept { return ptr_; }

    /** & de salida para APIs COM: garantiza ptr_ == nullptr al llamar. */
    T** operator&() noexcept {
        Release();
        return &ptr_;
    }

    /** Igual que operator&, con nombre explícito para get_* de WebView2. */
    T** GetAddressOf() noexcept {
        Release();
        return &ptr_;
    }

    T* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    /** QueryInterface tipado (helpers para interfaces WebView2 modernas). */
    template <typename U>
    HRESULT As(U** out) const noexcept {
        if (!ptr_) { *out = nullptr; return E_POINTER; }
        return ptr_->QueryInterface(IID_PPV_ARGS(out));
    }

    template <typename U>
    HRESULT As(ComPtr<U>* out) const noexcept {
        return As(out->operator&());
    }

private:
    T* ptr_ = nullptr;
};

/** Comparaciones (requisito para find/remove_if sobre colecciones). */
template <typename T, typename U>
bool operator==(const ComPtr<T>& a, const ComPtr<U>& b) noexcept {
    return a.Get() == b.Get();
}
template <typename T>
bool operator==(const ComPtr<T>& a, std::nullptr_t) noexcept {
    return a.Get() == nullptr;
}
template <typename T>
bool operator!=(const ComPtr<T>& a, std::nullptr_t) noexcept {
    return a.Get() != nullptr;
}

/** Consulta una interfaz con IID explícito (helpers de conveniencia). */
template <typename T, typename U>
HRESULT AsWithIID(const ComPtr<T>& obj, REFIID iid, U** out) noexcept {
    if (!obj) { *out = nullptr; return E_POINTER; }
    return obj->QueryInterface(iid, reinterpret_cast<void**>(out));
}

} // namespace smac
