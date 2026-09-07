/*
 * DIR_$SET_PROTECTION - Set protection on a file
 *
 * Sets the ACL/protection on a specific file.
 *
 * Original address: 0x00E52228
 * Original size: 362 bytes
 */

#include "dir/dir_internal.h"

/*
 * Constant word cell at 0xE52392 (the two bytes just past this function's
 * `rts`), passed by reference as FILE_$SET_PROT's second argument with
 * `pea (0x40,PC)` at 0x00E52350.  The image holds 0x0006 - protection type 6,
 * "the object carries an extended ACL".  FILE_$SET_PROT dereferences it
 * immediately (`move.w (A4),D2w` at 0x00E5DF56), so it can never be NULL.
 */
static int16_t dir_$set_protection_prot_type = 6;

/*
 * Constant longword cell at 0xE4B33C, passed by reference as FILE_$PRIV_LOCK's
 * `acl_ctx` argument with `pea (-0x6fe0,PC)` at 0x00E5231A.  The image holds
 * 0x00000000, i.e. "no ACL context"; FILE_$PRIV_LOCK loads the pointer it
 * addresses at 0x00E5EDDE, so this too can never be NULL.
 */
static void *dir_$set_protection_acl_ctx = NULL;

/*
 * DIR_$SET_PROTECTION - Set protection on a file
 *
 * Sends opcode 0x52 to the directory server; if the server reports that the
 * object is not one it owns, the work is redone locally under a file lock.
 *
 * Frame (`link.w A6,-0x100`, `movem.l {A5 A4 A3 A2 D3 D2},-(SP)`):
 *   A5 = 0xE7DC00, the DIR module base.
 *   D2 = A6+0x08  file_uid
 *   D3 = A6+0x0C  prot_buf   the 44-byte protection image
 *   A4 = A6+0x10  acl_uid
 *   A2 = A6+0x14  prot_type  in/out word the caller supplies
 *   A3 = A6+0x18  status_ret
 *
 *   A6-0x100 word  do_op_extra   DIR_$DO_OP's fifth argument
 *   A6-0x0FE word  lock_result   FILE_$PRIV_LOCK's rights word
 *   A6-0x0F8 long  lock_status   FILE_$PRIV_UNLOCK's status
 *   A6-0x0F4 8     dtv_buf       FILE_$PRIV_UNLOCK's DTV output
 *   A6-0x0EC long  lock_handle   FILE_$PRIV_LOCK's slot number
 *   A6-0x0E8 0xC4  request       the server request; its OPCODE BYTE sits at
 *                                request+0x03 (`move.b #0x52,(-0xe5,A6)`)
 *   A6-0x020 0x18  response      Dir_$OpResponse; status at +0x04 (A6-0x1C)
 *   A6-0x008 8     temp_acl      the ACL UID handed to FILE_$SET_PROT
 *
 * Parameters:
 *   file_uid   - UID of file
 *   prot_buf   - Protection data to set (44 bytes)
 *   acl_uid    - ACL UID
 *   prot_type  - Pointer to protection type (4, 5, or 6)
 *   status_ret - Output: status code
 */

/* Offsets inside the 0xC4-byte server request, all relative to A6-0xE8. */
typedef struct __attribute__((packed, aligned(2))) dir_$set_prot_request_t {
    uint8_t   header[3];    /* 0x00: never written */
    uint8_t   op;           /* 0x03: `move.b #0x52,(-0xe5,A6)` at 0x00E5224A */
    uid_t     uid;          /* 0x04: 0x00E52252 */
    uint16_t  pad_0c;       /* 0x0C: never written */
    uint16_t  version;      /* 0x0E: 0x00E5225A, from DIR module base +0x20E2 */
    uint8_t   gap[0x7E];    /* 0x10: never written */
    uint32_t  prot[11];     /* 0x8E: 0x00E52262, 11 longwords */
    uid_t     acl;          /* 0xBA: 0x00E5226E */
    int16_t   type;         /* 0xC2: 0x00E52278, `move.w (A2),(-0x26,A6)` */
} dir_$set_prot_request_t;

_Static_assert(offsetof(dir_$set_prot_request_t, op)      == 0x03, "set_prot req.op");
_Static_assert(offsetof(dir_$set_prot_request_t, uid)     == 0x04, "set_prot req.uid");
_Static_assert(offsetof(dir_$set_prot_request_t, version) == 0x0E, "set_prot req.version");
_Static_assert(offsetof(dir_$set_prot_request_t, prot)    == 0x8E, "set_prot req.prot");
_Static_assert(offsetof(dir_$set_prot_request_t, acl)     == 0xBA, "set_prot req.acl");
_Static_assert(offsetof(dir_$set_prot_request_t, type)    == 0xC2, "set_prot req.type");
_Static_assert(sizeof(dir_$set_prot_request_t)            == 0xC4, "sizeof set_prot req");

/* Protection types the local fallback path knows how to apply
 * (0x00E522B0-0x00E522C0). */
#define DIR_SET_PROT_TYPE_MIN   4
#define DIR_SET_PROT_TYPE_MID   5
#define DIR_SET_PROT_TYPE_MAX   6

