#include <inc/vsyscall.h>
#include <inc/lib.h>

static inline uint64_t
vsyscall(int num) {
    // LAB 12: Your code here
    switch (num) {
    case VSYS_gettime:
        return vsys[VSYS_gettime];
    default:
        return -E_NO_SYS;
    }
}

int
vsys_gettime(void) {
    return vsyscall(VSYS_gettime);
}
