/*
 * FILE_$MK_PERMANENT - Mark file as permanent
 *
 * Stub function that always returns success. The permanent/temporary
 * distinction may be handled at a higher level (e.g., object manager)
 * rather than in the file system itself.
 *
 * Original address: 0x00E5DBA4
 */

#include "file/file_internal.h"

/*
 * FILE_$MK_PERMANENT
 *
 * Assembly analysis:
 *   00e5dba4    link.w A6,0x0        ; Set up stack frame (no locals)
 *   00e5dba8    movea.l (0xc,A6),A0  ; A0 = status_ret (param 2)
 *   00e5dbac    clr.l (A0)           ; *status_ret = 0 (status_$ok)
 *   00e5dbae    unlk A6
 *   00e5dbb0    rts
 *
 * This is a stub that does nothing except return success.
 * The file_uid parameter is accepted but not used.
 */
void FILE_$MK_PERMANENT(uid_t *file_uid, status_$t *status_ret)
{
    (void)file_uid;  /* Unused - for API compatibility */

    *status_ret = status_$ok;
}
