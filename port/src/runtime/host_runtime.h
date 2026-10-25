#pragma once

#include <filesystem>
#include <string>

// Process services used by the otherwise shared guest loader/entry point.
namespace host_runtime {
std::filesystem::path InstalledLayout(int argc);
std::filesystem::path VfsRoot();
const char* DefaultImagePath();
const char* DefaultVfsRoot();
void StartLogWriter();
void ConfigureTimer();
void StartShaderPrebuild();
void InstallFatalHandler();
void InstallGuestGuards();
}
