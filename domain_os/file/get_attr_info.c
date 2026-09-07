/*
 * FILE_$GET_ATTR_INFO - Get file attributes in the compact 0x7A-byte format
 *
 * Original address: 0x00E5D7F4, 400 bytes.
 *
 * Frame (`link.w A6,-0xb8`, `movem.l {A5 A4 A3 A2 D3 D2},-(SP)`):
 *   A5 = 0xE82128 = FILE_$LOCK_CONTROL, established at 0x00E5D7FC but never
 *        dereferenced by this routine.
 *   A6+0x08  file_uid    caller's object UID
 *   A6+0x0C  param_2     (A0) pointer to the two-byte request word
 *   A6+0x10  size_ptr    pointer to the caller's buffer size word
 *   A6+0x14  loc_rec     (D2) the caller's 0x20-byte object-location record
 *   A6+0x18  attr_out    (A2) the caller's 0x7A-byte compact record
 *   A6+0x1C  status_ret  (A3)
 *
 *   A6-0xB8  uint8_t   lock_info[2]   FILE_$DELETE_INT's third argument
 *   A6-0xB6  uint16_t  flags          AST_$GET_ATTRIBUTES' flag word
 *   A6-0xB4  status_$t status
 *   A6-0xB0  uint8_t   attrs[0x90]    the full attribute record; a listing
 *                                     displacement d is record offset d+0xB0
 *   A6-0x20  file_$obj_loc_t desc     UID at A6-0x18, flags byte at A6-0x03
 */

#include "file/file_internal.h"

/*
 * Bits tested in the request word (`btst.b #n,(0x1,A0)` at 0x00E5D812,
 * 0x00E5D81A and 0x00E5D822 - the second byte of a big-endian word is its low
 * byte) - the same three-way dispatch FILE_$GET_ATTRIBUTES uses.
 */
#define FILE_ATTR_INFO_REQ_LOCKED   0x01    /* bit 0 */
#define FILE_ATTR_INFO_REQ_NO_PROBE 0x04    /* bit 2 */
#define FILE_ATTR_INFO_REQ_PROBE    0x02    /* bit 1 */

/* `move.w #0x1` / `#0x21` at 0x00E5D848 / 0x00E5D850. */
#define FILE_ATTR_INFO_FLAGS_LOCKED 0x0001
#define FILE_ATTR_INFO_FLAGS_NORMAL 0x0021

/*
 * The 0x7A-byte compact record this routine builds, recovered store by store
 * from 0x00E5D8B0-0x00E5D978.  Every displacement below is the A2 offset in
 * the listing; the three byte ranges the record never writes (0x34, 0x6B-0x6D
 * and the alignment holes) are spelled out so the layout is exact.
 */
typedef struct __attribute__((packed, aligned(2))) file_$attr_info_t {
    uint8_t  obj_flags[4];  /* 0x00: attrs+0x00 masked with 0x00FF1F06.  Kept
                             * as four bytes rather than a longword because
                             * the routine goes on to do bit surgery on
                             * record+0x02 and +0x03 individually, which only
                             * lines up with the image's longword if the bytes
                             * are placed big-endian by hand. */
    /* obj_flags[2] (record+0x02) is rewritten bit by bit:
     *   bit 1 <- attrs+0x65 bit 7 (0x00E5D8BC-0x00E5D8CC)
     *   bit 0 <- attrs+0x65 bit 5 (0x00E5D950-0x00E5D962)
     * and record+0x03 bit 7 <- attrs+0x65 bit 4 (0x00E5D964-0x00E5D976).
     * They are addressed as bytes because the mask leaves the rest alone. */
    uint8_t  name[24];      /* 0x04: attrs+0x04, 24 bytes (0x00E5D8D0) */
    uint32_t dtu_high;      /* 0x1C: attrs+0x3C (0x00E5D8E0) */
    uint32_t dtu_low;       /* 0x20: attrs+0x40 */
    uint32_t dtm_high;      /* 0x24: attrs+0x1C (0x00E5D8EC) */
    uint16_t dtm_low;       /* 0x28: attrs+0x20 (0x00E5D8F2) */
    uint16_t pad_2a;        /* 0x2A: never written */
    uint32_t dtc_high;      /* 0x2C: attrs+0x24 (0x00E5D8F8) */
    uint16_t dtc_low;       /* 0x30: attrs+0x28 (0x00E5D8FE) */
    uint16_t devno;         /* 0x32: attrs+0x76 (0x00E5D904) - PACCT_$LOG
                             * reads this word back as the TTY device number */
    uint16_t pad_34;        /* 0x34: never written */
    uint32_t blocks;        /* 0x36: attrs+0x34 (0x00E5D90A) */
    uint16_t blocks_low;    /* 0x3A: attrs+0x38 (0x00E5D910) */
    uint16_t refcount;      /* 0x3C: attrs+0x74 (0x00E5D916) */
    uint8_t  uids[16];      /* 0x3E: attrs+0x78, 16 bytes (0x00E5D91C) */
    uint8_t  acl[28];       /* 0x4E: attrs+0x48, 28 bytes (0x00E5D92C) */
    uint8_t  access_mode;   /* 0x6A: attrs+0x64 (0x00E5D93C) */
    uint8_t  pad_6b[3];     /* 0x6B: never written */
    uint32_t tail[3];       /* 0x6E: attrs+0x68/0x6C/0x70 (0x00E5D942) */
} file_$attr_info_t;

