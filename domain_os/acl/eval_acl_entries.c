/*
 * acl_$eval_acl_entries - evaluate one cached ACL image against a SID triple
 *
 * Original address: 0x00E46172 (was FUN_00e46172), 626 bytes.
 * Sole caller: acl_$eval_rights (0x00E46878).
 *
 * Frame (A6+), from the caller's pushes at 0x00E4685A-0x00E46874:
 *   0x08 slot        (long) the cached ACL image
 *   0x0C sids        (long) three consecutive UIDs: person, group, org
 *   0x10 proj_uids   (long) the caller's eight-entry project-UID row
 *   0x14 prot        (long) the object's 44-byte protection record
 *
 * Locals (A6-):
 *   -0x3C matched      byte    a project/group pass produced rights
 *   -0x30 start_index  word    where the person scan stopped
 *   -0x2E max_index    word    the furthest a project scan reached
 *   -0x28 local_sids   0x24    a 36-byte copy of *sids whose group slot is
 *                              replaced by each project UID in turn
 *
 * The routine has three phases:
 *   1. 0x00E461A2  walk entries 1..entry_count while entry->person is not NIL,
 *                  looking for an exact person match (group and org may be NIL
 *                  wildcards).
 *   2. 0x00E46250  nine passes - the caller's own SIDs, then each of the eight
 *                  project UIDs substituted for the group SID - accumulating
 *                  rights from every entry whose group matches.
 *   3. 0x00E46364  only when phase 2 matched nothing: the org record, then an
 *                  org-only entry scan, then the world rights.
 *
 * Result: a 16-bit rights word in D0w.  acl_$eval_rights zero-extends it.
 */

#include "acl/acl_internal.h"

