/*
 * AST_$ACTIVATE_AOTE_CANNED - Activate an AOTE from a pre-read VTOCE
 *
 * Boot-time helper: allocates an AOTE, fills its attribute block (aote
 * 0x0C..0x9B) verbatim from a VTOCE already in memory and its embedded
 * object-location record (aote 0x9C..0xBB) from the caller's
 * file_$obj_loc_t, then threads it onto the UID hash chain.  A duplicate
 * UID already on the chain is a fatal error.
 *
 * Parameters (frame at 0x00E2F0C2, `link.w A6,-0xc`):
 *   attrs    (0x8,A6): 36 longwords (0x90 bytes) copied to aote+0x0C
 *   obj_info (0xC,A6): a 0x20-byte file_$obj_loc_t copied to aote+0x9C;
 *            its +0x1D flags byte, +0x04 block hint and +0x14 node word are
 *            also read to build aote->location.  The public prototype keeps
 *            the `uint32_t *` shape os/init.c already calls with.
 *
 * Original address: 0x00E2F0C2 (268 bytes).  A5 = 0xE35004 (the init
 * segment's own base) is loaded but never used; the AOTH is addressed
 * absolutely (`movea.l #0xe1dc80,A0`).
 */

#include "ast/ast_internal.h"
#include "misc/misc.h"
#include "uid/uid.h"

/*
 * The AOTE hash table (`AOTH`, 0xE1DC80 in the SAU2 map): an array of
 * chain heads indexed by UID_$HASH's result.
 */
/* TODO(source-gmxj): the AST_ segment and the AST/AOT tables are still absolute on the target (tools/check_guards.py exemption). */
#if defined(ARCH_M68K)
#define AST_AOTH_BASE ((aote_t **)0xE1DC80)
#else
#define AST_AOTH_BASE ast_aoth_base
#endif

/*
 * The hash-table size word UID_$HASH is given, `pea (0x6e,PC)` at
 * 0x00E2F15E -> 0x00E2F1CE.  Image bytes: 00 FB (251 buckets).  This is
 * the init segment's own copy of the constant; the resident AST code has
 * its own at 0x00E01BEC.
 */
static const uint16_t ast_$canned_hash_size_00e2f1ce = 0x00FB;

void AST_$ACTIVATE_AOTE_CANNED(uint32_t *attrs, uint32_t *obj_info)
{
    file_$obj_loc_t *obj_loc = (file_$obj_loc_t *)obj_info;   /* A2 */
    aote_t *aote;                                              /* A3 */
    aote_t *chain;                                             /* A2 */
    uint32_t *src;
    uint32_t *dst;
    int16_t hash;                                              /* D3 */
    int16_t i;

    /* 0x00E2F0D0..0x00E2F0E0: ML_$LOCK(0x12) */
    ML_$LOCK(AST_LOCK_ID);

    /* 0x00E2F0E2..0x00E2F0E8: a fresh AOTE */
    aote = ast_$allocate_aote();

    /* 0x00E2F0EA..0x00E2F0FC: four bclr.b on (0xbf,A3): bits 7, 6, 5, 4 */
    aote->flags &= (uint8_t)~AOTE_FLAG_IN_TRANS;
    aote->flags &= (uint8_t)~AOTE_FLAG_BUSY;
    aote->flags &= (uint8_t)~AOTE_FLAG_DIRTY;
    aote->flags &= (uint8_t)~AOTE_FLAG_TOUCHED;

    /* 0x00E2F102..0x00E2F10E */
    aote->ref_count = 1;            /* move.b #1,(0xbe,A3) */
    aote->status_flags = 0;         /* clr.w (0xbc,A3) */
    aote->hash_next = NULL;         /* clr.l (A3) */
    aote->aste_list = NULL;         /* clr.l (0x4,A3) */

    /* 0x00E2F112..0x00E2F13A: build the location word */
    if (obj_loc->flags < 0) {
        /*
         * Remote object.  bset.b #7,(0x8,A3) sets bit 31; andi.w #0xFC0F
         * on the high word clears bits 25..20; andi.l #0xFFF00000 clears
         * bits 19..0; then the node id is OR'ed in.
         */
        aote->location |= 0x80000000;
        aote->location &= 0xFC0FFFFF;
        aote->location &= 0xFFF00000;
        aote->location |= obj_loc->node;
    } else {
        /* Local: the block hint IS the location (move.l (0x4,A2),(0x8,A3)) */
        aote->location = obj_loc->block_hint;
    }

    /* 0x00E2F13C..0x00E2F14A: moveq #0x23 / dbf = 36 longwords to aote+0x0C */
    src = attrs;
    dst = (uint32_t *)&aote->obj_type;
    for (i = 0x23; i >= 0; i--) {
        *dst++ = *src++;
    }

    /* 0x00E2F14E..0x00E2F15A: moveq #0x7 / dbf = 8 longwords to aote+0x9C */
    src = obj_info;
    dst = (uint32_t *)&aote->obj_uid;
    for (i = 0x7; i >= 0; i--) {
        *dst++ = *src++;
    }

    /* 0x00E2F15E..0x00E2F17A: hash aote->uid (aote+0x10); D2 = hash*4 */
    hash = (int16_t)UID_$HASH(&aote->uid,
                              (uint16_t *)&ast_$canned_hash_size_00e2f1ce);
    chain = AST_AOTH_BASE[hash];

    /*
     * 0x00E2F17E..0x00E2F1A8: walk the chain; a matching UID (two cmpm.l
     * over aote+0x10..0x17) is fatal.  CRASH_SYSTEM does not return; the
     * image would otherwise re-test the same entry forever (bra 0x00E2F1A4
     * without advancing A2), which the loop below reproduces.
     */
    while (chain != NULL) {
        if (chain->uid.high == aote->uid.high &&
            chain->uid.low == aote->uid.low) {
            CRASH_SYSTEM(&status_$t_00e2f1d0);
        } else {
            chain = chain->hash_next;
        }
    }

    /* 0x00E2F1AA..0x00E2F1B4: push onto the head of the bucket */
    aote->hash_next = AST_AOTH_BASE[hash];
    AST_AOTH_BASE[hash] = aote;

    /* 0x00E2F1B8..0x00E2F1BE: ML_$UNLOCK(0x12) */
    ML_$UNLOCK(AST_LOCK_ID);
}
