#include "setup/mobile_install.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    if (argc != 4) { fprintf(stderr, "Usage: ttt2_extract disc.iso TU3 NEW_DESTINATION\n"); return 2; }
    int iso = open(argv[1], O_RDONLY), tu = open(argv[2], O_RDONLY);
    std::string error;
    int last = -1;
    const char* stop = std::getenv("TTT2_SETUP_CANCEL_AT");
    int cancelAt = stop ? std::atoi(stop) : 101;
    bool ok = ttt2::setup::InstallFromDescriptors(iso, tu, argv[3], [&](int pct, const std::string& msg) {
        if (pct != last) { fprintf(stderr, "%d%% %s\n", pct, msg.c_str()); last = pct; }
        return pct < cancelAt;
    }, error);
    close(iso); close(tu);
    if (!ok) fprintf(stderr, "%s\n", error.c_str());
    return ok ? 0 : 1;
}
