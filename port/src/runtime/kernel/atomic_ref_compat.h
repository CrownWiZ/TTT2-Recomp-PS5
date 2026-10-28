#pragma once
// std::atomic_ref is missing from the console pinned libc++. The kernel uses
// it only on aligned 32-bit guest words; this shim keeps the same interface
// over the __atomic builtins. Legal-C++ note: no declarations are injected
// into namespace std — the translation units alias it after the include.
#include <atomic>
#if defined(TTT2_HOST_PS5) && !defined(__cpp_lib_atomic_ref)
namespace ttt2_c20 {
template <typename T>
class atomic_ref;
template <>
class atomic_ref<uint32_t>
{
public:
    explicit atomic_ref(uint32_t& word) : word_(&word) {}
    uint32_t load(std::memory_order order = std::memory_order_seq_cst) const
        { return __atomic_load_n(word_, order); }
    void store(uint32_t value, std::memory_order order = std::memory_order_seq_cst)
        { __atomic_store_n(word_, value, order); }
    uint32_t exchange(uint32_t value, std::memory_order order = std::memory_order_seq_cst)
        { return __atomic_exchange_n(word_, value, order); }
    bool compare_exchange_weak(uint32_t& expected, uint32_t desired,
                               std::memory_order order = std::memory_order_seq_cst,
                               std::memory_order failure = std::memory_order_relaxed)
        { (void)failure; return __atomic_compare_exchange_n(word_, &expected, desired, true, order, order); }
    uint32_t fetch_add(uint32_t value, std::memory_order order = std::memory_order_seq_cst)
        { return __atomic_fetch_add(word_, value, order); }
private:
    uint32_t* word_;
};
}
using ttt2_c20::atomic_ref;
#endif
