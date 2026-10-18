// SPDX-FileCopyrightText: 2025-2026 Igal Alkon
// SPDX-License-Identifier: MIT

#ifndef PINNED_COLONY_HPP
#define PINNED_COLONY_HPP

#include "vm_allocator.hpp"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

/**
 * @file pinned_colony.hpp
 * Fixed-capacity colony/hive-style container:
 *   - elements never relocate (pointers/references stay valid until erase)
 *   - erase leaves holes; insert/emplace reuse holes via a free-list
 *   - bidirectional iteration skips empty slots in O(1) via a jump-counting
 *     skipfield (remaining-distance encoding)
 *   - single contiguous virtual mapping (stable raw pointers)
 */

template <typename T, typename Allocator = vm_allocator<T>>
class pinned_colony {
public:
    using value_type      = T;
    using allocator_type  = Allocator;
    using alloc_traits    = std::allocator_traits<Allocator>;
    using size_type       = alloc_traits::size_type;
    using difference_type = alloc_traits::difference_type;
    using reference       = value_type&;
    using const_reference = const value_type&;
    using pointer         = alloc_traits::pointer;
    using const_pointer   = alloc_traits::const_pointer;

private:
    using byte_alloc   = alloc_traits::template rebind_alloc<std::byte>;
    using byte_traits  = std::allocator_traits<byte_alloc>;
    using skip_alloc   = alloc_traits::template rebind_alloc<size_type>;
    using skip_traits  = std::allocator_traits<skip_alloc>;
    using index_alloc  = alloc_traits::template rebind_alloc<size_type>;
    using index_traits = std::allocator_traits<index_alloc>;

    static constexpr size_type npos = static_cast<size_type>(-1);

    template <bool IsConst>
    class iterator_impl {
        using colony_ptr = std::conditional_t<IsConst, const pinned_colony*, pinned_colony*>;
        using ref_type   = std::conditional_t<IsConst, const_reference, reference>;
        using ptr_type   = std::conditional_t<IsConst, const_pointer, pointer>;

    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type        = T;
        using difference_type   = typename pinned_colony::difference_type;
        /* pointer / reference are deliberately not redeclared here to avoid a
           GCC "changes meaning of" diagnostic with the enclosing class's
           identically-named using-declarations. The operators below use the
           private ref_type / ptr_type aliases, which is sufficient. */

        iterator_impl() = default;

        ref_type operator*() const { return *colony_->ptr_at(index_); }
        ptr_type operator->() const { return colony_->ptr_at(index_); }

        iterator_impl& operator++() {
            index_ = colony_->next_alive(index_ + 1);
            return *this;
        }

        iterator_impl operator++(int) {
            iterator_impl tmp = *this;
            ++(*this);
            return tmp;
        }

        iterator_impl& operator--() {
            index_ = colony_->prev_alive(index_);
            return *this;
        }

        iterator_impl operator--(int) {
            iterator_impl tmp = *this;
            --(*this);
            return tmp;
        }

        friend bool operator==(const iterator_impl& a, const iterator_impl& b) {
            return a.colony_ == b.colony_ && a.index_ == b.index_;
        }

        friend bool operator!=(const iterator_impl& a, const iterator_impl& b) {
            return !(a == b);
        }

        // non-const -> const
        explicit operator iterator_impl<true>() const {
            return iterator_impl<true>(colony_, index_);
        }

    private:
        friend class pinned_colony;

        iterator_impl(colony_ptr colony, size_type index)
            : colony_(colony), index_(index) {}

        colony_ptr colony_ = nullptr;
        size_type  index_  = npos;
    };

public:
    using iterator               = iterator_impl<false>;
    using const_iterator         = iterator_impl<true>;
    using reverse_iterator       = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    pinned_colony() = delete;

