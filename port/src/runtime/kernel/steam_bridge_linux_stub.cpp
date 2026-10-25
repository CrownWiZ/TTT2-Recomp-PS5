#include "steam_net.h"

// Steam's helper process is optional. The native game's local UDP transport
// remains available when no Linux Steamworks helper is packaged.
namespace steamnet {
bool Start(Receiver) { return false; }
bool Running() { return false; }
uint64_t LocalId() { return 0; }
bool Send(uint64_t, bool, const void*, size_t) { return false; }
void PublishLobby(uint64_t, const std::vector<uint8_t>&) {}
void RemoveLobby(uint64_t) {}
std::vector<std::vector<uint8_t>> ListLobbies(int, int) { return {}; }
bool TakeJoinRequest(std::vector<uint8_t>&) { return false; }
int RunBridge(int, char**) { return 2; }
std::vector<FriendInfo> Friends(int) { return {}; }
bool InviteToSession(uint64_t, uint64_t, int) { return false; }
bool WebApiTicket(const std::string&, std::string& hex, uint32_t& handle, int) { hex.clear(); handle = 0; return false; }
void CancelTicket(uint32_t) {}
} // namespace steamnet
