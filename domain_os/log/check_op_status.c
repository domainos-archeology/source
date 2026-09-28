/*
 * log/check_op_status.c - log_$check_op_status (0x00E2FF7C, 84 bytes)
 *
 * Nested Pascal procedure of LOG_$INIT.  It declares one parameter (the
 * operation name at (0x8,A6)) and reaches LOG_$INIT's status variable
 * through the static link: `movea.l (A6),A2` (0x00E2FF82) then
 * `tst.w (-0x42,A2)` / `pea (-0x44,A2)` - the slot LOG_$INIT passes every
 * callee as `&status`.  The flattening passes that uplevel variable
 * explicitly, so the address VFMT_$WRITE10 receives is the caller's own.
 *
 * The three VFMT_$WRITE10 calls form ONE message: a format ending in "%$"
 * continues in the next call and "%." ends it, which is why LOG_$INIT's
 * operation names are themselves "...%$" strings and the middle call has to
 * supply two placeholder arguments.
 */

#include "log/log_internal.h"

/* 0x00E2FFD4: "%/%/Warning: Status %lh, Unable to %$" NUL
 * (pea (0x40,PC) at 0x00E2FF92) */
static const char log_$msg_unable_to[] =
    "%/%/Warning: Status %lh, Unable to %$";

/* 0x00E30000: " %a -- error logging disabled.%." (no terminator: the path
 * follows at 0x00E30020; pea (0x42,PC) at 0x00E2FFBC) */
static const char log_$msg_logging_disabled[] =
    " %a -- error logging disabled.%.";

int8_t log_$check_op_status(const char *op, status_$t *status)
{
    /* 0x00E2FF84-0x00E2FF88: only the HIGH word of the status is tested. */
    if ((int16_t)(*status >> 16) == 0) {
        return 0;                               /* 0x00E2FFCA clr.b D0b */
    }

    /* 0x00E2FF8A-0x00E2FF9C: "%lh" takes &status; the third argument is
     * the zero cell at 0x00E2FFFC (pea (0x70,PC)). */
    VFMT_$WRITE10(log_$msg_unable_to, status, &LOG_$VFMT_NO_ARG);

    /* 0x00E2FFA0-0x00E2FFB0: the operation name as the continuation
     * format, with the same zero cell pushed twice (`move.l (SP),-(SP)`). */
    VFMT_$WRITE10(op, &LOG_$VFMT_NO_ARG, &LOG_$VFMT_NO_ARG);

    /* 0x00E2FFB4-0x00E2FFC0: "%a" takes the path at 0x00E30020 and its
     * LONG length at 0x00E30044. */
    VFMT_$WRITE10(log_$msg_logging_disabled, log_$logfile_path,
                  &log_$logfile_path_len_l);

    return -1;                                  /* 0x00E2FFC6 st D0b */
}