_Static_assert(offsetof(file_$attr_info_t, obj_flags) == 0x00, "attr_info.obj_flags");
_Static_assert(offsetof(file_$attr_info_t, name)     == 0x04, "attr_info.name");
_Static_assert(offsetof(file_$attr_info_t, dtu_high) == 0x1C, "attr_info.dtu_high");
_Static_assert(offsetof(file_$attr_info_t, dtm_high) == 0x24, "attr_info.dtm_high");
_Static_assert(offsetof(file_$attr_info_t, devno)    == 0x32, "attr_info.devno");
_Static_assert(offsetof(file_$attr_info_t, blocks)   == 0x36, "attr_info.blocks");
_Static_assert(offsetof(file_$attr_info_t, refcount) == 0x3C, "attr_info.refcount");
_Static_assert(offsetof(file_$attr_info_t, uids)     == 0x3E, "attr_info.uids");
_Static_assert(offsetof(file_$attr_info_t, acl)      == 0x4E, "attr_info.acl");
_Static_assert(offsetof(file_$attr_info_t, access_mode) == 0x6A, "attr_info.access_mode");
_Static_assert(offsetof(file_$attr_info_t, tail)     == 0x6E, "attr_info.tail");
_Static_assert(sizeof(file_$attr_info_t) == FILE_ATTR_INFO_SIZE, "sizeof attr_info");

/*
 * Offsets into the 0x90-byte attribute record.  Each is the listing's A6
 * displacement plus 0xB0.
 */
#define ATTR_TYPE_FLAGS     0x00    /* -0xB0 */
#define ATTR_NAME           0x04    /* -0xAC */
#define ATTR_DTM_HIGH       0x1C    /* -0x94 */
#define ATTR_DTM_LOW        0x20    /* -0x90 */
#define ATTR_DTC_HIGH       0x24    /* -0x8C */
#define ATTR_DTC_LOW        0x28    /* -0x88 */
#define ATTR_BLOCKS         0x34    /* -0x7C */
#define ATTR_BLOCKS_LOW     0x38    /* -0x78 */
#define ATTR_DTU_HIGH       0x3C    /* -0x74 */
#define ATTR_ACL            0x48    /* -0x68 */
#define ATTR_ACCESS_MODE    0x64    /* -0x4C */
#define ATTR_ACCESS_FLAGS   0x65    /* -0x4B */
#define ATTR_TAIL           0x68    /* -0x48 */
#define ATTR_REFCOUNT       0x74    /* -0x3C */
#define ATTR_DEVNO          0x76    /* -0x3A */
#define ATTR_UIDS           0x78    /* -0x38 */

/* `andi.l #0xff1f06,D1` at 0x00E5D8B4. */
#define ATTR_INFO_FLAG_MASK 0x00FF1F06

/* Bits of the attrs+0x65 access-flags byte that are folded into the record. */
#define ATTR_ACCESS_OS_ONLY 0x80    /* -> record+0x02 bit 1 */
#define ATTR_ACCESS_MODE5   0x20    /* -> record+0x02 bit 0 */
#define ATTR_ACCESS_MODE4   0x10    /* -> record+0x03 bit 7 */

