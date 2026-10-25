// Seekable, read-only input shared by desktop paths and Android document descriptors.
#pragma once
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <cstring>
#ifndef _WIN32
#include <unistd.h>
#include <strings.h>
#endif
namespace ttt2::setup {
inline int CompareNames(const char* a, const char* b) {
#ifdef _WIN32
    return _stricmp(a, b);
#else
    return strcasecmp(a, b);
#endif
}
class InputFile {
public:
    InputFile() = default;
    InputFile(const InputFile&) = delete;
    InputFile& operator=(const InputFile&) = delete;
    ~InputFile() { Close(); }
    void Close() { if (file_) fclose(file_); file_ = nullptr; size_ = 0; }
    bool Open(const std::filesystem::path& path) {
        Close();
#ifdef _WIN32
        file_ = _wfopen(path.c_str(), L"rb");
#else
        file_ = fopen(path.c_str(), "rb");
#endif
        return Measure();
    }
#ifndef _WIN32
    // Own a duplicate: Java retains ownership of the original descriptor.
    bool OpenFd(int fd) {
        Close();
        int copy = dup(fd);
        if (copy < 0) return false;
        file_ = fdopen(copy, "rb");
        if (!file_) { close(copy); return false; }
        return Measure();
    }
#endif
    uint64_t Size() const { return size_; }
    bool Read(uint64_t at, void* out, uint32_t bytes) {
        if (!file_ || at > size_ || bytes > size_ - at) return false;
#ifdef _WIN32
        if (_fseeki64(file_, static_cast<int64_t>(at), SEEK_SET)) return false;
#else
        if (fseeko(file_, static_cast<off_t>(at), SEEK_SET)) return false;
#endif
        return fread(out, 1, bytes, file_) == bytes;
    }
private:
    bool Measure() {
        if (!file_) return false;
#ifdef _WIN32
        if (_fseeki64(file_, 0, SEEK_END)) { Close(); return false; }
        const auto n = _ftelli64(file_);
#else
        if (fseeko(file_, 0, SEEK_END)) { Close(); return false; }
        const auto n = ftello(file_);
#endif
        if (n < 0) { Close(); return false; }
        size_ = static_cast<uint64_t>(n);
        return true;
    }
    FILE* file_ = nullptr;
    uint64_t size_ = 0;
};
}
