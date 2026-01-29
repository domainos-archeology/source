/*
 * FILE_$AUDIT_LOCK - Log audit event for file lock/unlock operations
 *
 * Original address: 0x00E5E88A
 * Size: 84 bytes
 *
 * Called when AUDIT_$ENABLED is set during lock/unlock operations.
 * Builds an audit event record and logs it via AUDIT_$LOG_EVENT.
 *
 * The event data consists of the file UID (8 bytes) followed by
 * the lock mode (2 bytes), for a total of 10 bytes.
 *
 * Assembly:
 *   link.w A6,-0x28
 *   move.w (0x10,A6),D0w       ; param_3 (lock_mode)
 *   move.l #0x40009,(-0x10,A6) ; event_uid.high
 *   clr.l (-0xc,A6)            ; event_uid.low = 0
 *   move.w D0w,(-0x18,A6)      ; copy lock_mode to local
 *   movea.l (0xc,A6),A0        ; param_2 (file_uid ptr)
 *   move.l (A0)+,(-0x20,A6)    ; copy file_uid.high
 *   move.l (A0)+,(-0x1c,A6)    ; copy file_uid.low
 *   tst.l (0x8,A6)             ; test param_1 (status)
 *   bne.b set_one
 *   clr.w (-0x26,A6)           ; event_flags = 0
 *   bra.b call_audit
 * set_one:
 *   move.w #0x1,(-0x26,A6)     ; event_flags = 1
 * call_audit:
 *   pea (0x1c,PC)              ; &data_len (constant 0x000A at 0xE5E8DE)
 *   pea (-0x20,A6)             ; &file_uid_copy (data buffer)
 *   pea (0x8,A6)               ; &param_1 (status)
 *   pea (-0x26,A6)             ; &event_flags
 *   pea (-0x10,A6)             ; &event_uid
 *   jsr AUDIT_$LOG_EVENT
 *   unlk A6
 *   rts
 */

#include "file/file_internal.h"

void FILE_$AUDIT_LOCK(status_$t status, uid_t *file_uid, uint16_t lock_mode)
{
    uid_t event_uid;
    uint16_t event_flags;

    /*
     * Audit event data: file UID (8 bytes) + lock mode (2 bytes)
     * Laid out contiguously on stack in original assembly.
     */
    struct {
        uid_t    uid_copy;   /* 8 bytes */
        uint16_t mode;       /* 2 bytes */
    } event_data;

    /* Data length constant (10 bytes = sizeof(uid_t) + sizeof(uint16_t)) */
    uint16_t data_len = 10;

    /* Event UID for file lock audit events: {0x00040009, 0x00000000} */
    event_uid.high = 0x00040009;
    event_uid.low  = 0x00000000;

    /* Copy file UID and lock mode into event data buffer */
    event_data.uid_copy.high = file_uid->high;
    event_data.uid_copy.low  = file_uid->low;
    event_data.mode = lock_mode;

    /* Event flags: 0 if status is ok (zero), 1 if non-zero (error) */
    event_flags = (status != 0) ? 1 : 0;

    /* Log the audit event */
    AUDIT_$LOG_EVENT(&event_uid, &event_flags, &status,
                     (char *)&event_data, &data_len);
}
