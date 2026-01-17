/*
 * HINT_$GET_NET - Get network port from hint file
 *
 * Retrieves the network port stored in the hint file header, if the
 * hint file is valid and the network info matches the current node.
 *
 * Original address: 0x00E49CC0
 */

#include "hint/hint_internal.h"

void HINT_$GET_NET(uint32_t *port_ret)
{
    hint_file_t *hintfile;
    uint16_t *net_info;
    uint16_t *route_info;

    /* Default to 0 */
    *port_ret = 0;

    hintfile = HINT_$HINTFILE_PTR;

    /* Check if hint file is mapped */
    if (hintfile == NULL) {
        return;
    }

    /* Check if version indicates uninitialized */
    if (hintfile->header.version == HINT_FILE_UNINIT) {
        return;
    }

    /* Check if network info matches current node */
    net_info = (uint16_t *)&hintfile->header.net_info;
    route_info = (uint16_t *)(ROUTE_$PORTP + 0x2E);

    if (net_info[0] != route_info[0]) {
        return;
    }

    if (net_info[1] != route_info[1]) {
        return;
    }

    /* Network matches - return the stored port */
    *port_ret = hintfile->header.net_port;
}
