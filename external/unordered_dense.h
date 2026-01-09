// A fast & densely stored hashmap and hashset based on robin-hood backward shift deletion.
// Version 4.8.1
// https://github.com/martinus/unordered_dense
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2022 Martin Leitner-Ankerl <martin.ankerl@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef ANKERL_UNORDERED_DENSE_H
#define ANKERL_UNORDERED_DENSE_H

#if defined(_MSVC_LANG)
#    define ANKERL_UNORDERED_DENSE_CPP_VERSION _MSVC_LANG
#else
#    define ANKERL_UNORDERED_DENSE_CPP_VERSION __cplusplus
#endif

#if defined(__GNUC__)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#    define ANKERL_UNORDERED_DENSE_PACK(decl) decl __attribute__((__packed__))
#elif defined(_MSC_VER)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#    define ANKERL_UNORDERED_DENSE_PACK(decl) __pragma(pack(push, 1)) decl __pragma(pack(pop))
#endif

#if ANKERL_UNORDERED_DENSE_CPP_VERSION < 201703L
#    error ankerl::unordered_dense requires C++17 or higher
#else

#include <cstdint>          // for uint64_t, uint32_t, std::uint8_t, UINT64_C
#include <cstring>          // for size_t, memcpy, memset
//#include <functional>       // for equal_to, hash
#include <iterator>         // for pair, distance
#include <limits>           // for numeric_limits
#include <memory>           // for allocator, allocator_traits, shared_ptr
#include <tuple>            // for forward_as_tuple
#include <type_traits>      // for enable_if_t, declval, conditional_t, ena...
#include <utility>          // for forward, exchange, pair, as_const, piece...
#include <vector>           // for vector


#    if __has_cpp_attribute(likely) && __has_cpp_attribute(unlikely) && ANKERL_UNORDERED_DENSE_CPP_VERSION >= 202002L
#        define ANKERL_UNORDERED_DENSE_LIKELY_ATTR [[likely]]     // NOLINT(cppcoreguidelines-macro-usage)
#        define ANKERL_UNORDERED_DENSE_UNLIKELY_ATTR [[unlikely]] // NOLINT(cppcoreguidelines-macro-usage)
#        define ANKERL_UNORDERED_DENSE_LIKELY(x) (x)              // NOLINT(cppcoreguidelines-macro-usage)
#        define ANKERL_UNORDERED_DENSE_UNLIKELY(x) (x)            // NOLINT(cppcoreguidelines-macro-usage)
#    else
#        define ANKERL_UNORDERED_DENSE_LIKELY_ATTR   // NOLINT(cppcoreguidelines-macro-usage)
#        define ANKERL_UNORDERED_DENSE_UNLIKELY_ATTR // NOLINT(cppcoreguidelines-macro-usage)

#        if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__clang__)
#            define ANKERL_UNORDERED_DENSE_LIKELY(x) __builtin_expect(x, 1)   // NOLINT(cppcoreguidelines-macro-usage)
#            define ANKERL_UNORDERED_DENSE_UNLIKELY(x) __builtin_expect(x, 0) // NOLINT(cppcoreguidelines-macro-usage)
#        else
#            define ANKERL_UNORDERED_DENSE_LIKELY(x) (x)   // NOLINT(cppcoreguidelines-macro-usage)
#            define ANKERL_UNORDERED_DENSE_UNLIKELY(x) (x) // NOLINT(cppcoreguidelines-macro-usage)
#        endif

#    endif

namespace ankerl {

namespace detail {

[[noreturn]] inline void on_error_bucket_overflow() {
    abort();
}

} // namespace detail

using std::hash;

// bucket_type //////////////////////////////////////////////////////////

namespace bucket_type {

struct standard {
    static constexpr std::uint32_t dist_inc = 1U << 8U;             // skip 1 byte fingerprint
    static constexpr std::uint32_t fingerprint_mask = dist_inc - 1; // mask for 1 byte of fingerprint

    std::uint32_t m_dist_and_fingerprint; // upper 3 byte: distance to original bucket. lower byte: fingerprint from hash
    std::uint32_t m_value_idx;            // index into the m_values vector.
};

ANKERL_UNORDERED_DENSE_PACK(struct big {
    static constexpr std::uint32_t dist_inc = 1U << 8U;             // skip 1 byte fingerprint
    static constexpr std::uint32_t fingerprint_mask = dist_inc - 1; // mask for 1 byte of fingerprint

    std::uint32_t m_dist_and_fingerprint; // upper 3 byte: distance to original bucket. lower byte: fingerprint from hash
    std::size_t m_value_idx;              // index into the m_values vector.
});

} // namespace bucket_type

namespace detail {

struct nonesuch {};
struct default_container_t {};

template <class Default, class AlwaysVoid, template <class...> class Op, class... Args>
struct detector {
    using value_t = std::false_type;
    using type = Default;
};

template <class Default, template <class...> class Op, class... Args>
struct detector<Default, std::void_t<Op<Args...>>, Op, Args...> {
    using value_t = std::true_type;
    using type = Op<Args...>;
};

template <template <class...> class Op, class... Args>
using is_detected = typename detail::detector<detail::nonesuch, void, Op, Args...>::value_t;

template <template <class...> class Op, class... Args>
constexpr bool is_detected_v = is_detected<Op, Args...>::value;

template <typename T>
using detect_is_transparent = typename T::is_transparent;

template <typename T>
using detect_iterator = typename T::iterator;

template <typename T>
using detect_reserve = decltype(std::declval<T&>().reserve(std::size_t{}));

// enable_if helpers

template <typename Mapped>
constexpr bool is_map_v = !std::is_void_v<Mapped>;

// clang-format off
template <typename Hash, typename KeyEqual>
constexpr bool is_transparent_v = is_detected_v<detect_is_transparent, Hash> && is_detected_v<detect_is_transparent, KeyEqual>;
// clang-format on

template <typename From, typename To1, typename To2>
constexpr bool is_neither_convertible_v = !std::is_convertible_v<From, To1> && !std::is_convertible_v<From, To2>;

template <typename T>
constexpr bool has_reserve = is_detected_v<detect_reserve, T>;

// base type for map has mapped_type
template <class T>
struct base_table_type_map {
    using mapped_type = T;
};

// base type for set doesn't have mapped_type
struct base_table_type_set {};

} // namespace detail

namespace detail {

// This is it, the table. Doubles as map and set, and uses `void` for T when its used as a set.
template <class Key,
          class T, // when void, treat it as a set.
          class Hash,
          class KeyEqual,
          class AllocatorOrContainer,
          class Bucket,
          class BucketContainer,
          bool IsSegmented>
class table : public std::conditional_t<is_map_v<T>, base_table_type_map<T>, base_table_type_set> {
    using underlying_value_type = std::conditional_t<is_map_v<T>, std::pair<Key, T>, Key>;
    using underlying_container_type = std::vector<underlying_value_type, AllocatorOrContainer>;

public:
    using value_container_type = std::
        conditional_t<is_detected_v<detect_iterator, AllocatorOrContainer>, AllocatorOrContainer, underlying_container_type>;

private:
    using bucket_alloc =
        typename std::allocator_traits<typename value_container_type::allocator_type>::template rebind_alloc<Bucket>;
    using default_bucket_container_type = std::vector<Bucket, bucket_alloc>;

