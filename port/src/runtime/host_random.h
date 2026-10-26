#pragma once

#include <cstddef>

// Fill a guest-visible buffer from the operating system's secure random source.
namespace host_random {
void Fill(void* data, size_t bytes);
}
