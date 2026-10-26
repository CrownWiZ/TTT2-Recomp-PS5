#include "host_fault.h"

#if !defined(_WIN32)
#error "The Windows fault boundary requires Windows"
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// main.cpp owns the detailed guest fault report. Keep its platform context here.
int GuestFaultReport(EXCEPTION_POINTERS* ep);

namespace host_fault {
bool Run(Procedure procedure, void* argument)
{
    if (!procedure) return false;
    __try { procedure(argument); }
    __except (GuestFaultReport(GetExceptionInformation())) { return false; }
    return true;
}
}
