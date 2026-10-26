#pragma once

// Catch a synchronous host fault during one guest-facing call. The callback must be
// kept in a small frame without C++ objects that need unwinding: Windows SEH
// and Linux siglongjmp both bypass normal exception unwinding after a fault.
namespace host_fault {
using Procedure = void (*)(void*);
bool Run(Procedure procedure, void* argument);
}
