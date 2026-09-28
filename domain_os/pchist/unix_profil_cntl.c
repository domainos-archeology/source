/*
 * PCHIST_$UNIX_PROFIL_CNTL - Control per-process profiling (UNIX profil)
 *
 * Re-emitted from the image (0x00E5CA58..0x00E5CC30, 474 bytes).  A5 =
 * 0xE2C204 (PCHIST_$CONTROL); the per-process record is 0xE85718 +
 * pid*0x14 biased by 0x14, i.e. PCHIST_$PROC_DATA[pid] with fields at
 * (-0x14) buffer, (-0x10) bufsize, (-0xC) offset, (-0x8) scale, (-0x4)
 * overflow_ptr.  Frame: (0x8,A6) cmd ptr, (0xC) buffer ptr -> A0, (0x10)
 * bufsize ptr, (0x14) offset ptr, (0x18) scale ptr, (0x1C) status (cleared
 * first); A6-0x22 = PROC1_$CURRENT (its low byte at -0x21 drives the
 * bitmap byte index on the TEST paths, the full word on the SET/CLEAR
 * paths); A6-0x18 the FIM cleanup record.
 *
 * The old body dropped the two page touches at 0x00E5CB02 / 0x00E5CB12
 * (the first and last word of the user buffer are read under the cleanup
 * handler before it is released).
 *
 * Callers: 0x00E3E900 (PROC2 cleanup) and the SVC table entry 0x00E7BD3A.
 */

#include "pchist/pchist_internal.h"

/*
 * PCHIST_$UNIX_PROFIL_CNTL
 *
 * Implements the UNIX profil() system call for per-process profiling.
 *
 * Commands:
 *   0 - Enable profiling for current process
 *   1 - Disable profiling for current process
 *   2 - Set overflow pointer
 */
void PCHIST_$UNIX_PROFIL_CNTL(
    int16_t *cmd_ptr,
    void **buffer_ptr,
    uint32_t *bufsize_ptr,
    uint32_t *offset_ptr,
    uint32_t *scale_ptr,
    status_$t *status_ret)
{
    int16_t cmd;
    int16_t current_pid;
    int16_t pid_index;
    pchist_proc_t *proc_data;
    int16_t byte_index, bit_index;
    status_$t cleanup_status;
    uint8_t cleanup_data[24];

    *status_ret = status_$ok;
    current_pid = PROC1_$CURRENT;
    cmd = *cmd_ptr;

    /*
     * Calculate per-process data index
     * Data is at offset (pid * 0x14) from base
     */
    pid_index = current_pid;
    proc_data = &PCHIST_$PROC_DATA[pid_index];

    if (cmd == 0) {
        /*
         * Command 0: Enable profiling
         */

        /* Store profiling parameters */
        proc_data->buffer = *buffer_ptr;
        proc_data->bufsize = *bufsize_ptr;
        proc_data->offset = *offset_ptr;
        proc_data->scale = *scale_ptr;
        proc_data->overflow_ptr = NULL;

        /*
         * Set up cleanup handler to ensure profiling is
         * disabled if the process exits
         */
        cleanup_status = FIM_$CLEANUP(cleanup_data);

        if (cleanup_status == status_$cleanup_handler_set) {
            /*
             * 0x00E5CAFA-0x00E5CB12: touch the first word of the buffer and
             * the word at buffer + bufsize - 2 (both `move.w (A0),D0w`, the
             * value discarded) while the cleanup handler is armed.
             */
            (void)*(volatile uint16_t *)proc_data->buffer;
            (void)*(volatile uint16_t *)((uint8_t *)proc_data->buffer + proc_data->bufsize - 2);

            /* 0x00E5CB14 */
            FIM_$RLS_CLEANUP(cleanup_data);

            ML_$EXCLUSION_START(&PCHIST_$CONTROL.lock);

            /*
             * Check if profiling was not already enabled for this process
             */
            byte_index = (int16_t)(((uint8_t)(current_pid - 1)) >> 3);
            bit_index = 7 - ((current_pid - 1) & 7);

            if ((PCHIST_$PROC_BITMAP[byte_index] & (1 << bit_index)) == 0) {
                /* Increment the per-process profiling count */
                PCHIST_$CONTROL.proc_profiling_count++;
            }

            /* 0x00E5CB5C-0x00E5CB76: the SET path indexes with the full
             * word (pid - 1) >> 3, not the low byte */
            PCHIST_$PROC_BITMAP[(uint16_t)(current_pid - 1) >> 3] |=
                (uint8_t)(0x80 >> ((current_pid - 1) & 7));

            /* Set up cleanup handler for process exit */
            PROC2_$SET_CLEANUP(0x0B);  /* PCHIST cleanup type */

            /* Clear any pending PC sample */
            PCHIST_$PROC_PC[current_pid] = 0;

            /* Notify terminal */
            PCHIST_$ENABLE_TERMINAL(0);

            ML_$EXCLUSION_STOP(&PCHIST_$CONTROL.lock);
        }
        else {
            /* Cleanup setup failed - pop signal state */
            FIM_$POP_SIGNAL(cleanup_data);
        }
    }
    else if (cmd == 1) {
        /*
         * Command 1: Disable profiling
         */

        /* Check if profiling is enabled for this process */
        byte_index = (int16_t)(((uint8_t)(current_pid - 1)) >> 3);
        bit_index = 7 - ((current_pid - 1) & 7);

        if ((PCHIST_$PROC_BITMAP[byte_index] & (1 << bit_index)) != 0) {
            /* Clear any pending PC sample */
            PCHIST_$PROC_PC[current_pid] = 0;

            ML_$EXCLUSION_START(&PCHIST_$CONTROL.lock);

            /* Decrement the per-process profiling count */
            PCHIST_$CONTROL.proc_profiling_count--;

            /* Notify terminal */
            PCHIST_$ENABLE_TERMINAL(1);

            ML_$EXCLUSION_STOP(&PCHIST_$CONTROL.lock);

            /* 0x00E5CBF2-0x00E5CC0C: `move.l #-0x81,D2 ; lsr.l` gives
             * ~(0x80 >> n) in the low byte; word-based index */
            PCHIST_$PROC_BITMAP[(uint16_t)(current_pid - 1) >> 3] &=
                (uint8_t)~(0x80 >> ((current_pid - 1) & 7));
        }
    }
    else if (cmd == 2) {
        /*
         * Command 2: Set overflow pointer
         * This allows tracking overflow in user space
         */
        proc_data->overflow_ptr = (uint32_t *)*buffer_ptr;
    }
}
