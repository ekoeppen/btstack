#include <stdarg.h>
#include <stdio.h>

void nwt_log(const char *str)
{
    printf(str);
}

void nwt_logf(const char *fmt, ...)
{
    va_list args;
    vprintf(fmt, args);
}
