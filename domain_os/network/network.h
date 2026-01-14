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
 * @param partner    Network partner
 * @param uid_info   UID information for the object
 * @param ppn_array  Array of physical page numbers
 * @param count      Number of pages to read
 * @param offset     Starting offset
 * @param status     Output status code
 */
void NETWORK_$READ_AHEAD(void *partner, void *uid_info, uint32_t *ppn_array,
                         uint16_t count, uint32_t offset, status_$t *status);

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
