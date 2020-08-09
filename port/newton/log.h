#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum {
    BLACK = 30,
    RED = 31,
    GREEN = 32,
    YELLOW = 33,
    BLUE = 34,
    MAGENTA = 35,
    CYAN = 36,
    WHITE = 37,
    GRAY = 90
};

#define BRIGHT(color) (color + 60)

void einstein_puts(const char *str);
void einstein_here(int color, const char *func, int line);
void einstein_log(int color, const char *func, int line, const char *fmt, ...);
void einstein_break();

#ifdef __cplusplus
}
#endif
