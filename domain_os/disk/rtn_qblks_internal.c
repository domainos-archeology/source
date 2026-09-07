/*
 * disk_$rtn_qblks_internal - Return queue blocks to the disk free pool
 *
 * Puts a chain of queue blocks back on the module free list and wakes as
 * many queued read requests as the returned blocks can satisfy.  The
 * "waiter" the old comment mentioned is really the first block of the chain
 * being returned; comparing it with the reserve block is how the write-mode
 * reserve block is recognised on its way back.
 *
 * Original address: 0x00e3c01a
 * Size: 170 bytes
 *
 * A5 is not loaded here: the function inherits it from its callers, all of
 * which use the disk module base 0x00E7A1CC (DISK_$DATA) --
 * DISK_$RTN_QBLKS establishes it with `lea (0xe7a1cc).l,A5` at 0x00E3C0CA,
 * and the three call sites in DISK_IO / disk_$wait_io (0x00E3D388,
 * 0x00E3D500, 0x00E3D944) are in routines that do the same.  So every
 * A5-relative cell below is a module global, not per-process data.
 *
 * Assembly (0x00e3c01a):
 *   link.w   A6,-0x4
 *   movem.l  {A2 D3 D2},-(SP)
 *   move.w   (0x8,A6),D2w           ; count
 *   move.l   (0xa,A6),D3            ; first block of the returned chain
 *   movea.l  (0xe,A6),A2            ; last block of the returned chain
 *   pea      (0x90,A5) ; jsr ML_$EXCLUSION_START
 *   cmp.l    (0xbc,A5),D3           ; DMOD_RESERVE_BLOCK
 *   bne.b    normal
 *   st       (0xafc,A5)             ; DMOD_RESERVE_AVAIL = true
 *   tst.w    (0xaf6,A5)             ; DMOD_PENDING_COUNT
 *   bne.b    unlock
 *   pea      (A5) ; jsr EC_$ADVANCE ; the module eventcount is at A5+0
 *   bra.b    unlock
 * normal:
 *   move.l   (0xc0,A5),(0x8,A2)     ; last->free_next = DMOD_FREE_HEAD
 *   move.l   D3,(0xc0,A5)           ; DMOD_FREE_HEAD  = first
 *   add.w    D2w,(0xaf8,A5)         ; DMOD_AVAIL_COUNT += count
 *   bra.b    check
 * serve:
 *   move.w   (0xaf2,A5),D0w ; ext.l D0 ; add.l D0,D0
 *   move.w   (0xe,A5,D0*0x1),D1w    ; DMOD_REQ_QUEUE[read_idx]
 *   sub.w    D1w,(0xaf8,A5)         ; avail -= that request's block count
 *   subq.w   #0x1,(0xaf6,A5)        ; one fewer pending request
 *   pea      (A5) ; jsr EC_$ADVANCE
 *   cmpi.w   #0x40,(0xaf2,A5)
 *   bne.b    bump
 *   move.w   #0x1,(0xaf2,A5)        ; wrap 64 -> 1
 *   bra.b    check
 * bump:
 *   addq.w   #0x1,(0xaf2,A5)
 * check:
 *   tst.w    (0xaf6,A5) ; ble.b unlock
 *   move.w   (0xaf2,A5),D0w ; ext.l D0 ; add.l D0,D0
 *   move.w   (0xe,A5,D0*0x1),D1w
 *   cmp.w    (0xaf8,A5),D1w         ; request size <= available?
 *   ble.b    serve
 * unlock:
 *   pea      (0x90,A5) ; jsr ML_$EXCLUSION_STOP
 *   movem.l  (-0x10,A6),{D2 D3 A2}
 *   unlk     A6
 *   rts
 *
 * The request queue is ONE-BASED: the index register is doubled and added to
 * A5+0x0E, so read index k names the word at A5+0x0E+2k, and the index wraps
 * from 0x40 back to 1.  DMOD_REQ_QUEUE carries that bias in its offset.
 *
 * Parameters:
 *   count - number of blocks in the chain being returned
 *   first - first block of the chain (equal to the reserve block when the
 *           write-mode reserve is what is coming back)
 *   last  - last block of the chain; its free_next link is rewritten
 */

