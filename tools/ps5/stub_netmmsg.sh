#!/bin/sh
# Kernel stand-ins for symbols the SDK libc does not export, pulled in by
# OpenSSL and the net stack:
#   sendmmsg/recvmmsg  - as in bss_dgram; the port's UDP layer goes through the
#                        SceNetSocket compat path in host_udp and never calls them.
#   async_local_cleanup - async.c's teardown hook. OpenSSL is built no-async, so
#                        async_posix.c (the only definition) is not compiled and
#                        there is no fibre state to release.
cat > /root/m4/port-ps5/obj/netmmsg_stub.c <<'EOF'
#include <stddef.h>
struct mmsghdr { void *msg_hdr; unsigned int msg_len; };
int sendmmsg(int, struct mmsghdr*, unsigned int, unsigned int) { return -1; }
int recvmmsg(int, struct mmsghdr*, unsigned int, unsigned int, void*) { return -1; }
void async_local_cleanup(void) { }
EOF
/root/m4/native/ps5-payload-sdk-mihawk/bin/clang -c /root/m4/port-ps5/obj/netmmsg_stub.c -o /root/m4/port-ps5/obj/netmmsg_stub.o
echo STUB_OK
