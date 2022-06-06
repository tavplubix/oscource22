#include <inc/lib.h>
#include <inc/string.h>
#include <string.h>

void
usage(void) {
    printf("usage: kill pid signum [sigarg]\n");
    exit();
}

void kek(int) {
    cprintf("kek\n");
}

void
umain(int argc, char **argv) {
    struct sigaction sa;
    sa.sa_handler = kek;
    sigaction(2, &sa, NULL);

    if (argc < 3 || 4 < argc)
        usage();

    long pid = -1;
    long signum = -1;
    pid = strtol(argv[1], NULL, 10);
    signum = strtol(argv[2], NULL, 10);
    long sigarg = 0;
    if (argc == 4)
        sigarg = strtol(argv[3], NULL, 10);

    if (pid < 0 || signum <= 0)
        usage();
    
    union sigval val;
    memset(&val, 0, sizeof(val));
    val.sival_int = sigarg;

    int err = sigqueue(pid, signum, val);
    if (err)
        printf("kill: failed to send signal: %d\n", err);
}
