/*
 * misc/crash_system.h - Fatal System Crash Handler
 *
 * CRASH_SYSTEM and the crash console it drives are hand-written assembly in
 * the SAU2 image (they save every register with `movem`, read the USP, take
 * their arguments in registers and end with `trap #15`).  The m68k build gets
 * them from misc/sau2/crash_system.s; misc/crash_system.c holds the crash
 * data block, which both builds share, plus a portable model of the console
 * used by the host unit tests.
 */

#ifndef MISC_CRASH_SYSTEM_H
#define MISC_CRASH_SYSTEM_H

#include "base/base.h"

/*
 * ===========================================================================
 * The crash report block (0x00E1E97E .. 0x00E1E9ED)
 * ===========================================================================
 *
 * CRASH_SYSTEM does not build a message: the message template and the live
 * crash fields are one contiguous block in the image, and the fields sit
 * *inside* the template where crash_puts_string will substitute them.  The
 * routine simply stores into those fields and hands the head of the block to
 * crash_puts_string.
 *
 * crash_puts_string's format bytes:
 *   0x01..0x24, 0x26..0x7F  printed literally
 *   0x25 ('%')              end of string: emit CR LF and return
 *   0x00                    the next 2 bytes are printed as 4 hex digits
 *   0x80..0xFF (negative)   the next 4 bytes are printed as 8 hex digits
 *
 * So the block renders as
 *   "\r\nCrash_Status <8 hex>  PC <8 hex> pid <4 hex>\r\n"
 *
 * Image layout (A5 = 0x00E1E700, the module base loaded by
 * `lea (-0xc,PC),A5` at 0x00E1E70A):
 *
 *   0x00E1E97E  +0x00  CR LF
 *   0x00E1E980  +0x02  "Crash_Status "
 *   0x00E1E98D  +0x0F  0xFF          32-bit field marker
 *   0x00E1E98E  +0x10  status        (0x28e,A5)  <- *status_p
 *   0x00E1E992  +0x14  "  PC "
 *   0x00E1E997  +0x19  0xFF          32-bit field marker
 *   0x00E1E998  +0x1A  pc            (0x298,A5)  <- return address at 0x42(SP)
 *   0x00E1E99C  +0x1E  " pid "
 *   0x00E1E9A1  +0x23  0x00          16-bit field marker
 *   0x00E1E9A2  +0x24  pid           (0x2a2,A5)  <- PROC1_$CURRENT (a word)
 *   0x00E1E9A4  +0x26  '%'           terminator
 *   0x00E1E9A5  +0x27  pad
 *   0x00E1E9A6  +0x28  regs[16]      D0-D7/A0-A7 as saved by the entry movem
 *   0x00E1E9E6  +0x68  reserved
 *   0x00E1E9EA  +0x6C  usp           (0x2ea,A5)  <- movec USP,D0
 *
 * The block is packed so that the same offsets hold in the host test build.
 *
 * The numeric members are on-image BIG-ENDIAN data, not host-native scalars:
 * crash_puts_string walks them a byte at a time, most significant first.  Use
 * BE32_CONST()/BE16_CONST() when reading or writing them (both are the
 * identity on the m68k target, so the assembly's plain stores are correct).
 * status is spelled uint32_t rather than status_$t because status_$t is
 * `long`, which is 8 bytes on a 64-bit host and would move every field after
 * it.
 */
