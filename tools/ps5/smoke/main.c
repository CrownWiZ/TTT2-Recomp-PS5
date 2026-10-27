/* TTT2 PS5 smoke payload (M3).
 *
 * Minimal on-console proof of the build/load/run cycle:
 *  - shows a notification bubble on the PS5 home screen (user-visible),
 *  - prints one line through sceKernelDebugOutText (UART console),
 *    which M4+ will use for headless debugging.
 * No game assets, no generated code: this file is hand-written.
 *
 * Copyright notice: BSD-style pattern follows the ps5-payload-sdk
 * samples (notify/hello_world); this file is project code (see repo LICENSE).
 */

#include <string.h>

typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);
int sceKernelDebugOutText(int, const char *);

int
main(void) {
  notify_request_t req;

  bzero(&req, sizeof req);
  strncpy(req.message, "TTT2 PS5 smoke: build/load OK", sizeof req.message);
  sceKernelSendNotificationRequest(0, &req, sizeof req, 0);

  sceKernelDebugOutText(0, "[ttt2-smoke] notification sent; ELF alive\n");

  return 0;
}
