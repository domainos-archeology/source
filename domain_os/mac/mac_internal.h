/*
 * MAC - Media Access Control Module - Internal Header
 *
 * Internal definitions for the MAC subsystem.
 */

#ifndef MAC_INTERNAL_H
#define MAC_INTERNAL_H

#include "mac/mac.h"
#include "ml/ml.h"
#include "sock/sock.h"
#include "ec/ec.h"
#include "fim/fim.h"
#include "proc1/proc1.h"
#include "proc2/proc2.h"
#include "route/route.h"
#include "netbuf/netbuf.h"
#include "os/os.h"

/*
 * ============================================================================
 * Internal Constants
 * ============================================================================
 */

/* Port info table entry size */
#define MAC_PORT_INFO_SIZE      0x5C

/*
 * ============================================================================
 * MAC_OS Functions (Lower-level MAC operations)
 * ============================================================================
 */

/*
 * MAC_OS_$OPEN - Open MAC at OS level
 * Original address: 0x00E0B246
 */
void MAC_OS_$OPEN(int16_t *port_num, void *params, status_$t *status_ret);

/*
 * MAC_OS_$CLOSE - Close MAC at OS level
 * Original address: 0x00E0B45C
 */
void MAC_OS_$CLOSE(uint16_t *channel, status_$t *status_ret);

/*
 * MAC_OS_$SEND - Send packet at OS level
 * Original address: 0x00E0B5A8
 */
void MAC_OS_$SEND(uint16_t *channel, void *pkt_desc, uint16_t *bytes_sent, status_$t *status_ret);

/*
 * MAC_OS_$DEMUX - Demux at OS level
 * Original address: 0x00E0B816
 */
void MAC_OS_$DEMUX(void *pkt_info, void *port_info, status_$t *status_ret);

/*
 * MAC_OS_$PROC2_CLEANUP - Process cleanup handler for MAC
 * Original address: 0x00E0BFDE
 */
void MAC_OS_$PROC2_CLEANUP(void);

/*
 * MAC_OS_$ARP - Perform ARP lookup
 *
 * Parameters:
 *   arp_table  - ARP table pointer
 *   port_num   - Port number
 *   pkt_desc   - Packet descriptor with destination address
 *   result     - Result buffer
 *   status_ret - Status return
 *
 * Original address: 0x00E0C0CE
 */
void MAC_OS_$ARP(void *arp_table, uint16_t port_num, void *pkt_desc,
                 void *result, status_$t *status_ret);

/*
 * MAC_OS_$PUT_INFO - Put MAC info
 * Original address: 0x00E0C228
 */
void MAC_OS_$PUT_INFO(void *info);

/*
 * MAC_OS_$INIT - Initialize MAC_OS subsystem
 * Original address: 0x00E2F4FC
 */
void MAC_OS_$INIT(void);

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * mac_$copy_to_buffers - Copy packet data to user buffers
 *
 * Copies data from a packet into a chain of user-provided buffers.
 * Uses parent's stack frame for buffer chain tracking.
 *
 * Parameters:
 *   src_ptr    - Pointer to source data pointer
 *   length     - Number of bytes to copy
 *
 * This is a nested procedure that accesses the caller's stack frame
 * for buffer chain state (offset -0x5C for current buffer, -0x72 for offset).
 *
 * Original address: 0x00E0BD2C
 */
void mac_$copy_to_buffers(void *src_ptr, int16_t length);

/*
 * NETBUF_$RTN_PKT - Return network packet buffers
 *
 * Returns packet header and data buffers to the pool.
 *
 * Parameters:
 *   hdr_buf    - Header buffer
 *   data_buf   - Data buffer
 *   info       - Buffer info
 *   length     - Data length
 *
 * Original address: 0x00E0F0C6
 */
void NETBUF_$RTN_PKT(void *hdr_buf, void *data_buf, void *info, uint16_t length);

/*
 * ============================================================================
 * Global Data References
 * ============================================================================
 */

#if defined(M68K)
/* MAC exclusion lock (ml_$exclusion_t at base + 0x868) */
#define mac_$exclusion_lock     (*(ml_$exclusion_t *)(MAC_$DATA_BASE + 0x868))

/* Port info table (at 0xE2E0A0, entries of 0x5C bytes) */
#define MAC_$PORT_INFO_BASE     0xE2E0A0
#define MAC_$PORT_INFO(port)    ((void *)(MAC_$PORT_INFO_BASE + (port) * MAC_PORT_INFO_SIZE))

/* Socket pointer array */
#define MAC_$SOCK_PTR_ARRAY     ((void **)0xE28DB0)
#else
extern ml_$exclusion_t mac_$exclusion_lock;
extern void *mac_$port_info_table;
extern void **mac_$sock_ptr_array;
#endif

#endif /* MAC_INTERNAL_H */
