// SPDX-FileCopyrightText: 2025-2026 Igal Alkon
// SPDX-License-Identifier: MIT

#ifndef BOX_H
#define BOX_H

#include <utility>
#include <type_traits>

namespace util {

namespace tags
{
    struct no_create_t  { explicit no_create_t()  = default; };
    struct no_init_t    { explicit no_init_t()    = default; };
    struct value_init_t { explicit value_init_t() = default; };

    using in_place_t = std::in_place_t;

    inline constexpr no_create_t  no_create{};
    inline constexpr no_init_t    no_init{};
    inline constexpr value_init_t value_init{};
    inline constexpr in_place_t   in_place{};
}

template<typename T>
class box
{
public:
    box() = default;

    explicit box(T* p) noexcept : _ptr{p} {}

    /* Construct in-place from args (In-Place) */
    template<typename... Args>
    explicit box(tags::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args&&...>)
        : _ptr{new T(std::forward<Args>(args)...)}
    {}

    /* This is the idiomatic std:: way to support initializer_list cases
       (used by std::optional, many third-party libraries, etc.) */
    template<typename U>
    explicit box(tags::in_place_t, std::initializer_list<U> il)
        noexcept(std::is_nothrow_constructible_v<T, std::initializer_list<U>>)
        : _ptr{new T(il)}
    {}

    /* Allocate handle but don't create resource (No Create) */
    explicit box(tags::no_create_t) noexcept : _ptr{nullptr} {}

    /* Allocate storage but don't initialize elements (the fastest allocation) */
    explicit box(tags::no_init_t)
        noexcept(std::is_nothrow_default_constructible_v<T>)
        : _ptr{new T}
    {}

    /* Allocate + value-initialize (zero-init for PODs) */
    explicit box(tags::value_init_t)
        noexcept(std::is_nothrow_default_constructible_v<T>)
        : _ptr{new T{}}
    {}

    ~box() { delete _ptr; }

    // move semantics
    box(box&& other) noexcept : _ptr{other._ptr} { other._ptr = nullptr; }
    box& operator=(box&& other) noexcept {
        delete _ptr;
        _ptr = other._ptr;
        other._ptr = nullptr;
        return *this;
    }

    // no-copy
    box(const box&) = delete;
    box& operator=(const box&) = delete;

    // accessors
    T& operator*()  const noexcept { return *_ptr; }
    T* operator->() const noexcept { return  _ptr; }
    T* get() const noexcept { return  _ptr; }
    explicit operator bool() const noexcept { return _ptr != nullptr; }

    // release ownership
    T* release() noexcept {
        T* p = _ptr;
        _ptr = nullptr;
        return p;
    }

    void reset(T* p = nullptr) noexcept {
        delete _ptr;
        _ptr = p;
    }

private:
    T* _ptr = nullptr;
};
}
#endif // BOX_H
