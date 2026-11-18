// SPDX-License-Identifier: GPL-2.0-or-later
#define _GNU_SOURCE
#include "../src/android_cpu_policy.h"
#include <assert.h>
#include <sys/types.h>

int main(void) {
    char path[] = "/tmp/tlou-native-pid-XXXXXX";
    const int fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    assert(setenv("TLOU_NATIVE_PID_FILE", path, 1) == 0);

    // Native publication must replace any stale wrapper PID.
    FILE *file = fopen(path, "w");
    assert(file != NULL);
    assert(fputs("1\n", file) >= 0);
    assert(fclose(file) == 0);
    tlou_cpu_publish_native_pid();

    file = fopen(path, "r");
    assert(file != NULL);
    long published = -1;
    assert(fscanf(file, "%ld", &published) == 1);
    assert(fclose(file) == 0);
    assert(published == (long)getpid());

    // No env variable: do not create or modify any file.
    assert(unsetenv("TLOU_NATIVE_PID_FILE") == 0);
    tlou_cpu_publish_native_pid();
    file = fopen(path, "r");
    assert(file != NULL);
    long unchanged = -1;
    assert(fscanf(file, "%ld", &unchanged) == 1);
    assert(fclose(file) == 0);
    assert(unchanged == (long)getpid());
    assert(unlink(path) == 0);
    puts("TLOU native PID publication tests passed");
    return 0;
}
