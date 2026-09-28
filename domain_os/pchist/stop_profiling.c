/*
 * PCHIST_$STOP_PROFILING - Stop system-wide profiling
 *
 * Re-emitted from the image (0x00E5CD66..0x00E5CDB4, 80 bytes).  A nested
 * procedure of PCHIST_$CNTL: A5 (= 0xE2C204, PCHIST_$CONTROL) is inherited,
 * and 0x00E5CD6C `movea.l (A6),A2` loads the static link -- CNTL's frame --
 * so that (0x8,A2) at 0x00E5CD82 is CNTL's cmd pointer.  That uplevel
 * variable is the explicit `cmd_ptr` argument here.
 *
 *   00e5cd6e  tst.w (0x120,A5) / beq 0x00E5CDA2        ; sys_profiling_count
 *   00e5cd74  ML_$EXCLUSION_START(A5)
 *   00e5cd7e  subq.w #1,(0x120,A5)
 *   00e5cd82  movea.l (0x8,A2),A0 ; cmpi.w #3,(A0)     ; *cmd_ptr != 3 ->
 *   00e5cd8c  PCHIST_$ENABLE_TERMINAL(1)  (result slot)
 *   00e5cd98  ML_$EXCLUSION_STOP(A5)
 *   00e5cda2  movea.l A2,A1 ; bsr PCHIST_$UNWIRE_CLEANUP  ; same static link
 *   00e5cda8  clr.b (0x00e85c26).l                     ; HISTOGRAM+0x02: the
 *             HIGH byte of the doalign word (the byte CNTL sets with `st`)
 *
 * The old body tested PCHIST_$CONTROL.doalign instead of CNTL's command and
 * cleared PCHIST_$DOALIGN (CONTROL+0x126) instead of the histogram byte.
 *
 * Callers: PCHIST_$CNTL 0x00E5CDE4 and 0x00E5CF7E.
 *
 * Original address: 0x00e5cd66
 */

#include "pchist/pchist_internal.h"

void PCHIST_$STOP_PROFILING(int16_t *cmd_ptr)
{
    /* 0x00E5CD6E */
    if (PCHIST_$CONTROL.sys_profiling_count != 0) {
        ML_$EXCLUSION_START(&PCHIST_$CONTROL.lock);          /* 0x00E5CD74 */
        PCHIST_$CONTROL.sys_profiling_count--;               /* 0x00E5CD7E */
        if (*cmd_ptr != 3) {                                 /* 0x00E5CD82-0x00E5CD8A */
            PCHIST_$ENABLE_TERMINAL(1);                      /* 0x00E5CD92 */
        }
        ML_$EXCLUSION_STOP(&PCHIST_$CONTROL.lock);           /* 0x00E5CD9A */
    }

    /* 0x00E5CDA2-0x00E5CDA4 */
    PCHIST_$UNWIRE_CLEANUP();

    /* 0x00E5CDA8: clr.b 0xE85C26 */
    PCHIST_$HISTOGRAM.doalign = (int16_t)(PCHIST_$HISTOGRAM.doalign & 0x00FF);
}