void FILE_$GET_ATTR_INFO(uid_t *file_uid, void *param_2, int16_t *size_ptr,
                         file_$obj_loc_t *loc_rec, void *attr_out,
                         status_$t *status_ret)
{
    /* `btst.b #n,(0x1,A0)` addresses the SECOND byte of the two-byte request
     * cell, which on the m68k is the word's LOW byte - so the tests below
     * are on the word, not on a byte at a fixed array index. */
    const uint16_t *req = (const uint16_t *)param_2;    /* A0 */
    uint8_t         lock_info[2];               /* A6-0xB8 */
    uint16_t        flags;                      /* A6-0xB6 */
    status_$t       status;                     /* A6-0xB4 */
    uint8_t         attrs[AST_ATTR_REC_SIZE];   /* A6-0xB0 */
    file_$obj_loc_t desc;                       /* A6-0x20 */
    file_$attr_info_t *out = (file_$attr_info_t *)attr_out;     /* A2 */
    uint32_t        obj_flags;
    int8_t          access_flags;               /* D3 */
    const uint32_t *src32;
    uint32_t       *dst32;
    const uint8_t  *src8;
    uint8_t        *dst8;
    int16_t         i;

    /* 0x00E5D812-0x00E5D856 */
    if ((*req & FILE_ATTR_INFO_REQ_LOCKED) != 0) {
        flags = FILE_ATTR_INFO_FLAGS_LOCKED;
    } else if ((*req & FILE_ATTR_INFO_REQ_NO_PROBE) != 0) {
        flags = FILE_ATTR_INFO_FLAGS_NORMAL;
    } else if ((*req & FILE_ATTR_INFO_REQ_PROBE) != 0) {
        /* 0x00E5D82A-0x00E5D846: `movea.l D2,A1; pea (0x8,A1)` - the probe
         * reads the UID out of the CALLER's record, before it is rewritten. */
        if (FILE_$DELETE_INT(&loc_rec->uid, 0, lock_info, &status) < 0) {
            flags = FILE_ATTR_INFO_FLAGS_LOCKED;
        } else {
            flags = FILE_ATTR_INFO_FLAGS_NORMAL;
        }
    } else {
        /* 0x00E5D828 `beq` shares the 0x00E5D8A6 error store. */
        *status_ret = file_$invalid_arg;
        return;
    }

    /* 0x00E5D856-0x00E5D862 */
    desc.uid.high = file_uid->high;
    desc.uid.low  = file_uid->low;
    desc.flags   &= (int8_t)~FILE_OBJ_LOC_SCRATCH;

    /* 0x00E5D868-0x00E5D880 */
    AST_$GET_ATTRIBUTES(&desc, flags, attrs, &status);

    /* 0x00E5D884-0x00E5D88A: unlike FILE_$GET_ATTRIBUTES, this routine stops
     * on a bad status before copying anything out. */
    *status_ret = status;
    if (status != status_$ok) {
        return;
    }

    /* 0x00E5D88E-0x00E5D89A: 8 longwords, the local descriptor -> the
     * caller's record. */
    src32 = (const uint32_t *)(const void *)&desc;
    dst32 = (uint32_t *)(void *)loc_rec;
    for (i = (AST_$LOC_REC_SIZE / 4) - 1; i >= 0; i--) {
        *dst32++ = *src32++;
    }

    /* 0x00E5D89C-0x00E5D8AC */
    if (*size_ptr != FILE_ATTR_INFO_SIZE) {
        *status_ret = file_$invalid_arg;
        return;
    }

    /* 0x00E5D8B0-0x00E5D8BA: `move.l (-0xb0,A6),D1`, `andi.l #0xff1f06,D1`,
     * `move.l D1,(A2)`.  Both ends are spelled out byte by byte so the four
     * record bytes keep their image order on a little-endian host. */
    obj_flags = ((uint32_t)attrs[ATTR_TYPE_FLAGS + 0] << 24) |
                ((uint32_t)attrs[ATTR_TYPE_FLAGS + 1] << 16) |
                ((uint32_t)attrs[ATTR_TYPE_FLAGS + 2] << 8) |
                 (uint32_t)attrs[ATTR_TYPE_FLAGS + 3];
    obj_flags &= ATTR_INFO_FLAG_MASK;
    out->obj_flags[0] = (uint8_t)(obj_flags >> 24);
    out->obj_flags[1] = (uint8_t)(obj_flags >> 16);
    out->obj_flags[2] = (uint8_t)(obj_flags >> 8);
    out->obj_flags[3] = (uint8_t)obj_flags;

    access_flags = (int8_t)attrs[ATTR_ACCESS_FLAGS];

    /* 0x00E5D8BC-0x00E5D8CE: `tst.b`/`smi`/`lsr.b #7`/`add.b D3b,D3b` puts the
     * sign bit of the access byte into bit 1 of record+0x02. */
    out->obj_flags[2] &= (uint8_t)~0x02;
    if ((access_flags & ATTR_ACCESS_OS_ONLY) != 0) {
        out->obj_flags[2] |= 0x02;
    }

    /* The 0x00FF1F06 mask above already zeroed record+0x00 entirely and cut
     * record+0x02 to bits 0-4 and record+0x03 to bits 1-2; the two remaining
     * access bits are folded in at the end of the routine. */

    /* 0x00E5D8D0-0x00E5D8DE: 24 bytes, attrs+0x04 -> record+0x04. */
    src8 = &attrs[ATTR_NAME];
    dst8 = out->name;
    for (i = 0x17; i >= 0; i--) {
        *dst8++ = *src8++;
    }

    /* 0x00E5D8E0-0x00E5D8EA: two longwords, attrs+0x3C/0x40. */
    out->dtu_high = *(const uint32_t *)(const void *)&attrs[ATTR_DTU_HIGH];
    out->dtu_low  = *(const uint32_t *)(const void *)&attrs[ATTR_DTU_HIGH + 4];

    /* 0x00E5D8EC-0x00E5D8F6 */
    out->dtm_high = *(const uint32_t *)(const void *)&attrs[ATTR_DTM_HIGH];
    out->dtm_low  = *(const uint16_t *)(const void *)&attrs[ATTR_DTM_LOW];

    /* 0x00E5D8F8-0x00E5D902 */
    out->dtc_high = *(const uint32_t *)(const void *)&attrs[ATTR_DTC_HIGH];
    out->dtc_low  = *(const uint16_t *)(const void *)&attrs[ATTR_DTC_LOW];

    /* 0x00E5D904 */
    out->devno = *(const uint16_t *)(const void *)&attrs[ATTR_DEVNO];

    /* 0x00E5D90A-0x00E5D914 */
    out->blocks     = *(const uint32_t *)(const void *)&attrs[ATTR_BLOCKS];
    out->blocks_low = *(const uint16_t *)(const void *)&attrs[ATTR_BLOCKS_LOW];

    /* 0x00E5D916 */
    out->refcount = *(const uint16_t *)(const void *)&attrs[ATTR_REFCOUNT];

    /* 0x00E5D91C-0x00E5D92A: 16 bytes, attrs+0x78 -> record+0x3E. */
    src8 = &attrs[ATTR_UIDS];
    dst8 = out->uids;
    for (i = 0x0F; i >= 0; i--) {
        *dst8++ = *src8++;
    }

    /* 0x00E5D92C-0x00E5D93A: 28 bytes, attrs+0x48 -> record+0x4E. */
    src8 = &attrs[ATTR_ACL];
    dst8 = out->acl;
    for (i = 0x1B; i >= 0; i--) {
        *dst8++ = *src8++;
    }

    /* 0x00E5D93C */
    out->access_mode = attrs[ATTR_ACCESS_MODE];

    /* 0x00E5D942-0x00E5D94E: three longwords, attrs+0x68 -> record+0x6E.
     * Neither end is longword-aligned in the image; the copy is byte-exact
     * either way. */
    src32 = (const uint32_t *)(const void *)&attrs[ATTR_TAIL];
    for (i = 0; i < 3; i++) {
        out->tail[i] = src32[i];
    }

    /* 0x00E5D950-0x00E5D962: bit 5 of the access byte becomes bit 0 of
     * record+0x02 (`sne` + `lsr.b #7` yields 0 or 1). */
    out->obj_flags[2] &= (uint8_t)~0x01;
    if ((access_flags & ATTR_ACCESS_MODE5) != 0) {
        out->obj_flags[2] |= 0x01;
    }

    /* 0x00E5D964-0x00E5D978: bit 4 becomes bit 7 of record+0x03
     * (`sne` gives 0xFF, `andi.b #-0x80` keeps only bit 7). */
    out->obj_flags[3] &= (uint8_t)0x7F;
    if ((access_flags & ATTR_ACCESS_MODE4) != 0) {
        out->obj_flags[3] |= 0x80;
    }
}
