/*
 * PACCT_$ON - Check if process accounting is enabled
 *
 * Returns true (-1) if accounting is enabled, false (0) otherwise.
 *
 * Accounting is considered enabled when pacct_owner != UID_$NIL.
 *
 * Original address: 0x00E5A9A4
 * Size: 38 bytes
 *
 * Assembly:
 *   link.w A6,-0x4
 *   pea (A5)
 *   lea (0xe817ec).l,A5        ; A5 = &pacct_owner
 *   lea (A5),A0
 *   movea.l #0xe1737c,A1       ; A1 = &UID_$NIL
 *   moveq #0x1,D0
 *   cmpm.l (A1)+,(A0)+         ; Compare high words
 *   bne.b not_equal
 *   cmpm.l (A1)+,(A0)+         ; Compare low words
 *   not_equal:
 *   sne D0b                    ; D0 = (pacct_owner != UID_$NIL) ? -1 : 0
 *   movea.l (-0x8,A6),A5
 *   unlk A6
 *   rts
 */

#include "pacct_internal.h"

boolean PACCT_$ON(void)
{
    /*
     * Return true if accounting owner is not nil
     * Using the Domain/OS boolean convention: -1 = true, 0 = false
     */
    if (pacct_owner.high != UID_$NIL.high ||
        pacct_owner.low != UID_$NIL.low) {
        return true;    /* -1 */
    }
    return false;       /* 0 */
}
