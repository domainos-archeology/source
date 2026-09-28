/*
 * PCHIST_$UNIX_PROFIL_ADDUPC - Update profiling buffer
 *
 * Re-emitted from the image (0x00E5CFAC..0x00E5D04C, 162 bytes) and verified
 * block by block; the previous body was faithful.  A5 = 0xE2C204, A1 =
 * 0xE85718 + pid*0x14 (PCHIST_$PROC_DATA[pid] biased by 0x14: (-0x4)
 * overflow_ptr, (-0x8) scale, (-0xC) offset, (-0x10) bufsize, (-0x14)
 * buffer).
 *
 *   00e5cfd4  overflow_ptr ? *overflow_ptr : PROC_PC[pid]     -> D1 = pc
 *   00e5cfee  PROC_PC[pid] = 0
 *   00e5cff6  pc < offset (bcs) -> exit ; D1 = pc - offset
 *   00e5cffc  D2 = D1 >> 16 (clr.w/swap) ; M$MIU$LLL(D2, scale)  -> D2
 *   00e5d012  D1 &= 0x7FFF ; M$MIU$LLL(D1, scale) ; clr.w/swap -> >> 16
 *   00e5d02a  D1 = (hi + lo + 1) & ~1 (andi.b #-2 on the low byte)
 *   00e5d034  D1 >= bufsize (bcc) -> exit ; else (buffer + D1) word ++
 *
 * Sole caller 0x00E215DC (FIM trace-fault delivery).
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
