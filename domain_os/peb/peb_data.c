#include "peb/peb_internal.h"

status_$t PEB_interrupt = status_$peb_interrupt;
status_$t PEB_FPU_Is_Hung_Err = status_$peb_fpu_is_hung | 0x80000000;
status_$t PEB_WCS_Verify_Failed_Err = status_$peb_wcs_verify_failed;

/*
 * Constant pointer cells from the code segment (see peb_internal.h).
 * Original values read from the binary.
 */
void *PTR_PEB_CTL_00e31dd0 = (void *)0x00FF7000;               /* 0x00E31DD0 */
void *PTR_PEB_$WIRED_DATA_START_00e322dc = (void *)0x00E84E80; /* 0x00E322DC */
void *PTR_PEB_$TOUCH_00e322e4 = (void *)0x00E70810;            /* 0x00E322E4 */
