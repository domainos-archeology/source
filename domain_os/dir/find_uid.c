/*
 * DIR_$FIND_UID - Find a UID in a directory
 *
 * Searches a directory for an entry with the specified UID and returns
 * its name.  A thin wrapper around dir_$find_uid_internal with the
 * search-mode flag clear (a UID search rather than a network search).
 *
 * Frame: `link.w A6,-0x4` (0x00E4E87C) - the only local is the 4-byte
 * cell at A6-0x4 that becomes the internal routine's net_ret argument.
 *
 * Original address: 0x00E4E87C
 * Original size: 56 bytes
 */

#include "dir/dir_internal.h"

/*
 * Parameters (A6+0x08..A6+0x1C) - see dir/dir.h for the argument mapping.
 */
void DIR_$FIND_UID(uid_t *dir_uid, uid_t *target_uid, uint16_t *name_buf_len,
                   char *name_buf, int16_t *name_len_ret,
                   status_$t *status_ret)
{
    /*
     * A6-0x4: the whole frame.  It is handed over as arg 7 (net_ret) at
     * 0x00E4E88C (`pea (-0x4,A6)`) and never read back, so the network
     * address the internal routine writes is discarded on this path.
     */
    uint32_t discarded_net;

    /*
     * 0x00E4E888-0x00E4E8A4, pushed right to left:
     *   (0x1c,A6)  status_ret       arg 8
     *   -0x4(A6)   &discarded_net   arg 7
     *   (0x18,A6)  name_len_ret     arg 6  <- the caller's fifth parameter
     *   (0x14,A6)  name_buf         arg 5
     *   *(0x10,A6) the size WORD    arg 4  (loaded through A0, by value)
     *   clr.w      flag = 0         arg 3  (a false Domain boolean)
     *   (0xc,A6)   target_uid       arg 2
     *   (0x8,A6)   dir_uid          arg 1
     */
    dir_$find_uid_internal(dir_uid, target_uid, false,
                           (int16_t)*name_buf_len, name_buf,
                           name_len_ret, &discarded_net, status_ret);
}
