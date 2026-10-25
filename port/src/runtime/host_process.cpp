#include "host_process.h"

#include <cstdlib>

namespace host_process {
[[noreturn]] void ExitNow(int status)
{
    std::_Exit(status);
}
}
