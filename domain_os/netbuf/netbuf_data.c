/*
 * NETBUF - Global Data
 *
 * This file contains global data declarations for the NETBUF subsystem.
 * On m68k, these are at fixed addresses. On other platforms, they are
 * allocated here.
 */

#include "netbuf/netbuf_internal.h"

#if !defined(ARCH_M68K)

/*
 * Global data structure for non-m68k platforms
 */
static netbuf_globals_t netbuf_globals_storage;
netbuf_globals_t *netbuf_globals = &netbuf_globals_storage;

/*
 * VA base address
 */
uint32_t netbuf_va_base = 0xD64C00;

#endif /* !M68K */

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
