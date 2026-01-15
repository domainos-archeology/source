/*
 * NETWORK - Network Operations
 *
 * This module provides network operations for remote objects.
 */

#ifndef NETWORK_H
#define NETWORK_H

#include "base/base.h"

/*
 * Network global variables
 */
extern int8_t NETWORK_$REALLY_DISKLESS;
extern uid_t NETWORK_$PAGING_FILE_UID;

/*
 * NETWORK_$READ_AHEAD - Read pages ahead from network partner
 *
 * @param net_info       Network partner info
 * @param uid            UID information
 * @param ppn_array      Physical page number array
 * @param page_size      Page size
 * @param count          Number of pages
 * @param no_read_ahead  Disable read-ahead flag
 * @param flags          Operation flags
 * @param dtm            Data timestamp output
 * @param clock          Clock output
 * @param acl_info       ACL info output
 * @param status         Output status code
 *
 * @return Number of pages successfully read
 */
int16_t NETWORK_$READ_AHEAD(void *net_info, void *uid, uint32_t *ppn_array,
                            uint16_t page_size, int16_t count,
                            int8_t no_read_ahead, uint8_t flags,
                            int32_t *dtm, clock_t *clock,
                            uint32_t *acl_info, status_$t *status);

/*
 * NETWORK_$INSTALL_NET - Install network node
 *
 * @param node    Node ID to install
 * @param info    Node information
 * @param status  Output status code
 */
void NETWORK_$INSTALL_NET(uint32_t node, void *info, status_$t *status);

/*
 * NETWORK_$AST_GET_INFO - Get AST info for network object
 *
 * @param uid_info  UID information
 * @param flags     Output flags
 * @param attrs     Output attributes
 * @param status    Output status code
 */
void NETWORK_$AST_GET_INFO(void *uid_info, uint16_t *flags, void *attrs,
                           status_$t *status);

#endif /* NETWORK_H */
