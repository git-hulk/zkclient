#include "util.h"
#include <time.h>

int32_t atomic_inc(volatile int32_t* operand, int incr) {
    int32_t result;
    asm __volatile__(
            "lock xaddl %0,%1\n"
            : "=r"(result), "=m"(*(int *)operand)
            : "0"(incr)
            : "memory");
    return result;
}

__attribute__((constructor)) int32_t get_xid() {
    static int32_t xid = -1; 
    if (xid == -1) {
        xid = time(0);
    }   
    return atomic_inc(&xid,1);
}
