/*
 * AST_$GET_LOCATION - Return an object's location word and location record
 *
 * A NIL UID is refused through ast_$validate_uid.  Otherwise the AOTE is
 * found (or activated when flags bit 0 asks for it), its location word
 * (aote+0x08) is handed back through `location_out`, and the embedded
 * 0x20-byte file_$obj_loc_t (aote+0x9C) overwrites the caller's record.
 * A zero loc_info word in the returned record is replaced by ROUTE_$PORT.
 *
 * Parameters (frame at 0x00E046C8, `link.w A6,-0xc`):
 *   loc_rec      (0x08,A6)  the 0x20-byte record; its UID is at +0x08 (A2)
 *   flags        (0x0C,A6)  word (D2); bit 0 = activate when not resident
 *   unused       (0x0E,A6)  4 bytes never read or written
 *   location_out (0x12,A6)
 *   status       (0x16,A6)  (A3)
 * (-0x4,A6) is the zero location word given to ast_$force_activate_segment.
 *
 * Original address: 0x00E046C8 (216 bytes), A5 = 0xE1DC80 (unused here).
 */

#include "ast/ast_internal.h"
#include "route/route.h"

void AST_$GET_LOCATION(file_$obj_loc_t *loc_rec, uint16_t flags,
                       uint32_t *unused, uint32_t *location_out,
                       status_$t *status)
{
    uid_t *uid = &loc_rec->uid;         /* lea (0x8,A2),A0 */
    uint32_t location;                  /* (-0x4,A6) */
    aote_t *aote;                       /* A0 */
    uint32_t *src;                      /* A4 */
    uint32_t *dst;                      /* A1 */
    int16_t i;

    (void)unused;

    /* 0x00E046E2..0x00E04706: two cmpm.l against UID_$NIL (0xE1737C) */
    if (uid->high == UID_$NIL.high && uid->low == UID_$NIL.low) {
        *status = ast_$validate_uid(uid, 0x30F01);
        return;
    }

    /* 0x00E0470A..0x00E04716 */
    ML_$LOCK(AST_LOCK_ID);

    /* 0x00E04718..0x00E04726 */
    aote = ast_$lookup_aote_by_uid(uid);
    if (aote == NULL) {
        /* 0x00E04728..0x00E0474C: location := 0; force := sne(flags bit 0) */
        location = 0;
        aote = ast_$force_activate_segment(uid, location, status,
                                           (flags & 1) ? -1 : 0);
        if (aote == NULL) {
            /* 0x00E0474E..0x00E0475A: the callee's status stands */
            ML_$UNLOCK(AST_LOCK_ID);
            return;
        }
    } else {
        /* 0x00E0475C: bset.b #0x6,(0xbf,A0) */
        aote->flags |= AOTE_FLAG_BUSY;
    }

    /* 0x00E04762..0x00E04766 */
    *location_out = aote->location;

    /* 0x00E0476A..0x00E04774: moveq #0x7 / dbf = 8 longwords from 0x9C */
    src = (uint32_t *)&aote->obj_uid;
    dst = (uint32_t *)(void *)loc_rec;
    for (i = 0x7; i >= 0; i--) {
        *dst++ = *src++;
    }

    /* 0x00E04778..0x00E04784 */
    ML_$UNLOCK(AST_LOCK_ID);

    /* 0x00E04786..0x00E04792: record+0x10 (loc_info) defaults to ROUTE_$PORT */
    if (loc_rec->loc_info == 0) {
        loc_rec->loc_info = ROUTE_$PORT;
    }

    /* 0x00E04794 */
    *status = status_$ok;
}
