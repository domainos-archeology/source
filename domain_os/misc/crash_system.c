/*
 * misc/crash_system.c - Crash report data block (and the host model)
 *
 * CRASH_SYSTEM (0x00E1E700) and the crash console it drives are hand-written
 * assembly; the m68k build gets them from misc/sau2/crash_system.s.  What
 * lives here is:
 *
 *   - CRASH_REPORT, the message template with the live crash fields embedded
 *     in it (0x00E1E97E), and CRASH_SAVED_SR (0x00E1E7B6).  Both builds share
 *     these; the assembly writes into them by name.
 *   - Under !ARCH_M68K, a portable C model of crash_puts_string,
 *     CRASH_SHOW_STRING and CRASH_SYSTEM so the formatter can be unit tested
 *     against the real template bytes on the host.
 *   - The status constants callers hand to CRASH_SYSTEM (see the caveat in
 *     misc/crash_system.h and bead source-tzmw).
 */

#include "misc/misc_internal.h"
#include "base/base.h"
#include "proc1/proc1.h"
#include "time/time.h"

/*
 * ===========================================================================
 * The crash report block, 0x00E1E97E .. 0x00E1E9ED
 *
 * The bytes below are the template exactly as it appears in the image; the
 * status/pc/pid/regs/usp members are the live fields CRASH_SYSTEM stores into
 * before handing &CRASH_REPORT to crash_puts_string.  See crash_report_t in
 * misc/crash_system.h for the offset-by-offset mapping.
 * ===========================================================================
 */
crash_report_t CRASH_REPORT = {
    .lead         = { 0x0d, 0x0a },
    .status_label = { 'C', 'r', 'a', 's', 'h', '_',
                      'S', 't', 'a', 't', 'u', 's', ' ' },
    .status_fmt   = 0xff,
    .status       = 0,
    .pc_label     = { ' ', ' ', 'P', 'C', ' ' },
    .pc_fmt       = 0xff,
    .pc           = 0,
    .pid_label    = { ' ', 'p', 'i', 'd', ' ' },
    .pid_fmt      = 0x00,
    .pid          = 0,
    .terminator   = '%',
    .pad          = 0x00,
    .regs         = { 0 },
    .reserved_0x68 = 0,
    .usp          = 0,
};

/* 0x00E1E7B6: the SR CRASH_SYSTEM pushed at entry, reloaded after trap #15 */
uint16_t CRASH_SAVED_SR;

#if !defined(ARCH_M68K)

/*
 * ===========================================================================
 * Portable model of the crash console (host build / unit tests only)
 *
 * On the target these are the register-convention routines in
 * misc/sau2/crash_system.s.  The logic below is a faithful C rendering of
 * crash_puts_string at 0x00E1E7C8; the display remap and the PROM calls have
 * no host equivalent, so the model emits through crash_putc(), which the
 * caller (the unit test) supplies.
 * ===========================================================================
 */

/* 0x00E1E7E0 / 0x00E1E7E4 */
#define CRASH_ASCII_CR 0x0d
#define CRASH_ASCII_LF 0x0a

/* 0x00E1E7D6: the '%' that terminates a crash string */
#define CRASH_STRING_TERMINATOR 0x25

void crash_puts_string(const char *str)
{
    const uint8_t *p = (const uint8_t *)str;

    for (;;) {
        /* 0xE1E7D2: move.b (A0)+,D1b - the byte is tested SIGNED */
        int8_t c = (int8_t)*p++;
        uint32_t value;
        int nibbles;
        int i;

        if (c > 0) {
            if (c == CRASH_STRING_TERMINATOR) {
                /* 0xE1E7E0: '%' ends the string with CR LF */
                crash_putc(CRASH_ASCII_CR);
                crash_putc(CRASH_ASCII_LF);
                return;
            }
            crash_putc((char)c);
            continue;
        }

        if (c < 0) {
            /* 0xE1E7F4: a negative byte introduces a 32-bit hex field */
            value = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
                  | ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
            p += 4;
            nibbles = 8;
        } else {
            /*
             * 0xE1E7EC: a 0x00 byte introduces a 16-bit hex field, loaded
             * into the low word and swapped into the high half so that the
             * same rol.l #4 loop prints it.
             */
            value = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16);
            p += 2;
            nibbles = 4;
        }

        /* 0xE1E7F8: rol.l #4 then take the low nibble, MSB first */
        for (i = 0; i < nibbles; i++) {
            uint8_t nibble;
            value = (value << 4) | (value >> 28);
            nibble = (uint8_t)(value & 0x0f);
            if (nibble >= 10) {
                nibble = (uint8_t)(nibble + 7);   /* 0xE1E804: addq.b #7 */
            }
            crash_putc((char)(nibble + 0x30));    /* 0xE1E806 */
        }
    }
}

void CRASH_SHOW_STRING(const char *str)
{
    /* 0xE1E7B8: the register save/restore has no host counterpart */
    crash_puts_string(str);
}

void CRASH_SYSTEM(const status_$t *status_p)
{
    uint32_t status = (uint32_t)*status_p;

    /* 0xE1E712 */
    CRASH_REPORT.status = BE32_CONST(status);

    if (status != (uint32_t)status_$ok && status != status_$system_reboot) {
        /*
         * 0xE1E722 / 0xE1E728.  On the target `pc` is the caller's return
         * address read straight off the stack at 0x42(SP); the host model
         * uses the builtin, which is the same value.
         */
        CRASH_REPORT.pc =
            BE32_CONST((uint32_t)(uintptr_t)__builtin_return_address(0));
        CRASH_REPORT.pid = BE16_CONST(PROC1_$CURRENT);
        crash_puts_string((const char *)&CRASH_REPORT);
    }

    /*
     * 0xE1E738..0xE1E75C writes the crash record at 0x00E00000 and
     * 0xE1E760..0xE1E776 dumps the register block and the USP; neither has a
     * host equivalent, and 0xE1E77C onwards either jumps to the PROM or takes
     * trap #15.  The target behaviour is in misc/sau2/crash_system.s.
     */
}

#endif /* !ARCH_M68K */

/*
 * ===========================================================================
 * Status constants passed to CRASH_SYSTEM
 * ===========================================================================
 *
 * There are none here, and there never were any in the image.  Every caller of
 * CRASH_SYSTEM passes `pea (d,PC)` to a constant longword sitting in its own
 * module's code region, so each status is a file-static constant next to the
 * call site that uses it, named after the cell address (bead source-tzmw).
 * Grep for `_00e` in the callers to find them.
 * ===========================================================================
 */
