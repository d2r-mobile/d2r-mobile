// #234 producer/consumer name check against a real no-game probe output.
#include "../native/target_guard.h"
#include <stdio.h>
int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s <probe.fixups.tsv>\n", argv[0]); return 2; }
    char error[160];
    if (dm_android_target_validate(argv[1], error, sizeof error)) {
        fprintf(stderr, "FAIL: %s\n", error);
        return 1;
    }
    puts("PASS: real probe producer's probe.target accepted by the module-specific consumer");
    return 0;
}
