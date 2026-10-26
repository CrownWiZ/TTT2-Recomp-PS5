#include "host_file.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace host_file {
struct File { HANDLE handle; };

namespace {
std::wstring FinalPath(HANDLE handle)
{
    std::wstring path(32768, L'\0');
    const DWORD length = GetFinalPathNameByHandleW(handle, path.data(),
                                                    DWORD(path.size()), FILE_NAME_NORMALIZED);
    if (!length || length >= path.size()) return {};
    path.resize(length);
    if (path.rfind(L"\\\\?\\", 0) == 0) path.erase(0, 4);
    return path;
}

std::wstring FinalPathOf(const std::filesystem::path& directory)
{
    HANDLE handle = CreateFileW(directory.c_str(), FILE_TRAVERSE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    std::wstring path = FinalPath(handle);
    CloseHandle(handle);
    return path;
}
} // namespace

OpenResult OpenDisc(const std::filesystem::path& path)
{
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {nullptr, 0xC0000034u, 0, "disc-open"};
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
    {
        CloseHandle(handle);
        return {nullptr, 0xC0000034u, 0, "disc-type"};
    }
    return {new File{handle}, 0, 1, ""};
}

uint32_t QueryAttributes(const std::filesystem::path& path,
                         const std::filesystem::path& container,
                         uint64_t& size, bool& directory)
{
    size = 0;
    directory = false;
    if (path.empty()) return 0xC0000034u; // STATUS_OBJECT_NAME_NOT_FOUND
    if (!path.is_absolute() || (!container.empty() &&
        path != container && path.parent_path() != container))
        return 0xC0000033u; // STATUS_OBJECT_NAME_INVALID
    File* opened = nullptr;
    HANDLE handle = INVALID_HANDLE_VALUE;
    if (!container.empty())
    {
        // OpenSave pins and checks the container, then opens a child relative
        // to that handle. A later container rename cannot redirect the child.
        const OpenResult result = OpenSave(path, container, FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 1,
            path == container ? 1u : 0u, FILE_ATTRIBUTE_NORMAL, false, 0);
        if (result.status) return result.status;
        opened = result.file;
        handle = opened->handle;
    }
    else
    {
        handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                             nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
                return 0xC0000034u; // STATUS_OBJECT_NAME_NOT_FOUND
            return DosErrorStatus(error);
        }
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info))
    {
        const DWORD error = GetLastError();
        if (opened) Close(opened); else CloseHandle(handle);
        return DosErrorStatus(error);
    }
    if (opened) Close(opened); else CloseHandle(handle);
    if (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE))
        return 0xC0000022u; // STATUS_ACCESS_DENIED
    directory = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    size = directory ? 0 : ((uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow);
    return 0;
}