    using bucket_container_type = std::conditional_t<std::is_same_v<BucketContainer, detail::default_container_t>,
                                                     default_bucket_container_type,
                                                     BucketContainer>;

    static constexpr std::uint8_t initial_shifts = 64 - 2; // 2^(64-m_shift) number of buckets
    static constexpr float default_max_load_factor = 0.8F;

public:
    using key_type = Key;
    using value_type = typename value_container_type::value_type;
    using size_type = typename value_container_type::size_type;
    using difference_type = typename value_container_type::difference_type;
    using hasher = Hash;
    using key_equal = KeyEqual;
    using allocator_type = typename value_container_type::allocator_type;
    using reference = typename value_container_type::reference;
    using const_reference = typename value_container_type::const_reference;
    using pointer = typename value_container_type::pointer;
    using const_pointer = typename value_container_type::const_pointer;
    using const_iterator = typename value_container_type::const_iterator;
    using iterator = std::conditional_t<is_map_v<T>, typename value_container_type::iterator, const_iterator>;
    using bucket_type = Bucket;

private:
    using value_idx_type = decltype(Bucket::m_value_idx);
    using dist_and_fingerprint_type = decltype(Bucket::m_dist_and_fingerprint);

    static_assert(std::is_trivially_destructible_v<Bucket>, "assert there's no need to call destructor / std::destroy");
    static_assert(std::is_trivially_copyable_v<Bucket>, "assert we can just memset / memcpy");

    value_container_type m_values{}; // Contains all the key-value pairs in one densely stored container. No holes.
    bucket_container_type m_buckets{};
    std::size_t m_max_bucket_capacity = 0;
    float m_max_load_factor = default_max_load_factor;
    Hash m_hash{};
    KeyEqual m_equal{};
    std::uint8_t m_shifts = initial_shifts;

    [[nodiscard]] auto next(value_idx_type bucket_idx) const -> value_idx_type {
        if (ANKERL_UNORDERED_DENSE_UNLIKELY(bucket_idx + 1U == bucket_count()))
            ANKERL_UNORDERED_DENSE_UNLIKELY_ATTR {
                return 0;
            }

        return static_cast<value_idx_type>(bucket_idx + 1U);
    }

    // Helper to access bucket through pointer types
    [[nodiscard]] static constexpr auto at(bucket_container_type& bucket, std::size_t offset) -> Bucket& {
        return bucket[offset];
    }

    [[nodiscard]] static constexpr auto at(const bucket_container_type& bucket, std::size_t offset) -> const Bucket& {
        return bucket[offset];
    }

    // use the dist_inc and dist_dec functions so that std::uint16_t types work without warning
    [[nodiscard]] static constexpr auto dist_inc(dist_and_fingerprint_type x) -> dist_and_fingerprint_type {
        return static_cast<dist_and_fingerprint_type>(x + Bucket::dist_inc);
    }

    [[nodiscard]] static constexpr auto dist_dec(dist_and_fingerprint_type x) -> dist_and_fingerprint_type {
        return static_cast<dist_and_fingerprint_type>(x - Bucket::dist_inc);
    }

    // The goal of mixed_hash is to always produce a high quality 64bit hash.
    template <typename K>
    [[nodiscard]] constexpr auto mixed_hash(K const& key) const -> std::uint64_t {
        return m_hash(key);
    }

    [[nodiscard]] constexpr auto dist_and_fingerprint_from_hash(std::uint64_t hash) const -> dist_and_fingerprint_type {
        return Bucket::dist_inc | (static_cast<dist_and_fingerprint_type>(hash) & Bucket::fingerprint_mask);
    }

    [[nodiscard]] constexpr auto bucket_idx_from_hash(std::uint64_t hash) const -> value_idx_type {
        return static_cast<value_idx_type>(hash >> m_shifts);
    }

    [[nodiscard]] static constexpr auto get_key(value_type const& vt) -> key_type const& {
        if constexpr (is_map_v<T>) {
            return vt.first;
        } else {
            return vt;
        }
    }

    template <typename K>
    [[nodiscard]] auto next_while_less(K const& key) const -> Bucket {
        auto hash = mixed_hash(key);
        auto dist_and_fingerprint = dist_and_fingerprint_from_hash(hash);
        auto bucket_idx = bucket_idx_from_hash(hash);

        while (dist_and_fingerprint < at(m_buckets, bucket_idx).m_dist_and_fingerprint) {
            dist_and_fingerprint = dist_inc(dist_and_fingerprint);
            bucket_idx = next(bucket_idx);
        }
        return {dist_and_fingerprint, bucket_idx};
    }

