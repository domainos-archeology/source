#include "base/base.h"

status_$t PEB_interrupt = status_$peb_interrupt;
status_$t PEB_FPU_Is_Hung_Err = status_$peb_fpu_is_hung | 0x80000000h;
status_$t PEB_WCS_Verify_Failed_Err = status_$peb_wcs_verify_failed;