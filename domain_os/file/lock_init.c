/*
 * FILE_$LOCK_INIT - Initialise the file-locking subsystem
 *
 * Original address: 0x00E32744 (240 bytes)
 *
 * Re-derived instruction by instruction for bead source-0sgi.  The previous
 * version of this file was written against the stale file_lock_entry_t model
 * and got three things wrong: it cleared the per-process lock rows starting
 * two bytes in, it cleared the entry byte at +0x18 through a field called
 * `flags` (the machine clears the REFCOUNT there), and it dropped both the
 * clear of the word at 0xE9F9C4 and the first of the two writes of
 * FILE_$LOT_FREE.
 *
 *   00e32744  link.w A6,-0x10
 *   00e32748  movem.l { A5 A3 A2 D2},-(SP)
 *   00e3274c  lea (0xe35154).l,A5          ; module base (unused below)
 *
 * Step 1 - per-process lock table, 58 rows of 300 bytes (0x00E32752-0x00E32780)
 *   00e32752  movea.l #0xea202c,A0         ; biased row cursor
 *   00e32758  moveq #0x39,D0               ; 58 rows (dbf 0x39)
 *   00e32760  move.w #0x95,D1w             ; 150 words (dbf 0x95)
 *   00e32764  moveq #0x2,D2                ; word displacement starts at 2
 *   00e32768  lea (0x0,A2,D2*0x1),A0
 *   00e3276c  clr.w (-0x2662,A0)           ; 0xEA202C-0x2662 = 0xE9F9CA
 *   00e32770  addq.l #0x2,D2
 *   00e32772  dbf D1w,0x00e32768
 *   00e32776  clr.w (0x1d98,A1)            ; 0xEA202C+0x1D98 = 0xEA3DC4
 *   00e3277a  lea (0x12c,A2),A2
 *   00e3277e  addq.l #0x2,A1
 *   00e32780  dbf D0w,0x00e32760
 * The word displacement runs 2,4,...,300 off the biased base 0xE9F9CA, i.e.
 * 0xE9F9CC..0xE9FAF7 for row 0 - the WHOLE 300-byte row, not "all but the
 * first two bytes".  0xE9F9CC is confirmed as the first word cleared by the
 * xref 0x00E3276C -> 0xE9F9CC.  In the 1-based slot numbering the rest of the
 * subsystem uses, that is slots 1..150 of each row.
 *
 * Step 2 - lock object table free list (0x00E32784-0x00E327A8); see
 * file/file_internal.h for the addressing proof.
 *
 * Step 3 - the word at 0xE9F9C4 (0x00E327AC).
 *
 * Step 4 - lock control block at 0xE82128 (0x00E327B2-0x00E32820).
 *
 * Step 5 - EC_$INIT on the UID-lock eventcount at 0xE2C028 (0x00E327CE).
 *
 * Finally REM_FILE_$UNLOCK_ALL (0x00E32824, jsr 0x00E61C72).
 */

#include "file/file_internal.h"
#include "ec/ec.h"
#include "rem_file/rem_file.h"

