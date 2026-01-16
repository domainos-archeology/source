/*
 * FILE_$MK_TEMPORARY - Mark file as temporary
 *
 * Stub function that always returns success. The permanent/temporary
 * distinction may be handled at a higher level (e.g., object manager)
 * rather than in the file system itself.
 *
 * Original address: 0x00E5DBB2
 */

#include "file/file_internal.h"

/*
 * FILE_$MK_TEMPORARY
 *
 * Assembly analysis:
 *   00e5dbb2    link.w A6,0x0        ; Set up stack frame (no locals)
 *   00e5dbb6    movea.l (0xc,A6),A0  ; A0 = status_ret (param 2)
 *   00e5dbba    clr.l (A0)           ; *status_ret = 0 (status_$ok)
 *   00e5dbbc    unlk A6
 *   00e5dbbe    rts
 *
 * This is a stub that does nothing except return success.
 * The file_uid parameter is accepted but not used.
 */
void FILE_$MK_TEMPORARY(uid_t *file_uid, status_$t *status_ret)
{
    (void)file_uid;  /* Unused - for API compatibility */

    *status_ret = status_$ok;
}