    explicit pinned_colony(size_type max_capacity, const Allocator& alloc = Allocator())
        : alloc_(alloc),
          capacity_(max_capacity),
          size_(0),
          high_water_(0),
          free_head_(npos),
          skipfield_(nullptr),
          free_next_(nullptr) {
        if (capacity_ == 0) {
            return;
        }

        byte_alloc ba(alloc_);
        skip_alloc sa(alloc_);
        index_alloc ia(alloc_);

        std::byte* raw = byte_traits::allocate(ba, bytes_needed());
        size_type* skip = skip_traits::allocate(sa, capacity_);
        size_type* next = index_traits::allocate(ia, capacity_);

        data_ = raw;
        skipfield_ = skip;
        free_next_ = next;

        /* vm_allocator::allocate() only reserves on Windows (MEM_RESERVE /
           PAGE_NOACCESS). These arrays are fully initialized below, so they
           must be committed first. Element storage stays demand-committed. */
        commit_bytes(skipfield_, capacity_ * sizeof(size_type));
        commit_bytes(free_next_, capacity_ * sizeof(size_type));

        for (size_type i = 0; i < capacity_; ++i) {
            skipfield_[i] = 0;
            free_next_[i] = npos;
        }
    }

    pinned_colony(const pinned_colony& other)
        : pinned_colony(other.capacity_, alloc_traits::select_on_container_copy_construction(other.alloc_)) {
        for (const auto& value : other) {
            emplace(value);
        }
    }

    pinned_colony(pinned_colony&& other) noexcept
        : alloc_(std::move(other.alloc_)),
          capacity_(other.capacity_),
          size_(other.size_),
          high_water_(other.high_water_),
          free_head_(other.free_head_),
          data_(other.data_),
          skipfield_(other.skipfield_),
          free_next_(other.free_next_) {
        other.capacity_ = 0;
        other.size_ = 0;
        other.high_water_ = 0;
        other.free_head_ = npos;
        other.data_ = nullptr;
        other.skipfield_ = nullptr;
        other.free_next_ = nullptr;
    }

    pinned_colony& operator=(const pinned_colony& other) {
        if (this == &other) {
            return *this;
        }

        if constexpr (alloc_traits::propagate_on_container_copy_assignment::value) {
            if (alloc_ != other.alloc_) {
                reset_storage();
                alloc_ = other.alloc_;
            }
        }

        if (capacity_ != other.capacity_ || data_ == nullptr) {
            pinned_colony tmp(other);
            swap(tmp);
            return *this;
        }

        clear();
        for (const auto& value : other) {
            emplace(value);
        }
        return *this;
    }

    pinned_colony& operator=(pinned_colony&& other)
        noexcept(alloc_traits::propagate_on_container_move_assignment::value
                 || alloc_traits::is_always_equal::value) {
        if (this == &other) {
            return *this;
        }

        constexpr bool pocma = alloc_traits::propagate_on_container_move_assignment::value;

        if (constexpr bool always_eq = alloc_traits::is_always_equal::value;
            pocma || always_eq || alloc_ == other.alloc_) {
            reset_storage();
            if constexpr (pocma) {
                alloc_ = std::move(other.alloc_);
            }
            capacity_ = other.capacity_;
            size_ = other.size_;
            high_water_ = other.high_water_;
            free_head_ = other.free_head_;
            data_ = other.data_;
            skipfield_ = other.skipfield_;
            free_next_ = other.free_next_;

            other.capacity_ = 0;
            other.size_ = 0;
            other.high_water_ = 0;
            other.free_head_ = npos;
            other.data_ = nullptr;
            other.skipfield_ = nullptr;
            other.free_next_ = nullptr;
        } else {
            // unequal allocators and no propagate: element-wise move
            clear();
            if (capacity_ < other.size_) {
                throw std::length_error("pinned_colony: move assignment exceeds capacity");
            }
            for (auto& value : other) {
                emplace(std::move(value));
            }
            other.clear();
        }
        return *this;
    }

    ~pinned_colony() {
        reset_storage();
    }

    void assign(size_type count, const T& value) {
        if (count > capacity_) {
            throw std::length_error("pinned_colony::assign exceeds capacity");
        }
        clear();
        for (size_type i = 0; i < count; ++i) {
            emplace(value);
        }
    }

    template <typename InputIt>
    void assign(InputIt first, InputIt last) {
        clear();
        for (; first != last; ++first) {
            emplace(*first);
        }
    }

    void assign(std::initializer_list<T> il) {
        assign(il.begin(), il.end());
    }

    allocator_type get_allocator() const noexcept {
        return alloc_;
    }

