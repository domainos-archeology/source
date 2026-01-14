/*
 * CRASH_SYSTEM - Fatal system crash handler
 *
 * Called when the kernel encounters an unrecoverable error.
 * Saves system state, displays error info, and either reboots
 * or enters the crash debugger.
 *
 * Original address: 0x00E1E700
 *
 * The function:
 * 1. Disables interrupts and saves all registers
 * 2. Stores crash status and related info
 * 3. Prints crash message if not a clean reboot
 * 4. Saves crash dump info at 0xE00000
 * 5. Resets keyboard
 * 6. Either returns to PROM or enters crash debugger
 */

#include "base/base.h"
#include "proc1/proc1.h"

/* Status codes for special handling */
#define status_$system_reboot  0x001b0008

/* Crash data area - stored near CRASH_SYSTEM code at 0xE1E98E */
status_$t CRASH_STATUS;                    /* +0x28e from base */
void *DAT_00e1e9a2;                         /* +0x298: PROC1 current at crash */
void *CRASH_ECB;                            /* Return address at crash */
uint32_t CRASH_REGS[16];                    /* Saved registers D0-D7/A0-A7 */
uint32_t CRASH_USP;                         /* User stack pointer */

/* Crash dump area at 0xE00000 */
#define CRASH_DUMP_BASE  ((volatile uint32_t *)0x00e00000)
#define CRASH_MAGIC      0xabcdef01

/* External references */
extern uint32_t TIME_$CLOCKH;              /* High word of system clock */
extern void *PROM_$QUIET_RET_ADDR;         /* PROM warm restart entry */

/* Keyboard functions */
extern void KBD_$RESET(void);
extern void KBD_$CRASH_INIT(void);

/* Internal: print crash message */
static void crash_puts_string(const char *str);

/*
 * CRASH_SYSTEM - Main crash handler
 *
 * @param status_p  Pointer to status code that caused the crash
 */
void CRASH_SYSTEM(const status_$t *status_p)
{
    uint16_t saved_sr;
    uint32_t *reg_src;
    uint32_t *reg_dst;
    int16_t i;

    /* Disable interrupts - we're crashing */
    DISABLE_INTERRUPTS(saved_sr);
    (void)saved_sr;

    /* Save crash status */
    CRASH_STATUS = *status_p;

    /* If not ok and not clean reboot, save debug info */
    if (CRASH_STATUS != status_$ok && CRASH_STATUS != status_$system_reboot) {
        DAT_00e1e9a2 = PROC1_$CURRENT;
        /* CRASH_ECB would be set from stack - return address */
        crash_puts_string("Crash Status");
    }

    /* Clear or set crash dump area */
    CRASH_DUMP_BASE[0] = 0;

    if (CRASH_STATUS != status_$ok && CRASH_STATUS != status_$system_reboot) {
        CRASH_DUMP_BASE[0] = CRASH_MAGIC;
        CRASH_DUMP_BASE[1] = TIME_$CLOCKH;
        CRASH_DUMP_BASE[2] = CRASH_STATUS;
    }

    /*
     * Save registers to CRASH_REGS
     * In the original, this copies from stack where movem.l saved them.
     * We can't easily replicate this in C - would need inline asm.
     */
    reg_dst = CRASH_REGS;
    for (i = 0; i < 16; i++) {
        *reg_dst++ = 0;  /* Placeholder - real impl saves actual regs */
    }

    /* Save USP - requires privileged instruction */
#if defined(__m68k__) || defined(M68K)
    __asm__ volatile ("movec %%usp, %0" : "=d" (CRASH_USP));
#else
    CRASH_USP = 0;
#endif

    /* Reset keyboard controller */
    KBD_$RESET();

    if (CRASH_STATUS == status_$ok) {
        /*
         * Clean shutdown - jump to PROM warm restart.
         * In original: movea.l (0x11c).w,A0; jmp (A0)
         * 0x11c is the PROM quiet return vector.
         */
        void (*prom_ret)(void) = (void (*)(void))PROM_$QUIET_RET_ADDR;
        prom_ret();
        /* Should not return */
    }

    /*
     * Crash case - enter debugger via trap #15.
     * The trap handler will display crash info and allow debugging.
     */
#if defined(__m68k__) || defined(M68K)
    __asm__ volatile ("trap #15");
#endif

    /* Initialize keyboard for crash console */
    KBD_$CRASH_INIT();

    /* Clear saved registers (original does this after trap returns) */
    reg_dst = CRASH_REGS;
    for (i = 0; i < 16; i++) {
        *reg_dst++ = 0;
    }

    /*
     * Original restores SR and returns here, but that seems wrong
     * for a crash handler. The trap #15 handler likely doesn't return
     * in normal operation.
     */
}

/*
 * crash_puts_string - Print string to crash console
 *
 * This would output to the display or serial console.
 * Original at 0x00E1E7C8.
 */
static void crash_puts_string(const char *str)
{
    (void)str;
    /* TODO: Implement console output */
    /* Original likely writes directly to display hardware */
}

/*
 * Error codes used with CRASH_SYSTEM throughout the kernel
 */
status_$t Lock_ordering_violation = 0x00010001;
status_$t Illegal_lock_err = 0x00010002;
status_$t Lock_order_violation_err = 0x00010003;
status_$t No_calendar_on_system_err = 0x00010004;
status_$t OS_BAT_disk_needs_salvaging_err = 0x00010005;
status_$t No_err = status_$ok;
status_$t PMAP_VM_Resources_exhausted_err = 0x00010006;
status_$t MST_Ref_OutOfBounds_Err = 0x00040004;
status_$t Disk_Queued_Drivers_Not_Supported_Err = 0x00080030;
status_$t Disk_Driver_Logic_Err = 0x00080031;
status_$t Disk_controller_err = 0x00080004;
status_$t Disk_driver_logic_err = 0x00080031;

/* Error strings */
const char *Illegal_PID_Err = "Illegal PID";
const char *Illegal_WSL_Index_Err = "Illegal WSL Index";
const char *WSL_Exhausted_Err = "WSL Exhausted";
const char *Inconsistent_MMAPE_Err = "Inconsistent MMAPE";
const char *MMAP_Bad_Unavail_err = "MMAP Bad Unavail";
const char *mmap_bad_avail = "MMAP Bad Avail";
const char *MMAP_Bad_Reclaim_Err = "MMAP Bad Reclaim";
const char *MMAP_Error_Examined_Max = "MMAP Error Examined Max";
const char *Some_ASTE_Error = "ASTE Error";
const char *OS_PMAP_mismatch_err = "PMAP Mismatch";
const char *OS_MMAP_bad_install = "MMAP Bad Install";
