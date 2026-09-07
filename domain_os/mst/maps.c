/*
 * MST_$MAPS - Map an object into the address space, searching from the top
 *
 * Original address: 0x00E43982
 * Size: 82 bytes
 *
 * The routine has no logic of its own: it reloads the module data base into
 * A5 (`lea (0xe7cf0c).l,A5` at 0x00E43988 - the MST_UNWIRED data segment) and
 * forwards its ten arguments, plus the constant address hint 0x7FFFFFFF and
 * the global MST_$TOUCH_COUNT, to mst_$alloc_segs.
 *
 * Assembly:
 *   00e43982    link.w A6,-0x4
 *   00e43986    pea (A5)
 *   00e43988    lea (0xe7cf0c).l,A5
 *   00e4398e    subq.l #0x2,SP            ; 2-byte gap above the arguments
 *   00e43990    move.l (0x24,A6),-(SP)    ; status
 *   00e43994    move.l (0x20,A6),-(SP)    ; map_info
 *   00e43998    move.b (0xa,A6),-(SP)     ; direction   (BYTE, argument 2)
 *   00e4399c    move.b (0x1e,A6),-(SP)    ; access_rights (BYTE, argument 8)
 *   00e439a0    move.w (0x00e24448).l,-(SP)  ; MST_$TOUCH_COUNT
 *   00e439a6    move.w (0x18,A6),-(SP)    ; area_id
 *   00e439aa    move.w (0x8,A6),-(SP)     ; asid
 *   00e439ae    move.l (0x1a,A6),-(SP)    ; area_size
 *   00e439b2    move.l (0x14,A6),-(SP)    ; length
 *   00e439b6    move.l (0x10,A6),-(SP)    ; start_va
 *   00e439ba    move.l (0xc,A6),-(SP)     ; uid
 *   00e439be    move.l #0x7fffffff,-(SP)  ; addr_hint = search from the top
 *   00e439c4    bsr.w 0x00e43182          ; mst_$alloc_segs
 *   00e439c8    move.l A0,(-0x4,A6)       ; keep the A0 result
 *   00e439cc    movea.l (-0x8,A6),A5
 *   00e439d0    unlk A6
 *   00e439d2    rts
 *
 * Argument 2 (A6+0x0A) and argument 8 (A6+0x1E) are Domain Pascal BOOLEANs.
 * Each is read with `move.b` from the EVEN byte of its word slot, which is
 * where `st -(SP)` / `move.b Dn,-(SP)` put it: those forms decrement A7 by
 * two (to keep the stack word aligned) and then write the byte at the new,
 * even, A7.  Every one of the fifteen call sites in the image pushes both of
 * them that way, so both are passed as plain booleans (0xFF == true).
 *
 * mst_$alloc_segs reads the forwarded copies the same way - `tst.b (0x22,A6)`
 * at 0x00E43234/0x00E43288 for access_rights and `tst.b (0x24,A6)` at
 * 0x00E432F0/0x00E4331A for direction - so the byte survives byte-for-byte
 * from caller to final consumer.
 *
 * The `subq.l #0x2,SP` at 0x00E4398E reserves a two-byte slot ABOVE
 * mst_$alloc_segs' last argument (the callee never reads it); it is the
 * Pascal function-result slot for a routine that in fact returns in A0, and
 * it has no C counterpart.
 */

#include "mst/mst_internal.h"

void *MST_$MAPS(int16_t asid, boolean direction, uid_t *uid, uint32_t start_va,
                uint32_t length, int16_t area_id, uint32_t area_size,
                boolean access_rights, void *map_info, status_$t *status)
{
    return mst_$alloc_segs(0x7fffffff,       /* 0x00E439BE: search from top */
                           uid,
                           start_va,
                           length,
                           area_size,
                           asid,
                           (uint16_t)area_id,
                           MST_$TOUCH_COUNT, /* 0x00E439A0 */
                           access_rights,    /* 0x00E4399C: BYTE */
                           direction,        /* 0x00E43998: BYTE */
                           map_info,
                           status);
}
