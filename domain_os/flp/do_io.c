/*
 * FLP_$DO_IO - Perform floppy disk I/O operation
 *
 * This function is a wrapper that reformats the parameters and
 * calls the internal FLP_DO_IO function to perform the actual I/O.
 *
 * The LBA (Logical Block Address) is passed as a packed 32-bit value
 * and needs to be split into high and low portions for the internal
 * function.
 */

#include "flp.h"

/*
 * FLP_$DO_IO - Perform I/O operation
 *
 * @param param_1  I/O request block
 * @param param_2  Data buffer
 * @param param_3  Transfer count
 * @param param_4  Packed LBA (high 16 bits in upper word)
 */
void FLP_$DO_IO(void *param_1, void *param_2, void *param_3, uint32_t param_4)
{
    /*
     * Split the packed LBA into high and low portions.
     * The internal function expects:
     *   p4_hi: Upper 16 bits (param_4 >> 16)
     *   p4_lo: Full 32-bit value with an additional 16-bit prefix
     *
     * The assembly shows:
     *   move.l (0x14,A6),-(SP)   ; push full param_4
     *   clr.w -(SP)              ; push 0 (padding)
     *   ...
     * So we pass (param_4 >> 16) and ((param_4 & 0xFFFF) << 16) | 0
     */
    FLP_DO_IO(param_1, param_2, param_3,
              (uint16_t)(param_4 >> 16),
              ((uint32_t)(uint16_t)param_4 << 16));
}
