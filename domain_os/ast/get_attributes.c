/*
 * AST_$GET_ATTRIBUTES - fill the caller's attribute record from the AOTE
 *
 * Original address: 0x00E047A0, 608 bytes.
 *
 * Frame (`link.w A6,-0x1c`, `movem.l {A5 A4 A3 A2 D3 D2},-(SP)`):
 *   A5 = 0xE1DC80, the AST module base (only used for
 *        AST_$AST_IN_TRANS_EC at A5+0x428 = 0xE1E0A8)
 *   A4 = A6+0x08  loc_rec       the 0x20-byte object-location record
 *   D2 = A6+0x0C  flags         (word)
 *   A3 = A6+0x0E  attrs         the caller's 0x90-byte attribute buffer
 *   D3 = A6+0x12  status
 *   A6+0x16       Pascal result slot: the callers reserve it
 *                 (`subq.l #0x2,SP`) but the body never writes it, so this
 *                 is a procedure and returns nothing.
 *   A2            the AOTE under work
 *
 *   A6-0x1A  word  net_flags   NETWORK_$AST_GET_INFO request flags
 *   A6-0x14  long  zero        cleared at 0x00E04822, passed by value as
 *                              ast_$force_activate_segment's segment number
 *   A6-0x10  long  lstatus     inner status
 *   A6-0x08  long  saved_dtv_high
 *   A6-0x04  word  saved_dtv_low
 *
 * ARGUMENT 1 IS THE OBJECT-LOCATION RECORD, NOT A UID.  The object UID lives
 * at loc_rec+0x08 (`lea (0x8,A4),A0` at 0x00E047D2 and the three
 * `pea (0x8,A4)` at 0x00E047EC / 0x00E04810 / 0x00E04836), and on every path
 * that reaches an AOTE the routine overwrites all 0x20 bytes of the record
 * from aote+0x9C (0x00E0492C and 0x00E049B0).
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"
#include "route/route.h"

/* `move.l #0x30f01,-(SP)` at 0x00E047E6 - the flag word ast_$validate_uid is
 * given for a nil object UID. */
#define AST_GET_ATTR_VALIDATE_FLAGS     0x00030F01

/* Bits tested in the `flags` word (D2). */
#define AST_GET_ATTR_RESERVED   0xFC00  /* andi.w #-0x400 at 0x00E047C0 */
#define AST_GET_ATTR_REFRESH    0x0020  /* btst.l #0x5,D2 at 0x00E04860 */
#define AST_GET_ATTR_FULL       0x0200  /* btst.l #0x9,D2 at 0x00E0488C/0x00E0495E */
#define AST_GET_ATTR_TOUCH      0x0080  /* tst.b D2b + bmi/bpl - bit 7 of the
                                         * LOW byte of the word */

/* `move.w #0x88` / `#0x8` at 0x00E0489C / 0x00E048A4 - the NETWORK_$AST_GET_INFO
 * request selector. */
#define AST_NET_GET_INFO_ATTRS      0x0008
#define AST_NET_GET_INFO_ATTRS_ACL  0x0088

/* Number of longwords in the 0x90-byte attribute record (`moveq #0x23` + dbf). */
#define AST_ATTR_REC_LONGS      (AST_ATTR_REC_SIZE / 4)
/* Number of longwords in the 0x20-byte location record (`moveq #0x7` + dbf). */
#define AST_LOC_REC_LONGS       (AST_$LOC_REC_SIZE / 4)

