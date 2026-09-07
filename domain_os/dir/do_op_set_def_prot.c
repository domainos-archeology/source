/*
 * dir_$do_op_set_def_prot - DO_OP handler for set default protection
 *
 * Server-side handler for opcode 0x54 (DIR_OP_SET_DEF_PROTECTION) in
 * DIR_$DO_OP. Opens the directory for write (mode 2, rights 8 = write ACL),
 * writes the default protection data via dir_$write_def_prot, then releases
 * the handle.
 *
 * Argument mapping (call site 0x00E4C81C, pushed right to left):
 *   pea (0x4,A3)   -> status_ret   (response + 0x04)
 *   pea (0xc2,A2)  -> param_4      (request + 0xC2)
 *   pea (0x96,A2)  -> param_3      (request + 0x96)
 *   pea (0x8e,A2)  -> param_2      (request + 0x8E)
 *   pea (-0x10,A6) -> param_1      (resolved directory UID)
 * The three request pointers are forwarded unchanged to dir_$write_def_prot
 * (0x00E5207E pushes param_2, 0x00E5207A param_3, 0x00E52076 param_4).  There
 * param_2 lands in D5 and is compared against ACL_$DIR_ACL (0x00E1744C, at
 * 0x00E51F2A) and ACL_$FILE_ACL (0x00E17444, at 0x00E51F7C), so request+0x8E
 * is an 8-byte ACL TYPE UID (0x8E..0x95); param_3 is read as 11 longwords =
 * 44 bytes by the copy loop at 0x00E51E7C-0x00E51E88, so request+0x96 is the
 * default-protection (SR10 "10-ACL") data (0x96..0xC1); param_4 lands in A2,
 * whose "funky" bits are tested at 0x00E51E48 (and.w (0x4,A2),D0w) and whose
 * first 8 bytes are copied as the ACL UID on the non-funky path at
 * 0x00E51E8C-0x00E51E92, so request+0xC2 is the source ACL UID (0xC2..0xC9).
 *
 * Parameters:
 *   dir_uid      - Directory UID
 *   acl_type     - ACL type UID: ACL_$DIR_ACL or ACL_$FILE_ACL (request + 0x8E)
 *   prot_data    - Default protection data, 44 bytes (request + 0x96)
 *   src_acl_uid  - Source ACL object UID, possibly in "funky" form
 *                  (request + 0xC2)
 *   status_ret   - Output: status code
 *
 * Original address: 0x00E52044
 * Original size: 98 bytes
 */

#include "dir/dir_internal.h"

void dir_$do_op_set_def_prot(uid_t *dir_uid, void *acl_type, void *prot_data,
                             void *src_acl_uid, status_$t *status_ret)
{
    uint32_t local_handle;

    ACL_$ENTER_SUPER();

    /* Open directory for write with ACL right 8 (write ACL) */
    dir_$open_dir(dir_uid, 2, 8, &local_handle, status_ret);

    if (*status_ret == status_$ok) {
        /* Write default protection; flush_flag = 0xFF (true) */
        dir_$write_def_prot(local_handle, acl_type, prot_data, src_acl_uid,
                            (char)0xFF, status_ret);
    }

    dir_$release_handle(&local_handle);
    ACL_$EXIT_SUPER();
}