    void place_and_shift_up(Bucket bucket, value_idx_type place) {
        while (0 != at(m_buckets, place).m_dist_and_fingerprint) {
            bucket = std::exchange(at(m_buckets, place), bucket);
            bucket.m_dist_and_fingerprint = dist_inc(bucket.m_dist_and_fingerprint);
            place = next(place);
        }
        at(m_buckets, place) = bucket;
    }

    void erase_and_shift_down(value_idx_type bucket_idx) {
        // shift down until either empty or an element with correct spot is found
        auto next_bucket_idx = next(bucket_idx);
        while (at(m_buckets, next_bucket_idx).m_dist_and_fingerprint >= Bucket::dist_inc * 2) {
            auto& next_bucket = at(m_buckets, next_bucket_idx);
            at(m_buckets, bucket_idx) = {dist_dec(next_bucket.m_dist_and_fingerprint), next_bucket.m_value_idx};
            bucket_idx = std::exchange(next_bucket_idx, next(next_bucket_idx));
        }
        at(m_buckets, bucket_idx) = {};
    }

    [[nodiscard]] static constexpr auto calc_num_buckets(std::uint8_t shifts) -> std::size_t {
        return (std::min)(max_bucket_count(), std::size_t{1} << (64U - shifts));
    }

    [[nodiscard]] constexpr auto calc_shifts_for_size(std::size_t s) const -> std::uint8_t {
        auto shifts = initial_shifts;
        while (shifts > 0 && static_cast<std::size_t>(static_cast<float>(calc_num_buckets(shifts)) * max_load_factor()) < s) {
            --shifts;
        }
        return shifts;
    }

    // assumes m_values has data, m_buckets=m_buckets_end=nullptr, m_shifts is INITIAL_SHIFTS
    void copy_buckets(table const& other) {
        // assumes m_values has already the correct data copied over.
        if (empty()) {
            // when empty, at least allocate an initial buckets and clear them.
            allocate_buckets_from_shift();
            clear_buckets();
        } else {
            m_shifts = other.m_shifts;
            allocate_buckets_from_shift();
            if constexpr (IsSegmented || !std::is_same_v<BucketContainer, default_container_t>) {
                for (auto i = 0UL; i < bucket_count(); ++i) {
                    at(m_buckets, i) = at(other.m_buckets, i);
                }
            } else {
                std::memcpy(m_buckets.data(), other.m_buckets.data(), sizeof(Bucket) * bucket_count());
            }
        }
    }

    /**
     * True when no element can be added any more without increasing the size
     */
    [[nodiscard]] auto is_full() const -> bool {
        return size() > m_max_bucket_capacity;
    }

    void deallocate_buckets() {
        m_buckets.clear();
        m_buckets.shrink_to_fit();
        m_max_bucket_capacity = 0;
    }

    void allocate_buckets_from_shift() {
        auto num_buckets = calc_num_buckets(m_shifts);
        if constexpr (IsSegmented || !std::is_same_v<BucketContainer, default_container_t>) {
            if constexpr (has_reserve<bucket_container_type>) {
                m_buckets.reserve(num_buckets);
            }
            for (std::size_t i = m_buckets.size(); i < num_buckets; ++i) {
                m_buckets.emplace_back();
            }
        } else {
            m_buckets.resize(num_buckets);
        }
        if (num_buckets == max_bucket_count()) {
            // reached the maximum, make sure we can use each bucket
            m_max_bucket_capacity = max_bucket_count();
        } else {
            m_max_bucket_capacity = static_cast<value_idx_type>(static_cast<float>(num_buckets) * max_load_factor());
        }
    }

    void clear_buckets() {
        if constexpr (IsSegmented || !std::is_same_v<BucketContainer, default_container_t>) {
            for (auto&& e : m_buckets) {
                std::memset(&e, 0, sizeof(e));
            }
        } else {
            std::memset(m_buckets.data(), 0, sizeof(Bucket) * bucket_count());
        }
    }

    void clear_and_fill_buckets_from_values() {
        clear_buckets();
        for (value_idx_type value_idx = 0, end_idx = static_cast<value_idx_type>(m_values.size()); value_idx < end_idx;
             ++value_idx) {
            auto const& key = get_key(m_values[value_idx]);
            auto [dist_and_fingerprint, bucket] = next_while_less(key);

            // we know for certain that key has not yet been inserted, so no need to check it.
            place_and_shift_up({dist_and_fingerprint, value_idx}, bucket);
        }
    }

    void increase_size() {
        if (m_max_bucket_capacity == max_bucket_count()) {
            // remove the value again, we can't add it!
            m_values.pop_back();
            on_error_bucket_overflow();
        }
        --m_shifts;
        if constexpr (!IsSegmented || std::is_same_v<BucketContainer, default_container_t>) {
            deallocate_buckets();
        }
        allocate_buckets_from_shift();
        clear_and_fill_buckets_from_values();
    }

    template <typename Op>
    void do_erase(value_idx_type bucket_idx, Op handle_erased_value) {
        auto const value_idx_to_remove = at(m_buckets, bucket_idx).m_value_idx;
        erase_and_shift_down(bucket_idx);
        handle_erased_value(std::move(m_values[value_idx_to_remove]));

        // update m_values
        if (value_idx_to_remove != m_values.size() - 1) {
            // no luck, we'll have to replace the value with the last one and update the index accordingly
            auto& val = m_values[value_idx_to_remove];
            val = std::move(m_values.back());

            // update the values_idx of the moved entry. No need to play the info game, just look until we find the values_idx
            bucket_idx = bucket_idx_from_hash(mixed_hash(get_key(val)));
            auto const values_idx_back = static_cast<value_idx_type>(m_values.size() - 1);
            while (values_idx_back != at(m_buckets, bucket_idx).m_value_idx) {
                bucket_idx = next(bucket_idx);
            }
            at(m_buckets, bucket_idx).m_value_idx = value_idx_to_remove;
        }
        m_values.pop_back();
    }

