/*
 * ACL_$RIGHTS - Check access rights for an object
 *
 * Original address: 0x00E46A00 (142 bytes)
 *
 * A thin gate in front of acl_$eval_rights (0x00E464B8): it copies the
 * object UID into its own frame, picks the current process' SID block and
 * project list out of the two per-process tables, derives the two privilege
 * booleans, and passes everything on.  There is no stack cleanup after the
 * `bsr` at 0x00E46A80 - the 0x1C bytes of arguments are dropped by the
 * `unlk A6` at 0x00E46A8A - and the result acl_$eval_rights leaves in D0 is
 * ACL_$RIGHTS' own result.
 *
 * Disassembly (A5 = 0xE7CF54, the ACL module data base):
 *
 *   00e46a0e  movea.l (0x8,A6),A0        ; uid
 *   00e46a12  move.l  (A0)+,(-0x8,A6)    ; local_uid.high
 *   00e46a16  move.l  (A0)+,(-0x4,A6)    ; local_uid.low
 *   00e46a1a  move.l  (0x18,A6),-(SP)    ; arg9  status_ret
 *   00e46a1e  move.w  (0x00e20608).l,D2w ; PROC1_$CURRENT
 *   00e46a24  movea.l #0xe97294,A0
 *   00e46a2a  add.w   D2w,D2w
 *   00e46a30  tst.w   (-0x3d5a,A1)       ; ACL_$DATA.subsys_level[cur] (0xE9353A)
 *   00e46a34  sgt     D0b
 *   00e46a36  move.b  D0b,-(SP)          ; arg8  in_subsys
 *   00e46a3c  tst.w   (0xb76,A1)         ; ACL_$UNWIRED_DATA.super_count[cur]  (0xE7DACA)
 *   00e46a40  sgt     D1b
 *   00e46a42  move.b  D1b,-(SP)          ; arg7  in_super
 *   00e46a44  movea.l (0x14,A6),A1
 *   00e46a48  move.w  (A1),-(SP)         ; arg6  *option_flags   (WORD)
 *   00e46a4a  movea.l (0x10,A6),A2
 *   00e46a4e  move.l  (A2),-(SP)         ; arg5  *required_mask  (LONGWORD)
 *   00e46a50  movea.l (0xc,A6),A3
 *   00e46a54  move.b  (A3),-(SP)         ; arg4  *ignore_super   (BYTE)
 *   00e46a56  pea     (-0x8,A6)          ; arg3  &local_uid
 *   00e46a66  pea     (-0x4d98,A4)       ; arg2  0xE924FC + cur*0x40
 *   00e46a7c  pea     (-0x6584,A0)       ; arg1  0xE90D10 + cur*0x24
 *   00e46a80  bsr.w   0x00e464b8
 *
 * The second parameter is a POINTER TO A DOMAIN BOOLEAN.  It was previously
 * modelled as an unused `void *` and several call sites passed NULL, which
 * would fault on the target at 0x00E46A54 (source-6vl5).
 */

#include "acl/acl_internal.h"

uint32_t ACL_$RIGHTS(uid_t *uid, boolean *ignore_super, uint32_t *required_mask,
                     int16_t *option_flags, status_$t *status_ret)
{
    uid_t local_uid;    /* A6-0x08 */

    /* 0x00E46A0E-0x00E46A18: two longword moves through A0 post-increment. */
    local_uid.high = uid->high;
    local_uid.low  = uid->low;

    /*
     * arg1 is 0xE90D10 + PROC1_$CURRENT * 0x24, the current process' SID
     * block (0x00E46A6A-0x00E46A7C computes cur*4 + cur*32 = cur*36).
     *
     * arg2 is 0xE924FC + PROC1_$CURRENT * 0x40, i.e. &ACL_$DATA.proj_uids[cur][0]
     * with the base 0xE924FC recorded in acl/acl_internal.h (source-4h7g).
     * ACL_$ADD_PROJ (0x00E47EFE), ACL_$DELETE_PROJ (0x00E47FA4),
     * ACL_$GET_PROJ_LIST (0x00E48052) and ACL_$SET_PROJ_LIST (0x00E4815A) all
     * start their row displacement at 8 with the same `-0x4da0` bias, so every
     * one of them addresses element [0] of this declaration first.
     *
     * Each of the three PROC1_$CURRENT reads in the original is a separate
     * load of the global at 0xE20608; they are kept separate here.
     */
    return acl_$eval_rights(&ACL_$DATA.current_sids[PROC1_$CURRENT],
                            &ACL_$DATA.proj_uids[PROC1_$CURRENT][0],
                            &local_uid,
                            *ignore_super,      /* 0x00E46A54 move.b (A3) */
                            *required_mask,     /* 0x00E46A4E move.l (A2) */
                            *option_flags,      /* 0x00E46A48 move.w (A1) */
                            /* 0x00E46A3C `tst.w` + `sgt`: 0xFF when > 0 */
                            ACL_$UNWIRED_DATA.super_count[PROC1_$CURRENT] > 0 ? true : false,
                            /* 0x00E46A30 `tst.w` + `sgt`: 0xFF when > 0 */
                            ACL_$DATA.subsys_level[PROC1_$CURRENT] > 0 ? true : false,
                            status_ret);
}
