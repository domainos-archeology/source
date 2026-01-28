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
#include "kbd/kbd.h"
#include "proc1/proc1.h"
#include "prom/prom.h"
#include "time/time.h"

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

/* PROM vector addresses for crash console output */
#define PROM_PUTC_VECTOR        ((void (**)(void))0x00000108)
#define PROM_RELOAD_FONT_VECTOR ((void (**)(void))0x00000114)

/* Display memory mapping constants */
#define DISPLAY_VA_START    0x00FC0000
#define DISPLAY_PPN_START   0x80
#define DISPLAY_PPN_END     0x100
#define DISPLAY_PAGE_SIZE   0x400

/* MMU flags for display mapping */
#define MMU_DISPLAY_FLAGS   0x26

/* ASCII characters */
#define ASCII_CR    0x0D
#define ASCII_LF    0x0A
#define ASCII_PERCENT 0x25

/* External MMU function */
extern void MMU_$INSTALL(uint32_t ppn, uint32_t va, uint16_t flags);

/* Internal helper functions for crash output */
static void remap_display(void);
static void call_prom_reload_font(void);
static void call_prom_putc(char c);
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
        DAT_00e1e9a2 = (void *)(uintptr_t)PROC1_$CURRENT;
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
#if defined(__m68k__) || defined(ARCH_M68K)
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
#if defined(__m68k__) || defined(ARCH_M68K)
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
 * remap_display - Remap display memory for crash output
 *
 * Maps display memory (0xFC0000-0xFFFFF) to physical pages 0x80-0xFF.
 * This ensures the display is accessible during a crash even if the
 * normal mappings have been corrupted.
 *
 * Original address: 0x00E1E82A
 * Size: 58 bytes
 */
static void remap_display(void)
{
    uint32_t ppn;
    uint32_t va;
    uint16_t saved_sr;

    /* Disable interrupts during MMU manipulation */
    DISABLE_INTERRUPTS(saved_sr);

    va = DISPLAY_VA_START;
    for (ppn = DISPLAY_PPN_START; ppn < DISPLAY_PPN_END; ppn++) {
        MMU_$INSTALL(ppn, va, MMU_DISPLAY_FLAGS);
        va += DISPLAY_PAGE_SIZE;
    }

    ENABLE_INTERRUPTS(saved_sr);
}

/*
 * call_prom_reload_font - Reload display font from PROM
 *
 * Tail-calls the PROM font reload routine via vector at 0x114.
 * This ensures the font is available for crash message display.
 *
 * Original address: 0x00E1E822
 * Size: 6 bytes
 */
static void call_prom_reload_font(void)
{
    void (*reload_font)(void) = *PROM_RELOAD_FONT_VECTOR;
    reload_font();
}

/*
 * call_prom_putc - Output a character via PROM
 *
 * Calls the PROM putc routine via vector at 0x108 to output
 * a single character to the crash console.
 *
 * This function preserves D0-D2 and A0 as the original does.
 *
 * Original address: 0x00E1E812
 * Size: 16 bytes
 *
 * @param c: Character to output
 */
static void call_prom_putc(char c)
{
    /*
     * The original saves D0-D2 and A0, calls PROM via vector,
     * and restores them. In C we rely on the calling convention
     * to handle this, but the PROM routine might clobber registers.
     */
#if defined(__m68k__) || defined(ARCH_M68K)
    register char ch __asm__("d1") = c;
    void (*putc_func)(void) = *PROM_PUTC_VECTOR;

    __asm__ volatile (
        "movem.l %%d0-%%d2/%%a0, -(%%sp)\n\t"
        "jsr (%0)\n\t"
        "movem.l (%%sp)+, %%d0-%%d2/%%a0"
        :
        : "a" (putc_func), "d" (ch)
        : "memory"
    );
#else
    /* Non-m68k stub for compilation testing */
    (void)c;
#endif
}

/*
 * crash_puts_string - Print formatted string to crash console
 *
 * Outputs a formatted string to the crash console. The string format
 * supports embedded hex values:
 *   - Normal chars (> 0): printed as-is
 *   - '%' (0x25): terminates string, prints CR/LF
 *   - NUL (0x00) followed by 2 bytes: prints as 4-digit hex
 *   - Negative byte (< 0) followed by 4 bytes: prints as 8-digit hex
 *
 * Original address: 0x00E1E7C8
 * Size: 74 bytes
 *
 * @param str: Pointer to format string
 */
