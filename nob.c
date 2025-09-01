#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    Cmd cmd = {0};

    // build example
    cmd_append(&cmd, "gcc", "example.c", "-Wall", "-Wextra", "-Wpedantic");

    cmd_append(&cmd, "-O0", "-g", "-ggdb");

    cmd_append(&cmd, "-o", "example");

    if (!cmd_run_sync_and_reset(&cmd)) {
        nob_log(NOB_ERROR, "unable to compile example program");
        return 1;
    }

    cmd_append(&cmd, "gcc", "tester.c", "-Wall", "-Wextra", "-Wpedantic");
    cmd_append(&cmd, "-O0", "-g", "-ggdb");
    cmd_append(&cmd, "-o", "tester");

    if (!cmd_run_sync_and_reset(&cmd)) {
        nob_log(NOB_ERROR, "unable to compile test program");
        return 1;
    }

    return 0;
}