#pragma once

// gpu_render.cpp consumes the common GPU contract and the backend selected by
// the build. Keep host graphics APIs inside their backend headers.
#include "gpu_hw_control.h"

#if defined(TTT2_GPU_HW_D3D11)
#include "gpu_hw_d3d11.h"
#else
#error "A hardware GPU backend was requested without a selected implementation"
#endif
