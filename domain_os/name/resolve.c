/*
 * NAME_$RESOLVE - Resolve a pathname to a UID (0x00E4A258, 96 bytes)
 *
 * SAU2 map: NAMEU segment at E49E48 (size 0x5C0).  This file holds
 *   name_$parse_component   0x00E4A004  ( 92 bytes, module-local)
 *   name_$resolve_internal  0x00E4A060  (304 bytes, module-local)
 *   NAME_$RESOLVE           0x00E4A258  ( 96 bytes)
 *
 * The resolver is deliberately small: it understands absolute ("/...") and
 * "`node_data..." paths only, walks the components with DIR_$GET_ENTRYU, and
 * turns "directory not found in pathname" into "name not found" for its
 * caller.  Relative and "//" paths are rejected outright.
 */

#include "name/name_internal.h"

/*
 * Record DIR_$GET_ENTRYU fills at A6-0x18 in name_$resolve_internal:
 * `move.w (-0x18,A6),D0w` (0x00E4A1AC) reads the entry type, and the
 * `lea (-0x16,A6),A0` at 0x00E4A1CA copies the entry UID that follows it.
 */
typedef struct __attribute__((packed, aligned(2))) name_$lookup_entry_t {
    int16_t type;               /* +0x00: 0 = absent, 1 = has a UID, 3 = bad */
    uid_t   uid;                /* +0x02 */
} name_$lookup_entry_t;

_Static_assert(__builtin_offsetof(name_$lookup_entry_t, uid) == 0x02, "name_$lookup_entry_t.uid");
_Static_assert(sizeof(name_$lookup_entry_t) == 0x0A, "sizeof name_$lookup_entry_t");

/*
 * name_$parse_component (0x00E4A004, 92 bytes)
 *
 * Starting at 1-based `start_pos`, skip '/' bytes, then count the bytes up
 * to the next '/' or the end.  All bookkeeping is done through the output
 * pointers (the routine keeps its cursor in *next_pos).
 *
 * Frame (A6+): 0x08 path -> A0, 0x0C path_len (word) -> D0w,
 * 0x0E start_pos (word) -> D1w, 0x10 next_pos -> A1, 0x14 comp_start -> A3,
 * 0x18 comp_len -> A2.
 */
static void name_$parse_component(char *path, uint16_t path_len, uint16_t start_pos,
                                  uint16_t *next_pos, uint16_t *comp_start,
                                  int16_t *comp_len)
{
    uint16_t pos;

    *comp_len = 0;                                          /* 0x00E4A020 */
    *next_pos = start_pos;                                  /* 0x00E4A022 */

    /* 0x00E4A026-0x00E4A034: skip leading slashes while *next_pos <= len
     * (`cmp.w (A1),D0w / bcs` = unsigned path_len < *next_pos exits). */
    while (path_len >= *next_pos && (uint8_t)path[*next_pos - 1] == 0x2F) {
        *next_pos = (uint16_t)(*next_pos + 1);
    }

    /* 0x00E4A036-0x00E4A03A: nothing left */
    pos = *next_pos;
    if (pos > path_len) {
        return;
    }

    *comp_start = pos;                                      /* 0x00E4A040 */

    /* 0x00E4A044-0x00E4A054: count up to the next '/' or the end. */
    while (path_len >= *next_pos && (uint8_t)path[*next_pos - 1] != 0x2F) {
        *next_pos = (uint16_t)(*next_pos + 1);
        *comp_len = (int16_t)(*comp_len + 1);
    }
}

