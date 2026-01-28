/*
 * PROC1_$DISPATCH - High-level dispatch wrapper
 *
 * This is a C wrapper that calls the assembly dispatcher.
 * The actual context switch is implemented in sau2/dispatch.s
 *
 * On m68k, this simply calls DISPATCH_INT then clears the
 * interrupt mask and returns.
 *
 * Original address: 0x00e20a18
 */

#include "proc1.h"

/*
 * Note: The actual implementation is in sau2/dispatch.s
 * This file exists to document the function and provide
 * a C-callable interface on non-m68k platforms.
 */

#if !defined(ARCH_M68K)

void PROC1_$DISPATCH(void)
{
    PROC1_$DISPATCH_INT();
    /* On m68k, this is followed by: andi #-0x701,SR
     * which clears the interrupt mask. On other platforms,
     * this would need to be handled appropriately. */
}

#endif /* !M68K */
