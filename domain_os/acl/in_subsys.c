/*
 * ACL_$IN_SUBSYS - Check if current process is in subsystem context
 *
 * Returns a Domain boolean: TRUE (0xFF) when the current process's subsystem
 * level is greater than zero.
 *
 * Original address: 0x00E47098
 * Original size: 44 bytes
 *
 *   00e470a4  move.w (0x00e20608).l,D0w   ; PROC1_$CURRENT
 *   00e470b0  add.w D0w,D0w               ; *2: the table holds words
 *   00e470b6  tst.w (-0x3d5a,A1)          ; ACL_$SUBSYS_LEVEL[current]
 *   00e470ba  sgt D0b                     ; SIGNED greater-than, LOW BYTE
 *
 * The answer is a BYTE.  `sgt D0b` writes only D0's low byte, so the upper
 * half of D0 still holds PROC1_$CURRENT * 2 from 0x00E470B0 - it is not a
 * clean -1/0 word.  Both callers agree: they do `tst.b D0b / bpl`
 * (REM_FILE_$RN_DO_OP at 0x00E615AC and REM_FILE_$LOCK at 0x00E61B28), which
 * is why the C return type is `boolean` and not int16_t: a word return would
 * invite callers to read the dirty high half as part of the answer.
 * (source-lpk8)
 */

#include "acl/acl_internal.h"

boolean ACL_$IN_SUBSYS(void)
{
    /* 0x00E470B6 "tst.w" then 0x00E470BA "sgt": a SIGNED greater-than. */
    return (ACL_$SUBSYS_LEVEL[PROC1_$CURRENT] > 0) ? (boolean)0xFF : 0;
}
