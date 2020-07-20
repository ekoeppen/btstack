#include <stdarg.h>
#include <stdio.h>

__attribute__((naked)) void nwt_log(const char *str)
{
    asm("stmdb   sp!, {r1, lr}\n"
        "ldr     lr, id0\n"
        "mov     r1, r0\n"
        "mcr     p10, 0, lr, c0, c0\n"
        "ldmia   sp!, {r1, pc}\n"
        "id0:\n"
        ".word      0x11a\n");
}

__attribute__((naked)) void nwt_break()
{
    asm("stmdb   sp!, {lr}\n"
        "ldr     lr, id1\n"
        "mcr     p10, 0, lr, c0, c0\n"
        "ldmia   sp!, {pc}\n"
        "id1:\n"
        ".word      0x116\n");
}

void nwt_logf(const char *fmt, ...)
{
    char buffer[80];
    va_list args;
    va_start(args, fmt);
    vsprintf(buffer, fmt, args);
    nwt_log(buffer);
    va_end(args);
}

void LH(const char *func, int line, int v)
{
    char buffer[80];
    sprintf(buffer, "%s %d %d", func, line, v);
    nwt_log(buffer);
}

void LHC(int color, const char *func, int line, int v)
{
    char buffer[80];
    sprintf(buffer, "\x1b[%dm%s %d %d\x1b[0m", color, func, line, v);
    nwt_log(buffer);
}
