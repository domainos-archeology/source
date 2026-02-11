/*
 * os_$start_proc2 - Free initialization pages and start proc2
 *
 * Final step of OS initialization. Frees the memory pages used by
 * boot/init code (from 0xE2F000 to 0xE38000 in 0x400-byte pages),
 * exits supervisor mode, and transfers control to FIM_$PROC2_STARTUP.
 *
 * Parameters:
 *   param - Passed through to FIM_$PROC2_STARTUP (stack high address)
 *
 * Original address: 0x00E6D254
 * Size: 70 bytes
 */

#include "os/os_internal.h"

extern void FIM_$PROC2_STARTUP(void *param);

void os_$start_proc2(void *param)
{
    uint32_t addr;

    /* Free init code pages from 0xE2F000 to 0xE38000 (0xE3D37F & ~0x7FFF) */
    for (addr = 0xE2F000; addr < 0xE38000; addr += 0x400) {
        os_$free_va_page(addr);
    }

    /* Exit supervisor mode */
    ACL_$CLEAR_SUPER();

    /* Transfer control to proc2 startup */
    FIM_$PROC2_STARTUP(param);
}