/* `move.w #0xff0,D1w` + `and.w (0x4,A4),D1w` + `lsr.w #0x4,D1w` +
 * `btst.l #0x4,D1` at 0x00E522CC-0x00E522DA: bit 8 of the first word of
 * acl_uid.low marks an ACL that is already in 9ACL form. */
#define DIR_ACL_UID_MASK        0x0FF0
#define DIR_ACL_UID_IS_9ACL     0x0010

/* FILE_$PRIV_LOCK arguments at 0x00E5230A-0x00E5233A. */
#define DIR_SET_PROT_LOCK_MODE      4
#define DIR_SET_PROT_LOCK_FLAGS     0x0008
#define DIR_SET_PROT_LOCK_WAIT      1

void DIR_$SET_PROTECTION(uid_t *file_uid, void *prot_buf, uid_t *acl_uid,
                         int16_t *prot_type, status_$t *status_ret)
{
    uint16_t        do_op_extra;    /* A6-0x100 */
    uint16_t        lock_result;    /* A6-0x0FE */
    status_$t       lock_status;    /* A6-0x0F8 */
    uint32_t        dtv_buf[2];     /* A6-0x0F4 */
    uint32_t        lock_handle;    /* A6-0x0EC */
    dir_$set_prot_request_t request; /* A6-0x0E8 */
    Dir_$OpResponse response;       /* A6-0x020 */
    uid_t           temp_acl;       /* A6-0x008 */
    status_$t       status;
    const uint32_t *src;
    uint32_t       *dst;
    int16_t         i, type;
    uint16_t        acl_word;

    /* 0x00E5224A-0x00E5227A: build the request. */
    request.op = DIR_OP_SET_PROTECTION;
    request.uid.high = file_uid->high;
    request.uid.low  = file_uid->low;
    request.version  = DAT_00e7fce2;

    src = (const uint32_t *)prot_buf;
    dst = request.prot;
    for (i = 0x0A; i >= 0; i--) {
        *dst++ = *src++;
    }

    request.acl.high = acl_uid->high;
    request.acl.low  = acl_uid->low;
    request.type     = *prot_type;

    /*
     * 0x00E5227C-0x00E52294.  The fifth argument is the frame word at
     * A6-0x100, which lies 0x18 bytes BELOW the request - it is not the
     * request's own base.
     * TODO (bead source-32ld): what DIR_$DO_OP's fifth argument is for is
     * still unknown; it is forwarded verbatim to REM_FILE_$RN_DO_OP
     * (0x00E4C104).  Every other dir/ caller in this port passes its request
     * buffer there instead.
     */
    do_op_extra = 0;
    DIR_$DO_OP(&request, DAT_00e7fce6, 0x14, &response, &do_op_extra);

    /* 0x00E52298-0x00E522AA */
    status = response.status;
    if (status != file_$bad_reply_received_from_remote_node &&
        status != status_$naming_bad_directory) {
        /* 0x00E52386 */
        *status_ret = status;
        return;
    }

    /* 0x00E522AE-0x00E522C8 */
    type = *prot_type;
    if (type != DIR_SET_PROT_TYPE_MIN && type != DIR_SET_PROT_TYPE_MID &&
        type != DIR_SET_PROT_TYPE_MAX) {
        *status_ret = file_$incompatible_request;
        return;
    }

    /* 0x00E522CC-0x00E52308 */
    acl_word = (uint16_t)(((uint16_t)(acl_uid->low >> 16) & DIR_ACL_UID_MASK) >> 4);
    if ((acl_word & DIR_ACL_UID_IS_9ACL) != 0) {
        /* 0x00E52300: the ACL is already in 9ACL form. */
        temp_acl.high = acl_uid->high;
        temp_acl.low  = acl_uid->low;
    } else {
        ACL_$CONVERT_TO_9ACL(prot_buf, acl_uid, file_uid,
                             &ACL_$DIR_ACL, &temp_acl, status_ret);
        if (*status_ret != status_$ok) {
            return;
        }
    }

    /* 0x00E5230A-0x00E52346: lock the file for the protection update. */
    FILE_$PRIV_LOCK(file_uid, PROC1_$AS_ID, 0, DIR_SET_PROT_LOCK_MODE, 0,
                    DIR_SET_PROT_LOCK_FLAGS, 0x0000, 0, 0, 0,
                    &dir_$set_protection_acl_ctx, DIR_SET_PROT_LOCK_WAIT,
                    &lock_handle, &lock_result, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    /* 0x00E52348-0x00E52356 */
    FILE_$SET_PROT(file_uid, &dir_$set_protection_prot_type, prot_buf,
                   &temp_acl, status_ret);

    /* 0x00E52360-0x00E5237E */
    (void)FILE_$PRIV_UNLOCK(file_uid, (int32_t)lock_handle,
                            DIR_SET_PROT_LOCK_MODE, (uint16_t)PROC1_$AS_ID,
                            0, 0, 0, 0, dtv_buf, &lock_status);
}
