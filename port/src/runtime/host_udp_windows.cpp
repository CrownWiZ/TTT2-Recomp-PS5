#include "host_udp.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <windows.h>

// (cross: dropped; the link line carries the lib)(lib, "ws2_32.lib")
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

namespace host_udp {
struct Socket { SOCKET native = INVALID_SOCKET; };

Socket* BindLoopback(uint16_t firstPort, uint16_t attempts, uint16_t& boundPort)
{
    boundPort = 0;
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) return nullptr;
    SOCKET native = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (native == INVALID_SOCKET) return nullptr;
    int big = 8 << 20;
    setsockopt(native, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&big), sizeof(big));
    setsockopt(native, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&big), sizeof(big));
    BOOL exclusive = TRUE;
    setsockopt(native, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    BOOL off = FALSE;
    DWORD ignored = 0;
    WSAIoctl(native, SIO_UDP_CONNRESET, &off, sizeof(off), nullptr, 0,
             &ignored, nullptr, nullptr);
    for (uint16_t index = 0; index < attempts; ++index)
    {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(uint16_t(firstPort + index));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(native, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0)
        { boundPort = uint16_t(firstPort + index); break; }
    }
    if (!boundPort) { closesocket(native); return nullptr; }
    return new Socket{native};
}

int Receive(Socket* socket, void* data, size_t capacity, uint16_t& fromPort)
{
    sockaddr_in from{};
    int fromLength = sizeof(from);
    const int count = recvfrom(socket->native, static_cast<char*>(data), int(capacity), 0,
                               reinterpret_cast<sockaddr*>(&from), &fromLength);
    if (count == SOCKET_ERROR)
        return WSAGetLastError() == WSAENOTSOCK ? -1 : 0;
    fromPort = ntohs(from.sin_port);
    return count;
}

bool Send(Socket* socket, uint16_t toPort, const void* data, size_t bytes)
{
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(toPort);
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return sendto(socket->native, static_cast<const char*>(data), int(bytes), 0,
                  reinterpret_cast<sockaddr*>(&to), sizeof(to)) == int(bytes);
}
}