void AST_$GET_ATTRIBUTES(file_$obj_loc_t *loc_rec, uint16_t flags, void *attrs,
                         status_$t *status)
{
    uint32_t *attr_buf = (uint32_t *)attrs;                     /* A3 */
    uint32_t *loc_words = (uint32_t *)(void *)loc_rec;          /* A4 */
    aote_t *aote;                                               /* A2 */
    status_$t lstatus;          /* A6-0x10 */
    uint32_t zero;              /* A6-0x14 */
    uint16_t net_flags;         /* A6-0x1A */
    uint32_t saved_dtv_high;    /* A6-0x08 */
    uint16_t saved_dtv_low;     /* A6-0x04 */
    uint32_t length;
    const uint32_t *src;
    uint32_t *dst;
    int16_t i;

    /* 0x00E047BA-0x00E047CE: any bit above 9 makes the request illegal. */
    if ((flags & AST_GET_ATTR_RESERVED) != 0) {
        *status = status_$ast_incompatible_request;
        return;
    }

    /* 0x00E047D2-0x00E047F8: a nil object UID is handed to ast_$validate_uid,
     * whose D0 result becomes the caller's status. */
    if (loc_rec->uid.high == UID_$NIL.high && loc_rec->uid.low == UID_$NIL.low) {
        *status = ast_$validate_uid(&loc_rec->uid, AST_GET_ATTR_VALIDATE_FLAGS);
        return;
    }

    /* 0x00E047FC-0x00E0481A */
    PROC1_$INHIBIT_BEGIN();
    ML_$LOCK(AST_LOCK_ID);
    aote = ast_$lookup_aote_by_uid(&loc_rec->uid);

    if (aote == NULL) {
        /* 0x00E04822-0x00E04852: not cached - activate it.  The boolean is
         * `tst.b D2b; smi D0b`, i.e. bit 7 of the flags word's low byte. */
        zero = 0;
        aote = ast_$force_activate_segment(&loc_rec->uid, zero, &lstatus,
                                           ((flags & AST_GET_ATTR_TOUCH) != 0)
                                               ? (int8_t)0xFF : (int8_t)0x00);
        *status = lstatus;
        if (aote == NULL) {
            goto unlock;
        }
        goto check_remote;
    }

    /* 0x00E04856-0x00E04864 */
    aote->flags |= AOTE_FLAG_BUSY;
    *status = status_$ok;
    if ((flags & AST_GET_ATTR_REFRESH) == 0) {
        goto check_remote;
    }
    /* 0x00E04868: a local object needs no network round trip. */
    if (aote->remote_flag >= 0) {
        goto maybe_touch;
    }

    /*
     * 0x00E04870-0x00E04956: remote object, refresh requested.
     */
    aote->flags |= AOTE_FLAG_IN_TRANS;
    ML_$UNLOCK(AST_LOCK_ID);

    /* 0x00E04884-0x00E048A8.  attr_flags_lo bit 0 forces the short form. */
    if ((aote->attr_flags_lo & 0x01) != 0) {
        net_flags = AST_NET_GET_INFO_ATTRS;
    } else if ((flags & AST_GET_ATTR_FULL) != 0 ||
               (aote->flags & AOTE_FLAG_TOUCHED) != 0) {
        /* `move.w (0xbe,A2),D0w; btst.l #0x4,D0` at 0x00E04892 - the word at
         * aote+0xBE, whose bit 4 is bit 4 of the flags byte at aote+0xBF. */
        net_flags = AST_NET_GET_INFO_ATTRS_ACL;
    } else {
        net_flags = AST_NET_GET_INFO_ATTRS;
    }

    aote->flags &= (uint8_t)~AOTE_FLAG_TOUCHED;     /* bclr #4 at 0x00E048AA */

    /* 0x00E048B0-0x00E048CA: the "who/where" block at aote+0x9C is the
     * location record the remote side is asked about. */
    NETWORK_$AST_GET_INFO(&aote->obj_uid, &net_flags, attrs, &lstatus);
    *status = lstatus;

    ML_$LOCK(AST_LOCK_ID);
    if (*status == status_$ok) {
        ML_$LOCK(PMAP_LOCK_ID);

        /* 0x00E048F0-0x00E048FC: keep the larger of the cached length and the
         * one the remote node reported at attrs+0x14 (unsigned compare). */
        length = aote->length;
        if (length <= attr_buf[0x14 / 4]) {
            length = attr_buf[0x14 / 4];
        }

        /* 0x00E048FE-0x00E04904: DTV survives the wholesale overwrite. */
        saved_dtv_high = aote->dtv_high;
        saved_dtv_low = aote->dtv_low;

        /* 0x00E0490A-0x00E04916: 36 longwords, attrs -> aote+0x0C. */
        src = attr_buf;
        dst = (uint32_t *)(void *)&aote->obj_type;
        for (i = AST_ATTR_REC_LONGS - 1; i >= 0; i--) {
            *dst++ = *src++;
        }

        /* 0x00E04918-0x00E04922 */
        aote->length = length;
        aote->dtv_high = saved_dtv_high;
        aote->dtv_low = saved_dtv_low;

        /* 0x00E04928-0x00E04934: 8 longwords, aote+0x9C -> the caller's
         * location record. */
        src = (const uint32_t *)(const void *)&aote->obj_uid;
        dst = loc_words;
        for (i = AST_LOC_REC_LONGS - 1; i >= 0; i--) {
            *dst++ = *src++;
        }

        ML_$UNLOCK(PMAP_LOCK_ID);
    }

    /* 0x00E04944-0x00E04954 */
    aote->flags &= (uint8_t)~AOTE_FLAG_IN_TRANS;
    EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);
    goto unlock;

