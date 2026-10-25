#pragma once
#include <chrono>
#include <climits>
#include <filesystem>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#else
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <cerrno>
#include <strings.h>
inline int _strnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a,b,n); }
#include <openssl/rand.h>
using SOCKET = int;
constexpr int INVALID_SOCKET = -1, SOCKET_ERROR = -1;
inline int closesocket(int s) { return close(s); }
inline int WSAGetLastError() { return errno; }
constexpr int WSAEWOULDBLOCK = EWOULDBLOCK;
inline int ioctlsocket(int s, unsigned long op, unsigned long* value) { return ioctl(s, op, value); }
inline uint64_t GetTickCount64() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
using ULONGLONG = unsigned long long;
inline bool CreateDirectoryA(const char* path, void*) { std::error_code e; return std::filesystem::create_directories(path,e); }
#endif
inline bool CommunityRandom(void* out, size_t size) {
#ifdef _WIN32
return BCryptGenRandom(nullptr, static_cast<PUCHAR>(out), ULONG(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
return size <= INT_MAX && RAND_bytes(static_cast<unsigned char*>(out), int(size)) == 1;
#endif
}
