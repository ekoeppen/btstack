#include <stdlib.h>
#include <string.h>
#include "btstack_state.h"
#include "hal_newton.h"

void btstack_hal_init(btstack_state_t *btstack) {
    btstack->hal = calloc(1, sizeof(btstack_hal_state_t));
}

extern unsigned int __rt_udiv(unsigned int a, unsigned int b);
extern int __rt_sdiv(int a, int b);
extern void *NewPtr(unsigned long);

int __aeabi_idiv(int a, int b)
{
    return __rt_sdiv(b, a);
}

unsigned int __aeabi_uidiv(unsigned int a, unsigned int b)
{
    return __rt_udiv(b, a);
}

void hal_cpu_disable_irqs()
{
}

void hal_cpu_enable_irqs()
{
}

void hal_cpu_enable_irqs_and_sleep()
{
}

void* calloc (size_t num, size_t size)
{
    void *p = NewPtr(num * size);
    memset(p, 0, num * size);
    return p;
}
