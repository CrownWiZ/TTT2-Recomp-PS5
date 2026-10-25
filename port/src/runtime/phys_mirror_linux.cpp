#include "kernel/phys_mirror.h"
#include "host_vm.h"

namespace {
bool HandleCachedViewFault(uintptr_t fault)
{
    return PhysMirrorHandleFault(fault) != PhysMirrorFaultResult::Unhandled;
}
}

bool PhysMirrorInstallFaultHandler(uint8_t* view, size_t bytes)
{
    return host_vm::RegisterDeferredFaultHandler(view, bytes, HandleCachedViewFault);
}
