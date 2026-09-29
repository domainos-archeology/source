/*
 * MAC_OS_$PUT_INFO - store a port's version/config record
 *
 * Rejects any record whose version is not 1, refuses a configuration that
 * duplicates another port's XNS address, and otherwise copies the eight-byte
 * record into MAC_OS_$PORT_TABLE.
 *
 * Original address: 0x00E0C228, size 296 bytes (0x00E0C228-0x00E0C34F).
 * A5 = 0x00E22990 (MAC_OS_$DATA).
 *
 * Nothing here is architecture specific, so the body is portable
 * (bead source-ht0n; it used to sit under "#if defined(ARCH_M68K)" and reach
 * ROUTE_$PORTP through the raw address 0xE26EE8).
 */

#include "mac_os/mac_os_internal.h"

/*
 * The comparison the duplicate check makes, at record + 0x20 with a 12-byte
 * stride (0x00E0C2B6-0x00E0C2E6): one longword then four words.  Those are
 * route_$port_t.xns_addr's bytes, but the walk is a byte-displacement one
 * because the count that drives it (record + 0x06) is still inside
 * route_$port_t._unknown0 and route/route.h has not named it.
 */
#define MAC_OS_PORT_ADDR_COUNT_OFFSET   0x06    /* word: how many addresses */
#define MAC_OS_PORT_ADDR_OFFSET         0x20    /* first address record */
#define MAC_OS_PORT_ADDR_STRIDE         0x0C    /* "lea (0xc,A2),A2" / "moveq #0xc,D4" */

static int mac_os_$port_addr_equal(const uint8_t *a, const uint8_t *b)
{
    return *(const uint32_t *)(a + 0x00) == *(const uint32_t *)(b + 0x00) &&
           *(const uint16_t *)(a + 0x04) == *(const uint16_t *)(b + 0x04) &&
           *(const uint16_t *)(a + 0x06) == *(const uint16_t *)(b + 0x06) &&
           *(const uint16_t *)(a + 0x08) == *(const uint16_t *)(b + 0x08) &&
           *(const uint16_t *)(a + 0x0A) == *(const uint16_t *)(b + 0x0A);
}

/*
 * MAC_OS_$PUT_INFO
 *
 * Parameters (0x08, 0x0C, 0x10 off A6):
 *   info       - the eight-byte mac_os_$port_info_t to install
 *   port_num   - pointer to the port number (0..7)
 *   status_ret - status return
 */
void MAC_OS_$PUT_INFO(mac_os_$port_info_t *info, int16_t *port_num,
                      status_$t *status_ret)
{
    int16_t         other_port;
    int16_t         port;
    const uint8_t  *port_rec;
    const uint8_t  *other_rec;
    int16_t         a;
    int16_t         b;
    int16_t         port_count;
    int16_t         other_count;

    /* 0x00E0C23E */
    *status_ret = status_$ok;

    /* 0x00E0C244-0x00E0C252: the version check runs BEFORE the lock is taken */
    if (info->version != 1) {
        *status_ret = status_$mac_invalid_port_version;
        return;
    }

    /* 0x00E0C256 */
    ML_$EXCLUSION_START(&MAC_OS_$EXCLUSION);

    /* 0x00E0C262-0x00E0C30C: "moveq #0x7,D0" plus dbf - all eight ports */
    for (other_port = 0; other_port < MAC_OS_MAX_PORTS; other_port++) {
        /* 0x00E0C26E: skip the port being configured */
        port = *port_num;
        if (other_port == port) {
            continue;
        }

        /*
         * 0x00E0C276-0x00E0C28C.  Both ROUTE_$PORTP slots are dereferenced
         * unconditionally; the image makes no null test here (bead
         * source-ht0n).
         */
        port_rec  = (const uint8_t *)ROUTE_$WIRED_DATA.portp[port];
        other_rec = (const uint8_t *)ROUTE_$WIRED_DATA.portp[other_port];

        /* 0x00E0C28E: the outer count comes from the port being configured */
        port_count = *(const int16_t *)(port_rec + MAC_OS_PORT_ADDR_COUNT_OFFSET);
        if (port_count - 1 < 0) {
            continue;
        }

        /* 0x00E0C29C: the inner count comes from the other port, read once */
        other_count = *(const int16_t *)(other_rec + MAC_OS_PORT_ADDR_COUNT_OFFSET);

        for (a = 0; a < port_count; a++) {
            /* 0x00E0C2A6: the inner loop is skipped when other_count is 0 */
            for (b = 0; b < other_count; b++) {
                const uint8_t *addr_a = port_rec + MAC_OS_PORT_ADDR_OFFSET +
                                        (int32_t)a * MAC_OS_PORT_ADDR_STRIDE;
                const uint8_t *addr_b = other_rec + MAC_OS_PORT_ADDR_OFFSET +
                                        (int32_t)b * MAC_OS_PORT_ADDR_STRIDE;

                if (mac_os_$port_addr_equal(addr_a, addr_b)) {
                    /*
                     * 0x00E0C2E8-0x00E0C2F2: the duplicate path branches
                     * straight to the epilogue at 0x00E0C346 and therefore
                     * RETURNS WITH MAC_OS_$EXCLUSION STILL HELD.  That is the
                     * image's behaviour, not a transcription slip.
                     */
                    *status_ret = status_$mac_XXX_unknown_2;
                    return;
                }
            }
        }
    }

    /*
     * 0x00E0C310-0x00E0C338: OS_$DATA_COPY(info, &MAC_OS_$PORT_TABLE[port], 8).
     * The length is pushed as a longword with "pea (0x8).w".
     */
    port = *port_num;
    OS_$DATA_COPY((char *)info, (char *)&MAC_OS_$PORT_TABLE[port], 8);

    /* 0x00E0C33C */
    ML_$EXCLUSION_STOP(&MAC_OS_$EXCLUSION);
}
