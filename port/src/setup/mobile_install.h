#pragma once
#include <filesystem>
#include <functional>
#include <string>
namespace ttt2::setup {
using InstallProgress = std::function<bool(int percent, const std::string& message)>;
// Creates destination atomically; never modifies an existing destination or saves.
bool InstallFromDescriptors(int isoFd, int tuFd, const std::filesystem::path& destination,
                            const InstallProgress& progress, std::string& error);
}