#include "disk/disk_internal.h"
#include "ec/ec.h"
#include "ml/ml.h"
#include "arch/arch.h"

/*
 * DMOD_RESERVE_BLOCK (0x0BC) and DMOD_FREE_HEAD (0x0C0) are four bytes
 * apart, and DISK_QBLK_FREE_NEXT (0x08) sits four bytes below
 * DISK_QBLK_STATUS: these cells hold 32-bit target addresses, not host
 * pointers.  They are read and written as uint32_t and converted with
 * ARCH_VA_TO_PTR / ARCH_PTR_TO_VA, which are identity casts on m68k, so a
 * 64-bit host build does not overrun the neighbouring field.
 */

void disk_$rtn_qblks_internal(int16_t count, void *first, void *last)
{
    uint8_t *data = DISK_$DATA;
    uint32_t first_va = ARCH_PTR_TO_VA(first);

    ML_$EXCLUSION_START((ml_$exclusion_t *)(data + DMOD_EXCLUSION));

    if (first_va == *(uint32_t *)(data + DMOD_RESERVE_BLOCK)) {
        /* The write-mode reserve block is back. */
        *(int8_t *)(data + DMOD_RESERVE_AVAIL) = -1;    /* st: 0xFF */

        if (*(int16_t *)(data + DMOD_PENDING_COUNT) == 0) {
            EC_$ADVANCE((ec_$eventcount_t *)(data + DMOD_EVENTCOUNT));
        }
    } else {
        /* Push the chain onto the free list. */
        *(uint32_t *)((uint8_t *)last + DISK_QBLK_FREE_NEXT) =
            *(uint32_t *)(data + DMOD_FREE_HEAD);
        *(uint32_t *)(data + DMOD_FREE_HEAD) = first_va;
        *(int16_t *)(data + DMOD_AVAIL_COUNT) =
            (int16_t)(*(int16_t *)(data + DMOD_AVAIL_COUNT) + count);

        /*
         * Wake every queued request the free list can now satisfy.  The
         * loop test is at the bottom in the original, so a queue that is
         * empty or whose head asks for more blocks than are available is
         * never entered.
         */
        while (*(int16_t *)(data + DMOD_PENDING_COUNT) > 0) {
            int16_t read_idx = *(int16_t *)(data + DMOD_REQ_READ_IDX);
            int16_t wanted = ((int16_t *)(data + DMOD_REQ_QUEUE))[read_idx];

            if (wanted > *(int16_t *)(data + DMOD_AVAIL_COUNT)) {
                break;
            }

            *(int16_t *)(data + DMOD_AVAIL_COUNT) =
                (int16_t)(*(int16_t *)(data + DMOD_AVAIL_COUNT) - wanted);
            *(int16_t *)(data + DMOD_PENDING_COUNT) =
                (int16_t)(*(int16_t *)(data + DMOD_PENDING_COUNT) - 1);

            EC_$ADVANCE((ec_$eventcount_t *)(data + DMOD_EVENTCOUNT));

            if (*(int16_t *)(data + DMOD_REQ_READ_IDX) == DMOD_REQ_QUEUE_SIZE) {
                *(int16_t *)(data + DMOD_REQ_READ_IDX) = 1;
            } else {
                *(int16_t *)(data + DMOD_REQ_READ_IDX) =
                    (int16_t)(*(int16_t *)(data + DMOD_REQ_READ_IDX) + 1);
            }
        }
    }

    ML_$EXCLUSION_STOP((ml_$exclusion_t *)(data + DMOD_EXCLUSION));
}