    template <typename K, typename Op>
    auto do_erase_key(K&& key, Op handle_erased_value) -> std::size_t { // NOLINT(cppcoreguidelines-missing-std-forward)
        if (empty()) {
            return 0;
        }

        auto [dist_and_fingerprint, bucket_idx] = next_while_less(key);

        while (dist_and_fingerprint == at(m_buckets, bucket_idx).m_dist_and_fingerprint &&
               !m_equal(key, get_key(m_values[at(m_buckets, bucket_idx).m_value_idx]))) {
            dist_and_fingerprint = dist_inc(dist_and_fingerprint);
            bucket_idx = next(bucket_idx);
        }

        if (dist_and_fingerprint != at(m_buckets, bucket_idx).m_dist_and_fingerprint) {
            return 0;
        }
        do_erase(bucket_idx, handle_erased_value);
        return 1;
    }

    template <class K, class M>
    auto do_insert_or_assign(K&& key, M&& mapped) -> std::pair<iterator, bool> {
        auto it_isinserted = try_emplace(std::forward<K>(key), std::forward<M>(mapped));
        if (!it_isinserted.second) {
            it_isinserted.first->second = std::forward<M>(mapped);
        }
        return it_isinserted;
    }

    template <typename... Args>
    auto do_place_element(dist_and_fingerprint_type dist_and_fingerprint, value_idx_type bucket_idx, Args&&... args)
        -> std::pair<iterator, bool> {

        // emplace the new value. If that throws an exception, no harm done; index is still in a valid state
        m_values.emplace_back(std::forward<Args>(args)...);

        auto value_idx = static_cast<value_idx_type>(m_values.size() - 1);
        if (ANKERL_UNORDERED_DENSE_UNLIKELY(is_full()))
            ANKERL_UNORDERED_DENSE_UNLIKELY_ATTR {
                increase_size();
            }
        else {
            place_and_shift_up({dist_and_fingerprint, value_idx}, bucket_idx);
        }

        // place element and shift up until we find an empty spot
        return {begin() + static_cast<difference_type>(value_idx), true};
    }

    template <typename K, typename... Args>
    auto do_try_emplace(K&& key, Args&&... args) -> std::pair<iterator, bool> {
        auto hash = mixed_hash(key);
        auto dist_and_fingerprint = dist_and_fingerprint_from_hash(hash);
        auto bucket_idx = bucket_idx_from_hash(hash);

        while (true) {
            auto* bucket = &at(m_buckets, bucket_idx);
            if (dist_and_fingerprint == bucket->m_dist_and_fingerprint) {
                if (m_equal(key, get_key(m_values[bucket->m_value_idx]))) {
                    return {begin() + static_cast<difference_type>(bucket->m_value_idx), false};
                }
            } else if (dist_and_fingerprint > bucket->m_dist_and_fingerprint) {
                return do_place_element(dist_and_fingerprint,
                                        bucket_idx,
                                        std::piecewise_construct,
                                        std::forward_as_tuple(std::forward<K>(key)),
                                        std::forward_as_tuple(std::forward<Args>(args)...));
            }
            dist_and_fingerprint = dist_inc(dist_and_fingerprint);
            bucket_idx = next(bucket_idx);
        }
    }

    template <typename K>
    auto do_find(K const& key) -> iterator {
        if (ANKERL_UNORDERED_DENSE_UNLIKELY(empty()))
            ANKERL_UNORDERED_DENSE_UNLIKELY_ATTR {
                return end();
            }

        auto mh = mixed_hash(key);
        auto dist_and_fingerprint = dist_and_fingerprint_from_hash(mh);
        auto bucket_idx = bucket_idx_from_hash(mh);
        auto* bucket = &at(m_buckets, bucket_idx);

        // unrolled loop. *Always* check a few directly, then enter the loop. This is faster.
        if (dist_and_fingerprint == bucket->m_dist_and_fingerprint && m_equal(key, get_key(m_values[bucket->m_value_idx]))) {
            return begin() + static_cast<difference_type>(bucket->m_value_idx);
        }
        dist_and_fingerprint = dist_inc(dist_and_fingerprint);
        bucket_idx = next(bucket_idx);
        bucket = &at(m_buckets, bucket_idx);

        if (dist_and_fingerprint == bucket->m_dist_and_fingerprint && m_equal(key, get_key(m_values[bucket->m_value_idx]))) {
            return begin() + static_cast<difference_type>(bucket->m_value_idx);
        }
        dist_and_fingerprint = dist_inc(dist_and_fingerprint);
        bucket_idx = next(bucket_idx);
        bucket = &at(m_buckets, bucket_idx);

        while (true) {
            if (dist_and_fingerprint == bucket->m_dist_and_fingerprint) {
                if (m_equal(key, get_key(m_values[bucket->m_value_idx]))) {
                    return begin() + static_cast<difference_type>(bucket->m_value_idx);
                }
            } else if (dist_and_fingerprint > bucket->m_dist_and_fingerprint) {
                return end();
            }
            dist_and_fingerprint = dist_inc(dist_and_fingerprint);
            bucket_idx = next(bucket_idx);
            bucket = &at(m_buckets, bucket_idx);
        }
    }

    template <typename K>
    auto do_find(K const& key) const -> const_iterator {
        return const_cast<table*>(this)->do_find(key); // NOLINT(cppcoreguidelines-pro-type-const-cast)
    }

public:
    explicit table(std::size_t bucket_count,
                   Hash const& hash = Hash(),
                   KeyEqual const& equal = KeyEqual(),
                   allocator_type const& alloc_or_container = allocator_type())
        : m_values(alloc_or_container)
        , m_buckets(alloc_or_container)
        , m_hash(hash)
        , m_equal(equal) {
        if (0 != bucket_count) {
            reserve(bucket_count);
        } else {
            allocate_buckets_from_shift();
            clear_buckets();
        }
    }

