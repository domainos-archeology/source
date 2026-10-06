/*
 * disk_$get_qblks_internal - Internal queue block allocation
 *
 * Allocates queue blocks from the disk module's free pool.
 * Uses ML_$EXCLUSION for synchronization and EC_$WAIT for
 * blocking when insufficient blocks are available.
 *
 * Two allocation modes:
 *   - Read mode (mode >= 0): Enqueues request in circular buffer,
 *     waits for rtn_qblks_internal to fulfill it. Will not allocate
 *     if there are pending requests (ensures FIFO ordering).
 *   - Write mode (mode < 0): Uses a reserve block if available,
 *     or waits on ec+1. Loops until blocks are available.
 *     Writes have priority over reads.
 *
 * When blocks are available, walks the free list and initializes each:
 *   - Clears status, flags, and reserved field
 *   - Sets owner to current process ID
 *   - Links blocks through forward pointer (offset 0x00)
 *   - Terminates list with NULL pointers on last block
 *
 * A5 is not loaded here: the function inherits the disk module base
 * 0x00E7A1CC (DISK_$DATA) from its callers, so every A5-relative cell below
 * is a module global rather than per-process data.
 *
 * Allocation loop (0x00E3BF90-0x00E3BFD0), one iteration:
 *   movea.l (0xc0,A5),A2 ; clr.l  (0xc,A2)         ; status  = 0  (longword)
 *   movea.l (0xc0,A5),A2 ; clr.w  (0x1c,A2)        ; flags   = 0  (word)
 *   movea.l (0xc0,A5),A2 ; move.b (A0),(0x1e,A2)   ; owner   = PROC1_$CURRENT low byte
 *   movea.l (0xc0,A5),A2 ; clr.b  (0x1f,A2)        ; reserved = 0
 *   cmp.w   D1w,D2w ; bne.b + movea.l (0x10,A6),A2 ; *last_out = free head
 *                     move.l  (0xc0,A5),(A2)
 *   movea.l (0xc0,A5),A2 ; move.l (0x8,A2),(A2)    ; forward   = free_next
 *   movea.l (0xc0,A5),A2 ; move.l (0x8,A2),(0xc0,A5) ; head    = free_next
 *   addq.w  #0x1,D1w ; dbf D0w,0x00e3bf90
 *
 * A0 is set up once at 0x00E3BF8C as `lea (0x1,A1),A0` with A1 = A3 =
 * 0x00E20608 = &PROC1_$CURRENT, i.e. the LOW byte of that word on the
 * big-endian target; `(uint8_t)PROC1_$CURRENT` expresses the same value
 * without a byte-order assumption.
 *
 * The growth test at 0x00E3BEE4 is `cmpi.w #0x5,(-0x2,A4,D1w*0x1)` with
 * A4 = 0x00E2612C and D1 = PROC1_$CURRENT * 2, which addresses
 * 0x00E2612A + 2*current -- exactly PROC1_$DATA.type[PROC1_$CURRENT], since
 * proc1/proc1.h places the PROC1_$TYPE array at 0x00E2612A.
 *
 * Parameters:
 *   count     - Number of queue blocks to allocate
 *   mode      - Negative for write mode, non-negative for read
 *   first_out - Output: 32-bit VA cell, receives the first allocated block
 *   last_out  - Output: 32-bit VA cell, receives the last allocated block
 *
 * Original address: 0x00E3BE8A
 * Size: 362 bytes
 */

#include "disk/disk_internal.h"
#include "ec/ec.h"
#include "ml/ml.h"
#include "proc1/proc1.h"
#include "arch/arch.h"

