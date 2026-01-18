/*
 * PCHIST_$COUNT - Record a PC sample
 *
 * Reverse engineered from Domain/OS at address 0x00e1a134
 */

#include "pchist/pchist_internal.h"

/*
 * External references to control data
 */
extern pchist_control_t PCHIST_$CONTROL;
extern pchist_histogram_t PCHIST_$HISTOGRAM;

/*
 * Per-process PC storage - array of last sampled PC values
 * Indexed by process ID
 */
extern uint32_t PCHIST_$PROC_PC[];

/*
 * Process enable bitmap
 * Each bit indicates if profiling is enabled for that process
 */
extern uint8_t PCHIST_$PROC_BITMAP[];

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
