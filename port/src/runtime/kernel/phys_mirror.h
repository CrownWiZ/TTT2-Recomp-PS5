#pragma once
// Cached physical views (guest 0xC0000000-0xFFFFFFFF) kept coherent with the 0xA0000000 view
// the port allocates physical memory in. See phys_mirror.cpp.
#include <cstdint>
#include <cstddef>

enum class PhysMirrorFaultResult { Unhandled, AlreadyOpen, Filled };
// Platform fault adapters pass the faulting host address here. A handled fault
// may retry its instruction after the cached physical chunk has been opened.
PhysMirrorFaultResult PhysMirrorHandleFault(uintptr_t fault);
bool PhysMirrorInstallFaultHandler(uint8_t* view, size_t bytes);

// Close the C and E views and install the fault handler that fills them.
bool PhysMirrorInit(uint8_t* base);
// Physical bytes in [start, start + size) changed (for example, a GPU resolve):
// close copied chunks in both cached views so the next access refills.
void PhysMirrorInvalidate(uint32_t start, uint32_t size);
// Before a GPU operation reads or replaces A-view bytes, close overlapping
// cached copies and copy their CPU writes back to A. This does not announce a
// new GPU resolve; the caller still invalidates after recording that resolve.
void PhysMirrorFlushRange(uint32_t start, uint32_t size);
// Close every open chunk (frame boundary): bounds staleness after CPU writes to the A view.
void PhysMirrorInvalidateAll();
