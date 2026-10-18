// SPDX-FileCopyrightText: 2025-2026 Igal Alkon
// SPDX-License-Identifier: MIT

#ifndef VM_ALLOCATOR_HPP
#define VM_ALLOCATOR_HPP

#include <cstddef>
#include <new>
#include <stdexcept>
#include <type_traits>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
#  include <cerrno>
#endif

template <typename T>
class vm_allocator {
public:
    using value_type      = T;
    using size_type       = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::true_type;

    constexpr vm_allocator() noexcept = default;

    template <typename U>
    explicit constexpr vm_allocator(const vm_allocator<U>&) noexcept {}

    /* Reserve virtual address space. Physical pages are committed on demand via
       commit(). On overcommit systems the distinction is mostly advisory. */
    [[nodiscard]] static T* allocate(const size_type n) {
        if (n == 0) {
            return nullptr;
        }
        if (n > max_size()) {
            throw std::bad_array_new_length();
        }

        const size_type bytes = n * sizeof(T);
        void* p = map_pages(bytes);
        if (!p) {
            throw std::bad_alloc();
        }
        return static_cast<T*>(p);
    }

    /* Commit (make accessible) a previously reserved range. Safe to call
       multiple times on the same range. */
    static void commit(void* p, size_type bytes) noexcept {
        if (!p || bytes == 0) {
            return;
        }
#if defined(_WIN32)
        VirtualAlloc(p, bytes, MEM_COMMIT, PAGE_READWRITE);
#else
        /* Ensure the pages are readable/writable. On Linux this is usually a
           no-op after a normal mmap, but makes the intent explicit and works
           correctly if the initial mapping used PROT_NONE. */
        ::mprotect(p, bytes, PROT_READ | PROT_WRITE);
#endif
    }

    static void deallocate(T* p, const size_type n) noexcept {
        if (!p || n == 0) {
            return;
        }
        unmap_pages(p, n * sizeof(T));
    }

    [[nodiscard]] static size_type max_size() noexcept {
        return static_cast<size_type>(-1) / sizeof(T);
    }

    template <typename U>
    struct rebind {
        using other = vm_allocator<U>;
    };

private:
    static void* map_pages(size_type bytes) {
#if defined(_WIN32)
        /* Reserve only; commit later via commit(). */
        return VirtualAlloc(nullptr, bytes, MEM_RESERVE, PAGE_NOACCESS);
#else
        /* Map with full permissions. Overcommit + demand paging already gives
           lazy physical commitment on most Unix systems. Using PROT_NONE +
           later mprotect is possible but less portable across mmap
           implementations; full protection is simpler and correct. */
        void* p = ::mmap(nullptr, bytes,
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS,
                         -1, 0);
        return (p == MAP_FAILED) ? nullptr : p;
#endif
    }

    static void unmap_pages(void* p, size_type bytes) noexcept {
#if defined(_WIN32)
        (void)bytes;
        VirtualFree(p, 0, MEM_RELEASE);
#else
        ::munmap(p, bytes);
#endif
    }
};

template <typename T, typename U>
constexpr bool operator==(const vm_allocator<T>&, const vm_allocator<U>&) noexcept {
    return true;
}

template <typename T, typename U>
constexpr bool operator!=(const vm_allocator<T>&, const vm_allocator<U>&) noexcept {
    return false;
}

#endif // VM_ALLOCATOR_HPP
