#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void nwt_log(const char *str);
void nwt_logf(const char *fmt, ...);
void LH(const char *func, int line, int v);
void nwt_break();

#ifdef __cplusplus
}
#endif
