/*
 * NAME Directory Setter Functions
 *
 * Functions to set the working directory and naming directory.
 * These are per-process (per-ASID) settings stored in the NAME data area.
 *
 * Original addresses:
 *   NAME_$SET_WDIR:   0x00E4A3D0 (56 bytes)
 *   NAME_$SET_WDIRUS: 0x00E58670 (294 bytes)
 *   NAME_$SET_NDIRUS: 0x00E587A0 (286 bytes)
 *
 * NAME_$SET_WDIRUS and NAME_$SET_NDIRUS are the same routine over two
 * different pairs of per-ASID tables in NAME_$DATA (A5 = 0xE80264):
 *
 *   working directory:  uid[] at +0x950 (stride 8), mapped_info[] at +0x5B0
 *                       (stride 0x10), audit event 0x0027
 *   naming directory:   uid[] at +0x3E0 (stride 8), mapped_info[] at +0x040
 *                       (stride 0x10), audit event 0x0028
 *
 * Both index directly by PROC1_$AS_ID (0xE2060A), with no 1-based bias:
 * `lsl.w #0x3` for the UID tables (0x00E5868C) and `lsl.w #0x4` for the
 * mapped-info tables (0x00E586E2).
 */

#include "name/name_internal.h"
#include "audit/audit.h"

/*
 * ============================================================================
 * Constant cells
 * ============================================================================
 *
 * The compiler pooled these four literals immediately after NAME_$SET_WDIRUS
 * and both functions pass their addresses with `pea (d,PC)`.  The PC value is
 * the address of the extension word, i.e. the instruction address + 2.
 */

/* 0x00E5879A, byte 0xFF.  ACL_$RIGHTS' second argument is a pointer to a
 * Domain boolean, not the unused `void *` acl/acl.h still declares:
 * `movea.l (0xc,A6),A3` + `move.b (A3),-(SP)` at 0x00E46A50-0x00E46A54.
 * Reached by `pea (0xe0,PC)` at 0x00E586B8 and `pea (-0x50,PC)` at
 * 0x00E587E8.
 * TODO(source-6vl5): retype ACL_$RIGHTS' second parameter and drop the cast
 * below. */
static const boolean name_$set_dir_acl_bool_00e5879a = true;

/* 0x00E5879C, longword 1: the rights mask ACL_$RIGHTS must find.
 * `pea (0xe6,PC)` at 0x00E586B4, `pea (-0x4a,PC)` at 0x00E587E4. */
static const uint32_t name_$set_dir_rights_00e5879c = 1;

/* 0x00E58796, word 1: ACL_$RIGHTS' option flags.
 * `pea (0xe4,PC)` at 0x00E586B0, `pea (-0x4c,PC)` at 0x00E587E0. */
static const int16_t name_$set_dir_acl_opts_00e58796 = 1;

/* 0x00E58798, word 8: the audit record length - one UID.
 * `pea (0x20,PC)` at 0x00E58776, `pea (-0x108,PC)` at 0x00E5889E. */
static const uint16_t name_$set_dir_audit_len_00e58798 = 8;

/*
 * Audit event UIDs.  Both are UID_$NIL with byte 1 of the high longword set
 * to 4 (`move.b #0x4,(-0x7,A6)` at 0x00E5875A / 0x00E58882) and the low word
 * of that longword set to the event code (`move.w #0x27,(-0x6,A6)` at
 * 0x00E58760, `#0x28` at 0x00E58888) - the same {0x000400NN, 0} shape
 * file/audit_lock.c uses.
 */
#define NAME_AUDIT_KIND         0x04        /* byte 1 of event_uid.high */
#define NAME_AUDIT_SET_WDIR     0x0027
#define NAME_AUDIT_SET_NDIR     0x0028

/*
 * ============================================================================
 * NAME_$SET_WDIR - Set working directory by pathname (0x00E4A3D0)
 * ============================================================================
 *
 * Resolves the given pathname to a UID and sets it as the working directory.
 * The resolved UID lives in this function's own frame at A6-0x08
 * (`pea (-0x8,A6)` at 0x00E4A3DC and again at 0x00E4A3F6).
 */
void NAME_$SET_WDIR(char *path, int16_t *path_len, status_$t *status_ret)
{
    uid_t wdir_uid;                                     /* A6-0x08 */

    NAME_$RESOLVE(path, path_len, &wdir_uid, status_ret);   /* 0x00E4A3E8 */

    /* 0x00E4A3F0 `tst.l (A2)`: the whole status longword. */
    if (*status_ret == status_$ok) {
        NAME_$SET_WDIRUS(&wdir_uid, status_ret);            /* 0x00E4A3FA */
    }
}

/*
 * ============================================================================
 * The body both NAME_$SET_WDIRUS and NAME_$SET_NDIRUS compile to
 * ============================================================================
 *
 * The two routines are byte-for-byte the same apart from the two table
 * offsets and the audit event code, so they are flattened onto one static
 * helper here.  Every instruction address cited is NAME_$SET_WDIRUS';
 * NAME_$SET_NDIRUS' counterpart is at the address given in the header
 * comment of each caller.
 */
