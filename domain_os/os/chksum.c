// OS_$CHKSUM - Calculate checksum (stub implementation)
// Address: 0x00e6d698
// Size: 32 bytes
//
// The body is a stub: it clears the caller's enable flag and returns
// status_$ok.  The three leading arguments are never read.  A5 is saved,
// pointed at OS_$BOOT_DEVICE (0xE82728) and restored, which is the module
// prologue the real implementation would have needed.

#include "os/os_internal.h"

void OS_$CHKSUM(void *param_1, void *param_2, void *param_3,
                char *enable, status_$t *status_ret)
{
    (void)param_1; /* the first three arguments are never read */
    (void)param_2;
    (void)param_3;

    *status_ret = status_$ok; /* 0x00E6D6A4: clr.l (A0), arg at (0x18,A6) */
    *enable = 0;              /* 0x00E6D6AA: clr.b (A1), arg at (0x14,A6) */
}