/*
 * name_$resolve_internal (0x00E4A060, 304 bytes)
 *
 * Frame (A6+): 0x08 path -> A2, 0x0C path_len (word) -> D2w,
 * 0x0E dir_uid_ret -> A4, 0x12 file_uid_ret -> D5, 0x16 status_ret -> A3.
 * Locals (A6-): -0x38 uid scratch for the NAME_$GET_* calls, -0x2E cursor
 * (NAME_$VALIDATE's "consumed"), -0x2C next_pos, -0x2A comp_start,
 * -0x28 comp_len, -0x26 start-path type (word), -0x20 current UID,
 * -0x18 the DIR_$GET_ENTRYU record {type, uid}.
 *
 * The start-path type selects one of five arms through the jump table at
 * 0x00E4A0C4 (index = type - 1, `cmpi.w #0x5 / bcc` sends anything else
 * straight to the loop):
 *   1 relative  -> 0x00E4A1D2  status_$naming_invalid_pathname
 *   2           -> 0x00E4A120  straight into the loop
 *   3 absolute  -> 0x00E4A0CE  cursor = 2, current = node directory UID
 *   4 network   -> 0x00E4A1D2  status_$naming_invalid_pathname
 *   5 node_data -> 0x00E4A0EE  current = node_data UID, cursor = 11, or 12
 *                              when path[10] is '/' (len > 10)
 * Types 0 and 2 (and >= 6) enter the loop with the current UID left as
 * whatever the frame held.
 */
void name_$resolve_internal(char *path, int16_t path_len, uid_t *dir_uid_ret,
                            uid_t *file_uid_ret, status_$t *status_ret)
{
    uid_t                 scratch_uid;      /* A6-0x38 */
    uint16_t              cursor;           /* A6-0x2E */
    uint16_t              next_pos;         /* A6-0x2C */
    uint16_t              comp_start;       /* A6-0x2A */
    int16_t               comp_len;         /* A6-0x28 */
    start_path_type_t     start_type;       /* A6-0x26 (word) */
    uid_t                 current_uid;      /* A6-0x20 */
    name_$lookup_entry_t  entry;            /* A6-0x18 */
    uint16_t              len_word = (uint16_t)path_len;   /* the (0xc,A6) slot */
    int16_t               etype;            /* D0w at 0x00E4A1AC */

    /* 0x00E4A078-0x00E4A092: both outputs start as UID_$NIL */
    dir_uid_ret->high  = UID_$NIL.high;
    dir_uid_ret->low   = UID_$NIL.low;
    file_uid_ret->high = UID_$NIL.high;
    file_uid_ret->low  = UID_$NIL.low;

    /* 0x00E4A096-0x00E4A0A8: NAME_$VALIDATE(path, &path_len (the argument
     * slot itself, `pea (0xc,A6)`), &cursor, &start_type); the boolean
     * result is ignored. */
    NAME_$VALIDATE(path, &len_word, (int16_t *)&cursor, &start_type);

    /* 0x00E4A0AC-0x00E4A0C0: dispatch on type - 1 */
    switch ((int16_t)start_type) {
    case start_path_$relative:                              /* 1 */
        goto invalid_pathname;                              /* 0x00E4A1D2 */

    case 2:                                                 /* 0x00E4A120 */
        break;

    case start_path_$absolute:                              /* 3: 0x00E4A0CE */
        cursor = 2;
        NAME_$GET_NODE_UID(&scratch_uid);                   /* 0x00E4A0D8 */
        current_uid.high = scratch_uid.high;                /* 0x00E4A0E4 */
        current_uid.low  = scratch_uid.low;
        break;

    case start_path_$network:                               /* 4 */
        goto invalid_pathname;                              /* 0x00E4A1D2 */

    case start_path_$node_data:                             /* 5: 0x00E4A0EE */
        NAME_$GET_NODE_DATA_UID(&scratch_uid);              /* 0x00E4A0F2 */
        current_uid.high = scratch_uid.high;                /* 0x00E4A0FE */
        current_uid.low  = scratch_uid.low;
        cursor = 11;                                        /* 0x00E4A106 */
        /* 0x00E4A10C `cmpi.w #0xa,D2w / bls`: unsigned len <= 10 keeps 11 */
        if ((uint16_t)path_len > 10 && (uint8_t)path[10] == 0x2F) {
            cursor = 12;                                    /* 0x00E4A11A */
        }
        break;

    default:                                                /* 0 or >= 6 */
        break;
    }

    /* 0x00E4A120: the component loop */
    for (;;) {
        /* 0x00E4A120-0x00E4A148 */
        name_$parse_component(path, (uint16_t)path_len, cursor,
                              &next_pos, &comp_start, &comp_len);
        cursor = next_pos;

        /* 0x00E4A14C-0x00E4A15E: no component left -> the current UID is
         * the answer. */
        if (comp_len == 0) {
            file_uid_ret->high = current_uid.high;
            file_uid_ret->low  = current_uid.low;
            *status_ret = status_$ok;
            return;
        }

        /* 0x00E4A160-0x00E4A16C: "." is skipped */
        if (comp_len == 1 && (uint8_t)path[comp_start - 1] == 0x2E) {
            continue;
        }

        /* 0x00E4A16E-0x00E4A174: the directory being searched */
        dir_uid_ret->high = current_uid.high;
        dir_uid_ret->low  = current_uid.low;

        /* 0x00E4A178-0x00E4A18C: ".." is rejected (second byte tested first) */
        if ((uint8_t)path[comp_start] == 0x2E && comp_len == 2 &&
            (uint8_t)path[comp_start - 1] == 0x2E) {
            goto invalid_pathname;
        }

        /* 0x00E4A18E-0x00E4A1A4: DIR_$GET_ENTRYU(dir_uid_ret, &path[start-1],
         * &comp_len, &entry, status_ret) */
        DIR_$GET_ENTRYU(dir_uid_ret, path + (comp_start - 1),
                        (uint16_t *)&comp_len, &entry, status_ret);

        if (*status_ret != status_$ok) {                    /* 0x00E4A1A8 */
            return;
        }

        /* 0x00E4A1AC-0x00E4A1BE */
        etype = entry.type;
        if (etype == 0) {
            *status_ret = status_$naming_name_not_found;    /* 0x00E4A1C2 */
            return;
        }
        if (etype == 1) {
            /* 0x00E4A1CA -> 0x00E4A0E4: descend into the entry's UID */
            current_uid.high = entry.uid.high;
            current_uid.low  = entry.uid.low;
            continue;
        }
        if (etype == 3) {
            goto invalid_pathname;                          /* 0x00E4A1D2 */
        }
        /* any other type: keep the current UID and carry on (0x00E4A1BE) */
    }

invalid_pathname:
    *status_ret = status_$naming_invalid_pathname;          /* 0x00E4A1D2 */
}

