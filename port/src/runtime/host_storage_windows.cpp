#include "host_storage.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>

namespace host_storage {
uint32_t Error(const std::error_code& error) { return error ? uint32_t(error.value()) : 0; }

uint32_t SaveBase(std::filesystem::path& root)
{
    const wchar_t* overridePath = _wgetenv(L"TTT2_SAVE_ROOT");
    const wchar_t* local = _wgetenv(L"LOCALAPPDATA");
    if (overridePath && *overridePath) root = overridePath;
    else if (local && *local) root = std::filesystem::path(local) / L"TTT2Recomp" / L"saves";
    else return ERROR_PATH_NOT_FOUND;
    return root.is_absolute() ? 0 : ERROR_INVALID_NAME;
}

uint32_t RejectLinks(const std::filesystem::path& path)
{
    std::filesystem::path part;
    for (const auto& component : path)
    {
        part /= component;
        if (part == path.root_name()) continue;
        const DWORD attributes = GetFileAttributesW(part.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) continue;
            return error;
        }
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return ERROR_ACCESS_DENIED;
    }
    return 0;
}

uint32_t CreateDirectories(const std::filesystem::path& path)
{
    if (const uint32_t error = RejectLinks(path)) return error;
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) return Error(error);
    return RejectLinks(path);
}

uint32_t RemoveTree(const std::filesystem::path& path)
{
    if (const uint32_t error = RejectLinks(path)) return error;
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(path, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
        if (const uint32_t linkError = RejectLinks(it->path())) return linkError;
    if (error) return Error(error);
    std::filesystem::remove_all(path, error);
    return Error(error);
}

uint32_t ReadSmallFile(const std::filesystem::path& path, std::string& data,
                       size_t maxBytes)
{
    data.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER size{};
    if (!GetFileInformationByHandle(file, &info) || !GetFileSizeEx(file, &size))
    {
        const uint32_t error = GetLastError();
        CloseHandle(file);
        return error;
    }
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        size.QuadPart < 0 || uint64_t(size.QuadPart) > maxBytes)
    {
        CloseHandle(file);
        return ERROR_FILE_CORRUPT;
    }
    data.resize(size_t(size.QuadPart));
    size_t done = 0;
    while (done < data.size())
    {
        DWORD got = 0;
        const DWORD requested = DWORD(std::min<size_t>(data.size() - done, MAXDWORD));
        const BOOL okay = ReadFile(file, data.data() + done, requested, &got, nullptr);
        if (!okay || !got)
        {
            const uint32_t error = okay ? ERROR_FILE_CORRUPT : GetLastError();
            data.clear();
            CloseHandle(file);
            return error;
        }
        done += got;
    }
    CloseHandle(file);
    return 0;
}

uint32_t ReadExact(const std::filesystem::path& path, void* data, size_t bytes)
{
    if (bytes > UINT32_MAX) return ERROR_INVALID_PARAMETER;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER size{};
    if (!GetFileInformationByHandle(file, &info) || !GetFileSizeEx(file, &size))
    {
        const uint32_t error = GetLastError();
        CloseHandle(file);
        return error;
    }
    if ((info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        size.QuadPart != static_cast<LONGLONG>(bytes))
    {
        CloseHandle(file);
        return ERROR_FILE_CORRUPT;
    }
    size_t done = 0;
    while (done < bytes)
    {
        DWORD got = 0;
        const BOOL okay = ReadFile(file, static_cast<uint8_t*>(data) + done,
                                   DWORD(bytes - done), &got, nullptr);
        if (!okay || !got)
        {
            const uint32_t error = okay ? ERROR_FILE_CORRUPT : GetLastError();
            CloseHandle(file);
            return error;
        }
        done += got;
    }
    CloseHandle(file);
    return 0;
}

uint32_t ListDirectories(const std::filesystem::path& root,
                         std::vector<std::filesystem::path>& paths)
{
    paths.clear();
    HANDLE directory = CreateFileW(root.c_str(), FILE_LIST_DIRECTORY,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING,
                                   FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                   nullptr);
    if (directory == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? 0 : error;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(directory, &info))
    {
        const DWORD error = GetLastError();
        CloseHandle(directory);
        return error;
    }
    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
    {
        CloseHandle(directory);
        return ERROR_ACCESS_DENIED;
    }
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        CloseHandle(directory);
        return ERROR_DIRECTORY;
    }
    alignas(8) std::array<uint8_t, 64 * 1024> buffer{};
    bool restart = true;
    for (;;)
    {
        const bool first = restart;
        const auto query = restart ? FileIdBothDirectoryRestartInfo : FileIdBothDirectoryInfo;
        restart = false;
        if (!GetFileInformationByHandleEx(directory, query, buffer.data(), DWORD(buffer.size())))
        {
            const DWORD error = GetLastError();
            CloseHandle(directory);
            if (error == ERROR_NO_MORE_FILES || (first && error == ERROR_FILE_NOT_FOUND)) return 0;
            paths.clear();
            return error;
        }
        size_t offset = 0;
        for (;;)
        {
            constexpr size_t header = offsetof(FILE_ID_BOTH_DIR_INFO, FileName);
            if (offset > buffer.size() - header) { CloseHandle(directory); paths.clear(); return ERROR_FILE_CORRUPT; }
            const auto* item = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(buffer.data() + offset);
            if (item->FileNameLength % sizeof(wchar_t) ||
                item->FileNameLength > buffer.size() - offset - header)
            { CloseHandle(directory); paths.clear(); return ERROR_FILE_CORRUPT; }
            if (item->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            { CloseHandle(directory); paths.clear(); return ERROR_ACCESS_DENIED; }
            if (item->FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                std::string name;
                const size_t length = item->FileNameLength / sizeof(wchar_t);
                name.reserve(length);
                for (size_t i = 0; i < length; ++i)
                {
                    if (item->FileName[i] > 0x7f) { name.clear(); break; }
                    name.push_back(char(item->FileName[i]));
                }
                if (!name.empty() && name != "." && name != "..")
                    paths.push_back(root / name);
            }
            if (!item->NextEntryOffset) break;
            if (item->NextEntryOffset < header + item->FileNameLength ||
                item->NextEntryOffset > buffer.size() - offset)
            { CloseHandle(directory); paths.clear(); return ERROR_FILE_CORRUPT; }
            offset += item->NextEntryOffset;
        }
    }
}

uint32_t WriteAtomic(const std::filesystem::path& destination,
                     const void* data, size_t bytes)
{
    if (bytes > UINT32_MAX) return ERROR_INVALID_PARAMETER;
    static std::atomic<uint64_t> sequence{0};
    std::filesystem::path temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 128; ++attempt)
    {
        temporary = destination.parent_path() /
            (L".ttt2-tmp." + std::to_wstring(GetCurrentProcessId()) + L"." +
             std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed)));
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) break;
        const DWORD createError = GetLastError();
        if (createError != ERROR_FILE_EXISTS && createError != ERROR_ALREADY_EXISTS)
            return createError;
    }
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    DWORD written = 0;
    uint32_t error = 0;
    if (!WriteFile(file, data, static_cast<DWORD>(bytes), &written, nullptr))
        error = GetLastError();
    else if (written != bytes) error = ERROR_WRITE_FAULT;
    else if (!FlushFileBuffers(file)) error = GetLastError();
    if (!CloseHandle(file) && !error) error = GetLastError();
    if (error) { DeleteFileW(temporary.c_str()); return error; }
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        error = GetLastError();
        DeleteFileW(temporary.c_str());
        return error;
    }
    return 0;
}
}