check_remote:
    /* 0x00E04958: a remote object's cached copy is handed back untouched. */
    if (aote->remote_flag < 0) {
        goto copy_out;
    }

maybe_touch:
    /* 0x00E0495E-0x00E049A0 */
    if ((flags & AST_GET_ATTR_FULL) != 0 ||
        (aote->flags & AOTE_FLAG_TOUCHED) != 0) {
        ML_$LOCK(PMAP_LOCK_ID);
        TIME_$CLOCK((clock_t *)(void *)&aote->dtu_high);
        aote->flags |= AOTE_FLAG_DIRTY;                 /* bset #5 */
        aote->flags &= (uint8_t)~AOTE_FLAG_TOUCHED;     /* bclr #4 */
        ML_$UNLOCK(PMAP_LOCK_ID);
    }

copy_out:
    /* 0x00E049A2-0x00E049AE: 36 longwords, aote+0x0C -> attrs. */
    src = (const uint32_t *)(const void *)&aote->obj_type;
    dst = attr_buf;
    for (i = AST_ATTR_REC_LONGS - 1; i >= 0; i--) {
        *dst++ = *src++;
    }

    /* 0x00E049B0-0x00E049BC: 8 longwords, aote+0x9C -> the location record. */
    src = (const uint32_t *)(const void *)&aote->obj_uid;
    dst = loc_words;
    for (i = AST_LOC_REC_LONGS - 1; i >= 0; i--) {
        *dst++ = *src++;
    }

unlock:
    /* 0x00E049BE-0x00E049CA */
    ML_$UNLOCK(AST_LOCK_ID);

    /* 0x00E049CC-0x00E049E0: a "touch" request against an object that turned
     * out to be remote is reported as object-not-found.  Both tests are on
     * signed bytes (`tst.b D2b` and `tst.b (0x1d,A4)` + `bpl`). */
    if (*status == status_$ok && (flags & AST_GET_ATTR_TOUCH) != 0 &&
        (loc_rec->flags & FILE_OBJ_LOC_REMOTE) != 0) {
        *status = file_$object_not_found;
    }

    /* 0x00E049E2 */
    PROC1_$INHIBIT_END();

    /* 0x00E049E8-0x00E049F4: a record with no locator gets this node's own.
     * The image reads the longword at 0xE2E0A0, which route_data.c declares
     * both as ROUTE_$PORT and as ROUTE_$PORT_ARRAY[0].network. */
    if (loc_rec->loc_info == 0) {
        loc_rec->loc_info = ROUTE_$PORT;
    }
}
