/*
 * OS_$BOOT_ERRCHK - report a boot-time error status
 *
 * Original address: 0x00E34B14 (SAU2 map: OS_ code segment)
 * Size: 164 bytes (0x00E34B14 .. 0x00E34BB7); its two constant cells
 * follow at 0x00E34BB8.
 *
 * A status whose LOW word is zero is "no error" and answers true.  Any
 * other status is formatted with the caller's message, the caller's
 * second string and the status itself into a 100-byte line, shown on the
 * crash console, followed by a fixed TIME_$WAIT; the answer is then false.
 *
 * Frame (link.w A6,-0x84; D2/A2/A5 saved; A5 = 0xE351F4, the OS data
 * segment):
 *   (0x8,A6)   msg         pointer -> A2; a %a string, its length is the
 *                          index of its first '%' (see below)
 *   (0xc,A6)   arg_str     pointer; a %a string
 *   (0x10,A6)  arg_len_p   pointer to a word -> D2w, sign-extended into
 *                          (-0x6c,A6) and used as arg_str's length
 *   (0x14,A6)  status_p    pointer; copied to (-0x7c,A6)
 *   (-0x68,A6) line        the 100-byte output buffer
 *   (-0x70,A6) msg_len     longword, 0 unless a '%' is found
 *   (-0x78,A6) wait_status status_$t TIME_$WAIT fills in
 *   (-0x7e,A6) out_len     word VFMT_$FORMATN fills in
 *
 *   0x00E34B30  move.l (A1),(-0x7c,A6) / tst.w (-0x7a,A6) / beq   low word zero -> true
 *   0x00E34B3A  clr.l (-0x70,A6)
 *   0x00E34B3E  moveq #0x31,D1 / moveq #1,D0                       50 probes, 1-based index
 *   0x00E34B42    cmpi.b #0x25,(-0x1,A2,D0*0x1) / bne              msg[D0-1] == '%':
 *   0x00E34B4A      subq.l #1,D0 / move.l D0,(-0x70,A6) / bra        msg_len = D0-1
 *   0x00E34B52    addq.l #1,D0 / dbf D1w
 *   0x00E34B58  ext.l D2 / move.l D2,(-0x6c,A6)
 *   0x00E34B5E  VFMT_$FORMATN(fmt 0x00E34BBA, line, &max 0x00E34BB8, &out_len,
 *                             msg, &msg_len, arg_str, &arg_len, &status)
 *   0x00E34B8A  CRASH_SHOW_STRING(line)
 *   0x00E34B96  TIME_$WAIT(&type 0x00E347CA, &(0x180,A5) = 0x00E35374, &wait_status)
 *   0x00E34BA8  clr.b D0b                                           false
 *   0x00E34BAC  st D0b                                              true
 *
 * Re-emitted from the disassembly 2026-09-27: the previous C tested the
 * HIGH word of the status, advanced msg_len past the end when no '%' was
 * found, passed the VFMT arguments by value with an invented format
 * string, and waited on a zero local instead of the module's clock cell.
 */

#include "os/os_internal.h"
#include "misc/crash_system.h"
#include "time/time.h"
#include "vfmt/vfmt.h"

/* 0x00E34BB8: 00 64 - the maximum length handed to VFMT_$FORMATN */
static const int16_t os_$boot_errchk_max_len_00e34bb8 = 100;
/* 0x00E34BBA: the format, '%$'-terminated */
static const char os_$boot_errchk_fmt_00e34bba[] = "%/%/%a %a -- %lh%/%%%$";
/* 0x00E347CA: 00 00 - TIME_$WAIT's delay type (relative) */
static const uint16_t os_$boot_errchk_wait_type_00e347ca = 0;
/*
 * 0x00E35374: 00 00 00 05 00 00 (00 00) - the clock_t at +0x180 of the OS
 * data segment (`D E351F4 OS size = 188`), its last cell; this routine is
 * its only reader (`pea (0x180,A5)` at 0x00E34B9A).
 */
static clock_t os_$boot_errchk_wait_00e35374 = { 0x00000005u, 0x0000u };

char OS_$BOOT_ERRCHK(const char *format_str, const char *arg_str,
                     short *line_ptr, status_$t *status)
{
    status_$t local_status;         /* (-0x7c,A6) */
    int32_t msg_len;                /* (-0x70,A6) */
    int32_t arg_len;                /* (-0x6c,A6) */
    char line[100];                 /* (-0x68,A6) */
    int16_t out_len;                /* (-0x7e,A6) */
    status_$t wait_status;          /* (-0x78,A6) */
    int16_t arg_len_w;              /* D2w */
    int32_t idx;                    /* D0 */
    int16_t i;                      /* D1w */

    /* 0x00E34B2E .. 0x00E34B38 */
    arg_len_w = *line_ptr;
    local_status = *status;
    if ((local_status & 0xFFFF) == 0) {
        return (char)0xFF;                                  /* 0x00E34BAC st D0b */
    }

    /* 0x00E34B3A .. 0x00E34B54: index of the first '%' in the first 50
     * characters, 0 when there is none */
    msg_len = 0;
    idx = 1;
    for (i = 0x31; i != -1; i--) {
        if (format_str[idx - 1] == '%') {
            idx--;
            msg_len = idx;
            break;
        }
        idx++;
    }

    /* 0x00E34B58 .. 0x00E34B5A */
    arg_len = (int32_t)arg_len_w;

    /* 0x00E34B5E .. 0x00E34B86: nine arguments, every one by reference */
    VFMT_$FORMATN(os_$boot_errchk_fmt_00e34bba, line,
                  (int16_t *)&os_$boot_errchk_max_len_00e34bb8, &out_len,
                  format_str, &msg_len, arg_str, &arg_len, &local_status);

    /* 0x00E34B8A .. 0x00E34B94 */
    CRASH_SHOW_STRING(line);

    /* 0x00E34B96 .. 0x00E34BA2: no stack cleanup follows; unlk discards */
    TIME_$WAIT((uint16_t *)&os_$boot_errchk_wait_type_00e347ca,
               &os_$boot_errchk_wait_00e35374, &wait_status);

    return 0;                                               /* 0x00E34BA8 clr.b D0b */
}
