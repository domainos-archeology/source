/*
 * MAC_OS_$PUT_INFO - Store port information
 *
 * Stores port version/configuration information after validating
 * that the network parameters don't conflict with existing ports.
 *
 * Original address: 0x00E0C228
 * Original size: 296 bytes
 */

#include "mac_os/mac_os_internal.h"

/*
 * MAC_OS_$PUT_INFO
 *
 * This function validates and stores port configuration information.
 * It checks that the network configuration doesn't duplicate any
 * existing port's configuration by comparing network addresses.
 *
 * Parameters:
 *   info       - Port info structure:
 *                0x00: Version (must be 1)
 *                0x04: Configuration data
 *   port_num   - Pointer to port number (0-7)
 *   status_ret - Pointer to receive status code
 *
 * Assembly notes:
 *   - Uses A5 = 0xE22990 (MAC_OS_$DATA base)
 *   - Port info stored at base + 0x89C + port * 8
 *   - Validates against all other ports' configurations
 *   - Compares fields at offsets 0x20, 0x24, 0x26, 0x28, 0x2A of route_port
 */
void MAC_OS_$PUT_INFO(mac_os_$port_info_t *info, int16_t *port_num, status_$t *status_ret)
{
#if defined(ARCH_M68K)
    int16_t port;
    int16_t other_port;
    void **route_portp;
    void *route_port;
    void *other_route_port;

    *status_ret = status_$ok;

    /* Validate version field (must be 1) */
    if (info->version != 1) {
        *status_ret = status_$mac_invalid_port_version;
        return;
    }

    port = *port_num;
    route_portp = (void **)0xE26EE8;

    /* Enter exclusion region */
    ML_$EXCLUSION_START((ml_$exclusion_t *)MAC_OS_$EXCLUSION);

    /* Check for conflicts with other ports */
    for (other_port = 0; other_port < MAC_OS_MAX_PORTS; other_port++) {
        int16_t entry_idx;
        int16_t num_entries;

        /* Skip self */
        if (other_port == port) {
            continue;
        }

        /* Get route port for the port being configured */
        route_port = route_portp[port];
        if (route_port == NULL) {
            continue;
        }

        /* Get other port's route port */
        other_route_port = route_portp[other_port];
        if (other_route_port == NULL) {
            continue;
        }

        /* Compare network configurations */
        /* The original compares entries at offsets 0x20, 0x24, 0x26, 0x28, 0x2A */
        /* which appear to be network address fields */
        num_entries = *(int16_t *)((uint8_t *)route_port + 6);
        if (num_entries <= 0) {
            continue;
        }

        for (entry_idx = 0; entry_idx < num_entries; entry_idx++) {
            int16_t other_entry_idx;
            int16_t other_num_entries;
            uint8_t *cfg_ptr;

            other_num_entries = *(int16_t *)((uint8_t *)other_route_port + 6);
            if (other_num_entries <= 0) {
                continue;
            }

            cfg_ptr = (uint8_t *)route_port;

            for (other_entry_idx = 0; other_entry_idx < other_num_entries; other_entry_idx++) {
                uint8_t *other_cfg_ptr = (uint8_t *)other_route_port;

                /* Compare 5 fields: offset 0x20 (4 bytes), 0x24, 0x26, 0x28, 0x2A (2 bytes each) */
                if (*(uint32_t *)(cfg_ptr + 0x20) == *(uint32_t *)(other_cfg_ptr + 0x20) &&
                    *(uint16_t *)(cfg_ptr + 0x24) == *(uint16_t *)(other_cfg_ptr + 0x24) &&
                    *(uint16_t *)(cfg_ptr + 0x26) == *(uint16_t *)(other_cfg_ptr + 0x26) &&
                    *(uint16_t *)(cfg_ptr + 0x28) == *(uint16_t *)(other_cfg_ptr + 0x28) &&
                    *(uint16_t *)(cfg_ptr + 0x2A) == *(uint16_t *)(other_cfg_ptr + 0x2A)) {
                    /* Duplicate configuration found */
                    *status_ret = status_$mac_XXX_unknown_2;
                    /* Note: Original doesn't unlock before return - may be a bug */
                    goto done;
                }

                other_cfg_ptr += 0x0C;  /* Next entry in other port */
            }

            cfg_ptr += 0x0C;  /* Next entry in this port */
        }
    }

    /* Copy port info to storage */
    /* Original uses OS_$DATA_COPY to copy 8 bytes */
    {
        mac_os_$port_info_t *dest = &MAC_OS_$PORT_INFO_TABLE[port];
        OS_$DATA_COPY(info, dest, 8);
    }

done:
    ML_$EXCLUSION_STOP((ml_$exclusion_t *)MAC_OS_$EXCLUSION);
#else
    /* Non-M68K implementation stub */
    (void)info;
    (void)port_num;
    *status_ret = status_$mac_port_op_not_implemented;
#endif
}
