/*
 * DIR_$FIND_NET - Find the network node that owns a directory entry
 *
 * Calls dir_$find_uid_internal in network-search mode (the flag byte is
 * true) and returns the node address it writes, or 0 when the search
 * failed.
 *
 * Frame: `link.w A6,-0x18` (0x00E4E8B4).  Every argument the internal
 * routine writes through is a cell of this 0x18-byte frame:
 *   A6-0x18  name_buf   - TWO bytes, not a 256-byte buffer: A6-0x16 is the
 *                         very next cell (source-5hyd)
 *   A6-0x16  name_len   - word
 *   A6-0x10  net_ret    - longword, the return value
 *   A6-0x0C  status     - longword
 *   A6-0x08  local_uid  - the 8-byte search UID handed over as arg 2
 *
 * Original address: 0x00E4E8B4
 * Original size: 86 bytes
 */

#include "dir/dir_internal.h"

/* 0x00E4E8C0: `andi.l #-0x100000,(-0x4,A6)` keeps the top 12 bits. */
#define DIR_FIND_NET_NODE_MASK  0xFFF00000u

/*
 * Parameters (A6+0x08..A6+0x0C):
 *   dir_uid - UID of the directory to search; passed straight through as
 *             the internal routine's first argument
 *   index   - pointer to the longword OR'ed into the search UID's low half
 *
 * Returns: the node address at A6-0x10 when the status at A6-0x0C is zero,
 *          0 otherwise (0x00E4E8F6-0x00E4E900).
 */
uint32_t DIR_$FIND_NET(uid_t *dir_uid, uint32_t *index)
{
    /*
     * A6-0x08.  NOTHING copies dir_uid into this cell: the routine only
     * touches the LOW longword at A6-0x04, and it does so with a
     * read-modify-write of whatever the frame slot already held.  The high
     * longword at A6-0x08 is never written at all.  Reproduced as found -
     * giving either half a value would be an invention (source-5hyd).
     */
    uid_t local_uid;
    /* A6-0x18: two bytes, the whole of the "name buffer" the frame has. */
    char      name_buf[2];
    int16_t   name_len;         /* A6-0x16 */
    uint32_t  net_ret;          /* A6-0x10 */
    status_$t status;           /* A6-0x0C */

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
#if !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#endif
    /* 0x00E4E8C0: the mask is applied to the stale frame contents. */
    local_uid.low &= DIR_FIND_NET_NODE_MASK;
    /* 0x00E4E8C8-0x00E4E8CE: `move.l (A0),D0` then `or.l D0,(-0x4,A6)` -
     * the whole index longword goes in, with no 20-bit mask of its own. */
    local_uid.low |= *index;

    /*
     * 0x00E4E8D2-0x00E4E8EA, pushed right to left:
     *   -0xc(A6)   &status      arg 8
     *   -0x10(A6)  &net_ret     arg 7
     *   -0x16(A6)  &name_len    arg 6
     *   -0x18(A6)  name_buf     arg 5
     *   clr.w      0            arg 4  (name_buf_len)
     *   st         true         arg 3  (network-search flag, 0xFF in the
     *                                   EVEN byte of the 2-byte slot)
     *   -0x8(A6)   &local_uid   arg 2
     *   (0x8,A6)   dir_uid      arg 1
     */
    dir_$find_uid_internal(dir_uid, &local_uid, true, 0, name_buf,
                           &name_len, &net_ret, &status);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    /* 0x00E4E8F6-0x00E4E900: D0 = net_ret, cleared when the status is
     * non-zero. */
    if (status == status_$ok) {
        return net_ret;
    }
    return 0;
}
