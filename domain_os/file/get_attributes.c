/*
 * FILE_$GET_ATTRIBUTES - Get file attributes (full 0x90-byte format)
 *
 * Original address: 0x00E5D984, 200 bytes.
 *
 * Frame (`link.w A6,-0xbc`, `movem.l {A5 A3 A2},-(SP)`):
 *   A5 = 0xE82128 = FILE_$LOCK_CONTROL, established at 0x00E5D98C but never
 *        dereferenced by this routine.
 *   A6+0x08  file_uid    caller's object UID
 *   A6+0x0C  param_2     (A0) pointer to the two-byte request word; the
 *                        three `btst.b #n,(0x1,A0)` address its LOW byte
 *   A6+0x10  size_ptr    pointer to the caller's buffer size word
 *   A6+0x14  loc_rec     (A3) the caller's 0x20-byte object-location record
 *   A6+0x18  attr_out    the caller's 0x90-byte attribute buffer
 *   A6+0x1C  status_ret  (A2)
 *
 *   A6-0xBC  uint8_t   lock_info[2]   FILE_$DELETE_INT's third argument
 *   A6-0xBA  uint16_t  flags          AST_$GET_ATTRIBUTES' flag word
 *   A6-0xB4  status_$t status
 *   A6-0xB0  uint8_t   attrs[0x90]    the local attribute record
 *   A6-0x20  file_$obj_loc_t desc     UID at A6-0x18, flags byte at A6-0x03
 *
 * NOTE the order of operations: the request-word dispatch (including the
 * FILE_$DELETE_INT probe) and the descriptor set-up both happen BEFORE the
 * buffer-size check at 0x00E5D9F2.
 */

#include "file/file_internal.h"

/*
 * Bits tested in the request word (`btst.b #n,(0x1,A0)` at 0x00E5D99E,
 * 0x00E5D9A6 and 0x00E5D9AE - the second byte of a big-endian word is its low
 * byte, so these are bits 0..2 of the word).  Exactly one selects the AST flag
 * word; the two constant cells the callers pass hold 0x0004 and 0x0401.
 */
#define FILE_GET_ATTR_REQ_LOCKED    0x01    /* bit 0: caller holds a lock */
#define FILE_GET_ATTR_REQ_NO_PROBE  0x04    /* bit 2: skip the delete probe */
#define FILE_GET_ATTR_REQ_PROBE     0x02    /* bit 1: probe with FILE_$DELETE_INT */

/* `move.w #0x1` / `#0x21` at 0x00E5D9D2 / 0x00E5D9DA. */
#define FILE_GET_ATTR_FLAGS_LOCKED  0x0001
#define FILE_GET_ATTR_FLAGS_NORMAL  0x0021

void FILE_$GET_ATTRIBUTES(uid_t *file_uid, void *param_2, int16_t *size_ptr,
                          file_$obj_loc_t *loc_rec, void *attr_out,
                          status_$t *status_ret)
{
    /* `btst.b #n,(0x1,A0)` addresses the SECOND byte of the two-byte request
     * cell, which on the m68k is the word's LOW byte - so the tests below
     * are on the word, not on a byte at a fixed array index. */
    const uint16_t *req = (const uint16_t *)param_2;    /* A0 */
    uint8_t         lock_info[2];               /* A6-0xBC */
    uint16_t        flags;                      /* A6-0xBA */
    status_$t       status;                     /* A6-0xB4 */
    uint8_t         attrs[AST_ATTR_REC_SIZE];   /* A6-0xB0 */
    file_$obj_loc_t desc;                       /* A6-0x20 */
    const uint32_t *src;
    uint32_t       *dst;
    int16_t         i;

    /* 0x00E5D99E-0x00E5D9E0 */
    if ((*req & FILE_GET_ATTR_REQ_LOCKED) != 0) {
        flags = FILE_GET_ATTR_FLAGS_LOCKED;
    } else if ((*req & FILE_GET_ATTR_REQ_NO_PROBE) != 0) {
        flags = FILE_GET_ATTR_FLAGS_NORMAL;
    } else if ((*req & FILE_GET_ATTR_REQ_PROBE) != 0) {
        /* 0x00E5D9B6-0x00E5D9D0.  The probe reads the UID out of the
         * CALLER's record (`pea (0x8,A3)`), which has not been rewritten
         * yet, and a negative result means "the object is locked". */
        if (FILE_$DELETE_INT(&loc_rec->uid, 0, lock_info, &status) < 0) {
            flags = FILE_GET_ATTR_FLAGS_LOCKED;
        } else {
            flags = FILE_GET_ATTR_FLAGS_NORMAL;
        }
    } else {
        /* 0x00E5D9B4 `beq` shares the 0x00E5D9FC error store. */
        *status_ret = file_$invalid_arg;
        return;
    }

    /* 0x00E5D9E0-0x00E5D9EC: the caller's UID into the local descriptor at
     * +0x08, then `bclr.b #0x6,(-0x3,A6)` on the flags byte at +0x1D. */
    desc.uid.high = file_uid->high;
    desc.uid.low  = file_uid->low;
    desc.flags   &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

    /* 0x00E5D9F2-0x00E5DA02 */
    if (*size_ptr != FILE_ATTR_FULL_SIZE) {
        *status_ret = file_$invalid_arg;
        return;
    }

    /* 0x00E5DA04-0x00E5DA1C */
    AST_$GET_ATTRIBUTES(&desc, flags, attrs, &status);

    /* 0x00E5DA20-0x00E5DA2E: 36 longwords, attrs -> the caller's buffer.
     * Unconditional - the status is not consulted first. */
    src = (const uint32_t *)(const void *)attrs;
    dst = (uint32_t *)attr_out;
    for (i = (AST_ATTR_REC_SIZE / 4) - 1; i >= 0; i--) {
        *dst++ = *src++;
    }

    /* 0x00E5DA30-0x00E5DA3C: 8 longwords, the local descriptor -> the
     * caller's record. */
    src = (const uint32_t *)(const void *)&desc;
    dst = (uint32_t *)(void *)loc_rec;
    for (i = (AST_$LOC_REC_SIZE / 4) - 1; i >= 0; i--) {
        *dst++ = *src++;
    }

    /* 0x00E5DA3E */
    *status_ret = status;
}
