/*
 * ACL_$CONVERT_FROM_9ACL - render an ACL object as the old 9-entry image
 *
 * Original address: 0x00E48F56, 182 bytes.  A5 = 0xE7CF54.
 *
 * Despite the name it is not a converter of its own: it raises the caller's
 * privilege, takes both ACL locks, and asks acl_$image_internal (0x00E47B78)
 * for a 0x400-byte image of `source_acl` into the module workspace, handing
 * the caller's `prot_buf_out` in as the routine's `data_out`.  The caller's
 * `prot_uid_out` then gets a copy of the source ACL UID with bit 24 of its
 * low longword set.
 *
 * Frame (A6+):
 *   0x08 source_acl   (long) -> A3
 *   0x0C acl_type     (long)  NEVER READ
 *   0x10 prot_buf_out (long)
 *   0x14 prot_uid_out (long) -> A2
 *   0x18 status_ret   (long)
 *
 * Its only caller is DIR_$GET_DEF_PROTECTION (0x00E51DEA).
 */

#include "acl/acl_internal.h"

/*
 * `move.l #0x4000000,-(SP)` at 0x00E48FB6 is the merged pair
 * acl_$image_internal takes at A6+0x0C and A6+0x0E: a 0x0400-byte buffer and
 * a zero flag byte.
 */
#define ACL_9ACL_IMAGE_BUF_LEN  0x0400
#define ACL_9ACL_IMAGE_FLAG     0

/*
 * `bset.b #0x0,(0x4,A2)` at 0x00E48FFC.  Byte +4 of a uid_t is the most
 * significant byte of its `low` longword on the big-endian m68k, so bit 0 of
 * that byte is bit 24 of `low`.
 */
#define ACL_9ACL_UID_MARK       0x01000000U

void ACL_$CONVERT_FROM_9ACL(uid_t *source_acl, uid_t *acl_type,
                            void *prot_buf_out, uid_t *prot_uid_out,
                            status_$t *status_ret)
{
    int16_t image_len;      /* A6-0x02, acl_$image_internal's len_out */
    int8_t  image_flag;     /* A6-0x04, its flag_out (one byte) */

    /* 0x00E48F6C never reads A6+0x0C. */
    (void)acl_type;

    /* 0x00E48F68-0x00E48F78: enter super mode for the duration. */
    ACL_$UNWIRED_DATA.super_count[PROC1_$CURRENT]++;

    /* 0x00E48F7C-0x00E48F94 */
    ML_$EXCLUSION_START(&ACL_$WIRED_DATA.exclusion_lock);
    ACL_$UNWIRED_DATA.locksmith_owner_pid = (int16_t)PROC1_$CURRENT;
    ACL_$UNWIRED_DATA.locksmith_override = (int8_t)0xFF;          /* `st (0xbf8,A5)` */

    ML_$LOCK(ML_LOCK_ACL);                      /* 0x00E48F9C */

    /* 0x00E48FA4-0x00E48FC2 */
    acl_$image_internal(source_acl,
                        ACL_9ACL_IMAGE_BUF_LEN,
                        ACL_9ACL_IMAGE_FLAG,
                        ACL_$UNWIRED_DATA.workspace,         /* `pea (A5)` = 0xE7CF54 */
                        &image_len,
                        prot_buf_out,
                        &image_flag,
                        status_ret);

    ML_$UNLOCK(ML_LOCK_ACL);                    /* 0x00E48FCC */

    /* 0x00E48FD4-0x00E48FF2 */
    ACL_$UNWIRED_DATA.locksmith_override = 0;
    ML_$EXCLUSION_STOP(&ACL_$WIRED_DATA.exclusion_lock);
    ACL_$UNWIRED_DATA.super_count[PROC1_$CURRENT]--;

    /*
     * 0x00E48FF4-0x00E48FFC.  The status acl_$image_internal produced is left
     * exactly as it is; the UID is copied and marked unconditionally.
     */
    *prot_uid_out = *source_acl;
    prot_uid_out->low |= ACL_9ACL_UID_MARK;
}
