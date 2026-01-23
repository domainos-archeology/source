/*
 * DIR_$RESOLVE - Resolve a pathname relative to a directory
 *
 * Resolves a pathname string starting from a base directory UID.
 * Returns the resolved object's UID and type information.
 *
 * Original address: 0x00E4D356
 * Original size: 266 bytes
 */

#include "dir/dir_internal.h"

/*
 * Request structure for RESOLVE operation
 */
typedef struct {
    uint8_t   op;           /* Operation code: DIR_OP_RESOLVE (0x58) */
    uint8_t   padding[3];
    uid_t     start_uid;    /* Starting directory UID */
    uint16_t  reserved;     /* Reserved field */
    uint16_t  path_len;     /* Pathname length */
    uid_t     base_uid;     /* Base UID (copy of start_uid) */
    uid_t     resolved;     /* Resolved UID output location */
    uint16_t  param5;       /* Resolution parameter 5 */
    uint16_t  param6;       /* Resolution parameter 6 */
    uint16_t  param7;       /* Resolution parameter 7 */
    uint16_t  param8;       /* Resolution parameter 8 */
    void     *flags;        /* Resolution flags */
    /* Pathname data follows */
} Dir_$ResolveRequest;

/*
 * DIR_$RESOLVE - Resolve a pathname relative to a directory
 *
 * Iteratively resolves a pathname by calling DIR_$DO_OP with RESOLVE
 * operations. Handles symlinks and continuation by looping until
 * resolution is complete or an error occurs.
 *
 * Parameters:
 *   pathname    - The pathname to resolve
 *   path_len    - Pointer to pathname length (max 1023)
 *   start_uid   - Starting directory UID (in/out - updated on partial resolution)
 *   resolved_uid - Output: UID of resolved object
 *   param5-8    - Various resolution parameters (updated on output)
 *   flags       - Resolution flags
 *   link_count  - Output: link nesting count
 *   status_ret  - Output: status code
 */
void DIR_$RESOLVE(void *pathname, uint16_t *path_len, uid_t *start_uid,
                  uid_t *resolved_uid, uint16_t *param5, uint16_t *param6,
                  uint16_t *param7, uint16_t *param8, void *flags,
                  uint16_t *link_count, status_$t *status_ret)
{
    struct {
        uint8_t   op;
        uint8_t   padding[3];
        uid_t     uid1;
        uint8_t   gap1[0x80];
        uint16_t  plen;
        uid_t     uid2;
        uid_t     uid3;
        uint16_t  p5;
        uint16_t  p6;
        uint16_t  p7;
        uint16_t  p8;
        void     *fl;
    } request;
    Dir_$OpResponse response;
    uint16_t len;

    /* Initialize link count output */
    *link_count = 0;

    /* Validate pathname length */
    len = *path_len;
    if (len == 0 || len > DIR_MAX_PATH_LEN) {
        *status_ret = status_$naming_invalid_pathname;
        return;
    }

    /* Build initial request */
    request.op = DIR_OP_RESOLVE;
    request.plen = len;
    /* Reserved field */
    request.fl = flags;

    /* Resolution loop - continues until complete or error */
    do {
        /* Copy UIDs to request */
        request.uid1.high = start_uid->high;
        request.uid1.low = start_uid->low;
        request.uid2.high = start_uid->high;
        request.uid2.low = start_uid->low;
        request.uid3.high = resolved_uid->high;
        request.uid3.low = resolved_uid->low;
        request.p5 = *param5;
        request.p6 = *param6;
        request.p7 = *param7;
        request.p8 = *param8;

        /* Clear response flags */
        response.f14 = 0;

        /* Send the request */
        DIR_$DO_OP(&request.op, DAT_00e7fcfe, 0x34, &response, &request);

        /* Store status */
        *status_ret = response.status;

        /* Check if resolution is complete (no continuation) */
        if ((int8_t)response.f14 >= 0) {
            return;
        }

        /* Update output parameters from response */
        start_uid->high = response._22_4_;
        start_uid->low = response.f1a;
        resolved_uid->high = response._24_4_;
        resolved_uid->low = *((uint32_t *)&response + 9);  /* Next field */
        /* Extract parameters from response */
        *param5 = *((uint16_t *)((uint8_t *)&response + 0x1a));
        *param6 = *((uint16_t *)((uint8_t *)&response + 0x18));
        *param7 = *((uint16_t *)((uint8_t *)&response + 0x16));
        *param8 = *((uint16_t *)((uint8_t *)&response + 0x14));
        *link_count = *((uint16_t *)((uint8_t *)&response + 0x12));

    } while ((int8_t)response.f15 < 0);  /* Continue while loop flag is set */
}
