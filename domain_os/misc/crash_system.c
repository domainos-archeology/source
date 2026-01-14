/*
 * CRASH_SYSTEM - Fatal system crash handler
 *
 * Called when the kernel encounters an unrecoverable error.
 * This function never returns - it halts the system after
 * attempting to display error information.
 *
 * The error parameter can be:
 *   - A pointer to a status_$t error code
 *   - A pointer to an error message string
 *   - A pointer to an error descriptor structure
 *
 * On Domain/OS hardware, this would typically:
 *   1. Disable interrupts
 *   2. Display error info on console/display
 *   3. Enter debug monitor or halt
 *
 * Original address: Unknown (likely in boot/trap code)
 */

#include "base/base.h"

/* Error message pointers used throughout the kernel */
status_$t Lock_ordering_violation = 0x00010001;
status_$t Illegal_lock_err = 0x00010002;
status_$t Lock_order_violation_err = 0x00010003;
status_$t No_calendar_on_system_err = 0x00010004;
status_$t OS_BAT_disk_needs_salvaging_err = 0x00010005;
status_$t No_err = 0x00000000;
status_$t PMAP_VM_Resources_exhausted_err = 0x00010006;
status_$t MST_Ref_OutOfBounds_Err = 0x00040004;
status_$t Disk_Queued_Drivers_Not_Supported_Err = 0x00080030;
status_$t Disk_Driver_Logic_Err = 0x00080031;
status_$t Disk_controller_err = 0x00080004;
status_$t Disk_driver_logic_err = 0x00080031;

/* Error strings used in mmap */
const char *Illegal_PID_Err = "Illegal PID";
const char *Illegal_WSL_Index_Err = "Illegal WSL Index";
const char *WSL_Exhausted_Err = "WSL Exhausted";
const char *Inconsistent_MMAPE_Err = "Inconsistent MMAPE";
const char *MMAP_Bad_Unavail_err = "MMAP Bad Unavail";
const char *mmap_bad_avail = "MMAP Bad Avail";
const char *MMAP_Bad_Reclaim_Err = "MMAP Bad Reclaim";
const char *MMAP_Error_Examined_Max = "MMAP Error Examined Max";

/* Error strings used in ast */
const char *Some_ASTE_Error = "ASTE Error";
const char *OS_PMAP_mismatch_err = "PMAP Mismatch";
const char *OS_MMAP_bad_install = "MMAP Bad Install";

/*
 * CRASH_SYSTEM - Halt the system with an error
 *
 * This function is marked noreturn - it never returns to the caller.
 *
 * @param error  Pointer to error info (status code, string, or struct)
 */
__attribute__((noreturn))
void CRASH_SYSTEM(void *error)
{
    uint16_t sr;

    /* Disable all interrupts */
    DISABLE_INTERRUPTS(sr);
    (void)sr;  /* Won't be restored - we're crashing */

    /*
     * TODO: On real hardware, this would:
     * 1. Save crash info to a known memory location
     * 2. Display error on console/bitmap display
     * 3. Enter debug monitor if available
     * 4. Otherwise halt with STOP instruction
     *
     * For now, we just save the error pointer to a known location
     * and halt in an infinite loop.
     */

    /* Store error info at a known crash dump location */
    /* On Domain/OS this might be at a fixed address like 0x400 */
    *(void **)0x00000400 = error;

    /*
     * Halt the processor.
     *
     * On m68k, STOP #imm halts until an interrupt, but since we've
     * masked all interrupts, we use an infinite loop instead.
     * A real implementation might use the debug monitor entry point.
     */
#if defined(__m68k__) || defined(M68K)
    /*
     * Could use: __asm__ volatile ("stop #0x2700");
     * But infinite loop is safer if interrupts somehow fire
     */
    for (;;) {
        __asm__ volatile ("nop");
    }
#else
    /* Host/test build - just loop forever */
    for (;;) {
        /* Spin */
    }
#endif

    /* Never reached, but silences compiler warnings */
    __builtin_unreachable();
}
