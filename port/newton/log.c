#include <stdarg_nwt.h>
#include <stdio.h>
#include <string.h>

__attribute__((naked)) void einstein_puts(const char *str)
{
    asm("stmdb   sp!, {r1, lr}\n"
        "ldr     lr, id0\n"
        "mov     r1, r0\n"
        "mcr     p10, 0, lr, c0, c0\n"
        "ldmia   sp!, {r1, pc}\n"
        "id0:\n"
        ".word      0x11a\n");
}

__attribute__((naked)) void einstein_break()
{
    asm("stmdb   sp!, {lr}\n"
        "ldr     lr, id1\n"
        "mcr     p10, 0, lr, c0, c0\n"
        "ldmia   sp!, {pc}\n"
        "id1:\n"
        ".word      0x116\n");
}

void einstein_here(int color, const char *func, int line)
{
    char buffer[120];
    sprintf(buffer, "\x1b[%dm%s %d\x1b[0m", color, func, line);
    einstein_puts(buffer);
}

void einstein_log(int color, const char *func, int line, const char *fmt, ...)
{
    char buffer[120];
    va_list args;
    sprintf(buffer, "\x1b[%dm%s %d\x1b[0m ", color, func, line);
    va_start(args, fmt);
    vsprintf(buffer + strlen(buffer), fmt, args);
    einstein_puts(buffer);
    va_end(args);
}
