#pragma once

#include <cstddef>
#include <cstdint>

// Private localhost packet transport for two game instances. Peer IDs and the
// guest's virtual sockets remain in the kernel; only the OS UDP socket lives here.
namespace host_udp {
struct Socket;
Socket* BindLoopback(uint16_t firstPort, uint16_t attempts, uint16_t& boundPort);
// 0 is a transient receive error; -1 means the socket has closed.
int Receive(Socket* socket, void* data, size_t capacity, uint16_t& fromPort);
bool Send(Socket* socket, uint16_t toPort, const void* data, size_t bytes);
}
