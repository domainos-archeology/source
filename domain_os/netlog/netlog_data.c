/*
 * NETLOG Data - Global Variables
 *
 * This file defines the global variables for the NETLOG subsystem.
 * On m68k, these are mapped to specific memory addresses.
 *
 * Original addresses:
 *   NETLOG_$OK_TO_LOG:        0xE248E0
 *   NETLOG_$OK_TO_LOG_SERVER: 0xE248E2
 *   NETLOG_$KINDS:            0xE248E4
 *   NETLOG_$EC:               0xE248E8
 *   NETLOG_$NODE:             0xE248F4
 *   NETLOG_$SOCK:             0xE248F8
 *   Internal data block:      0xE85684
 */

#include "netlog/netlog_internal.h"

/*
 * Logging control flags
 *
 * NETLOG_$OK_TO_LOG: Set to -1 (0xFF) when general logging is enabled
 * NETLOG_$OK_TO_LOG_SERVER: Set to -1 when server logging is enabled
 */
int8_t NETLOG_$OK_TO_LOG = 0;
int8_t NETLOG_$OK_TO_LOG_SERVER = 0;

/*
 * Bitmask of enabled log kinds
 *
 * Each bit (0-31) corresponds to a log category. When a bit is set,
 * events of that kind will be logged.
 *
 * Bits 20 and 21 are special: they control server-side logging.
 */
uint32_t NETLOG_$KINDS = 0;

/*
 * Event count for page-ready notifications
 *
 * Advanced when a buffer page fills up and is ready to send.
 * The network send process waits on this event count.
 */
ec_$eventcount_t NETLOG_$EC = { 0, NULL, NULL };

/*
 * Target logging server
 *
 * NETLOG_$NODE: Network node ID of the logging server
 * NETLOG_$SOCK: Socket number on the logging server
 */
uint32_t NETLOG_$NODE = 0;
uint16_t NETLOG_$SOCK = 0;

/*
 * Internal data structure (for non-m68k platforms)
 *
 * On m68k, this is at a fixed address (0xE85684) and accessed
 * via the NETLOG_DATA macro. On other platforms, we allocate it here.
 */
#if !defined(M68K)
netlog_data_t netlog_data = { 0 };
#endif

/*
 * Local node ID (for non-m68k platforms)
 */
#if !defined(M68K)
uint32_t NODE_$ME = 0;
#endif
