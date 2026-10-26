#pragma once

// The DXVK Native 1.9.2b d3dcompiler header includes this MinGW selector,
// although native Linux and Android only use the narrow library name.
#ifndef __MINGW_NAME_AW
#define __MINGW_NAME_AW(name) name##A
#endif
