/*
 * FILE_$SET_AUDITED - Set file audit flags
 *
 * Sets the audit flags for a file. Requires audit administrator privileges.
 * The two boolean parameters control different aspects of auditing.
 *
 * Original address: 0x00E5DBE8
 */

#include "file/file_internal.h"

/*
 * FILE_$SET_AUDITED
 *
 * Assembly analysis:
 *   00e5dbe8    link.w A6,-0x38       ; 56-byte local buffer
 *   00e5dbec    pea (A2)              ; Save A2
 *   00e5dbee    movea.l (0x14,A6),A2  ; A2 = status_ret (param 4)
 *   00e5dbf2    clr.w (-0x38,A6)      ; local_3c[0..1] = 0
 *   00e5dbf6    movea.l (0xc,A6),A0   ; A0 = param_2
 *   00e5dbfa    tst.b (A0)            ; Check *param_2
 *   00e5dbfc    bpl.b 0x00e5dc04      ; Skip if >= 0
 *   00e5dbfe    move.w #0x1,(-0x38,A6); Set bit 0 if param_2 < 0
 *   00e5dc04    movea.l (0x10,A6),A1  ; A1 = param_3
 *   00e5dc08    tst.b (A1)            ; Check *param_3
 *   00e5dc0a    bpl.b 0x00e5dc12      ; Skip if >= 0
 *   00e5dc0c    bset.b #0x1,(-0x37,A6); Set bit 1 if param_3 < 0
 *   00e5dc12    pea (A2)              ; Push status_ret
 *   00e5dc14    jsr 0x00e714b6.l      ; Call AUDIT_$ADMINISTRATOR
 *   00e5dc1a    addq.w #0x4,SP
 *   00e5dc1c    tst.b D0b             ; Check return value
 *   00e5dc1e    bpl.b 0x00e5dc3a      ; Skip if not admin
 *   00e5dc20    [call FILE_$SET_ATTRIBUTE with attr_id=0xD, flags=0xFFFF]
 *   00e5dc3a    movea.l (-0x3c,A6),A2 ; Restore A2
 *   00e5dc3e    unlk A6
 *   00e5dc40    rts
 *
 * The function:
 * 1. Builds a 16-bit audit flags value from two boolean parameters
 * 2. Checks if caller has audit administrator privileges
 * 3. If admin, calls FILE_$SET_ATTRIBUTE with attribute ID 0xD (13)
 */
void FILE_$SET_AUDITED(uid_t *file_uid, int8_t *param_2, int8_t *param_3,
                       status_$t *status_ret)
{
    uint16_t audit_flags[28];  /* 56-byte buffer, used as uint16_t for first word */
    int8_t is_admin;

    /* Build audit flags from the two boolean parameters */
    audit_flags[0] = 0;

    /* If param_2 is negative, set bit 0 */
    if (*param_2 < 0) {
        audit_flags[0] = 1;
    }

    /* If param_3 is negative, set bit 1 */
    if (*param_3 < 0) {
        audit_flags[0] |= 2;
    }

    /* Check if caller has audit administrator privileges */
    is_admin = AUDIT_$ADMINISTRATOR(status_ret);

    /* Only proceed if caller is an audit administrator */
    if (is_admin < 0) {
        /* Set attribute 0xD (audited flags) */
        FILE_$SET_ATTRIBUTE(file_uid, FILE_ATTR_AUDITED, audit_flags,
                            FILE_FLAGS_AUDITED_MASK, status_ret);
    }
}
