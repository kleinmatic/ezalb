/* host/screen/headless.rs — infinite CPU step loop (Ctrl-C is the exit). */
#include "host/host.h"

size_t screen_headless_run(machine m, i8051_cpu *cpu)
{
    for (;;)
        m.step(m.sys, cpu);
    return m.count(m.sys); /* unreachable; keeps the Rust signature */
}
