/*
 * NETBUF - Global Data
 *
 * This file contains global data declarations for the NETBUF subsystem,
 * the same on every build (source-702z).
 */

#include "netbuf/netbuf_internal.h"

/*
 * NETBUF_$DATA, 0x00E245A8..0x00E248DF (`gsk read 0xE245A8 0x338`): zero
 * except the delay NETBUF_$GET_HDR / _GET_DAT hand TIME_$WAIT,
 *
 *   +0x300  00 00 00 00 80 00      delay_time = { high 0, low 0x8000 }
 */
MODULE_DATA_DEFINE_INIT(netbuf_globals_t, NETBUF_$DATA, 0x00E245A8, {
    .delay_time = { .high = 0, .low = 0x8000 },
});

/*
 * TIME_$WAIT's delay-type constant: the zero word at 0x00E0EEB2, in the code
 * region between NETBUF_$GET_HDR's rts and NETBUF_$GET_DAT_COND.  The delay
 * value itself is not a constant - it lives in the globals at +0x300 and is
 * reached through NETBUF_$DELAY_TIME.
 */
uint16_t NETBUF_$DELAY_TYPE = 0;

/*
 * Error status for crash
 */
/*
 * The constant longword at 0x00E0E924, reached from NETBUF_$ADD_PAGES with
 * "pea (-0x6c,PC)" at 0x00E0E98E: 0x00110001 = "buffer error".
 */
status_$t netbuf_err = 0x00110001;  /* "buffer error" */
