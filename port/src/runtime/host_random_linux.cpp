#include "host_random.h"

#if (!defined(__linux__) && !defined(TTT2_HOST_PS5))
#error "This random backend requires Linux or the PS5 build"
#endif

#include <cerrno>
#include <fcntl.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>

namespace host_random {
void Fill(void* data, size_t bytes)
{
    auto* next = static_cast<unsigned char*>(data);
#if defined(TTT2_HOST_PS5) || !defined(__linux__)
    // The closed kernel exposes no getrandom; /dev/urandom is the source.
    const int random = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (random < 0)
    {
        std::perror("open /dev/urandom");
        std::abort();
    }
    while (bytes)
    {
        const ssize_t count = read(random, next, std::min(bytes, size_t(1 << 20)));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0)
        {
            std::perror("read /dev/urandom");
            std::abort();
        }
        next += count;
        bytes -= size_t(count);
    }
    close(random);
#else
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
#endif
}
}
