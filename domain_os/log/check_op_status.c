/*
 * log_$check_op_status - Check operation status and report errors
 *
 * Checks the status code from a log operation. If non-zero, prints
 * an error message and returns -1 (0xFF). Otherwise returns 0.
 *
 * This function accesses the status from the caller's stack frame,
 * which is a pattern from the original Pascal implementation.
 *
 * Original address: 00e2ff7c
 * Original size: 84 bytes
 *
 * Assembly:
 *   00e2ff7c    link.w A6,-0x8
 *   00e2ff80    pea (A2)
 *   00e2ff82    movea.l (A6),A2         ; get caller's frame pointer
 *   00e2ff84    tst.w (-0x42,A2)        ; check status (high word)
 *   00e2ff88    beq.b success
 *   00e2ff96    jsr ERROR_$PRINT        ; print error
 *   00e2ffc6    st D0b                  ; return -1
 *   00e2ffca    clr.b D0b               ; return 0
 *
 * Note: The original code accesses the caller's status variable at
 * a fixed offset from the frame pointer. In this C implementation,
 * we use a global status variable that should be set before calling.
 */

#include "log/log_internal.h"

/*
 * The original code accessed status through the caller's frame pointer.
 * This global variable should be set by the caller before calling this
 * function. This is a workaround for the Pascal calling convention.
 */
extern status_$t log_$last_status;

/* Error message format strings from original binary */
static const char msg_warning[] = "\n   Warning: Status %lh  Unable to ";
static const char msg_suffix[] = " - error logging disabled.\n";
static const char log_path[] = "//node_data/system_logs/sys_error";

int8_t log_$check_op_status(const char *op)
{
    /* Get high word of status - non-zero indicates error */
    int16_t status_high = (int16_t)(log_$last_status >> 16);

    if (status_high == 0) {
        return 0;  /* Success */
    }

    /* Print error message */
    ERROR_$PRINT(msg_warning, &log_$last_status, (void *)0x00e2fffc);
    ERROR_$PRINT(op, (void *)0x00e2fffc);
    ERROR_$PRINT(msg_suffix, log_path, (void *)0x00e30044);

    return (int8_t)-1;  /* Error (0xFF) */
}
