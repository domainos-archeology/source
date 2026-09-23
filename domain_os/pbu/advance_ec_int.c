/*
 * PBU_$ADVANCE_EC_INT - Advance a PBU eventcount from interrupt level
 *
 * Validates that the caller's eventcount id lies in the PBU range
 * 0x101..0x120 and that the owner word of the selected record matches the
 * caller's owner word, then advances the record's level-1 eventcount
 * without dispatching.  The status is preset to
 * status_$ec2_bad_event_count and only cleared on the success path.
 *
 * Original address: 0x00E88400, size 96 bytes (SAU2 map: "I E88400 EC2_PBU
 * size = 60", the only routine in PBU_WIRED_PROC).  A5 = 0xE88460 =
 * EC2_$PBU_ECS, the 32 x 0x18-byte record pool that follows the code.
 *
 *   00e88400    link.w A6,-0xc
 *   00e88404    movem.l {A5 A3 A2 D2},-(SP)
 *   00e88408    lea (0xe88460).l,A5        ; A5 = EC2_$PBU_ECS
 *   00e8840e    movea.l (0x10,A6),A0       ; arg 3 = status_ret
 *   00e88412    move.l #0x180004,(A0)      ; *status_ret = ec2 bad event count
 *   00e88418    movea.l (0xc,A6),A1        ; arg 2 = ec_index_ptr
 *   00e8841c    move.l (A1),D0             ; D0 = *ec_index_ptr (32-bit)
 *   00e8841e    cmpi.l #0x101,D0
 *   00e88424    bcs.b 0x00e88456           ; unsigned < 0x101 -> exit
 *   00e88426    cmpi.l #0x120,D0
 *   00e8842c    bhi.b 0x00e88456           ; unsigned > 0x120 -> exit
 *   00e8842e    subi.w #0x101,D0w          ; 16-bit record number 0..31
 *   00e88432    movea.l (0x8,A6),A3        ; arg 1 = owner_id_ptr
 *   00e88436    move.w D0w,D1w
 *   00e88438    lsl.w #0x3,D1w             ; n*8
 *   00e8843a    move.w D1w,D2w
 *   00e8843c    add.w D2w,D2w              ; n*16
 *   00e8843e    add.w D2w,D1w              ; n*24 (16-bit)
 *   00e88440    lea (0x0,A5,D1w*0x1),A2    ; A2 = &EC2_$PBU_ECS[n] (word index)
 *   00e88444    move.w (0xc,A2),D1w        ; record owner word
 *   00e88448    cmp.w (A3),D1w
 *   00e8844a    bne.b 0x00e88456           ; owner mismatch -> exit
 *   00e8844c    clr.l (A0)                 ; *status_ret = status_$ok
 *   00e8844e    pea (A2)
 *   00e88450    jsr 0x00e20718.l           ; EC_$ADVANCE_WITHOUT_DISPATCH(&rec->ec)
 *   00e88456    movem.l (-0x1c,A6),{D2 A2 A3 A5}
 *   00e8845c    unlk A6
 *   00e8845e    rts
 */

#include "pbu/pbu_internal.h"

void PBU_$ADVANCE_EC_INT(
    int16_t *owner_id_ptr,
    uint32_t *ec_index_ptr,
    status_$t *status_ret)
{
    uint32_t ec_index;
    int16_t record_offset;
    pbu_ec_entry_t *entry;

    /* 0x00E8840E-0x00E88412: preset the failure status */
    *status_ret = status_$ec2_bad_event_count;

    /* 0x00E88418-0x00E8842C: unsigned range check 0x101..0x120 */
    ec_index = *ec_index_ptr;
    if (ec_index < PBU_EC_INDEX_MIN) {
        return;
    }
    if (ec_index > PBU_EC_INDEX_MAX) {
        return;
    }

    /*
     * 0x00E8842E-0x00E88440: (n * 8) + (n * 16) in 16-bit arithmetic, then a
     * sign-extended word index from the pool base.  n is at most 31, so the
     * product is at most 0x2E8 and never wraps.
     */
    record_offset = (int16_t)((int16_t)(ec_index - PBU_EC_INDEX_MIN) * (int16_t)sizeof(pbu_ec_entry_t));
    entry = (pbu_ec_entry_t *)(void *)((uint8_t *)PBU_$EC_ARRAY + record_offset);

    /* 0x00E88444-0x00E8844A: owner word at +0xC must equal *owner_id_ptr */
    if (entry->owner_id != *owner_id_ptr) {
        return;
    }

    /* 0x00E8844C-0x00E88450 */
    *status_ret = status_$ok;
    EC_$ADVANCE_WITHOUT_DISPATCH(&entry->ec);
}
