#include "host_random.h"

#if !defined(__linux__)
#error "This random backend requires Linux"
#endif

#include <cerrno>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sys/random.h>

namespace host_random {
void Fill(void* data, size_t bytes)
{
    auto* next = static_cast<unsigned char*>(data);
    while (bytes)
    {
        const ssize_t count = getrandom(next, std::min(bytes, size_t(1 << 20)), 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0)
        {
            std::perror("getrandom");
            std::abort();
        }
        next += count;
        bytes -= size_t(count);
    }
}
}