/*
 * DMOD_RESERVE_BLOCK (0x0BC) and DMOD_FREE_HEAD (0x0C0) are four bytes
 * apart, and DISK_QBLK_FORWARD (0x00) and DISK_QBLK_FREE_NEXT (0x08) are
 * likewise four-byte cells with live fields immediately above them: every
 * one of them is moved with a `move.l`/`clr.l` and holds a 32-bit target
 * address, not a host pointer.  They are read and written as uint32_t and
 * converted at the boundary with ARCH_VA_TO_PTR / ARCH_PTR_TO_VA (identity
 * casts on m68k), so a 64-bit host build does not overrun the neighbouring
 * field.
 *
 * The two output parameters are 32-bit VA cells too, not host pointers.  Each
 * is written with a single `move.l` of DMOD_FREE_HEAD -- 0x00E3BF7E
 * `move.l (0xc0,A5),(A0)` for first_out and 0x00E3BFB8 `move.l (0xc0,A5),(A2)`
 * for last_out -- and last_out is read back at the same width at 0x00E3BFD8
 * `movea.l (A2),A3`.  Every caller supplies a four-byte cell
 * (ast/read_area_pages.c, ast/touch_area.c, pmap/flush_write_batch.c,
 * pmap/purifier_l.c, disk/as_xfer_multi.c), so storing a host pointer here
 * would overrun the neighbouring local on a 64-bit host.  Callers that want a
 * pointer convert with ARCH_VA_TO_PTR (disk/io.c, disk/format.c,
 * disk/format_whole.c).
 */