    table() : table(0) {}
    table(std::size_t bucket_count, allocator_type const& alloc) : table(bucket_count, Hash(), KeyEqual(), alloc) {}
    table(std::size_t bucket_count, Hash const& hash, allocator_type const& alloc) : table(bucket_count, hash, KeyEqual(), alloc) {}

    explicit table(allocator_type const& alloc) : table(0, Hash(), KeyEqual(), alloc) {}

    template <class InputIt>
    table(InputIt first,
          InputIt last,
          size_type bucket_count = 0,
          Hash const& hash = Hash(),
          KeyEqual const& equal = KeyEqual(),
          allocator_type const& alloc = allocator_type())
        : table(bucket_count, hash, equal, alloc) {
        insert(first, last);
    }

    template <class InputIt>
    table(InputIt first, InputIt last, size_type bucket_count, allocator_type const& alloc)
        : table(first, last, bucket_count, Hash(), KeyEqual(), alloc) {}

    template <class InputIt>
    table(InputIt first, InputIt last, size_type bucket_count, Hash const& hash, allocator_type const& alloc)
        : table(first, last, bucket_count, hash, KeyEqual(), alloc) {}

    table(table const& other)
        : table(other, other.m_values.get_allocator()) {}

    table(table const& other, allocator_type const& alloc)
        : m_values(other.m_values, alloc)
        , m_max_load_factor(other.m_max_load_factor)
        , m_hash(other.m_hash)
        , m_equal(other.m_equal) {
        copy_buckets(other);
    }

    table(table&& other) noexcept
        : table(std::move(other), other.m_values.get_allocator()) {}

    table(table&& other, allocator_type const& alloc) noexcept
        : m_values(alloc) {
        *this = std::move(other);
    }

    ~table() = default;

    auto operator=(table const& other) -> table& {
        if (&other != this) {
            deallocate_buckets(); // deallocate before m_values is set (might have another allocator)
            m_values = other.m_values;
            m_max_load_factor = other.m_max_load_factor;
            m_hash = other.m_hash;
            m_equal = other.m_equal;
            m_shifts = initial_shifts;
            copy_buckets(other);
        }
        return *this;
    }

    auto get_allocator() const noexcept -> allocator_type {
        return m_values.get_allocator();
    }

    // iterators //////////////////////////////////////////////////////////////

    auto begin() noexcept -> iterator {
        return m_values.begin();
    }

    auto begin() const noexcept -> const_iterator {
        return m_values.begin();
    }

    auto cbegin() const noexcept -> const_iterator {
        return m_values.cbegin();
    }

    auto end() noexcept -> iterator {
        return m_values.end();
    }

    auto cend() const noexcept -> const_iterator {
        return m_values.cend();
    }

    auto end() const noexcept -> const_iterator {
        return m_values.end();
    }

    // capacity ///////////////////////////////////////////////////////////////

    [[nodiscard]] auto empty() const noexcept -> bool {
        return m_values.empty();
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return m_values.size();
    }

    [[nodiscard]] static constexpr auto max_size() noexcept -> std::size_t {
        if constexpr ((std::numeric_limits<value_idx_type>::max)() == (std::numeric_limits<std::size_t>::max)()) {
            return std::size_t{1} << (sizeof(value_idx_type) * 8 - 1);
        } else {
            return std::size_t{1} << (sizeof(value_idx_type) * 8);
        }
    }

    // modifiers //////////////////////////////////////////////////////////////

    void clear() {
        m_values.clear();
        clear_buckets();
    }

    auto insert(value_type const& value) -> std::pair<iterator, bool> {
        return emplace(value);
    }

    auto insert(value_type&& value) -> std::pair<iterator, bool> {
        return emplace(std::move(value));
    }

    template <class P, std::enable_if_t<std::is_constructible_v<value_type, P&&>, bool> = true>
    auto insert(P&& value) -> std::pair<iterator, bool> {
        return emplace(std::forward<P>(value));
    }

    template <class InputIt>
    void insert(InputIt first, InputIt last) {
        while (first != last) {
            insert(*first);
            ++first;
        }
    }

    // nonstandard API: *this is emptied.
    // Also see "A Standard flat_map" https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0429r9.pdf
    auto extract() && -> value_container_type {
        return std::move(m_values);
    }

    template <class M, typename Q = T, std::enable_if_t<is_map_v<Q>, bool> = true>
    auto insert_or_assign(Key const& key, M&& mapped) -> std::pair<iterator, bool> {
        return do_insert_or_assign(key, std::forward<M>(mapped));
    }

    template <class M, typename Q = T, std::enable_if_t<is_map_v<Q>, bool> = true>
    auto insert_or_assign(Key&& key, M&& mapped) -> std::pair<iterator, bool> {
        return do_insert_or_assign(std::move(key), std::forward<M>(mapped));
    }

    template <typename K,
              typename M,
              typename Q = T,
              typename H = Hash,
              typename KE = KeyEqual,
              std::enable_if_t<is_map_v<Q> && is_transparent_v<H, KE>, bool> = true>
    auto insert_or_assign(K&& key, M&& mapped) -> std::pair<iterator, bool> {
        return do_insert_or_assign(std::forward<K>(key), std::forward<M>(mapped));
    }

    template <class M, typename Q = T, std::enable_if_t<is_map_v<Q>, bool> = true>
    auto insert_or_assign(const_iterator /*hint*/, Key const& key, M&& mapped) -> iterator {
        return do_insert_or_assign(key, std::forward<M>(mapped)).first;
    }

    template <class M, typename Q = T, std::enable_if_t<is_map_v<Q>, bool> = true>
    auto insert_or_assign(const_iterator /*hint*/, Key&& key, M&& mapped) -> iterator {
        return do_insert_or_assign(std::move(key), std::forward<M>(mapped)).first;
    }