OpenResult OpenSave(const std::filesystem::path& path,
                    const std::filesystem::path& container,
                    uint32_t access, uint32_t share, uint32_t disposition,
                    uint32_t options, uint32_t attributes,
                    bool hasAllocation, uint64_t allocation)
{
    HANDLE parent = CreateFileW(container.c_str(), FILE_LIST_DIRECTORY | FILE_TRAVERSE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                nullptr);
    if (parent == INVALID_HANDLE_VALUE)
        return {nullptr, DosErrorStatus(GetLastError()), 0, "container"};
    BY_HANDLE_FILE_INFORMATION info{};
    const std::wstring actual = FinalPath(parent);
    const std::wstring expected = FinalPathOf(container.parent_path()) + L"\\" +
                                  container.filename().wstring();
    if (!GetFileInformationByHandle(parent, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || actual.empty() ||
        _wcsicmp(actual.c_str(), expected.c_str()))
    {
        std::fprintf(stderr, "[io] save container final path mismatch\n");
        CloseHandle(parent);
        return {nullptr, 0xC0000022u, 0, "final-path"};
    }
    if (path == container)
    {
        if (!(options & 1) || disposition != 1 || (access & 0x50010116u))
        {
            CloseHandle(parent);
            return {nullptr, 0xC0000022u, 0, "root-access"};
        }
        return {new File{parent}, 0, 1, ""};
    }
    if (options & 1)
    {
        CloseHandle(parent);
        return {nullptr, 0xC0000002u, 0, "nested"};
    }

    struct NativeString { USHORT length, maximum; PWSTR buffer; };
    struct NativeAttributes {
        ULONG length; HANDLE root; NativeString* name; ULONG attributes;
        void* security; void* qos;
    };
    struct NativeStatus { union { LONG status; void* pointer; }; ULONG_PTR information; };
    using OpenNative = LONG (NTAPI*)(PHANDLE, ACCESS_MASK, NativeAttributes*,
                                      NativeStatus*, LARGE_INTEGER*, ULONG, ULONG,
                                      ULONG, ULONG, void*, ULONG);
    static auto ntCreate = reinterpret_cast<OpenNative>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile"));
    if (!ntCreate)
    {
        CloseHandle(parent);
        return {nullptr, 0xC0000002u, 0, "host-NtCreateFile"};
    }
    auto name = path.filename().wstring();
    NativeString text{USHORT(name.size() * 2), USHORT(name.size() * 2), name.data()};
    NativeAttributes attrs{sizeof(NativeAttributes), parent, &text, 0x40,
                           nullptr, nullptr};
    NativeStatus ios{};
    LARGE_INTEGER allocated{};
    allocated.QuadPart = allocation;
    HANDLE handle = INVALID_HANDLE_VALUE;
    // Drop FILE_NO_INTERMEDIATE_BUFFERING: guest writes need not be sector aligned.
    const ULONG hostOptions = (options & ~0x18u) | 0x20u | 0x00200000u;
    uint32_t status = uint32_t(ntCreate(&handle, access | SYNCHRONIZE |
        FILE_READ_ATTRIBUTES, &attrs, &ios, hasAllocation ? &allocated : nullptr,
        attributes, share, disposition, hostOptions, nullptr, 0));
    CloseHandle(parent);
    if (status & 0x80000000u)
        return {nullptr, status, 0, "host-NtCreateFile"};
    BY_HANDLE_FILE_INFORMATION child{};
    if (!GetFileInformationByHandle(handle, &child) ||
        (child.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        CloseHandle(handle);
        return {nullptr, 0xC0000022u, 0, "child-link"};
    }
    return {new File{handle}, 0, uint32_t(ios.information), ""};
}

void Close(File* file)
{
    if (!file) return;
    CloseHandle(file->handle);
    delete file;
}

uint32_t Flush(File* file)
{
    return FlushFileBuffers(file->handle) ? 0 : GetLastError();
}

uint32_t Seek(File* file, int64_t offset, Origin origin, uint64_t& position)
{
    LARGE_INTEGER input{}, output{};
    input.QuadPart = offset;
    const DWORD method = origin == Origin::Begin ? FILE_BEGIN :
                         origin == Origin::End ? FILE_END : FILE_CURRENT;
    if (!SetFilePointerEx(file->handle, input, &output, method))
        return DosErrorStatus(GetLastError());
    position = uint64_t(output.QuadPart);
    return 0;
}

uint32_t Read(File* file, void* buffer, uint32_t bytes, uint32_t& read)
{
    DWORD count = 0;
    if (!ReadFile(file->handle, buffer, bytes, &count, nullptr))
        return DosErrorStatus(GetLastError());
    read = count;
    return 0;
}

uint32_t Write(File* file, const void* buffer, uint32_t bytes, uint32_t& written)
{
    DWORD count = 0;
    if (!WriteFile(file->handle, buffer, bytes, &count, nullptr))
        return DosErrorStatus(GetLastError());
    written = count;
    return 0;
}

uint32_t Size(File* file, uint64_t& size)
{
    LARGE_INTEGER result{};
    if (!GetFileSizeEx(file->handle, &result)) return DosErrorStatus(GetLastError());
    size = uint64_t(result.QuadPart);
    return 0;
}

uint32_t SetSize(File* file, uint64_t size)
{
    FILE_END_OF_FILE_INFO info{};
    info.EndOfFile.QuadPart = size;
    return SetFileInformationByHandle(file->handle, FileEndOfFileInfo,
                                      &info, sizeof(info)) ? 0 : DosErrorStatus(GetLastError());
}

uint32_t SetDeleteDisposition(File* file, bool remove)
{
    FILE_DISPOSITION_INFO info{};
    info.DeleteFile = remove;
    return SetFileInformationByHandle(file->handle, FileDispositionInfo,
                                      &info, sizeof(info)) ? 0 : DosErrorStatus(GetLastError());
}

uint32_t Rename(File* file, const std::filesystem::path& target, bool replace)
{
    const std::wstring name = target.wstring();
    std::vector<uint8_t> bytes(sizeof(FILE_RENAME_INFO) + name.size() * sizeof(wchar_t));
    auto* info = reinterpret_cast<FILE_RENAME_INFO*>(bytes.data());
    info->ReplaceIfExists = replace;
    info->RootDirectory = nullptr;
    info->FileNameLength = DWORD(name.size() * sizeof(wchar_t));
    std::memcpy(info->FileName, name.data(), name.size() * sizeof(wchar_t));
    return SetFileInformationByHandle(file->handle, FileRenameInfo, info,
                                      DWORD(bytes.size())) ? 0 : DosErrorStatus(GetLastError());
}

uint32_t ListRegularFiles(File* directory, std::vector<DirectoryEntry>& entries)
{
    entries.clear();
    BY_HANDLE_FILE_INFORMATION info{};
    if (!directory || !GetFileInformationByHandle(directory->handle, &info))
        return 0xC0000008u;
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return 0xC0000103u;
    // Restart enumeration on the open handle, so renaming or replacing the
    // container path cannot redirect a later guest directory query.
    alignas(8) std::array<uint8_t, 64 * 1024> buffer{};
    bool restart = true;
    for (;;)
    {
        const bool first = restart;
        const auto query = restart ? FileIdBothDirectoryRestartInfo : FileIdBothDirectoryInfo;
        restart = false;
        if (!GetFileInformationByHandleEx(directory->handle, query,
                                          buffer.data(), DWORD(buffer.size())))
        {
            const DWORD error = GetLastError();
            if (error == ERROR_NO_MORE_FILES ||
                (first && error == ERROR_FILE_NOT_FOUND)) return 0;
            entries.clear();
            return DosErrorStatus(error);
        }
        size_t offset = 0;
        for (;;)
        {
            constexpr size_t header = offsetof(FILE_ID_BOTH_DIR_INFO, FileName);
            if (offset > buffer.size() - header) { entries.clear(); return 0xC0000001u; }
            const auto* item = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(buffer.data() + offset);
            if (item->FileNameLength % sizeof(wchar_t) ||
                item->FileNameLength > buffer.size() - offset - header)
            { entries.clear(); return 0xC0000001u; }
            const std::wstring wide(item->FileName,
                                    item->FileNameLength / sizeof(wchar_t));
            std::string name;
            name.reserve(wide.size());
            for (wchar_t ch : wide)
            {
                if (ch > 0x7f) { name.clear(); break; }
                name.push_back(char(ch));
            }
            if (!name.empty() && name[0] != '.')
            {
                if (item->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                { entries.clear(); return 0xC0000022u; }
                if (!(item->FileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    if (item->EndOfFile.QuadPart < 0)
                    { entries.clear(); return 0xC0000001u; }
                    entries.push_back({name, uint64_t(item->EndOfFile.QuadPart)});
                }
            }
            if (!item->NextEntryOffset) break;
            if (item->NextEntryOffset < header + item->FileNameLength ||
                item->NextEntryOffset > buffer.size() - offset)
            { entries.clear(); return 0xC0000001u; }
            offset += item->NextEntryOffset;
        }
    }
}
} // namespace host_file
