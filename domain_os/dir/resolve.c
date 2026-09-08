/*
 * DIR_$RESOLVE - Resolve a pathname relative to a directory
 *
 * Original address: 0x00E4D356
 * Original size: 266 bytes
 */

#include "dir/dir_internal.h"

/*
 * DIR_$RESOLVE (0x00E4D356)
 *
 * Builds a DIR_OP_RESOLVE request and sends it through DIR_$DO_OP, looping
 * while the reply asks for another round.  There is no legacy fallback.
 *
 * Frame: `link.w A6,-0x1f4` - request base A6-0x1F0, reply A6-0x40
 * (0x34 bytes), received-length word A6-0x1F2.
 *
 * The pathname itself is NOT copied into the request: 0x00E4D3BA stores the
 * caller's pointer at +0x8E.  The subject uid is written twice, once into
 * the request header at +0x04 and once into the body at +0x94, and BOTH are
 * rewritten on every loop iteration (the loop head is 0x00E4D3C0).
 *
 * Parameters (A6+0x08..A6+0x30):
 *   pathname     - pathname text (its address goes into the request)
 *   path_len     - pointer to the pathname length (1..0x3FF)
 *   start_uid    - in/out: directory to resolve from
 *   resolved_uid - in/out: the resolved object
 *   param5..8    - in/out words
 *   flags        - resolution flags longword, by value, copied to +0xAC
 *   link_count   - out: cleared on entry, reloaded from reply+0x2E
 *   status_ret   - out: status code
 */
void DIR_$RESOLVE(void *pathname, uint16_t *path_len, uid_t *start_uid,
                  uid_t *resolved_uid, uint16_t *param5, uint16_t *param6,
                  uint16_t *param7, uint16_t *param8, uint32_t flags,
                  uint16_t *link_count, status_$t *status_ret)
{
    dir_$do_op_request_t request;
    Dir_$OpResponse response;
    /* A6-0x1F2: DIR_$DO_OP's `received_len` out-parameter (source-32ld). */
    uint16_t do_op_rcvd_len;
    uint16_t plen;

    /* 0x00E4D388: `clr.w (A0)` on the link-count cell. */
    *link_count = 0;

    /* 0x00E4D38E-0x00E4D396: 1..0x3FF, unsigned. */
    plen = *path_len;
    if (plen == 0 || plen > DIR_MAX_LINK_LEN) {
        *status_ret = status_$naming_invalid_link;
        return;
    }

    /* 0x00E4D3A4-0x00E4D3BA: the parts that are written once. */
    request.op = DIR_OP_RESOLVE;
    request.version = DIR_$OP_REC(DIR_OP_RESOLVE >> 1).version;
    request.body.resolve.flags = flags;
    request.body.resolve.path_len = plen;
    request.body.resolve.path_ptr = ARCH_PTR_TO_VA(pathname);

    do {
        /* 0x00E4D3C0: the loop head.  The subject uid goes into the request
         * header at +0x04 ... */
        request.uid.high = start_uid->high;
        request.uid.low = start_uid->low;
        /* 0x00E4D3CA: ... and again into the body at +0x94. */
        request.body.resolve.start_uid.high = start_uid->high;
        request.body.resolve.start_uid.low = start_uid->low;
        /* 0x00E4D3D4 */
        request.body.resolve.resolved_uid.high = resolved_uid->high;
        request.body.resolve.resolved_uid.low = resolved_uid->low;
        /* 0x00E4D3DE-0x00E4D3EE */
        request.body.resolve.param5 = *param5;
        request.body.resolve.param6 = *param6;
        request.body.resolve.param7 = *param7;
        request.body.resolve.param8 = *param8;

        /* 0x00E4D3F2: `clr.b (-0x2c,A6)` - reply+0x14, the "more" byte. */
        response.resolve.more = 0;

        /* 0x00E4D3F6-0x00E4D40A */
        DIR_$DO_OP(&request,
                   (int16_t)DIR_$OP_REC(DIR_OP_RESOLVE >> 1).base_size,
                   0x34, &response, &do_op_rcvd_len);

        /* 0x00E4D412 */
        *status_ret = response.status;

        /* 0x00E4D418: `tst.b (-0x2c,A6)` / `bpl` - a non-negative "more"
         * byte ends the walk, whatever the status says. */
        if (response.resolve.more >= 0) {
            return;
        }

        /* 0x00E4D41E-0x00E4D44A */
        start_uid->high = response.resolve.start_uid.high;
        start_uid->low = response.resolve.start_uid.low;
        resolved_uid->high = response.resolve.resolved_uid.high;
        resolved_uid->low = response.resolve.resolved_uid.low;
        *link_count = response.resolve.link_count;
        *param5 = response.resolve.param5;
        *param6 = response.resolve.param6;
        *param7 = response.resolve.param7;
        *param8 = response.resolve.param8;

        /* 0x00E4D44E: `tst.b (-0x2b,A6)` / `bmi` - reply+0x15, the "loop"
         * byte; negative means go round again. */
    } while (response.resolve.loop < 0);
}