    template <typename K,
              typename M,
              typename Q = T,
              typename H = Hash,
              typename KE = KeyEqual,
              std::enable_if_t<is_map_v<Q> && is_transparent_v<H, KE>, bool> = true>
    auto insert_or_assign(const_iterator /*hint*/, K&& key, M&& mapped) -> iterator {
        return do_insert_or_assign(std::forward<K>(key), std::forward<M>(mapped)).first;
    }

    // Single arguments for unordered_set can be used without having to construct the value_type
    template <class K,
              typename Q = T,
              typename H = Hash,
              typename KE = KeyEqual,
              std::enable_if_t<!is_map_v<Q> && is_transparent_v<H, KE>, bool> = true>
    auto emplace(K&& key) -> std::pair<iterator, bool> {
        auto hash = mixed_hash(key);
        auto dist_and_fingerprint = dist_and_fingerprint_from_hash(hash);
        auto bucket_idx = bucket_idx_from_hash(hash);

        while (dist_and_fingerprint <= at(m_buckets, bucket_idx).m_dist_and_fingerprint) {
            if (dist_and_fingerprint == at(m_buckets, bucket_idx).m_dist_and_fingerprint &&
                m_equal(key, m_values[at(m_buckets, bucket_idx).m_value_idx])) {
                // found it, return without ever actually creating anything
                return {begin() + static_cast<difference_type>(at(m_buckets, bucket_idx).m_value_idx), false};
            }
            dist_and_fingerprint = dist_inc(dist_and_fingerprint);
            bucket_idx = next(bucket_idx);
        }

        // value is new, insert element first, so when exception happens we are in a valid state
        return do_place_element(dist_and_fingerprint, bucket_idx, std::forward<K>(key));
    }

    template <class... Args>
    auto emplace(Args&&... args) -> std::pair<iterator, bool> {
        // we have to instantiate the value_type to be able to access the key.
        // 1. emplace_back the object so it is constructed. 2. If the key is already there, pop it later in the loop.
        auto& key = get_key(m_values.emplace_back(std::forward<Args>(args)...));
        auto hash = mixed_hash(key);
        auto dist_and_fingerprint = dist_and_fingerprint_from_hash(hash);
        auto bucket_idx = bucket_idx_from_hash(hash);

        while (dist_and_fingerprint <= at(m_buckets, bucket_idx).m_dist_and_fingerprint) {
            if (dist_and_fingerprint == at(m_buckets, bucket_idx).m_dist_and_fingerprint &&
                m_equal(key, get_key(m_values[at(m_buckets, bucket_idx).m_value_idx]))) {
                m_values.pop_back(); // value was already there, so get rid of it
                return {begin() + static_cast<difference_type>(at(m_buckets, bucket_idx).m_value_idx), false};
            }
            dist_and_fingerprint = dist_inc(dist_and_fingerprint);
            bucket_idx = next(bucket_idx);
        }

        // value is new, place the bucket and shift up until we find an empty spot
        auto value_idx = static_cast<value_idx_type>(m_values.size() - 1);
        if (ANKERL_UNORDERED_DENSE_UNLIKELY(is_full()))
            ANKERL_UNORDERED_DENSE_UNLIKELY_ATTR {
                // increase_size just rehashes all the data we have in m_values
                increase_size();
            }
        else {
            // place element and shift up until we find an empty spot
            place_and_shift_up({dist_and_fingerprint, value_idx}, bucket_idx);
        }
        return {begin() + static_cast<difference_type>(value_idx), true};
    }

    template <class... Args>
    auto emplace_hint(const_iterator /*hint*/, Args&&... args) -> iterator {
        return emplace(std::forward<Args>(args)...).first;
    }

    template <class... Args, typename Q = T, std::enable_if_t<is_map_v<Q>, bool> = true>
    auto try_emplace(Key const& key, Args&&... args) -> std::pair<iterator, bool> {
        return do_try_emplace(key, std::forward<Args>(args)...);
    }

    template <class... Args, typename Q = T, std::enable_if_t<is_map_v<Q>, bool> = true>
    auto try_emplace(Key&& key, Args&&... args) -> std::pair<iterator, bool> {
        return do_try_emplace(std::move(key), std::forward<Args>(args)...);
    }

    template <class... Args, typename Q = T, std::enable_if_t<is_map_v<Q>, bool> = true>
    auto try_emplace(const_iterator /*hint*/, Key const& key, Args&&... args) -> iterator {
        return do_try_emplace(key, std::forward<Args>(args)...).first;
    }

    template <class... Args, typename Q = T, std::enable_if_t<is_map_v<Q>, bool> = true>
    auto try_emplace(const_iterator /*hint*/, Key&& key, Args&&... args) -> iterator {
        return do_try_emplace(std::move(key), std::forward<Args>(args)...).first;
    }

    template <
        typename K,
        typename... Args,
        typename Q = T,
        typename H = Hash,
        typename KE = KeyEqual,
        std::enable_if_t<is_map_v<Q> && is_transparent_v<H, KE> && is_neither_convertible_v<K&&, iterator, const_iterator>,
                         bool> = true>
    auto try_emplace(K&& key, Args&&... args) -> std::pair<iterator, bool> {
        return do_try_emplace(std::forward<K>(key), std::forward<Args>(args)...);
    }

    template <
        typename K,
        typename... Args,
        typename Q = T,
        typename H = Hash,
        typename KE = KeyEqual,
        std::enable_if_t<is_map_v<Q> && is_transparent_v<H, KE> && is_neither_convertible_v<K&&, iterator, const_iterator>,
                         bool> = true>
    auto try_emplace(const_iterator /*hint*/, K&& key, Args&&... args) -> iterator {
        return do_try_emplace(std::forward<K>(key), std::forward<Args>(args)...).first;
    }