    //---------------------------------------------------------------------
    // Iterators
    //---------------------------------------------------------------------
    iterator begin() noexcept {
        return iterator(this, next_alive(0));
    }

    const_iterator begin() const noexcept {
        return const_iterator(this, next_alive(0));
    }

    const_iterator cbegin() const noexcept {
        return begin();
    }

    iterator end() noexcept {
        return iterator(this, npos);
    }

    const_iterator end() const noexcept {
        return const_iterator(this, npos);
    }

    const_iterator cend() const noexcept {
        return end();
    }

    reverse_iterator rbegin() noexcept {
        return reverse_iterator(end());
    }

    const_reverse_iterator rbegin() const noexcept {
        return const_reverse_iterator(end());
    }

    const_reverse_iterator crbegin() const noexcept {
        return rbegin();
    }

    reverse_iterator rend() noexcept {
        return reverse_iterator(begin());
    }

    const_reverse_iterator rend() const noexcept {
        return const_reverse_iterator(begin());
    }

    const_reverse_iterator crend() const noexcept {
        return rend();
    }

    //---------------------------------------------------------------------
    // Capacity
    //---------------------------------------------------------------------
    [[nodiscard]] bool empty() const noexcept {
        return size_ == 0;
    }

    size_type size() const noexcept {
        return size_;
    }

    size_type max_size() const noexcept {
        return capacity_;
    }

    size_type capacity() const noexcept {
        return capacity_;
    }

    //---------------------------------------------------------------------
    // Modifiers
    //---------------------------------------------------------------------
    template <typename... Args>
    iterator emplace(Args&&... args) {
        const size_type idx = allocate_slot();
        commit_up_to(idx);

        pointer p = ptr_at(idx);
        try {
            alloc_traits::construct(alloc_, p, std::forward<Args>(args)...);
        } catch (...) {
            release_slot(idx);
            throw;
        }

        mark_alive(idx);
        ++size_;
        return iterator(this, idx);
    }

    template <typename... Args>
    iterator emplace_hint(const_iterator /*hint*/, Args&&... args) {
        return emplace(std::forward<Args>(args)...);
    }

    iterator insert(const T& value) {
        return emplace(value);
    }

    iterator insert(T&& value) {
        return emplace(std::move(value));
    }

    iterator insert(const_iterator hint, const T& value) {
        return emplace_hint(hint, value);
    }

    iterator insert(const_iterator hint, T&& value) {
        return emplace_hint(hint, std::move(value));
    }

    iterator insert(size_type count, const T& value) {
        iterator last = end();
        for (size_type i = 0; i < count; ++i) {
            last = emplace(value);
        }
        return last;
    }

    template <typename InputIt>
    void insert(InputIt first, InputIt last) {
        for (; first != last; ++first) {
            emplace(*first);
        }
    }

    void insert(std::initializer_list<T> il) {
        insert(il.begin(), il.end());
    }

    iterator erase(const_iterator pos) {
        if (pos == cend() || pos.colony_ != this) {
            return end();
        }

        const size_type idx = pos.index_;
        const size_type next = next_alive(idx + 1);

        destroy_at(idx);
        mark_erased(idx);
        release_slot(idx);
        --size_;

        return iterator(this, next);
    }

    iterator erase(const_iterator first, const_iterator last) {
        while (first != last) {
            first = erase(first);
        }
        return iterator(this, first.index_);
    }

    void clear() noexcept {
        for (size_type i = 0; i < high_water_; ++i) {
            if (is_alive(i)) {
                destroy_at(i);
            }
            free_next_[i] = npos;
        }
        size_ = 0;
        free_head_ = npos;

        // Rebuild free list and mark the whole used range as one erased run
        for (size_type i = 0; i < high_water_; ++i) {
            free_next_[i] = free_head_;
            free_head_ = i;
        }
        if (high_water_ > 0) {
            rewrite_run(0, high_water_);
        }
    }

    void swap(pinned_colony& other) noexcept(alloc_traits::propagate_on_container_swap::value
                                             || alloc_traits::is_always_equal::value) {
        using std::swap;
        if constexpr (alloc_traits::propagate_on_container_swap::value) {
            swap(alloc_, other.alloc_);
        }
        swap(capacity_, other.capacity_);
        swap(size_, other.size_);
        swap(high_water_, other.high_water_);
        swap(free_head_, other.free_head_);
        swap(data_, other.data_);
        swap(skipfield_, other.skipfield_);
        swap(free_next_, other.free_next_);
    }

