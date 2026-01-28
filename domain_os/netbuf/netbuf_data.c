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
 * Delay queue for waiting on buffers
 */
static time_queue_t netbuf_delay_q_storage;
time_queue_t *netbuf_delay_q = &netbuf_delay_q_storage;

/*
 * VA base address
 */
uint32_t netbuf_va_base = 0xD64C00;

#endif /* !M68K */

/*
 * Delay parameters (constant values)
 *
 * The delay type is 0 (relative time), and the delay time is a small value.
 * Original address: 0x00E0EEB2
 */
uint16_t NETBUF_$DELAY_TYPE = 0;

/*
 * Default delay time for waiting on buffers
 * TODO: Determine actual value from disassembly context
 */
clock_t NETBUF_$DELAY_TIME = { 0, 0x100 };

/*
 * Error status for crash
 */
status_$t netbuf_err = 0x00110000;  /* Network buffer error */
