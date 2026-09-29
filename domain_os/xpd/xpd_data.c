/*
 * xpd/xpd_data.c - XPD subsystem data definitions
 *
 * Original M68K addresses:
 *   XPD_$DATA            0x00EA5034  `D68 EA5034 XPD_$DATA size = 4E8`, a
 *                        MODULE_DATA block (xpd/xpd.h)
 *   xpd_$wire_limit      0x00E3238A  00 03
 *   PTR_PROC2_$DATA      0x00E3238C  00 EA 55 1C  (proc2/proc2.h)
 *   PTR_XPD_$DATA        0x00E32390  00 EA 50 34
 *   xpd_$find_asid_flag  0x00E5BDBE  00 00
 *   xpd_$response_two    0x00E5BDC0  00 02
 *   xpd_$unreg_response  0x00E75044  00 02
 *   xpd_$cleanup_event   0x00E7508A  00 03
 *   xpd_$cleanup_status  0x00E7508C  00 00 00 00
 */

#include "xpd/xpd_internal.h"

/* XPD_$DATA: Ghidra holds no bytes for it (uninitialised in the image);
 * XPD_$INIT zeroes it (xpd/init.c). */
MODULE_DATA_DEFINE(xpd_$data_t, XPD_$DATA, 0x00EA5034);

/*
 * 0x00E32388: 4e 75 | 00 03 | 00 ea 55 1c | 00 ea 50 34 - the tail of the
 * XPD_$INIT code segment.  MST_$WIRE_AREA gets the addresses of the two
 * pointer cells (the range to wire) and of the word 3 (its page limit).
 */
uint16_t xpd_$wire_limit = 0x0003;
/* The image cell holds the 32-bit VA of the block: as PTR_PROC2_$DATA
 * (proc2/proc2_data.c), ARCH_PTR_TO_VA_STATIC, not a host pointer. */
uint32_t PTR_XPD_$DATA = ARCH_PTR_TO_VA_STATIC(&XPD_$DATA, 0x00EA5034);

/*
 * 0x00E5BDBE: 00 00 00 02 - the tail of XPD_$SET_DEBUGGER (rts at
 * 0x00E5BDBC).  The zero byte is PROC2_$FIND_ASID's flag argument at every
 * call in the module (SET_DEBUGGER x3, CONTINUE_PROC, SET_ENABLE, GET_FP,
 * PUT_FP, GET_TARGET_INFO); the word 2 is the response SET_DEBUGGER and
 * SET_ENABLE hand XPD_$CONTINUE_PROC when they release a target.
 */
int8_t xpd_$find_asid_flag = 0;
xpd_$response_t xpd_$response_two = 2;

/* 0x00E75044: 00 02 - the same response, XPD_$UNREGISTER_DEBUGGER's own
 * cell (its rts is at 0x00E75042). */
xpd_$response_t xpd_$unreg_response = 2;

/* 0x00E7508A: 00 03 00 00 00 00 - XPD_$CLEANUP's event type (3) and the
 * zero status it posts (XPD_$CLEANUP's rts is at 0x00E75088). */
xpd_$event_type_t xpd_$cleanup_event = 3;
status_$t xpd_$cleanup_status = 0;