    // Replaces the key at the given iterator with new_key. This does not change any other data in the underlying table, so
    // all iterators and references remain valid. However, this operation can fail if new_key already exists in the table.
    // In that case, returns {iterator to the already existing new_key, false} and no change is made.
    //
    // In the case of a set, this effectively removes the old key and inserts the new key at the same spot, which is more
    // efficient than removing the old key and inserting the new key because it avoids repositioning the last element.
    template <typename K>
    auto replace_key(iterator it, K&& new_key) -> std::pair<iterator, bool> {
        auto const new_key_hash = mixed_hash(new_key);

        // first, check if new_key already exists and return if so
        auto dist_and_fingerprint = dist_and_fingerprint_from_hash(new_key_hash);
        auto bucket_idx = bucket_idx_from_hash(new_key_hash);
        while (dist_and_fingerprint <= at(m_buckets, bucket_idx).m_dist_and_fingerprint) {
            auto const& bucket = at(m_buckets, bucket_idx);
            if (dist_and_fingerprint == bucket.m_dist_and_fingerprint &&
                m_equal(new_key, get_key(m_values[bucket.m_value_idx]))) {
                return {begin() + static_cast<difference_type>(bucket.m_value_idx), false};
            }
            dist_and_fingerprint = dist_inc(dist_and_fingerprint);
            bucket_idx = next(bucket_idx);
        }

        // const_cast is needed because iterator for the set is always const, so adding another get_key overload is not
        // feasible.
        auto& target_key = const_cast<key_type&>(get_key(*it));
        auto const old_key_bucket_idx = bucket_idx_from_hash(mixed_hash(target_key));

        // Replace the key before doing any bucket changes. If it throws, no harm done, we are still in a valid state as we
        // have not modified any buckets yet.
        target_key = std::forward<K>(new_key);

        auto const value_idx = static_cast<value_idx_type>(it - begin());

        // Find the bucket containing our value_idx. It's guaranteed we find it, so no other stopping condition needed.
        bucket_idx = old_key_bucket_idx;
        while (value_idx != at(m_buckets, bucket_idx).m_value_idx) {
            bucket_idx = next(bucket_idx);
        }
        erase_and_shift_down(bucket_idx);

        // place the new bucket
        dist_and_fingerprint = dist_and_fingerprint_from_hash(new_key_hash);
        bucket_idx = bucket_idx_from_hash(new_key_hash);
        while (dist_and_fingerprint < at(m_buckets, bucket_idx).m_dist_and_fingerprint) {
            dist_and_fingerprint = dist_inc(dist_and_fingerprint);
            bucket_idx = next(bucket_idx);
        }
        place_and_shift_up({dist_and_fingerprint, value_idx}, bucket_idx);

        return {it, true};
    }

    auto erase(iterator it) -> iterator {
        auto hash = mixed_hash(get_key(*it));
        auto bucket_idx = bucket_idx_from_hash(hash);

        auto const value_idx_to_remove = static_cast<value_idx_type>(it - cbegin());
        while (at(m_buckets, bucket_idx).m_value_idx != value_idx_to_remove) {
            bucket_idx = next(bucket_idx);
        }

        do_erase(bucket_idx, [](value_type const& /*unused*/) -> void {
        });
        return begin() + static_cast<difference_type>(value_idx_to_remove);
    }

    template <typename Q = T, std::enable_if_t<is_map_v<Q>, bool> = true>
    auto erase(const_iterator it) -> iterator {
        return erase(begin() + (it - cbegin()));
    }

    template <typename Q = T, std::enable_if_t<is_map_v<Q>, bool> = true>
    auto extract(const_iterator it) -> value_type {
        return extract(begin() + (it - cbegin()));
    }

    auto erase(const_iterator first, const_iterator last) -> iterator {
        auto const idx_first = first - cbegin();
        auto const idx_last = last - cbegin();
        auto const first_to_last = std::distance(first, last);
        auto const last_to_end = std::distance(last, cend());

        // remove elements from left to right which moves elements from the end back
        auto const mid = idx_first + (std::min)(first_to_last, last_to_end);
        auto idx = idx_first;
        while (idx != mid) {
            erase(begin() + idx);
            ++idx;
        }

        // all elements from the right are moved, now remove the last element until all done
        idx = idx_last;
        while (idx != mid) {
            --idx;
            erase(begin() + idx);
        }

        return begin() + idx_first;
    }

    auto erase(Key const& key) -> std::size_t {
        return do_erase_key(key, [](value_type const& /*unused*/) -> void {
        });
    }

    template <class K, class H = Hash, class KE = KeyEqual, std::enable_if_t<is_transparent_v<H, KE>, bool> = true>
    auto erase(K&& key) -> std::size_t {
        return do_erase_key(std::forward<K>(key), [](value_type const& /*unused*/) -> void {
        });
    }

    void swap(table& other) noexcept(noexcept(std::is_nothrow_swappable_v<value_container_type> &&
                                              std::is_nothrow_swappable_v<Hash> && std::is_nothrow_swappable_v<KeyEqual>)) {
        using std::swap;
        swap(other, *this);
    }

    // lookup /////////////////////////////////////////////////////////////////

    template <typename Q = T, std::enable_if_t<is_map_v<Q>, bool> = true>
    auto operator[](Key const& key) -> Q& {
        return try_emplace(key).first->second;
    }

    template <typename Q = T, std::enable_if_t<is_map_v<Q>, bool> = true>
    auto operator[](Key&& key) -> Q& {
        return try_emplace(std::move(key)).first->second;
    }

    template <typename K,
              typename Q = T,
              typename H = Hash,
              typename KE = KeyEqual,
              std::enable_if_t<is_map_v<Q> && is_transparent_v<H, KE>, bool> = true>
    auto operator[](K&& key) -> Q& {
        return try_emplace(std::forward<K>(key)).first->second;
    }

    auto count(Key const& key) const -> std::size_t {
        return find(key) == end() ? 0 : 1;
    }

