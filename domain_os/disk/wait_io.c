/*
 * disk_$wait_io - Wait for disk I/O completion
 *
 * Waits on eventcounts for queued disk I/O operations to complete.
 * Uses EC_$WAIT with 3 eventcounts:
 *   1. Per-process I/O completion EC at data + PID*0x1c + 0x378
 *   2. Per-process error EC at data + PID*0x1c + 0x384
 *   3. TIME_$CLOCKH (global time clock for timeout)
 *
 * After each wait, iterates 10 disk volume entries (0x48 spacing)
 * and calls DISK_$ERROR_QUE for each matching bit in the disk mask.
 * Increments *error_wait_val when errors are detected (not timeouts).
 *
 * Loop continues until EC_$WAIT returns 0 (I/O completion EC satisfied).
 *
 * Parameters:
 *   disk_mask      - Bitmask of volumes to check for errors (bits 1-10)
 *   io_wait_val    - Pointer to I/O completion EC wait value
 *   error_wait_val - Pointer to error EC wait value (incremented on error)
 *
 * Original address: 0x00E3C9FE
 * Size: 188 bytes
 */

#include "disk/disk_internal.h"

#include "time/time.h"     /* TIME_$CLOCKH */
void disk_$wait_io(uint16_t disk_mask, int32_t *io_wait_val, int32_t *error_wait_val)
{
    uint8_t *data = DISK_$DATA;
    int16_t result;

    /*
     * Compute per-process base within disk module data.
     * Original m68k: A5 + sign_extend(PROC1_$CURRENT * 0x1c)
     * The multiplication is done in 16-bit word arithmetic.
     */
    uint8_t *per_proc_base = data + (int16_t)(PROC1_$CURRENT * DMOD_PER_PROC_SIZE);

    /* Outer loop: wait on eventcounts until I/O completion fires */
    do {
        /*
         * Set up 3 eventcounts to wait on:
         *   [0] = per-process I/O completion EC
         *   [1] = per-process error EC
         *   [2] = TIME_$CLOCKH (timeout after 0xf0 ticks)
         */
        /* 0xE3CA40-0xE3CA5A: both 3-element arrays are pushed by value.
         * ecs   = { A0+0x378, A0+0x384, &TIME_$CLOCKH }
         * vals  = { *io_wait_val, *error_wait_val, TIME_$CLOCKH + 0xF0 } */
        ec_$wait_ecs_t ecs;
        ec_$wait_vals_t wait_vals;

        ecs.ec[0] = (ec_$eventcount_t *)(per_proc_base + DMOD_PER_PROC_IO_EC);
        ecs.ec[1] = (ec_$eventcount_t *)(per_proc_base + DMOD_PER_PROC_ERR_EC);
        ecs.ec[2] = (ec_$eventcount_t *)&TIME_$CLOCKH;

        wait_vals.val[0] = *io_wait_val;
        wait_vals.val[1] = *error_wait_val;
        wait_vals.val[2] = (int32_t)(TIME_$CLOCKH + DMOD_WAIT_TIMEOUT);

        result = EC_$WAIT(ecs, wait_vals);

        if (result == 0) {
            break;  /* I/O completion EC satisfied - done */
        }

        /*
         * Determine which EC fired:
         *   result == 1: error EC fired -> is_timeout = 0
         *   result == 2: timeout (clock) -> is_timeout = 1
         *
         * Original m68k:
         *   cmpi.w #1,D0
         *   bne -> D3=1 (timeout)
         *   clr.w D3    (error)
         */
        int16_t is_timeout = (result != 1) ? 1 : 0;

        /*
         * Inner loop: check each of 10 disk volumes (indices 1-10).
         * For each volume whose bit is set in disk_mask, call
         * DISK_$ERROR_QUE to poll/dequeue error information.
         *
         * Original m68k: D4=9 (dbf counter), D5=1 (starting index),
         * A3 = data + 0x48 (volume 1 base), advancing by 0x48 each iteration.
         */
        int16_t count = 9;  /* dbf counter: 9 means 10 iterations */
        uint16_t disk_idx = 1;
        uint8_t *disk_desc = data + DISK_VOLUME_SIZE;  /* Volume 1 base */

        do {
            if (disk_mask & (1 << disk_idx)) {
                uint8_t err_result[12];  /* Result buffer from error handler */

                DISK_$ERROR_QUE(disk_desc + DMOD_VOL_ERROR_QUE,
                                (uint16_t)is_timeout, err_result);

                /*
                 * If error result byte 0 has bit 7 set (negative/error present)
                 * AND this was an actual error (not a timeout poll),
                 * increment the error wait value so the next EC_$WAIT
                 * waits for yet another error event.
                 *
                 * Original m68k:
                 *   move.b (-0xc,A6),D0b
                 *   bpl skip          ; bit 7 clear -> no error
                 *   tst.w D3w
                 *   bne skip          ; D3 != 0 -> timeout, skip
                 *   addq.l #1,(A4)    ; *error_wait_val++
                 */
                if ((int8_t)err_result[0] < 0 && is_timeout == 0) {
                    *error_wait_val = *error_wait_val + 1;
                }
            }

            disk_idx++;
            disk_desc += DISK_VOLUME_SIZE;
            count--;
        } while (count != -1);  /* dbf loop semantics: decrement then test for -1 */

    } while (1);
}