void FILE_$LOCK_INIT(void)
{
    int32_t asid;
    int32_t slot;
    int32_t index;
    int32_t i;
    file_lock_entry_detail_t *entry;

    /*
     * Step 1: clear all 58 per-process lock rows and their slot counts.
     * 0x00E32760-0x00E32780.
     */
    for (asid = 0; asid < FILE_LOCK_TABLE_ENTRIES; asid++) {
        /* 150 words per row - the full 300-byte row (0x00E3276C). */
        for (slot = 1; slot <= 150; slot++) {
            FILE_$PROC_LOT_SLOT(asid, slot) = 0;
        }
        /* Per-row slot high-water mark at 0xEA3DC4 + asid*2 (0x00E32776). */
        FILE_$PROC_LOT_COUNT(asid) = 0;
    }

    /*
     * Step 2: build the lock-entry free list.  0x00E32784-0x00E327A8:
     *   move.w #0x6ff,D0w      ; 1792 iterations
     *   movea.l #0xe935cc,A0
     *   moveq #0x1,D1          ; index counter starts at 1
     *   lea (0x1c,A0),A0       ; A0 = END of entry 1
     * loop:
     *   movea.l A0,A1
     *   clr.b (-0x4,A1)        ; entry.refcount (+0x18) = 0
     *   move.w D1w,D2w / addq.w #0x1,D2w
     *   move.w D2w,(-0x8,A1)   ; entry.next (+0x14) = index + 1
     *   move.w D2w,D1w
     *   lea (0x1c,A0),A0
     *   dbf D0w
     * Only refcount and next are touched; every other field keeps whatever it
     * held.  The last iteration stores 1793 into entry 1792's `next`, which is
     * why FILE_$LOCK_ENTRIES carries a 1793rd, never-written slot whose zero
     * `next` terminates the chain.
     */
    for (index = 1; index <= FILE_LOCK_ENTRY_COUNT; index++) {
        entry = FILE_$LOT_ENTRY(index);
        entry->refcount = 0;
        entry->next = (uint16_t)(index + 1);
    }

    /*
     * Step 3: 0x00E327AC `clr.w (0x00e9f9c4).l`.  This word sits 8 bytes below
     * the per-process lock table base and is the only reference to 0xE9F9C4 in
     * the whole image - nothing ever reads it.
     *
     * The SR10.2 SAU2 link map settles the question as far as it can be settled:
     * sau2.10.2.tar's sau2/domain_os.map is the map for THIS image (it places
     * FILE_$LOCK_INIT at E32744, our address), and it puts 0xE9F9C4 inside the
     * segment `D71  E935CC  FILE_$LOT_DATA  size = 1086C` - E935CC..EA3E38, which
     * ends exactly where the per-ASID count array does.  That segment exports NO
     * symbols at all, so the map cannot name the cell.  Nor can the code: the only
     * table bases the image ever loads in 16-bit displacement range of 0xE9F9C4
     * are #0xE935CC, #0xE97294 and #0xEA202C, and every displacement taken off
     * them in the lock routines is -0x2662 (= 0xE9F9CC).  The word is
     * write-only.  (source-9r49, closed as not-nameable.)
     */
    FILE_$LOT_E9F9C4 = 0;

    /*
     * Step 4a: 0x00E327B2-0x00E327B8, `movea.l #0xe82128,A3` then
     * `move.w #0x1,(0x2ce,A3)` - the free-list head is set to 1 BEFORE the
     * lock_map clear.  The original sets it again at 0x00E327E2; both writes
     * are reproduced.
     */
    FILE_$LOCK_CONTROL.lot_free = 1;
    FILE_$LOT_FREE = 1;

    /*
     * Step 4b: 0x00E327BE-0x00E327CA clears 0xFB = 251 words from
     * FILE_$LOCK_CONTROL + 0xC8 (0xE821F0).
     */
    for (i = 0; i < 251; i++) {
        FILE_$LOCK_CONTROL.lock_map[i] = 0;
    }

    /*
     * The standalone FILE_$LOT_HASHTAB aliases all 251 words of lock_map at
     * 0xE821F0 in the m68k image (the SAU2 map names +0xC8 FILE_$LOT_HASHTAB);
     * on a host build they are distinct objects, so keep them in step.
     */
    for (i = 0; i < FILE_LOT_HASH_BUCKETS; i++) {
        FILE_$LOT_HASHTAB[i] = 0;
    }

    /* Step 5: 0x00E327CE-0x00E327DA, EC_$INIT(0xE2C028). */
    EC_$INIT(&FILE_$UID_LOCK_EC);

    /* 0x00E327E2: free-list head written a second time. */
    FILE_$LOCK_CONTROL.lot_free = 1;
    FILE_$LOT_FREE = 1;

    /* 0x00E327E8: `move.w #0x1,(0x2cc,A0)` - highest allocated entry index. */
    FILE_$LOCK_CONTROL.flag_2cc = 1;
    FILE_$LOT_HIGH = 1;

    /* 0x00E327EE-0x00E327F8: `pea (0xc0,A0)` / UID_$GEN (jsr 0x00E1A018). */
    UID_$GEN(&FILE_$LOCK_CONTROL.generated_uid);

    /*
     * 0x00E327FA-0x00E3281C: base_uid = UID_$NIL with the low longword's
     * bottom 20 bits replaced by NODE_$ME.
     *   move.l (A0)+,(0xb8,A1) / move.l (A0)+,(0xbc,A1)
     *   andi.l #-0x100000,(0xbc,A1)      ; & 0xFFF00000
     *   move.l (0x00e245a4).l,D2         ; NODE_$ME
     *   or.l D2,(0xbc,A1)
     */
    FILE_$LOCK_CONTROL.base_uid.high = UID_$NIL.high;
    FILE_$LOCK_CONTROL.base_uid.low = (UID_$NIL.low & 0xFFF00000) | NODE_$ME;

    /* 0x00E32820: `clr.b (0x2d0,A1)`. */
    FILE_$LOCK_CONTROL.flag_2d0 = 0;
    FILE_$LOT_FULL = 0;

    /* 0x00E32824: jsr 0x00E61C72. */
    REM_FILE_$UNLOCK_ALL();
}
