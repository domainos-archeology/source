/*
 * mac_os/copy_buffer_data.c - MAC_OS_$COPY_BUFFER_DATA
 *
 * Copies the next `length` bytes of the caller's buffer chain into a
 * destination address, advancing through the chain as buffers are used up.
 *
 * Original address: 0x00E0B522
 * Size: 132 bytes
 *
 * This is a nested Pascal procedure of MAC_OS_$SEND (0x00E0B5A8).  It reads
 * the parent frame through the static link:
 *
 *   00e0b52e  movea.l (A6),A2            ; A2 = MAC_OS_$SEND's frame pointer
 *   ...       movea.l (-0x1c,A2),A0      ; the current chain entry
 *   ...       move.w  (-0x36,A2),D0w     ; the offset within that entry
 *
 * and it writes both back, so the two uplevel variables are threaded here as
 * explicit by-reference parameters rather than copied by value.  MAC_OS_$SEND
 * initialises them at 0x00E0B6AE / 0x00E0B6B2 (chain = &pkt_desc->hdr_desc,
 * offset = 0) just before the first call.
 *
 * Assembly (0x00E0B522):
 *   link.w   A6,-0x10
 *   movem.l  {A2 D5 D4 D3 D2},-(SP)
 *   move.w   (0xc,A6),D0w        ; length
 *   movea.l  (A6),A2             ; static link -> MAC_OS_$SEND frame
 *   movea.l  (0x8,A6),A0         ; dest_ptr
 *   move.w   D0w,D3w             ; D3 = bytes still to copy
 *   move.l   (A0),D4             ; D4 = *dest_ptr, read ONCE
 *   bra.b    check
 * loop:
 *   move.w   D3w,D5w ; ext.l D5                    ; want = remaining
 *   move.w   (-0x36,A2),D0w ; ext.l D0             ; offset
 *   movea.l  (-0x1c,A2),A0
 *   move.l   (A0),D1 ; sub.l D0,D1                 ; avail = entry.length - offset
 *   cmp.l    D1,D5 ; ble.b +2 ; move.l D1,D5       ; want = min(want, avail)
 *   move.w   (-0x36,A2),D1w ; ext.l D1
 *   movea.l  (-0x1c,A2),A0
 *   add.l    (0x4,A0),D1                           ; src = entry.address + offset
 *   move.l   D1,(-0xc,A6)
 *   move.w   D5w,D2w ; ext.l D2
 *   move.l   D2,-(SP) ; move.l D4,-(SP) ; move.l D1,-(SP)
 *   jsr      OS_$DATA_COPY                         ; (src, dest, count)
 *   lea      (0xc,SP),SP
 *   add.l    D2,D4                                 ; dest += count
 *   sub.w    D5w,D3w
 *   bne.b    next_entry
 *   add.w    D5w,(-0x36,A2)                        ; done: offset += count
 *   bra.b    check
 * next_entry:
 *   clr.w    (-0x36,A2)
 *   movea.l  (-0x1c,A2),A0
 *   move.l   (0x8,A0),(-0x1c,A2)                   ; chain = chain->next
 * check:
 *   tst.w    D3w ; beq.b done
 *   tst.l    (-0x1c,A2) ; bne.b loop
 * done:
 *   movem.l  (-0x24,A6),{D2 D3 D4 D5 A2}
 *   unlk     A6
 *   rts
 *
 * Two details worth keeping:
 *
 *  - The running destination address lives only in D4.  It is loaded from
 *    *dest_ptr at entry and never stored back, so the caller's pointer is
 *    unchanged on return.  MAC_OS_$SEND relies on that.
 *  - The "advance the chain" arm runs when bytes remain (`bne`), and the
 *    "bump the offset" arm when the request has been satisfied.  A buffer
 *    that is exactly consumed by the last chunk therefore leaves the offset
 *    at the end of that entry rather than moving to the next one.
 */

#include "mac_os/mac_os_internal.h"
#include "os/os.h"
#include "arch/arch.h"

void MAC_OS_$COPY_BUFFER_DATA(uint32_t *dest_ptr, int16_t length,
                              mac_os_$buf_desc_t **chain, int16_t *offset)
{
    int16_t remaining;      /* D3w */
    uint32_t dest;          /* D4 */
    int32_t want;           /* D5 */
    int32_t avail;          /* D1 */
    uint32_t src;           /* D1, then the local at A6-0xc */
    int32_t count;          /* D2 */

    remaining = length;
    dest = *dest_ptr;

    while (remaining != 0 && *chain != NULL) {
        want = remaining;

        avail = (*chain)->length - (int32_t)*offset;
        if (want > avail) {
            want = avail;
        }

        src = (*chain)->address + (uint32_t)(int32_t)*offset;

        count = (int32_t)(int16_t)want;
        OS_$DATA_COPY(ARCH_VA_TO_PTR(src), ARCH_VA_TO_PTR(dest), (uint32_t)count);

        dest += (uint32_t)count;
        remaining = (int16_t)(remaining - (int16_t)want);

        if (remaining == 0) {
            *offset = (int16_t)(*offset + (int16_t)want);
        } else {
            *offset = 0;
            *chain = (mac_os_$buf_desc_t *)ARCH_VA_TO_PTR((*chain)->next);
        }
    }
}
