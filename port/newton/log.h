#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void einstein_puts(const char *str);
void einstein_here(int color, const char *func, int line);
void einstein_log(int color, const char *func, int line, const char *fmt, ...);
void einstein_break();

#ifdef __cplusplus
}
#endif
