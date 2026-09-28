/*
 * PCHIST_$COUNT - Record a PC sample
 *
 * Re-emitted from the image (0x00E1A134..0x00E1A1F4, 194 bytes) and verified
 * block by block; the previous body was faithful.  A5 = 0xE2C204
 * (PCHIST_$CONTROL), A2 = pc_ptr, A0 = mode_ptr, D2 = PROC1_$CURRENT.
 *
 *   00e1a150  cmpi.w #1,(A0)                    ; mode 1: per-process trace
 *   00e1a156  D0b = pid - 1 ; byte (0x18,A5,D0>>3) ; bit 7 - (D0 & 7)
 *   00e1a176  tst.l (0x1c,A5,pid*4) / bne       ; a PC is already pending
 *   00e1a17c  store *pc ; FIM_$DELIVER_TRACE_FAULT(PROC1_$AS_ID) (result slot)
 *   00e1a190  tst.b (0x124,A5) / bpl exit       ; histogram_enabled
 *   00e1a19c  A1 = 0xE85718 ; (0x524)++         ; total_samples
 *   00e1a1a0  (0x510) pid filter: 0, or == pid, else (0x530)++ wrong_pid
 *   00e1a1ae  pc < (0x518) -> (0x52c)++ ; pc > (0x51c) -> (0x528)++
 *   00e1a1c6  bin = (pc - start) >> (0x514) ; ext.l of the LOW WORD indexes
 *             (0x534,A1,bin*4)                  ; histogram[(int16_t)bin]++
 *
 * Sole caller PCHIST_$INTERRUPT 0x00E1A202.
 */

#include "pchist/pchist_internal.h"

/*
 * PCHIST_$COUNT
 *
 * This function is called to record a PC sample. It handles both:
 * 1. Per-process trace fault delivery (mode == 1)
 * 2. System-wide histogram updates (if histogram enabled)
 *
 * The function runs at interrupt level and must be efficient.
 */
void PCHIST_$COUNT(uint32_t *pc_ptr, int16_t *mode_ptr)
{
    int16_t current_pid;
    uint32_t pc;
    int16_t byte_index;
    int16_t bit_index;
    int16_t bin_index;
    pchist_histogram_t *hist;

    /* Get current process ID */
    current_pid = PROC1_$CURRENT;

    /*
     * Check for per-process trace fault mode
     * mode == 1 indicates trace fault delivery is requested
     */
    if (*mode_ptr == 1) {
        /*
         * Check if this process has profiling enabled in the bitmap
         * Bitmap uses big-endian bit ordering
         */
        byte_index = (int16_t)(((uint8_t)(current_pid - 1)) >> 3);
        bit_index = 7 - ((current_pid - 1) & 7);

        if ((PCHIST_$PROC_BITMAP[byte_index] & (1 << bit_index)) != 0) {
            /*
             * Process has profiling enabled
             * Check if we don't already have a pending PC for this process
             */
            if (PCHIST_$PROC_PC[current_pid] == 0) {
                /* Store the PC and deliver a trace fault */
                PCHIST_$PROC_PC[current_pid] = *pc_ptr;
                FIM_$DELIVER_TRACE_FAULT(PROC1_$AS_ID);
            }
        }
    }

    /*
     * Check if system-wide histogram is enabled
     * The enabled flag has the high bit set when active
     */
    if (PCHIST_$CONTROL.histogram_enabled < 0) {
        hist = &PCHIST_$HISTOGRAM;

        /* Increment total sample count */
        hist->total_samples++;

        /*
         * Check if we're filtering by PID
         * pid_filter == 0 means profile all processes
         */
        if (hist->pid_filter == 0 || current_pid == hist->pid_filter) {
            pc = *pc_ptr;

            /* Check if PC is in range */
            if (pc < hist->range_start) {
                /* Below range - increment under_range counter */
                hist->under_range++;
            }
            else if (pc > hist->range_end) {
                /* Above range - increment over_range counter */
                hist->over_range++;
            }
            else {
                /*
                 * PC is in range - calculate histogram bin
                 * bin_index = (pc - range_start) >> shift
                 */
                bin_index = (int16_t)((pc - hist->range_start) >> hist->shift);
                hist->histogram[bin_index]++;
            }
        }
        else {
            /* Wrong PID - increment wrong_pid counter */
            hist->wrong_pid++;
        }
    }
}