static void crash_puts_string(const char *str)
{
    const uint8_t *p = (const uint8_t *)str;
    int8_t c;
    uint32_t hex_val;
    int nibbles;
    int i;
    uint8_t nibble;

    /* Ensure display is mapped and font is loaded */
    remap_display();
    call_prom_reload_font();

    while (1) {
        c = (int8_t)*p++;

        if (c > 0) {
            /* Normal printable character */
            if (c == ASCII_PERCENT) {
                /* '%' terminates string - print newline */
                call_prom_putc(ASCII_CR);
                call_prom_putc(ASCII_LF);
                return;
            }
            call_prom_putc(c);
        } else if (c < 0) {
            /* Negative byte: next 4 bytes are hex long */
            hex_val = ((uint32_t)p[0] << 24) |
                      ((uint32_t)p[1] << 16) |
                      ((uint32_t)p[2] << 8) |
                      (uint32_t)p[3];
            p += 4;
            nibbles = 8;  /* 8 hex digits */

            /* Print hex digits */
            for (i = 0; i < nibbles; i++) {
                /* Rotate left 4 bits to get next nibble */
                hex_val = (hex_val << 4) | (hex_val >> 28);
                nibble = hex_val & 0x0F;
                if (nibble >= 10) {
                    nibble += 7;  /* 'A' - '0' - 10 = 7 */
                }
                call_prom_putc('0' + nibble);
            }
        } else {
            /* NUL byte: next 2 bytes are hex word */
            hex_val = ((uint32_t)p[0] << 24) |
                      ((uint32_t)p[1] << 16);
            p += 2;
            nibbles = 4;  /* 4 hex digits */

            /* Print hex digits */
            for (i = 0; i < nibbles; i++) {
                hex_val = (hex_val << 4) | (hex_val >> 28);
                nibble = hex_val & 0x0F;
                if (nibble >= 10) {
                    nibble += 7;
                }
                call_prom_putc('0' + nibble);
            }
        }
    }
}

/*
 * CRASH_SHOW_STRING - Display a string during crash handling
 *
 * This function preserves ALL registers before calling crash_puts_string.
 * This is critical during crash handling to preserve the crash state
 * for debugging.
 *
 * Original address: 0x00E1E7B8
 * Size: 16 bytes
 *
 * Assembly:
 *   movem.l D0-D7/A0-A7,-(SP)   ; Save all registers
 *   movea.l (0x44,SP),A0        ; Get string pointer from original stack
 *   bsr     crash_puts_string   ; Call internal routine
 *   movem.l (SP)+,D0-D7/A0-A7   ; Restore all registers
 *   rts
 *
 * @param str: Pointer to format string
 */
void CRASH_SHOW_STRING(const char *str)
{
#if defined(__m68k__) || defined(ARCH_M68K)
    /*
     * We need to save ALL registers, call crash_puts_string,
     * then restore ALL registers. This is tricky in C because
     * the compiler may use registers before we can save them.
     *
     * The safest approach is inline assembly for the entire function,
     * but we can approximate by saving/restoring around the call.
     */
    __asm__ volatile (
        "movem.l %%d0-%%d7/%%a0-%%a6, -(%%sp)\n\t"
        "movea.l %0, %%a0\n\t"
        :
        : "g" (str)
        : "memory"
    );

    crash_puts_string(str);

    __asm__ volatile (
        "movem.l (%%sp)+, %%d0-%%d7/%%a0-%%a6"
        :
        :
        : "memory"
    );
#else
    /* Non-m68k: just call directly */
    crash_puts_string(str);
#endif
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

/* Error status codes for crash conditions */
status_$t Illegal_PID_Err = 0x00030001;
status_$t Illegal_WSL_Index_Err = 0x00030002;
status_$t WSL_Exhausted_Err = 0x00030003;
status_$t Inconsistent_MMAPE_Err = 0x00030004;
status_$t MMAP_Bad_Unavail_err = 0x00030005;
status_$t mmap_bad_avail = 0x00030006;
status_$t MMAP_Bad_Reclaim_Err = 0x00030007;
status_$t MMAP_Error_Examined_Max = 0x00030008;
status_$t Some_ASTE_Error = 0x00030009;
status_$t OS_PMAP_mismatch_err = 0x0003000A;
status_$t OS_MMAP_bad_install = 0x0003000B;