void disk_$get_qblks_internal(int16_t count, int8_t mode, uint32_t *first_out,
                              uint32_t *last_out)
{
    uint8_t *data = DISK_$DATA;

    ML_$EXCLUSION_START((ml_$exclusion_t *)(data + DMOD_EXCLUSION));

    do {
        /* Inner loop: try to satisfy the request or grow the pool */
        for (;;) {
            int16_t avail = *(int16_t *)(data + DMOD_AVAIL_COUNT);
            int16_t pending = *(int16_t *)(data + DMOD_PENDING_COUNT);

            /* Can allocate if enough blocks AND either:
             *   - Write mode (mode < 0): always OK (writes have priority)
             *   - Read mode (mode >= 0): only if no pending requests (FIFO) */
            if (count <= avail && (mode < 0 || pending == 0)) {
                *(int16_t *)(data + DMOD_AVAIL_COUNT) = avail - count;
                goto allocate;
            }

            /* Check if we can try to grow the pool:
             * Only possible if not disabled AND no pending requests.
             * Assembly uses: ~disabled & seq(pending==0), tests bit 7.
             * In practice disabled is always 0x00 or 0xFF (clr.b/st). */
            if (*(uint8_t *)(data + DMOD_ALLOC_DISABLED) == 0 && pending == 0) {
                /* Don't grow for process type 5 (helper/idle processes) */
                if (PROC1_$DATA.type[PROC1_$CURRENT] == 5) {
                    break;
                }
                /* 0x00E3BEEC: `subq.l #0x2,SP` is the Pascal function's
                 * result slot; the boolean it returns is never read. */
                (void)disk_$grow_qblk_pool((uint16_t)count);
                continue;  /* Re-check availability */
            }
            break;  /* Can't grow - must wait */
        }

        /* Insufficient blocks available - must wait or use reserve */
        int32_t wait_val;

        if (mode < 0) {
            /* Write mode: check for reserve block */
            if ((int8_t)*(uint8_t *)(data + DMOD_RESERVE_AVAIL) < 0) {
                /* Reserve available - swap it into the free list.
                 * 0x00E3BF02: move.l (0xbc,A5),(0xc0,A5) */
                *(uint32_t *)(data + DMOD_FREE_HEAD) =
                    *(uint32_t *)(data + DMOD_RESERVE_BLOCK);
                *(uint8_t *)(data + DMOD_RESERVE_AVAIL) = 0;
                goto allocate;
            }
            /* Wait for eventcount value + 1 */
            wait_val = ((ec_$eventcount_t *)data)->value + 1;
        } else {
            /* Read mode: enqueue this request in the circular buffer */
            int16_t write_idx = *(int16_t *)(data + DMOD_REQ_WRITE_IDX);
            *(int16_t *)(data + DMOD_REQ_QUEUE + write_idx * 2) = count;
            if (write_idx == DMOD_REQ_QUEUE_SIZE) {
                *(int16_t *)(data + DMOD_REQ_WRITE_IDX) = 1;  /* Wrap to 1 */
            } else {
                *(int16_t *)(data + DMOD_REQ_WRITE_IDX) = write_idx + 1;
            }
            int16_t new_pending = *(int16_t *)(data + DMOD_PENDING_COUNT) + 1;
            *(int16_t *)(data + DMOD_PENDING_COUNT) = new_pending;
            /* Wait for eventcount value + pending count */
            wait_val = ((ec_$eventcount_t *)data)->value + (int32_t)new_pending;
        }

        /* Release lock, wait on eventcount, re-acquire lock */
        ML_$EXCLUSION_STOP((ml_$exclusion_t *)(data + DMOD_EXCLUSION));

        /* 0xE3BF50-0xE3BF5E: both 3-element arrays go on the stack by
         * value.  ecs = { &DISK_$DATA[0], NULL, NULL } (A2 was loaded with
         * 0 at 0xE3BEA6), vals = { wait_val, 0, 0 }. */
        EC_$WAIT((ec_$wait_ecs_t){ { (ec_$eventcount_t *)data, NULL, NULL } },
                 (ec_$wait_vals_t){ { wait_val, 0, 0 } });

        ML_$EXCLUSION_START((ml_$exclusion_t *)(data + DMOD_EXCLUSION));
    } while (mode < 0);  /* Write mode loops; read mode falls through */

allocate:
    /* Record the first block (current free list head).
     * 0x00E3BF7E: move.l (0xc0,A5),(A0) */
    *first_out = *(uint32_t *)(data + DMOD_FREE_HEAD);

    /* Walk the free list, initialize each block, build allocated chain */
    int16_t remaining = count - 1;
    if (remaining >= 0) {
        uint8_t owner = (uint8_t)PROC1_$CURRENT;
        int16_t block_num = 1;

        do {
            uint32_t block_va = *(uint32_t *)(data + DMOD_FREE_HEAD);
            uint8_t *block = ARCH_VA_TO_PTR(block_va);

            /* Initialize block fields */
            *(uint32_t *)(block + DISK_QBLK_STATUS) = 0;
            *(uint16_t *)(block + DISK_QBLK_FLAGS) = 0;
            *(block + DISK_QBLK_OWNER) = owner;
            *(block + DISK_QBLK_RESERVED) = 0;

            /* Record last block when we reach the count'th one.
             * 0x00E3BFB8: move.l (0xc0,A5),(A2) */
            if (count == block_num) {
                *last_out = block_va;
            }

            /* 0x00E3BFC0 and 0x00E3BFC8 both re-read the free_next link;
             * nothing written above touches offset 0x08. */
            uint32_t next_free = *(uint32_t *)(block + DISK_QBLK_FREE_NEXT);

            /* Link: block->forward = block->free_next (build allocated chain) */
            *(uint32_t *)(block + DISK_QBLK_FORWARD) = next_free;
            /* Advance free list head to next block */
            *(uint32_t *)(data + DMOD_FREE_HEAD) = next_free;

            block_num++;
            remaining--;
        } while (remaining != -1);  /* dbf loop semantics */
    }

    /* Terminate the last allocated block.  0x00E3BFD8 `movea.l (A2),A3`
     * reloads the cell as an address, then 0x00E3BFDA / 0x00E3BFDE clear the
     * two links in that order. */
    uint8_t *last_block = ARCH_VA_TO_PTR(*last_out);
    *(uint32_t *)(last_block + DISK_QBLK_FREE_NEXT) = 0;
    *(uint32_t *)(last_block + DISK_QBLK_FORWARD) = 0;

    ML_$EXCLUSION_STOP((ml_$exclusion_t *)(data + DMOD_EXCLUSION));
}