typedef struct crash_report_t {
    uint8_t   lead[2];           /* +0x00: CR LF */
    char      status_label[13];  /* +0x02: "Crash_Status " (not NUL terminated) */
    uint8_t   status_fmt;        /* +0x0F: 0xFF - 32-bit hex field follows */
    uint32_t  status;            /* +0x10: the status that caused the crash */
    char      pc_label[5];       /* +0x14: "  PC " */
    uint8_t   pc_fmt;            /* +0x19: 0xFF */
    uint32_t  pc;                /* +0x1A: caller's return address (the "ECB") */
    char      pid_label[5];      /* +0x1E: " pid " */
    uint8_t   pid_fmt;           /* +0x23: 0x00 - 16-bit hex field follows */
    uint16_t  pid;               /* +0x24: PROC1_$CURRENT, a 16-bit value */
    uint8_t   terminator;        /* +0x26: '%' */
    uint8_t   pad;               /* +0x27 */
    uint32_t  regs[16];          /* +0x28: D0-D7 then A0-A7 */
    uint32_t  reserved_0x68;     /* +0x68 */
    uint32_t  usp;               /* +0x6C: user stack pointer */
} __attribute__((packed)) crash_report_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(crash_report_t, lead) == 0x00, "crash_report_t.lead");
_Static_assert(__builtin_offsetof(crash_report_t, status_label) == 0x02, "crash_report_t.status_label");
_Static_assert(__builtin_offsetof(crash_report_t, status_fmt) == 0x0F, "crash_report_t.status_fmt");
_Static_assert(__builtin_offsetof(crash_report_t, pc_label) == 0x14, "crash_report_t.pc_label");
_Static_assert(__builtin_offsetof(crash_report_t, pc_fmt) == 0x19, "crash_report_t.pc_fmt");
_Static_assert(__builtin_offsetof(crash_report_t, pid_label) == 0x1E, "crash_report_t.pid_label");
_Static_assert(__builtin_offsetof(crash_report_t, pid_fmt) == 0x23, "crash_report_t.pid_fmt");
_Static_assert(__builtin_offsetof(crash_report_t, pad) == 0x27, "crash_report_t.pad");
_Static_assert(__builtin_offsetof(crash_report_t, reserved_0x68) == 0x68, "crash_report_t.reserved_0x68");
#endif

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(crash_report_t, status) == 0x10, "crash_report_t.status");
_Static_assert(__builtin_offsetof(crash_report_t, pc) == 0x1A, "crash_report_t.pc");
_Static_assert(__builtin_offsetof(crash_report_t, pid) == 0x24, "crash_report_t.pid");
_Static_assert(__builtin_offsetof(crash_report_t, terminator) == 0x26, "crash_report_t.terminator");
_Static_assert(__builtin_offsetof(crash_report_t, regs) == 0x28, "crash_report_t.regs");
_Static_assert(__builtin_offsetof(crash_report_t, usp) == 0x6C, "crash_report_t.usp");
_Static_assert(sizeof(crash_report_t) == 0x70, "crash_report_t size");
#endif

/* The one crash report block; misc/sau2/crash_system.s writes into it. */
extern crash_report_t CRASH_REPORT;

/*
 * Stash for the caller's SR (0x00E1E7B6, i.e. (0xb6,A5)).  CRASH_SYSTEM
 * copies the SR it pushed at entry here just before restoring the register
 * block (0x00E1E788) and reloads it from here with `move (0x4,PC),SR` at
 * 0x00E1E7B0, after the trap #15 handler returns.
 */
extern uint16_t CRASH_SAVED_SR;

/* CRASH_SYSTEM's two "not really a crash" statuses (0x00E1E718/0x00E1E746) */
#define status_$system_reboot 0x001b0008

/*
 * CRASH_SYSTEM - Fatal system crash handler
 *
 * status 0                -> clean shutdown: jump to the PROM through the
 *                            vector at address 0x11C (PROM_$QUIET_RET_ADDR)
 *                            and never return.
 * status 0x001B0008       -> clean reboot: no message, no crash record.
 * anything else           -> print the crash report, leave a magic record at
 *                            0x00E00000, dump the registers, then trap #15
 *                            into the debugger.  If the debugger returns,
 *                            CRASH_SYSTEM restores SR and returns to its
 *                            caller (0x00E1E7B0..0x00E1E7B4).
 *
 * @param status_p: Pointer to the status code that caused the crash
 *
 * Original address: 0x00E1E700
 */
extern void CRASH_SYSTEM(const status_$t *status_p);

/*
 * CRASH_SHOW_STRING - Display a crash-console string
 *
 * Saves and restores every register around crash_puts_string so it can be
 * called from anywhere without disturbing the crash state.  The format is the
 * one described above (embedded hex fields, '%' terminator).
 *
 * Original address: 0x00E1E7B8
 * Size: 16 bytes
 */
extern void CRASH_SHOW_STRING(const char *str);

/*
 * crash_puts_string - crash console formatter (0x00E1E7C8)
 *
 * On the m68k target this is an assembly routine that takes its argument in
 * A0 and is entered with `bsr`; it is NOT C-callable there.  The host build
 * provides a C model with this signature for the unit tests.
 */
void crash_puts_string(const char *str);

/*
 * crash_putc - emit one character on the crash console (0x00E1E812)
 *
 * On the target this is `call_prom_putc`, an assembly thunk that saves
 * D0-D2/A0 and calls the PROM through the vector at address 0x108.  On the
 * host build the unit test supplies it.
 */
void crash_putc(char c);

/*
 * ===========================================================================
 * Status constants used with CRASH_SYSTEM
 * ===========================================================================
 *
 * None are exported.  In the image every caller passes `pea (d,PC)` to a
 * constant longword inside its own module, so each status lives as a
 * file-static `static const status_$t <name>_00eXXXXX` beside the call site
 * that takes its address (bead source-tzmw).  status_$system_reboot above is
 * the one exception: it is a value CRASH_SYSTEM itself compares against
 * (0x00E1E746), not a caller's cell.
 * ===========================================================================
 */

#endif /* MISC_CRASH_SYSTEM_H */
