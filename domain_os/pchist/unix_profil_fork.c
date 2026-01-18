/*
 * PCHIST_$UNIX_PROFIL_FORK - Copy profiling state on fork
 *
 * Reverse engineered from Domain/OS at address 0x00e5cc32
 */

#include "pchist/pchist_internal.h"

/*
 * External references
 */
extern pchist_control_t PCHIST_$CONTROL;
extern pchist_proc_t PCHIST_$PROC_DATA[];
extern uint32_t PCHIST_$PROC_PC[];
extern uint8_t PCHIST_$PROC_BITMAP[];

/*
 * PCHIST_$UNIX_PROFIL_FORK
 *
 * Called during process fork to copy the parent's profiling
 * configuration to the child process.
 *
 * If the parent has profiling enabled, the child inherits
 * the same profiling configuration.
 */
void PCHIST_$UNIX_PROFIL_FORK(int16_t *child_pid_ptr)
{
    int16_t parent_pid;
    int16_t child_pid;
    int16_t byte_index, bit_index;
    pchist_proc_t *parent_data;
    pchist_proc_t *child_data;
    int16_t i;

    parent_pid = PROC1_$CURRENT;
    child_pid = *child_pid_ptr;

    /*
     * Check if parent has profiling enabled
     */
    byte_index = (int16_t)(((uint8_t)(parent_pid - 1)) >> 3);
    bit_index = 7 - ((parent_pid - 1) & 7);

    if ((PCHIST_$PROC_BITMAP[byte_index] & (1 << bit_index)) == 0) {
        /* Parent doesn't have profiling - nothing to do */
        return;
    }

    /*
     * Copy parent's profiling data to child
     * Copy 5 longwords (20 bytes = sizeof(pchist_proc_t))
     */
    parent_data = &PCHIST_$PROC_DATA[parent_pid];
    child_data = &PCHIST_$PROC_DATA[child_pid];

    for (i = 0; i < 5; i++) {
        ((uint32_t *)child_data)[i] = ((uint32_t *)parent_data)[i];
    }

    /* Clear child's pending PC sample */
    PCHIST_$PROC_PC[child_pid] = 0;

    /*
     * Check if child's profiling bit is already set
     * (shouldn't be, but check anyway)
     */
    byte_index = (int16_t)(((uint8_t)(child_pid - 1)) >> 3);
    bit_index = 7 - ((child_pid - 1) & 7);

    if ((PCHIST_$PROC_BITMAP[byte_index] & (1 << bit_index)) == 0) {
        /* Child's bit not set - increment count and notify */
        ML_$EXCLUSION_START(&PCHIST_$CONTROL.lock);
        PCHIST_$CONTROL.proc_profiling_count++;
        PCHIST_$ENABLE_TERMINAL(0);
        ML_$EXCLUSION_STOP(&PCHIST_$CONTROL.lock);
    }

    /* Set child's profiling bit */
    byte_index = (int16_t)(((uint16_t)(child_pid - 1)) >> 3);
    PCHIST_$PROC_BITMAP[byte_index] |= (uint8_t)(0x80 >> ((child_pid - 1) & 7));
}