    template <class K, class H = Hash, class KE = KeyEqual, std::enable_if_t<is_transparent_v<H, KE>, bool> = true>
    auto count(K const& key) const -> std::size_t {
        return find(key) == end() ? 0 : 1;
    }

    auto find(Key const& key) -> iterator {
        return do_find(key);
    }

    auto find(Key const& key) const -> const_iterator {
        return do_find(key);
    }

    template <class K, class H = Hash, class KE = KeyEqual, std::enable_if_t<is_transparent_v<H, KE>, bool> = true>
    auto find(K const& key) -> iterator {
        return do_find(key);
    }

    template <class K, class H = Hash, class KE = KeyEqual, std::enable_if_t<is_transparent_v<H, KE>, bool> = true>
    auto find(K const& key) const -> const_iterator {
        return do_find(key);
    }

    auto contains(Key const& key) const -> bool {
        return find(key) != end();
    }

    template <class K, class H = Hash, class KE = KeyEqual, std::enable_if_t<is_transparent_v<H, KE>, bool> = true>
    auto contains(K const& key) const -> bool {
        return find(key) != end();
    }

    auto equal_range(Key const& key) -> std::pair<iterator, iterator> {
        auto it = do_find(key);
        return {it, it == end() ? end() : it + 1};
    }

    auto equal_range(const Key& key) const -> std::pair<const_iterator, const_iterator> {
        auto it = do_find(key);
        return {it, it == end() ? end() : it + 1};
    }

    template <class K, class H = Hash, class KE = KeyEqual, std::enable_if_t<is_transparent_v<H, KE>, bool> = true>
    auto equal_range(K const& key) -> std::pair<iterator, iterator> {
        auto it = do_find(key);
        return {it, it == end() ? end() : it + 1};
    }

    template <class K, class H = Hash, class KE = KeyEqual, std::enable_if_t<is_transparent_v<H, KE>, bool> = true>
    auto equal_range(K const& key) const -> std::pair<const_iterator, const_iterator> {
        auto it = do_find(key);
        return {it, it == end() ? end() : it + 1};
    }

    // bucket interface ///////////////////////////////////////////////////////

    auto bucket_count() const noexcept -> std::size_t { // NOLINT(modernize-use-nodiscard)
        return m_buckets.size();
    }

    static constexpr auto max_bucket_count() noexcept -> std::size_t { // NOLINT(modernize-use-nodiscard)
        return max_size();
    }

    // hash policy ////////////////////////////////////////////////////////////

    [[nodiscard]] auto load_factor() const -> float {
        return bucket_count() ? static_cast<float>(size()) / static_cast<float>(bucket_count()) : 0.0F;
    }

    [[nodiscard]] auto max_load_factor() const -> float {
        return m_max_load_factor;
    }

    void max_load_factor(float ml) {
        m_max_load_factor = ml;
        if (bucket_count() != max_bucket_count()) {
            m_max_bucket_capacity = static_cast<value_idx_type>(static_cast<float>(bucket_count()) * max_load_factor());
        }
    }

    void rehash(std::size_t count) {
        count = (std::min)(count, max_size());
        auto shifts = calc_shifts_for_size((std::max)(count, size()));
        if (shifts != m_shifts) {
            m_shifts = shifts;
            deallocate_buckets();
            m_values.shrink_to_fit();
            allocate_buckets_from_shift();
            clear_and_fill_buckets_from_values();
        }
    }

    void reserve(std::size_t capa) {
        capa = (std::min)(capa, max_size());
        if constexpr (has_reserve<value_container_type>) {
            // std::deque doesn't have reserve(). Make sure we only call when available
            m_values.reserve(capa);
        }
        auto shifts = calc_shifts_for_size((std::max)(capa, size()));
        if (0 == bucket_count() || shifts < m_shifts) {
            m_shifts = shifts;
            deallocate_buckets();
            allocate_buckets_from_shift();
            clear_and_fill_buckets_from_values();
        }
    }

    // observers //////////////////////////////////////////////////////////////

    auto hash_function() const -> hasher {
        return m_hash;
    }

    auto key_eq() const -> key_equal {
        return m_equal;
    }

    // nonstandard API: expose the underlying values container
    [[nodiscard]] auto values() const noexcept -> value_container_type const& {
        return m_values;
    }

    // non-member functions ///////////////////////////////////////////////////

    friend auto operator==(table const& a, table const& b) -> bool {
        if (&a == &b) {
            return true;
        }
        if (a.size() != b.size()) {
            return false;
        }
        for (auto const& b_entry : b) {
            auto it = a.find(get_key(b_entry));
            if constexpr (is_map_v<T>) {
                // map: check that key is here, then also check that value is the same
                if (a.end() == it || !(b_entry.second == it->second)) {
                    return false;
                }
            } else {
                // set: only check that the key is here
                if (a.end() == it) {
                    return false;
                }
            }
        }
        return true;
    }

    friend auto operator!=(table const& a, table const& b) -> bool {
        return !(a == b);
    }
};

} // namespace detail

template <class Key,
          class T,
          class Hash = hash<Key>,
          class KeyEqual = std::equal_to<Key>,
          class AllocatorOrContainer = std::allocator<std::pair<Key, T>>,
          class Bucket = bucket_type::standard,
          class BucketContainer = detail::default_container_t>
using dense_hash_map = detail::table<Key, T, Hash, KeyEqual, AllocatorOrContainer, Bucket, BucketContainer, false>;

template <class Key,
          class Hash = hash<Key>,
          class KeyEqual = std::equal_to<Key>,
          class AllocatorOrContainer = std::allocator<Key>,
          class Bucket = bucket_type::standard,
          class BucketContainer = detail::default_container_t>
using dense_hash_set = detail::table<Key, void, Hash, KeyEqual, AllocatorOrContainer, Bucket, BucketContainer, false>;

}

#endif
#endif
