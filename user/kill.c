#include <inc/lib.h>
#include <inc/string.h>
#include <string.h>

void
usage(void) {
    printf("usage: kill pid signum [sigarg]\n");
    exit();
}

void
umain(int argc, char **argv) {
    if (argc < 3 || 4 < argc)
        usage();

    long pid = strtol(argv[1], NULL, 10);
    long signum = strtol(argv[2], NULL, 10);
    long sigarg = 0;
    if (argc == 4)
        sigarg = strtol(argv[3], NULL, 10);

    if (!pid || !signum)
        usage();
    
    union sigval val;
    memset(&val, 0, sizeof(val));
    val.sival_int = sigarg;

    int err = sigqueue(pid, signum, val);
    if (err)
        printf("kill: failed to send signal: %d\n", err);
}
