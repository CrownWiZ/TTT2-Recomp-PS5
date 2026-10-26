#include "host_random.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace host_random {
void Fill(void* data, size_t bytes)
{
    auto* next = static_cast<unsigned char*>(data);
    while (bytes)
    {
        const ULONG count = static_cast<ULONG>(std::min(bytes, size_t(std::numeric_limits<ULONG>::max())));
        if (BCryptGenRandom(nullptr, next, count, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        {
            std::fputs("BCryptGenRandom failed\n", stderr);
            std::abort();
        }
        next += count;
        bytes -= count;
    }
}
}