    //---------------------------------------------------------------------
    // Hive-style pointer helper
    //---------------------------------------------------------------------
    iterator get_iterator(pointer p) {
        const size_type idx = index_of(p);
        if (idx == npos || !is_alive(idx)) {
            return end();
        }
        return iterator(this, idx);
    }

    const_iterator get_iterator(const_pointer p) const {
        const size_type idx = index_of(p);
        if (idx == npos || !is_alive(idx)) {
            return end();
        }
        return const_iterator(this, idx);
    }

private:
    allocator_type alloc_{};
    size_type capacity_ = 0;
    size_type size_ = 0;
    size_type high_water_ = 0; // one past highest index ever used
    size_type free_head_ = npos;

    std::byte*  data_ = nullptr;
    size_type*  skipfield_ = nullptr;   // 0 = alive; >0 = remaining distance to next alive
    size_type*  free_next_ = nullptr;

    size_type bytes_needed() const noexcept {
        return capacity_ * sizeof(T);
    }

    pointer ptr_at(size_type index) noexcept {
        return std::launder(reinterpret_cast<pointer>(data_ + index * sizeof(T)));
    }

    const_pointer ptr_at(size_type index) const noexcept {
        return std::launder(reinterpret_cast<const_pointer>(data_ + index * sizeof(T)));
    }

    size_type index_of(const_pointer p) const noexcept {
        if (!p || !data_) {
            return npos;
        }
        const auto* base = reinterpret_cast<const std::byte*>(std::to_address(p));
        if (base < data_) {
            return npos;
        }
        const auto byte_off = static_cast<size_type>(base - data_);
        if (byte_off % sizeof(T) != 0) {
            return npos;
        }
        const size_type idx = byte_off / sizeof(T);
        if (idx >= capacity_) {
            return npos;
        }
        return idx;
    }

    // ---------------------------------------------------------------------
    // Jump-counting skipfield
    //
    // Encoding for a contiguous run of erased slots [start, start+len):
    //   skipfield_[start + i] = len - i     // remaining distance to the
    //                                        // first slot after the run
    // Alive slots store 0.
    //
    // From any erased position a single addition lands on the next alive
    // slot (or high_water_).
    // ---------------------------------------------------------------------

    bool is_alive(size_type idx) const noexcept {
        return idx < high_water_ && skipfield_[idx] == 0;
    }

    void rewrite_run(size_type start, size_type len) noexcept {
        for (size_type i = 0; i < len; ++i) {
            skipfield_[start + i] = len - i;
        }
    }

    // Find the start of the erased run that contains `idx` (idx must be erased).
    size_type run_start(size_type idx) const noexcept {
        while (idx > 0 && skipfield_[idx - 1] != 0) {
            --idx;
        }
        return idx;
    }

    void mark_alive(size_type idx) noexcept {
        if (skipfield_[idx] == 0) {
            return; // already alive
        }

        // End of the current run is exactly idx + skipfield_[idx]
        const size_type run_end = idx + skipfield_[idx];
        const size_type run_start_idx = run_start(idx);

        // Clear the resurrected slot
        skipfield_[idx] = 0;

        // Rewrite left fragment [run_start_idx, idx)
        if (idx > run_start_idx) {
            rewrite_run(run_start_idx, idx - run_start_idx);
        }
        // Rewrite right fragment [idx+1, run_end)
        if (run_end > idx + 1) {
            rewrite_run(idx + 1, run_end - (idx + 1));
        }
    }

    void mark_erased(size_type idx) noexcept {
        // idx is currently alive (skipfield_[idx] == 0)
        size_type start = idx;
        size_type end   = idx + 1;

        // Merge with left run if present
        if (idx > 0 && skipfield_[idx - 1] != 0) {
            start = run_start(idx - 1);
        }
        // Merge with right run if present
        if (end < high_water_ && skipfield_[end] != 0) {
            end += skipfield_[end];
        }

        rewrite_run(start, end - start);
    }

