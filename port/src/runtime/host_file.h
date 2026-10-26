#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Host file handles and guest-visible NT file results. The caller serializes
// shared file-pointer operations; these functions do not take guest locks.
namespace host_file {
struct File;
struct OpenResult {
    File* file = nullptr;
    uint32_t status = 0;
    uint32_t information = 1;
    const char* stage = "";
};
enum class Origin { Begin, Current, End };
struct DirectoryEntry { std::string name; uint64_t size; };

uint32_t DosErrorStatus(uint32_t error);
// Resolve a guest-relative disc path under root, matching ASCII filename case
// as Windows does. An empty result means missing, ambiguous, or unsafe input.
std::filesystem::path ResolveDiscPath(const std::filesystem::path& root,
                                      const std::filesystem::path& relative,
                                      bool rootFallback);
OpenResult OpenDisc(const std::filesystem::path& path);
// Query file size and type without following links. An empty container means a
// disc path; a save path is restricted to the container or one direct child.
uint32_t QueryAttributes(const std::filesystem::path& path,
                         const std::filesystem::path& container,
                         uint64_t& size, bool& directory);
OpenResult OpenSave(const std::filesystem::path& path,
                    const std::filesystem::path& container,
                    uint32_t access, uint32_t share, uint32_t disposition,
                    uint32_t options, uint32_t attributes,
                    bool hasAllocation, uint64_t allocation);
void Close(File* file);
uint32_t Flush(File* file); // returns a guest DOS error for save-store callers
uint32_t Seek(File* file, int64_t offset, Origin origin, uint64_t& position);
uint32_t Read(File* file, void* buffer, uint32_t bytes, uint32_t& read);
uint32_t Write(File* file, const void* buffer, uint32_t bytes, uint32_t& written);
uint32_t Size(File* file, uint64_t& size);
uint32_t SetSize(File* file, uint64_t size);
uint32_t SetDeleteDisposition(File* file, bool remove);
uint32_t Rename(File* file, const std::filesystem::path& target, bool replace);
// Enumerate a save directory through its open host handle. Names beginning
// with '.', non-ASCII names, and subdirectories are omitted; links are rejected.
uint32_t ListRegularFiles(File* directory, std::vector<DirectoryEntry>& entries);
}
