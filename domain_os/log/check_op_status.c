/*
 * log_$check_op_status - Check a LOG_$INIT operation status and report errors
 *
 * Nested Pascal procedure of LOG_$INIT.  It declares exactly one parameter
 * (the operation name at (0x8,A6)) and reaches its enclosing scope through
 * the static link: "movea.l (A6),A2" loads LOG_$INIT's frame pointer and the
 * status variable is read at (-0x44,A2) - the same slot LOG_$INIT passes to
 * NAME_$RESOLVE, MST_$MAPS, FILE_$LOCK, etc. as "&status".
 *
 * The flattening passes that uplevel variable explicitly as a pointer, so the
 * ERROR_$PRINT call that takes its address (0xE2FF8E "pea (-0x44,A2)") stays
 * faithful.  (Previously it went through a file-scope log_$last_status copy;
 * bead source-5pu.)
 *
 * Original address: 00e2ff7c
 * Original size: 84 bytes
 *
 * Assembly:
 *   00e2ff7c    link.w A6,-0x8
 *   00e2ff80    pea (A2)
 *   00e2ff82    movea.l (A6),A2          ; static link -> LOG_$INIT's frame
 *   00e2ff84    tst.w (-0x42,A2)         ; HIGH word of the caller's status
 *   00e2ff88    beq.b 0x00e2ffca
 *   00e2ff8a    pea (0x70,PC)            ; &LOG_$VFMT_NO_ARG   (0xE2FFFC)
 *   00e2ff8e    pea (-0x44,A2)           ; &status
 *   00e2ff92    pea (0x40,PC)            ; log_$msg_unable_to  (0xE2FFD4)
 *   00e2ff96    jsr ERROR_$PRINT
 *   00e2ffa0    pea (0x5a,PC)            ; &LOG_$VFMT_NO_ARG   (0xE2FFFC)
 *   00e2ffa4    move.l (SP),-(SP)        ; ...pushed a second time
 *   00e2ffa6    move.l (0x8,A6),-(SP)    ; op (used as the continuation format)
 *   00e2ffaa    jsr ERROR_$PRINT
 *   00e2ffb4    pea (0x8e,PC)            ; &log_$logfile_path_len_l (0xE30044)
 *   00e2ffb8    pea (0x66,PC)            ; log_$logfile_path        (0xE30020)
 *   00e2ffbc    pea (0x42,PC)            ; log_$msg_logging_disabled(0xE30000)
 *   00e2ffc0    jsr ERROR_$PRINT
 *   00e2ffc6    st D0b                   ; return 0xFF
 *   00e2ffca    clr.b D0b                ; return 0
 *
 * The three ERROR_$PRINT calls form ONE message: the Apollo VFMT "%$"
 * directive at the end of a format means "the format continues in the next
 * call", and "%." ends it.  That is why the operation names in LOG_$INIT are
 * themselves "...%$" strings and why the middle call has to supply two
 * placeholder arguments.
 */

#include "log/log_internal.h"
#include "vfmt/vfmt.h"

/*
 * PC-relative constant cells in the code region, reproduced verbatim.
 *
 * 0xE2FFD4: "%/%/Warning: Status %lh, Unable to %$"
 * 0xE30000: " %a -- error logging disabled.%."
 * 0xE30020: "`node_data/system_logs/sys_error_log"  (36 characters)
 * 0xE30044: 0x00000024 - the length of that path, passed by reference
 */
static const char log_$msg_unable_to[] =
    "%/%/Warning: Status %lh, Unable to %$";
static const char log_$msg_logging_disabled[] =
    " %a -- error logging disabled.%.";
static const char log_$logfile_path[] =
    "`node_data/system_logs/sys_error_log";
static const int32_t log_$logfile_path_len_l = 36;

int8_t log_$check_op_status(const char *op, status_$t *status)
{
    /*
     * 0xE2FF84: tst.w (-0x42,A2) - only the HIGH word of the 32-bit status
     * is tested, i.e. the subsystem/module half.
     */
    if ((int16_t)(*status >> 16) == 0) {
        return 0;                       /* 0xE2FFCA: clr.b D0b */
    }

    ERROR_$PRINT(log_$msg_unable_to, status, &LOG_$VFMT_NO_ARG);
    ERROR_$PRINT(op, &LOG_$VFMT_NO_ARG, &LOG_$VFMT_NO_ARG);
    ERROR_$PRINT(log_$msg_logging_disabled, log_$logfile_path,
                 &log_$logfile_path_len_l);

    return (int8_t)-1;                  /* 0xE2FFC6: st D0b */
}
