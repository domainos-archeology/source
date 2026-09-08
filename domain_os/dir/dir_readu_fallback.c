/*
 * dir_$dir_readu_fallback - DIR_$DIR_READU's pre-DO_OP read path
 *
 * A nested Pascal subprocedure of DIR_$DIR_READU: 0x00E4E1B0
 * (`movea.l (A6),A2`) picks up the static link and every value it forwards
 * comes out of that parent frame.  It clears the parent's eof cell, then
 * either reads the canned replicated root or hands the whole request to
 * DIR_$OLD_DIR_READU.
 *
 * MODULE-LOCAL: the SAU2 map exports no symbol at 0x00E4E1A8 and it has a
 * single caller, DIR_$DIR_READU at 0x00E4E3F0, so it carries the lowercase
 * dir_$ prefix this tree uses for module-local Pascal procedures
 * (source-ka0m; Ghidra called it DIR_$DIR_READU_FUN_00e4e1a8).
 *
 * Original address: 0x00E4E1A8
 * Original size: 86 bytes
 */

#include "dir/dir_internal.h"

/*
 * dir_$dir_readu_fallback - Internal directory read helper
 *
 * Originally a nested Pascal subprocedure that accessed its parent's
 * stack frame. Flattened to take explicit parameters from the parent
 * DIR_$DIR_READU function.
 *
 * If the directory is the canned replicated root, calls dir_$read_canned_root
 * for the special root read. Otherwise delegates to DIR_$OLD_DIR_READU.
 *
 * Parameters, with the parent-frame displacement each one had (A2 is
 * DIR_$DIR_READU's own A6):
 *   dir_uid      - (0x08,A2)  UID of the directory to read
 *   continuation - (0x14,A2)  in/out continuation position
 *   max_entries  - (0x18,A2)
 *   count_ret    - (0x1C,A2)
 *   flags        - (0x20,A2)
 *   eof_ret      - (0x24,A2)  cleared at 0x00E4E1B2 before anything else
 *   status_ret   - (0x08,A6)  this routine's OWN parameter, the only one
 *                             that is not an uplevel reference
 */
void dir_$dir_readu_fallback(uid_t *dir_uid, int32_t *continuation,
                                  uint16_t *max_entries, int32_t *count_ret,
                                  void *flags, int32_t *eof_ret,
                                  status_$t *status_ret)
{
    /* 0x00E4E1B2-0x00E4E1B6: `movea.l (0x24,A2),A0` / `clr.l (A0)`. */
    *eof_ret = 0;

    /* 0x00E4E1B8-0x00E4E1CA: `cmpm.l` against 0x00E173FC,
     * NAME_$CANNED_REP_ROOT_UID. */
    if (dir_uid->high == NAME_$CANNED_REP_ROOT_UID.high &&
        dir_uid->low == NAME_$CANNED_REP_ROOT_UID.low) {
        /* 0x00E4E1CC: another nested procedure, reached with no arguments -
         * it reads the same parent frame through its own static link. */
        dir_$read_canned_root();
    } else {
        /* 0x00E4E1D2-0x00E4E1EE: the six uplevel values plus this
         * routine's own status pointer, pushed right to left. */
        DIR_$OLD_DIR_READU(dir_uid, continuation, max_entries,
                           count_ret, flags, eof_ret, status_ret);
    }
}