static void name_$set_dir(uid_t *uidp, uid_t *dir_uid_slot,
                          name_$mapped_info_t *mapped_info,
                          uint16_t audit_event, status_$t *status_ret)
{
    /* 0x00E5869A-0x00E586A4: `cmpm.l` twice - already this directory, so
     * there is nothing to do and nothing to audit. */
    if (uidp->high == dir_uid_slot->high && uidp->low == dir_uid_slot->low) {
        *status_ret = status_$ok;                       /* 0x00E586A2 */
        return;
    }

    ACL_$ENTER_SUPER();                                 /* 0x00E586A8 */

    /*
     * 0x00E586AE-0x00E586CE.  `tst.l D0` + `seq` + `bpl`: a zero result means
     * access denied, and only then is the ACL status translated.
     */
    if (ACL_$RIGHTS(uidp,
                    (void *)&name_$set_dir_acl_bool_00e5879a,
                    (uint32_t *)&name_$set_dir_rights_00e5879c,
                    (int16_t *)&name_$set_dir_acl_opts_00e58796,
                    status_ret) == 0) {
        NAME_CONVERT_ACL_STATUS(status_ret);            /* 0x00E586D2 */
    } else {
        /* 0x00E586DA-0x00E586F6: drop whatever this ASID had mapped. */
        name_$unmap_dir_buffers(PROC1_$AS_ID, mapped_info);

        /* 0x00E586F8-0x00E58718 */
        name_$map_dir(uidp, PROC1_$AS_ID, mapped_info, status_ret);

        /* 0x00E5871C `tst.l (A3)` - the whole status longword. */
        if (*status_ret == status_$ok) {
            /* 0x00E58734: two longword moves into the per-ASID slot. */
            dir_uid_slot->high = uidp->high;
            dir_uid_slot->low  = uidp->low;
            *status_ret = status_$ok;                   /* 0x00E5873C clr.l */
        } else {
            /* 0x00E58720 `bset.b #0x7,(A3)`: bit 7 of the FIRST byte of the
             * status longword, i.e. bit 31 - the "this is an error" bit. */
            *status_ret |= (status_$t)0x80000000u;
        }
    }

    ACL_$EXIT_SUPER();                                  /* 0x00E5873E */

    /*
     * 0x00E58744-0x00E58786: the audit trailer.  It runs for every outcome
     * that got past the "already set" fast path, including the ACL denial.
     */
    if (AUDIT_$ENABLED < 0) {
        uid_t    event_uid;                             /* A6-0x08 */
        uint16_t event_flags;                           /* A6-0x0A */

        event_uid = UID_$NIL;                           /* 0x00E5874C */
        /* 0x00E5875A / 0x00E58760 patch byte 1 and the low word of the high
         * longword; UID_$NIL is all zeros, so this is {0x000400NN, 0}. */
        event_uid.high = (event_uid.high & 0xFF000000u) |
                         ((uint32_t)NAME_AUDIT_KIND << 16) |
                         (uint32_t)audit_event;

        /* 0x00E58766-0x00E58772 */
        event_flags = (*status_ret != status_$ok) ? 1 : 0;

        /* 0x00E58776-0x00E58786, pushed right to left. */
        AUDIT_$LOG_EVENT(&event_uid, &event_flags, (uint32_t *)status_ret,
                         (char *)uidp,
                         (uint16_t *)&name_$set_dir_audit_len_00e58798);
    }
}

/*
 * NAME_$SET_WDIRUS - Set working directory by UID (0x00E58670)
 *
 * @param uidp       UID of the new working directory
 * @param status_ret Output status code
 */
void NAME_$SET_WDIRUS(uid_t *uidp, status_$t *status_ret)
{
    name_$set_dir(uidp,
                  &NAME_$DATA.wdir_uid[PROC1_$AS_ID],            /* A5+0x950 */
                  &NAME_$DATA.wdir_mapped_info[PROC1_$AS_ID],    /* A5+0x5B0 */
                  NAME_AUDIT_SET_WDIR,
                  status_ret);
}

/*
 * NAME_$SET_NDIRUS - Set naming directory by UID (0x00E587A0)
 *
 * Instruction addresses for the shared body, in the same order as the
 * comments in name_$set_dir: 0x00E587CA compare, 0x00E587D8 enter super,
 * 0x00E587EE ACL_$RIGHTS, 0x00E58802 convert, 0x00E5881E unmap,
 * 0x00E5883C map, 0x00E5885C store, 0x00E58848 error bit, 0x00E58866 exit
 * super, 0x00E5886C audit gate, 0x00E58888 event 0x0028, 0x00E588AE log.
 *
 * @param uidp       UID of the new naming directory
 * @param status_ret Output status code
 */
void NAME_$SET_NDIRUS(uid_t *uidp, status_$t *status_ret)
{
    name_$set_dir(uidp,
                  &NAME_$DATA.ndir_uid[PROC1_$AS_ID],            /* A5+0x3E0 */
                  &NAME_$DATA.ndir_mapped_info[PROC1_$AS_ID],    /* A5+0x040 */
                  NAME_AUDIT_SET_NDIR,
                  status_ret);
}
