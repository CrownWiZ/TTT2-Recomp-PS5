#pragma once
// C++20 gaps of the console's pinned libc++: span, atomic_ref aliases, bit cast.
// No declarations go into namespace std. Translation units include this after
// the standard headers and the sweep aliases std::span/std::atomic_ref to the
// shim namespace on the PS5 build.
#include <atomic>
#include <cstddef>
#include <cstring>
#include <type_traits>
#if defined(TTT2_HOST_PS5) && !defined(__cpp_lib_endian)
namespace ttt2_c20 {
enum class endian
{
    little = 1234567,
    big = 7654321,
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    native = little,
#else
    native = big,
#endif
};
}
using ttt2_c20::endian;
#endif
#if defined(TTT2_HOST_PS5) && !defined(__cpp_lib_span)
namespace ttt2_c20 {
inline constexpr size_t dynamic_extent = size_t(-1);

template <typename T>
class span
{
public:
    using element_type = T;
    using value_type = std::remove_cv_t<T>;
    using size_type = size_t;
    using pointer = T*;
    using iterator = T*;

    // Element views: the container and size stay in the plain fields for the
    // console ABI (no inline namespace tricks needed).
    constexpr span() : data_(nullptr), size_(0) {}
    constexpr span(T* first, size_type size) : data_(first), size_(size) {}
    constexpr span(T* first, T* last) : data_(first), size_(size_type(last - first)) {}

    template <size_t N>
    constexpr span(T (&array)[N]) : data_(array), size_(N) {}

    // Contiguous range conversion (vector/array/...): anything with .data()
    // and .size() converts implicitly.
    template <typename Range,
              typename = decltype(std::declval<Range&>().data()),
              typename = decltype(std::declval<Range&>().size())>
    constexpr span(Range& range) : data_(range.data()), size_(range.size()) {}

    constexpr pointer data() const { return data_; }
    constexpr size_type size() const { return size_; }
    constexpr size_type size_bytes() const { return size_ * sizeof(T); }
    constexpr bool empty() const { return size_ == 0; }

    constexpr T& operator[](size_type index) const { return data_[index]; }
    constexpr T& front() const { return data_[0]; }
    constexpr T& back() const { return data_[size_ - 1]; }
    constexpr iterator begin() const { return data_; }
    constexpr iterator end() const { return data_ + size_; }

    constexpr span subspan(size_type offset, size_type count = dynamic_extent) const
    {
        return span(data_ + offset, count == dynamic_extent ? size_ - offset : count);
    }
    constexpr span first(size_type count) const { return span(data_, count); }
    constexpr span last(size_type count) const { return span(data_ + size_ - count, count); }

private:
    T* data_;
    size_type size_;
};

template <typename T>
span(T*, size_t) -> span<T>;
template <typename T, size_t N>
span(T (&)[N]) -> span<T>;
}

using ttt2_c20::span;
#endif
