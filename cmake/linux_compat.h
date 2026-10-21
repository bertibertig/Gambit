// Force-included (-include) into the port's own sources by port/CMakeLists.txt
// when building with GCC or Clang outside MSVC. The runtime is developed with
// MSVC: this supplies the headers MSVC pulls in transitively and the MSVC
// intrinsics it uses (__cpuid, _BitScanReverse).
#pragma once

#ifdef __cplusplus
#include <algorithm>
#include <cstring>
#include <utility>
#endif

#if !defined(_MSC_VER) && (defined(__x86_64__) || defined(__i386__))
#include <cpuid.h>
#include <immintrin.h>

// MSVC: void __cpuid(int info[4], int leaf). GCC's <cpuid.h> defines a
// 5-argument __cpuid macro, so replace it.
#undef __cpuid
static inline void __cpuid(int info[4], int leaf) {
    __cpuid_count(leaf, 0, info[0], info[1], info[2], info[3]);
}

// MSVC: unsigned char _BitScanReverse(unsigned long* index, unsigned long mask)
static inline unsigned char _BitScanReverse(unsigned long* index, unsigned long mask) {
    if (!mask)
        return 0;
    *index = 31u - static_cast<unsigned long>(__builtin_clz(static_cast<unsigned int>(mask)));
    return 1;
}
#endif
