/*
 * NETBUF_$ADD_PAGES - Add pages to buffer pools
 *
 * Allocates physical pages and adds them to header and data buffer pools.
 *
 * Original address: 0x00E0E928
 *
 * Assembly analysis:
 *   - Link with local storage for page array (0x210 bytes = 132 uint32_t)
 *   - Parameter is packed: high word = header count, low word = data count
 *   - Caps header allocation at 0xB0 - current
 *   - Caps data allocation at dat_lim - dat_cnt
 *   - Crashes if total > 0x80
 *   - Calls WP_$CALLOC_LIST to allocate pages
 *   - Header buffers: calls NETBUF_$GETVA, links into HDR_TOP list
 *   - Data buffers: links page numbers via MMAPE next_vpn field
 */

#include "netbuf/netbuf_internal.h"

void NETBUF_$ADD_PAGES(uint32_t counts)
{
    int16_t hdr_requested;
    int16_t dat_requested;
    int16_t hdr_count;
    int16_t dat_count;
    int16_t total;
    int16_t i;
    uint32_t pages[NETBUF_MAX_ALLOC];
    ml_$spin_token_t token;
    uint32_t va;
    status_$t status;
    char *hdr_ptr;

    /* Unpack requested counts */
    hdr_requested = (int16_t)(counts >> 16);
    dat_requested = (int16_t)(counts & 0xFFFF);

    /* Acquire spin lock to check current allocations */
    token = ML_$SPIN_LOCK(&NETBUF_$SPIN_LOCK);

    /* Calculate how many header buffers we can allocate */
    if (NETBUF_$HDR_ALLOC >= NETBUF_HDR_MAX) {
        hdr_count = 0;
    } else {
        hdr_count = hdr_requested;
        if (hdr_count > NETBUF_HDR_MAX - NETBUF_$HDR_ALLOC) {
            hdr_count = NETBUF_HDR_MAX - NETBUF_$HDR_ALLOC;
        }
    }

    /* Calculate how many data buffers we can allocate */
    dat_count = dat_requested;
    if ((int32_t)dat_count > (int32_t)(NETBUF_$DAT_LIM - NETBUF_$DAT_CNT)) {
        dat_count = (int16_t)(NETBUF_$DAT_LIM - NETBUF_$DAT_CNT);
    }

    /* Check total doesn't exceed max allocation */
    total = hdr_count + dat_count;
    if (total > NETBUF_MAX_ALLOC) {
        CRASH_SYSTEM(&netbuf_err);
    }

    /* Update header allocation count */
    NETBUF_$HDR_ALLOC += hdr_requested;

    ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);

    /* Allocate physical pages if needed */
    if (total > 0) {
        WP_$CALLOC_LIST(total, pages);
    }

    /* Process header buffers */
    for (i = 0; i < hdr_count; i++) {
        /* Get virtual address mapping for this page */
        NETBUF_$GETVA(pages[i] << 10, &va, &status);
        if (status != status_$ok) {
            CRASH_SYSTEM(&status);
        }

        hdr_ptr = (char *)va;

        /* Clear the data area (offsets 0x3e8-0x3fb) */
        *(uint32_t *)(hdr_ptr + 0x3e8) = 0;
        *(uint32_t *)(hdr_ptr + 0x3ec) = 0;
        *(uint32_t *)(hdr_ptr + 0x3f0) = 0;
        *(uint32_t *)(hdr_ptr + 0x3f4) = 0;
        *(uint32_t *)(hdr_ptr + 0x3f8) = 0;

        /* Store physical address */
        *(uint32_t *)(hdr_ptr + NETBUF_HDR_PHYS_OFF) = pages[i] << 10;

        /* Link into header free list */
        token = ML_$SPIN_LOCK(&NETBUF_$SPIN_LOCK);
        *(uint32_t *)(hdr_ptr + NETBUF_HDR_NEXT_OFF) = NETBUF_$HDR_TOP;
        NETBUF_$HDR_TOP = va;
        ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);
    }

    /* Process data buffers */
    if (dat_count > 0) {
        /* Link data buffer pages together via MMAPE next_vpn field */
        for (i = hdr_count + 1; i < total; i++) {
            NETBUF_DAT_NEXT(pages[i]) = (uint16_t)pages[i - 1];
        }

        /* Link into data buffer free list */
        token = ML_$SPIN_LOCK(&NETBUF_$SPIN_LOCK);
        NETBUF_DAT_NEXT(pages[total - 1]) = (uint16_t)NETBUF_$DAT_TOP;
        NETBUF_$DAT_TOP = pages[hdr_count];
        NETBUF_$DAT_CNT += dat_count;
        ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);
    }

    /* If we over-allocated data buffers, trim back */
    if (NETBUF_$DAT_CNT > NETBUF_$DAT_LIM) {
        NETBUF_$DEL_PAGES(0, (int16_t)(NETBUF_$DAT_LIM - NETBUF_$DAT_CNT));
    }
}
