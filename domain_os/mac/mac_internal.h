/*
 * MAC - Media Access Control Module - Internal Header
 *
 * Internal definitions for the MAC subsystem.
 */

#ifndef MAC_INTERNAL_H
#define MAC_INTERNAL_H

#include "ec/ec.h"
#include "fim/fim.h"
#include "mac/mac.h"
#include "mac_os/mac_os.h"   /* MAC_OS_$* lower-level operations */
#include "ml/ml.h"
#include "netbuf/netbuf.h"
#include "os/os.h"
#include "proc1/proc1.h"
#include "proc2/proc2.h"
#include "route/route.h"
#include "sock/sock.h"

/*
 * ============================================================================
 * Internal Constants
 * ============================================================================
 */

/* Port info table entry size */
#define MAC_PORT_INFO_SIZE 0x5C

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * mac_$copy_to_buffers (0x00E0BD2C) is a nested Pascal procedure of
 * MAC_$RECEIVE and reaches its parent's frame through a static link, so it is
 * emitted as a file-static function inside mac/receive.c rather than declared
 * here.
 */

/*
 * ============================================================================
 * Global Data References
 * ============================================================================
 */

#if defined(ARCH_M68K)
/* MAC exclusion lock (ml_$exclusion_t at base + 0x868) */
#define mac_$exclusion_lock (*(ml_$exclusion_t *)(MAC_$DATA_BASE + 0x868))

/* Port info table (at 0xE2E0A0, entries of 0x5C bytes) */
#define MAC_$PORT_INFO_BASE 0xE2E0A0
#define MAC_$PORT_INFO(port)                                                   \
  ((void *)(MAC_$PORT_INFO_BASE + (port) * MAC_PORT_INFO_SIZE))

/* Socket pointer array */
#define MAC_$SOCK_PTR_ARRAY ((void **)0xE28DB0)
#else
extern ml_$exclusion_t mac_$exclusion_lock;
extern void *mac_$port_info_table;
extern void **mac_$sock_ptr_array;
#endif

#endif /* MAC_INTERNAL_H */