/*
 * NAME_$RESOLVE (0x00E4A258, 96 bytes)
 *
 * Frame (A6+): 0x08 path, 0x0C path_len (pointer), 0x10 resolved_uid -> A3,
 * 0x14 status_ret -> A2.  Locals: -0x8 dir UID, -0x10 file UID.
 */
void NAME_$RESOLVE(char *path, int16_t *path_len, uid_t *resolved_uid, status_$t *status_ret)
{
    uid_t dir_uid;                                          /* A6-0x8 */
    uid_t file_uid;                                         /* A6-0x10 */

    /* 0x00E4A264-0x00E4A270: output starts as UID_$NIL */
    resolved_uid->high = UID_$NIL.high;
    resolved_uid->low  = UID_$NIL.low;

    /* 0x00E4A274-0x00E4A28E: name_$resolve_internal(path, *path_len,
     * &dir_uid, &file_uid, status_ret) */
    name_$resolve_internal(path, *path_len, &dir_uid, &file_uid, status_ret);

    /* 0x00E4A292-0x00E4A29A */
    if (*status_ret == status_$naming_directory_not_found_in_pathname) {
        *status_ret = status_$naming_name_not_found;
    }

    /* 0x00E4A2A0-0x00E4A2AA */
    if (*status_ret == status_$ok) {
        resolved_uid->high = file_uid.high;
        resolved_uid->low  = file_uid.low;
    }
}
