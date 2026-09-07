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
    /* A6-relative 2-byte cell passed as DIR_$DO_OP's fifth argument;
     * it is REM_FILE_$SEND_REQUEST's `received_len` out-parameter
     * (source-32ld). */
    uint16_t do_op_rcvd_len;
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

        /* 0xE4D3F2: clr.b (-0x2c,A6) - response offset 0x14, the
         * "resolution incomplete" byte, not the header byte at 0x02. */
        response.resolve.more = 0;

        /* Send the request */
        DIR_$DO_OP(&request.op, DAT_00e7fcfe, 0x34, &response, &do_op_rcvd_len);

        /* Store status */
        *status_ret = response.status;

        /* 0xE4D418: tst.b (-0x2c,A6) / bpl - response offset 0x14. */
        if (response.resolve.more >= 0) {
            return;
        }

        /* 0xE4D41E-0xE4D44A: every output comes out of the resolve variant of
         * the reply record; the offsets are 0x16, 0x1E, 0x26, 0x28, 0x2A, 0x2C
         * and 0x2E respectively. */
        start_uid->high = response.resolve.start_uid.high;
        start_uid->low = response.resolve.start_uid.low;
        resolved_uid->high = response.resolve.resolved_uid.high;
        resolved_uid->low = response.resolve.resolved_uid.low;
        *param5 = response.resolve.param5;
        *param6 = response.resolve.param6;
        *param7 = response.resolve.param7;
        *param8 = response.resolve.param8;
        *link_count = response.resolve.link_count;

        /* 0xE4D44E: tst.b (-0x2b,A6) / bmi - response offset 0x15. */
    } while (response.resolve.loop < 0);
}
