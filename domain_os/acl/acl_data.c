/*
 * ACL Data - Global variables for ACL subsystem
 *
 * Module data blocks ACL_$WIRED_DATA, ACL_$UNWIRED_DATA and ACL_$DATA:
 * Claude Opus 5.5 (source-l2yd).
 *
 * ACL_ has three data segments in the SAU2 map that C addresses, each a
 * MODULE_DATA block linked in the map's order (the address is the ordering
 * key, not the link address); layouts, biases and asserts in acl/acl.h and
 * acl/acl_internal.h:
 *
 *   D    E2C014  ACL_WIRED   size = 14    ACL_$WIRED_DATA
 *   D    E7CF54  ACL_        size = BFC   ACL_$UNWIRED_DATA (A5)
 *   D69  E88834  ACL_$DATA   size = AD98  ACL_$DATA
 *
 * The three 4-byte ACL_ segments (0xE35034, 0xE86054 inside
 * PROC2_CREATE_DAT, 0xE86060 inside PROC2_DELETE_DAT) are A5 anchors of the
 * boot, ALLOC_ASID and FREE_ASID code; nothing addresses their bytes, so
 * they have no C object.
 */

#include "acl/acl_internal.h"

/* ACL_$WIRED_DATA, 0xE2C014..0xE2C027: zero in the image; ACL_$INIT runs
 * ML_$EXCLUSION_INIT on the lock (0x00E3117E). */
MODULE_DATA_DEFINE(acl_$wired_data_t, ACL_$WIRED_DATA, 0x00E2C014);

/*
 * ACL_$UNWIRED_DATA, 0xE7CF54..0xE7DB4F (`gsk read 0xE7CF54 3068'): zero
 * except the empty-cache markers - cache_hash_buckets[0..60] = ff ff
 * (0xE7DA44..0xE7DABD) and cache_lru_head = ff ff (0xE7DACA, which is also
 * super_count[0]).
 */
#define ACL_NO_SLOT_x8 \
    ACL_CACHE_NO_SLOT, ACL_CACHE_NO_SLOT, ACL_CACHE_NO_SLOT, ACL_CACHE_NO_SLOT, \
    ACL_CACHE_NO_SLOT, ACL_CACHE_NO_SLOT, ACL_CACHE_NO_SLOT, ACL_CACHE_NO_SLOT
MODULE_DATA_DEFINE_INIT(acl_$unwired_data_t, ACL_$UNWIRED_DATA, 0x00E7CF54, {
    .cache_hash_buckets = {
        ACL_NO_SLOT_x8, ACL_NO_SLOT_x8, ACL_NO_SLOT_x8, ACL_NO_SLOT_x8,
        ACL_NO_SLOT_x8, ACL_NO_SLOT_x8, ACL_NO_SLOT_x8,
        ACL_CACHE_NO_SLOT, ACL_CACHE_NO_SLOT, ACL_CACHE_NO_SLOT,
        ACL_CACHE_NO_SLOT, ACL_CACHE_NO_SLOT,               /* [0..60] */
    },
    .cache_lru_head = ACL_CACHE_NO_SLOT,
});
#undef ACL_NO_SLOT_x8
_Static_assert(ACL_CACHE_HASH_BUCKETS == 7 * 8 + 5, "the initialiser covers every bucket");

/*
 * ACL_$DATA, 0xE88834..0xE935CB: loaded at file offset 0x196B4E, the end of
 * the SR10.2 SAU2 file, so zero-filled; ACL_$INIT zeroes it again.
 */
MODULE_DATA_DEFINE(acl_$data_t, ACL_$DATA, 0x00E88834);

/*
 * Default ACL UIDs
 */
uid_t ACL_$DNDCAL;  /* 0xE174DC: Default ACL for dirs/links */
uid_t ACL_$FNDWRX;  /* 0xE174C4: Default ACL for files */

/*
 * ACL type UIDs - well-known UIDs used to identify ACL operation types
 */
uid_t ACL_$FILE_ACL;        /* 0xE17444 */
uid_t ACL_$FILEIN_ACL;      /* 0xE17454: {0x00000602, 0x00000000} */
uid_t ACL_$DIRIN_ACL;       /* 0xE1745C: {0x00000603, 0x00000000} */
uid_t ACL_$DIR_MERGE_ACL;   /* 0xE17464: {0x00000604, 0x00000000} */
uid_t ACL_$FILE_MERGE_ACL;  /* 0xE1746C: {0x00000605, 0x00000000} */
uid_t ACL_$FILE_SUBS_ACL;   /* 0xE17474: {0x00000606, 0x00000000} */

/*
 * ACL_$NIL - the "no ACL" UID (0x00E17384).
 *
 * Raw bytes in the image: 00 00 01 00 00 00 00 00, i.e. high = 0x00000100,
 * low = 0.  Passed by address by dir/ when creating or dropping an object
 * that carries no ACL of its own.
 */
uid_t ACL_$NIL = UID_CONST(0x00000100, 0);

/*
 * Well-known ACL UID for directories.  Ghidra labels 0xE1744C ACL_$DIR_ACL and
 * 0xE17444 ACL_$FILE_ACL; ACL_$SET_ACL_CHECK reads both (0xE1744C at
 * 0x00E4762E, 0xE17444 at 0x00E47660).  This resolves the "find actual
 * address" half of TODO(source-yii).
 */
uid_t ACL_$DIR_ACL;         /* 0xE1744C */
