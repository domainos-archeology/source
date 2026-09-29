/*
 * ACL_$INIT - initialise the ACL subsystem
 *
 * Original address: 0x00E3109C, 248 bytes (0x00E3109C-0x00E31193).
 *
 * Blocks:
 *   0x00E3109C-0x00E310A8  prologue (A5 = 0xE35038, never used in the body:
 *                          every global is reached through a literal base)
 *   0x00E310AA-0x00E310C6  OS_$DATA_ZERO over the whole ACL_$DATA segment
 *   0x00E310C8-0x00E31102  ACL_$FREE_ASID for ASIDs 1..64, marking each free
 *   0x00E31106-0x00E3111E  the locksmith UID into two login SID slots
 *   0x00E31122-0x00E3113C  eight UID_$NIL into ACL_$DATA.proj_uids[1][0..7]
 *   0x00E31140-0x00E3117A  the 31-entry circular free-list ring
 *   0x00E3117E-0x00E31192  ML_$EXCLUSION_INIT(&ACL_$WIRED_DATA.exclusion_lock), epilogue
 *
 * Called once during system startup (0x00E33D3A).
 */

#include "acl/acl_internal.h"
#include "os/os.h"

void ACL_$INIT(void)
{
    int16_t asid;
    int16_t i;
    status_$t status;

    /*
     * 0x00E310AA-0x00E310C6: zero 0xE935CC - 0xE88834 = 0xAD98 bytes from
     * 0xE88834, i.e. the entire ACL_$DATA segment (the ACL_$DATA block,
     * whose objects overlap as union arms - see acl/acl_internal.h).
     */
    OS_$DATA_ZERO((void *)&ACL_$DATA, ACL_$DATA_SIZE);

    /*
     * 0x00E310C8-0x00E31102: `moveq #0x3f,D2` + `dbf` is 64 iterations with
     * D3 running 1..64.  Each ASID is reset and then marked free; the bitmap
     * bit is byte (asid-1)>>3, mask 0x80 >> ((asid-1)&7).
     *
     * The call site reserves two extra bytes above the arguments
     * (`subq.l #0x2,SP` at 0x00E310D4) for a Pascal function result that
     * ACL_$FREE_ASID never writes, then drops all eight with `addq.w #0x8,SP`.
     */
    for (asid = 1; asid <= 64; asid++) {
        ACL_$FREE_ASID(asid, &status);
        ACL_$DATA.asid_free_bitmap[(asid - 1) >> 3] |= (uint8_t)(0x80 >> ((asid - 1) & 7));
    }

    /*
     * 0x00E31106-0x00E3111E: two eight-byte copies of RGYC_$G_LOCKSMITH_UID
     * (0xE17434, also spelled ACL_$LOCKSMITH_UID and RGYC_$P_ROOT_UID in the
     * SAU2 map).  The destinations are 0xE97294-0x6E48 = 0xE9044C and
     * 0xE97294-0x6548 = 0xE90D4C, i.e. offset 0x3C = 1*0x24 + 0x18 into each
     * table: the LOGIN SID of process 1, not the user SID and not process 0.
     */
    ACL_$DATA.original_sids[1].login_sid = RGYC_$G_LOCKSMITH_UID;
    ACL_$DATA.current_sids[1].login_sid  = RGYC_$G_LOCKSMITH_UID;

    /*
     * 0x00E31122-0x00E3113C: `moveq #0x7,D0` + `dbf` is eight iterations.  A2
     * starts at 0xE9729C and advances by 8, so the writes land at
     * 0xE9253C + k*8 = &ACL_$DATA.proj_uids[1][k] (the 0xE924FC base, source-4h7g).
     */
    for (i = 0; i <= 7; i++) {
        ACL_$DATA.proj_uids[1][i] = UID_$NIL;
    }

    /*
     * 0x00E31140-0x00E3117A: `moveq #0x1e,D3` + `dbf` is 31 iterations, i
     * running 0..30 in D4.  A1 and A2 both walk A5 = 0xE7CF54 by 4 bytes a
     * turn and store at +0xA70 / +0xA72, so this is
     * ACL_$UNWIRED_DATA.cache_hash_links[i].next / .prev -- the array the free list and the
     * 61 hash chains share.  Both indices come from `divs.w #0x1f`, a SIGNED
     * remainder, on i+1 and on (0x1f + i) - 1.
     */
    for (i = 0; i <= 30; i++) {
        ACL_$UNWIRED_DATA.cache_hash_links[i].next = (int16_t)((i + 1) % 31);
        ACL_$UNWIRED_DATA.cache_hash_links[i].prev = (int16_t)((i + 30) % 31);
    }

    /* 0x00E3117E-0x00E31188: the address is pushed as an immediate. */
    ML_$EXCLUSION_INIT(&ACL_$WIRED_DATA.exclusion_lock);
}