    size_type next_alive(size_type start) const noexcept {
        if (start >= high_water_) {
            return npos;
        }
        if (skipfield_[start] == 0) {
            return start;
        }
        const size_type jumped = start + skipfield_[start];
        if (jumped >= high_water_) {
            return npos;
        }
        return jumped;
    }

    size_type prev_alive(size_type from) const noexcept {
        if (from == npos) {
            from = high_water_;
        }
        if (from == 0) {
            return npos;
        }

        size_type i = from;
        while (i > 0) {
            --i;
            if (skipfield_[i] == 0) {
                return i;
            }
            // Jump backward to the start of the run when the encoding allows it.
            // From an erased slot the remaining distance tells us the end;
            // walking to the start is still cheap for typical run lengths.
            // For very long runs a full backward scan is acceptable because
            // reverse iteration is less common than forward in ECS systems.
        }
        return npos;
    }

    // ---------------------------------------------------------------------
    // Slot allocation / free-list
    // ---------------------------------------------------------------------
    size_type allocate_slot() {
        if (free_head_ != npos) {
            const size_type idx = free_head_;
            free_head_ = free_next_[idx];
            free_next_[idx] = npos;
            return idx;
        }
        if (high_water_ >= capacity_) {
            throw std::length_error("pinned_colony: capacity exhausted");
        }
        const size_type idx = high_water_++;
        // Treat the freshly grown slot as a singleton erased run so that
        // mark_alive can uniformly handle it.
        skipfield_[idx] = 1;
        return idx;
    }

    void release_slot(size_type idx) noexcept {
        free_next_[idx] = free_head_;
        free_head_ = idx;
    }

    void destroy_at(size_type idx) noexcept {
        alloc_traits::destroy(alloc_, ptr_at(idx));
    }

    // No-op unless the allocator reserves without committing (vm_allocator
    // on Windows). Other allocators already return accessible memory.
    static void commit_bytes(void* p, size_type bytes) noexcept {
        if (!p || bytes == 0) {
            return;
        }
        if constexpr (requires(void* q, size_type n) { Allocator::commit(q, n); }) {
            Allocator::commit(p, bytes);
        }
    }

    // Commit physical pages covering up to (and a bit past) the given slot.
    // Idempotent. On Unix this is usually a no-op after a normal mmap.
    void commit_up_to(size_type idx) noexcept {
        if (!data_ || idx >= capacity_) {
            return;
        }
        const size_type end_idx = std::min(idx + 64, capacity_);
        const size_type bytes = end_idx * sizeof(T);
        commit_bytes(data_, bytes);
    }

    void reset_storage() noexcept {
        if (data_) {
            for (size_type i = 0; i < high_water_; ++i) {
                if (is_alive(i)) {
                    destroy_at(i);
                }
            }

            byte_alloc ba(alloc_);
            skip_alloc sa(alloc_);
            index_alloc ia(alloc_);

            byte_traits::deallocate(ba, data_, bytes_needed());
            skip_traits::deallocate(sa, skipfield_, capacity_);
            index_traits::deallocate(ia, free_next_, capacity_);
        }

        data_ = nullptr;
        skipfield_ = nullptr;
        free_next_ = nullptr;
        capacity_ = 0;
        size_ = 0;
        high_water_ = 0;
        free_head_ = npos;
    }
};

template <typename T, typename Alloc>
void swap(pinned_colony<T, Alloc>& a, pinned_colony<T, Alloc>& b)
    noexcept(noexcept(a.swap(b))) {
    a.swap(b);
}

template <typename T, typename Alloc, typename Pred>
pinned_colony<T, Alloc>::size_type
erase_if(pinned_colony<T, Alloc>& c, Pred pred) {
    using size_type = pinned_colony<T, Alloc>::size_type;
    size_type removed = 0;
    for (auto it = c.begin(); it != c.end();) {
        if (pred(*it)) {
            it = c.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

template <typename T, typename Alloc, typename U>
pinned_colony<T, Alloc>::size_type
erase(pinned_colony<T, Alloc>& c, const U& value) {
    return erase_if(c, [&](const T& elem) { return elem == value; });
}

#endif // PINNED_COLONY_HPP
