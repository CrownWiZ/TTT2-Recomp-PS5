#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

// Host storage operations for the local save container. Return Win32-compatible
// error numbers because XAM exposes these values to the guest on every platform.
namespace host_storage {
uint32_t Error(const std::error_code& error);
uint32_t SaveBase(std::filesystem::path& root);
uint32_t RejectLinks(const std::filesystem::path& path);
uint32_t CreateDirectories(const std::filesystem::path& path);
uint32_t RemoveTree(const std::filesystem::path& path);
uint32_t ReadSmallFile(const std::filesystem::path& path, std::string& data,
                       size_t maxBytes);
uint32_t ReadExact(const std::filesystem::path& path, void* data, size_t bytes);
// List direct child directories through an open, no-follow root. A missing
// root is an empty list; links and read errors are reported to the caller.
uint32_t ListDirectories(const std::filesystem::path& root,
                         std::vector<std::filesystem::path>& paths);
// Choose an exclusive temporary file in the destination directory, sync it,
// then replace the destination. Callers never share a temporary filename.
uint32_t WriteAtomic(const std::filesystem::path& destination,
                     const void* data, size_t bytes);
}
