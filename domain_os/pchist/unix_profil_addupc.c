/*
 * PCHIST_$UNIX_PROFIL_ADDUPC - Update profiling buffer
 *
 * Reverse engineered from Domain/OS at address 0x00e5cfac
 */

#include "pchist/pchist_internal.h"
#include "math/math.h"

/*
 * PCHIST_$UNIX_PROFIL_ADDUPC
 *
 * Called to update the per-process profiling buffer with
 * the accumulated PC samples. This implements the UNIX
 * addupc() functionality.
 *
 * The function calculates which buffer entry corresponds
 * to the sampled PC and increments that entry.
 *
 * Formula: index = ((pc - offset) * scale) >> 16
 * The scale is a 16.16 fixed point multiplier.
 */
void PCHIST_$UNIX_PROFIL_ADDUPC(void)
{
    int16_t current_pid;
    pchist_proc_t *proc_data;
    uint32_t pc;
    uint32_t pc_offset;
    uint32_t high_part, low_part;
    uint32_t index;
    int16_t *buffer_entry;

    current_pid = PROC1_$CURRENT;
    proc_data = &PCHIST_$PROC_DATA[current_pid];

    /*
     * Get the sampled PC
     * If overflow_ptr is set, read from there, otherwise from PROC_PC array
     */
    if (proc_data->overflow_ptr != NULL) {
        pc = *proc_data->overflow_ptr;
    }
    else {
        pc = PCHIST_$PROC_PC[current_pid];
    }

    /* Clear the pending PC */
    PCHIST_$PROC_PC[current_pid] = 0;

    /*
     * Check if PC is within the profiled range
     * (pc >= offset means it's in range)
     */
    if (pc < proc_data->offset) {
        return;  /* PC below offset - ignore */
    }

    /*
     * Calculate buffer index using fixed-point multiplication
     * The scale is a 16.16 fixed point value
     *
     * index = ((pc - offset) * scale) >> 16
     *
     * To handle the 32-bit multiplication properly, we split
     * the (pc - offset) value and multiply in parts:
     *   high_part = (pc_offset >> 16) * scale
     *   low_part = (pc_offset & 0x7FFF) * scale
     *   index = high_part + (low_part >> 16)
     */
    pc_offset = pc - proc_data->offset;

    /* Multiply high 16 bits by scale */
    high_part = M$MIU$LLL(pc_offset >> 16, proc_data->scale);

    /* Multiply low 15 bits by scale (mask to avoid overflow) */
    low_part = M$MIU$LLL(pc_offset & 0x7FFF, proc_data->scale);

    /* Combine: high_part + (low_part >> 16), rounded, word-aligned */
    index = (high_part + (low_part >> 16) + 1) & ~1;

    /*
     * Check if index is within buffer bounds
     */
    if (index >= proc_data->bufsize) {
        return;  /* Index out of bounds - ignore */
    }

    /*
     * Increment the buffer entry
     * Buffer contains 16-bit counters
     */
    buffer_entry = (int16_t *)((uint8_t *)proc_data->buffer + index);
    (*buffer_entry)++;
}