uint16_t acl_$eval_acl_entries(acl_$cache_slot_t *slot, uid_t *sids,
                               uid_t *proj_uids, acl_$prot_data_t *prot)
{
    acl_$acl_entry_t *entry;        /* A0 */
    int16_t   index;                /* D0w */
    int16_t   count;                /* D2w */
    uint16_t  accum;                /* D1w */
    boolean   matched;              /* A6-0x3C */
    int16_t   start_index;          /* A6-0x30 */
    int16_t   max_index;            /* A6-0x2E */
    acl_sid_block_t local_sids;     /* A6-0x28 */
    uid_t    *proj;                 /* A2 */
    int16_t   which;                /* D3w, 0 = the caller's own SIDs */
    int16_t   pass;                 /* D4w, the dbf counter */

    /*
     * 0x00E4617A-0x00E4619E: the protection record's own owner slot wins
     * outright, unless it is marked "ignore".
     */
    if ((prot->owner_rights & ACL_RIGHT_IGNORE) == 0 &&
        acl_$uid_eq(&prot->owner, &sids[0]) < 0) {
        return (uint16_t)prot->owner_rights;
    }

    /* ------------------------- phase 1: person ------------------------- */

    index = 1;                                          /* 0x00E461A2 */
    /* `lea (0x34,A0),A0` - entry 1 is at slot+0x34. */
    entry = ACL_$CACHE_ENTRY(slot, 1);
    count = (int16_t)slot->entry_count;                 /* 0x00E461B6 */
    goto p1_test;                                       /* 0x00E461BC */

p1_body:                                                /* 0x00E461BE */
    if (acl_$uid_eq(&entry->person, &sids[0]) >= 0) {
        goto p1_next;
    }
    /* 0x00E461CE-0x00E461F6: the group must match, or be the NIL wildcard. */
    if (acl_$uid_eq(&entry->group, &sids[1]) >= 0 &&
        acl_$uid_eq(&entry->group, &UID_$NIL) >= 0) {
        goto p1_next;
    }
    /* 0x00E461F8-0x00E4621C: likewise the organisation. */
    if (acl_$uid_eq(&entry->org, &sids[2]) >= 0 &&
        acl_$uid_eq(&entry->org, &UID_$NIL) >= 0) {
        goto p1_next;
    }

entry_match:                                            /* 0x00E4621E */
    /* `clr.w D0w / move.b (0x1c,A2),D0b / and.w (0x1a,A0),D0w` */
    return (uint16_t)((uint16_t)prot->subsys_rights & entry->rights);

p1_next:                                                /* 0x00E46230 */
    index++;
    entry++;
p1_test:                                                /* 0x00E46236 */
    if (index > count) {
        goto phase2;
    }
    /* 0x00E4623A: the person section ends at the first NIL person UID. */
    if (acl_$uid_eq(&entry->person, &UID_$NIL) < 0) {
        goto phase2;
    }
    goto p1_body;

    /* ------------------- phase 2: group and projects ------------------- */

phase2:                                                 /* 0x00E46250 */
    accum       = 0;                                    /* D1w */
    matched     = false;                                /* 0x00E46252 clr.b */
    start_index = index;                                /* 0x00E46256 */
    max_index   = index;                                /* 0x00E4625A */
    proj        = proj_uids;                            /* 0x00E46268 A2 */

    /*
     * `moveq #0x8,D4` + `dbf` = nine passes.  D3w counts them; the Z flag
     * `move.w D1w,D3w` leaves at 0x00E46260 is what makes pass 0 take the
     * "use the caller's SIDs" arm (0x00E4626C `bne`), and D3w is never zero
     * again afterwards.
     */
    which = 0;
    for (pass = 8; pass >= 0; pass--) {
        if (which == 0) {
            /*
             * 0x00E4626E-0x00E4627C: nine longwords - the whole 36-byte SID
             * block, not just the three UIDs the rest of the routine reads.
             */
            const uint32_t *src = (const uint32_t *)(const void *)sids;
            uint32_t       *dst = (uint32_t *)(void *)&local_sids;
            int16_t         n;

            for (n = 8; n >= 0; n--) {
                *dst++ = *src++;
            }
        } else {
            /*
             * 0x00E46280-0x00E4629A: A2 has already been advanced once per
             * completed pass, so `lea (-0x8,A2),A3` is project UID which-1.
             * A NIL project ends the whole loop.
             */
            if (acl_$uid_eq(&proj[-1], &UID_$NIL) < 0) {
                goto done_phase2;                       /* 0x00E4628E */
            }
            local_sids.group_sid = proj[-1];            /* 0x00E46296 */
        }

        /* 0x00E4629E-0x00E462CC: the protection record's group slot. */
        if ((prot->group_rights & ACL_RIGHT_IGNORE) == 0 &&
            acl_$uid_eq(&prot->group, &local_sids.group_sid) < 0) {
            accum  |= (uint16_t)prot->group_rights;
            matched = true;                             /* 0x00E462C8 st */
            goto next_proj;
        }

        /* 0x00E462D0: rescan the entries from where phase 1 stopped. */
        index = start_index;
        entry = ACL_$CACHE_ENTRY(slot, index);
        goto p2_test;                                   /* 0x00E462E2 */

    p2_body:                                            /* 0x00E462E4 */
        if (acl_$uid_eq(&entry->group, &local_sids.group_sid) >= 0) {
            goto p2_next;
        }
        if (acl_$uid_eq(&entry->org, &local_sids.org_sid) >= 0 &&
            acl_$uid_eq(&entry->org, &UID_$NIL) >= 0) {
            goto p2_next;
        }
        /* 0x00E46318-0x00E4632C */
        accum  |= (uint16_t)((uint16_t)prot->subsys_rights & entry->rights);
        matched = true;
        goto p2_after;

    p2_next:                                            /* 0x00E4632E */
        index++;
        entry++;
    p2_test:                                            /* 0x00E46334 */
        if (index > count) {
            goto p2_after;
        }
        /* The group section ends at the first NIL group UID. */
        if (acl_$uid_eq(&entry->group, &UID_$NIL) < 0) {
            goto p2_after;
        }
        goto p2_body;

    p2_after:                                           /* 0x00E46348 */
        if (index > max_index) {
            max_index = index;
        }

    next_proj:                                          /* 0x00E46352 */
        which++;
        proj++;                                         /* 0x00E46354 */
    }

done_phase2:                                            /* 0x00E4635A */

    /* --------------------- phase 3: org and world --------------------- */

    if (matched < 0) {
        return accum;                                   /* 0x00E46360 */
    }

    /*
     * 0x00E46364.  Only `index` is reloaded here - `entry` (A0) keeps the
     * value the last phase-2 scan left it at.  The two agree because a phase 3
     * that runs at all means no pass matched, every pass therefore walked to
     * the same terminal index, and max_index is that index.
     */
    index = max_index;

    /* 0x00E46368-0x00E46390: the protection record's organisation slot. */
    if ((prot->org_rights & ACL_RIGHT_IGNORE) == 0 &&
        acl_$uid_eq(&prot->org, &sids[2]) < 0) {
        return (uint16_t)prot->org_rights;
    }

    goto p3_test;                                       /* 0x00E46398 */

p3_body:                                                /* 0x00E4639A */
    if (acl_$uid_eq(&entry->org, &sids[2]) < 0) {
        goto entry_match;                               /* 0x00E463AE */
    }
    index++;
    entry++;
p3_test:                                                /* 0x00E463B8 */
    if (index > count) {
        goto world;
    }
    /* The organisation section ends at the first NIL org UID. */
    if (acl_$uid_eq(&entry->org, &UID_$NIL) < 0) {
        goto world;
    }
    goto p3_body;

world:                                                  /* 0x00E463D0 */
    return (uint16_t)prot->world_rights;
}
