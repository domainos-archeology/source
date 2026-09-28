/*
 * MST_$GET_UID, MST_$GET_UID_ASID, MST_$GET_VA_INFO - Query segment information
 *
 * Three entry points of the MST_UNWIRED module (SAU2 map):
 *   MST_$GET_UID       0x00E43FD8   56 bytes   (0x00E43FD8 .. 0x00E4400F)
 *   MST_$GET_UID_ASID  0x00E44010   62 bytes   (0x00E44010 .. 0x00E4404D)
 *   MST_$GET_VA_INFO   0x00E4404E  206 bytes   (0x00E4404E .. 0x00E4411B)
 *
 * All three save A5 and set it to 0xE7CF0C (the module data base) without
 * dereferencing it.  The two MST_$GET_UID* routines are wrappers that call
 * MST_$GET_VA_INFO with three frame-local cells for the outputs they do not
 * return.
 *
 * Re-emitted from the disassembly 2026-09-19: the previous C read the
 * 16-byte entry copy as four longwords and pulled area_id / flags out of
 * `entry_copy[2]` with `>> 16` and a truncating cast, which is only right
 * on a big-endian host.  The copy is now an mst_entry_t (`move.w (-0x8,A6)`
 * is area_id, `move.w (-0x6,A6)` is flags), and the fifth parameter is
 * typed as what it is: the prot_out word mst_$va_to_pte fills in.
 */

#include "mst/mst_internal.h"

/*
 * MST_$GET_VA_INFO - Get full information about a virtual address
 *
 * Frame (link.w A6,-0x20; D2/D3/A2-A5 saved):
 *   (0x8,A6)   asid_p         pointer to a word          -> D3w
 *   (0xc,A6)   va_ptr         pointer to a longword      -> D2
 *   (0x10,A6)  uid_out        pointer to a uid_t
 *   (0x14,A6)  adjusted_va    pointer to a longword
 *   (0x18,A6)  prot_out       pointer to a word, forwarded to mst_$va_to_pte
 *                             (declared `void *` in mst.h for the callers)
 *   (0x1c,A6)  active_flag    pointer to a boolean
 *   (0x20,A6)  modified_flag  pointer to a boolean
 *   (0x24,A6)  status_ret     pointer                    -> A2
 *   (-0x10,A6) entry_copy     16 bytes, the MST entry copied under the lock
 *   (-0x14,A6) status         mst_$va_to_pte's status
 *   (-0x18,A6) entry          mst_$va_to_pte's entry pointer
 */
void MST_$GET_VA_INFO(uint16_t *asid_p,
                      uint32_t *va_ptr,
                      uid_t *uid_out,
                      uint32_t *adjusted_va,
                      void *prot_out,
                      boolean *active_flag,
                      boolean *modified_flag,
                      status_$t *status_ret)
{
    uint16_t asid;             /* D3w */
    uint32_t va;               /* D2 */
    void *entry;               /* (-0x18,A6) */
    status_$t status;          /* (-0x14,A6) */
    mst_entry_t entry_copy;    /* (-0x10,A6) */

    /* 0x00E44060 .. 0x00E4406A */
    asid = *asid_p;
    va = *va_ptr;

    /* 0x00E4406C .. 0x00E44078: `cmpi.w #0x39,D3w` / `bls` - ASIDs above
     * 57 are "reference to illegal address" (0x40004) */
    if (asid > 0x39) {
        *status_ret = status_$reference_to_illegal_address;
        return;
    }

    /* 0x00E4407C .. 0x00E44088: ML_$LOCK(0xC) */
    ML_$LOCK(MST_LOCK_ASID);

    /*
     * 0x00E4408A .. 0x00E4409E: mst_$va_to_pte(asid, va, prot_out, &entry,
     * &status), pushed after a `subq.l #0x2,SP` result slot that nothing
     * reads (the cleanup is `lea (0x14,SP),SP` = 20 bytes).
     */
    mst_$va_to_pte(asid, va, (uint16_t *)prot_out, &entry, &status);

    /* 0x00E440A2 .. 0x00E440B8: copy the 16-byte entry while locked */
    if (status == status_$ok) {
        entry_copy = *(mst_entry_t *)entry;
    }

    /* 0x00E440BA .. 0x00E440C6: ML_$UNLOCK(0xC) */
    ML_$UNLOCK(MST_LOCK_ASID);

    /* 0x00E440C8 .. 0x00E440CE */
    *status_ret = status;
    if (status != status_$ok) {
        return;
    }

    /* 0x00E440D0 .. 0x00E440DA: uid_out = entry_copy.uid */
    uid_out->high = entry_copy.uid.high;
    uid_out->low = entry_copy.uid.low;

    /*
     * 0x00E440DE .. 0x00E440F4: `clr.l D1` / `andi.l #0x7fff,D2` /
     * `move.w (-0x8,A6),D1w` (area_id) / `lsl.l #0x8` + `lsl.l #0x7` /
     * `add.l D1,D2` - the segment's base VA plus the offset within it.
     */
    *adjusted_va = ((uint32_t)entry_copy.area_id << 15) + (va & 0x7fff);

    /* 0x00E440F6 .. 0x00E44100: `tst.w (-0x6,A6)` / `smi` - flags bit 15 */
    *active_flag = ((entry_copy.flags & 0x8000) != 0) ? true : false;

    /* 0x00E44102 .. 0x00E44110: `btst.l #0xe,D1` / `sne` - flags bit 14 */
    *modified_flag = ((entry_copy.flags & 0x4000) != 0) ? true : false;
}

/*
 * MST_$GET_UID - Get UID for address in the current address space
 *
 * Frame (link.w A6,-0x8):
 *   (0x8,A6)   va_ptr
 *   (0xc,A6)   uid_out
 *   (0x10,A6)  adjusted_va
 *   (0x14,A6)  status_ret
 *   (-0x2,A6)  prot          word,  passed as prot_out      (0x00E43FF0)
 *   (-0x4,A6)  modified      byte,  passed as modified_flag (0x00E43FE8)
 *   (-0x6,A6)  active        byte,  passed as active_flag   (0x00E43FEC)
 *
 * The ASID pointer is `move.l #0xe2060a` = &PROC1_$AS_ID (0x00E44000).
 */
void MST_$GET_UID(uint32_t *va_ptr,
                  uid_t *uid_out,
                  uint32_t *adjusted_va,
                  status_$t *status_ret)
{
    uint16_t prot;         /* (-0x2,A6) */
    boolean modified;      /* (-0x4,A6) */
    boolean active;        /* (-0x6,A6) */

    /* 0x00E43FE4 .. 0x00E44006 */
    MST_$GET_VA_INFO(&PROC1_$AS_ID,
                     va_ptr,
                     uid_out,
                     adjusted_va,
                     &prot,
                     &active,
                     &modified,
                     status_ret);
}

/*
 * MST_$GET_UID_ASID - Get UID for address in a specified address space
 *
 * Frame (link.w A6,-0x8):
 *   (0x8,A6)   asid_p       pointer to a word, copied to (-0x4,A6)
 *   (0xc,A6)   va_ptr
 *   (0x10,A6)  uid_out
 *   (0x14,A6)  adjusted_va
 *   (0x18,A6)  status_ret
 *   (-0x2,A6)  prot         word,  passed as prot_out      (0x00E44030)
 *   (-0x4,A6)  asid         word,  the copied ASID         (0x00E44040)
 *   (-0x6,A6)  modified     byte,  passed as modified_flag (0x00E44028)
 *   (-0x8,A6)  active       byte,  passed as active_flag   (0x00E4402C)
 */
void MST_$GET_UID_ASID(uint16_t *asid_p,
                       uint32_t *va_ptr,
                       uid_t *uid_out,
                       uint32_t *adjusted_va,
                       status_$t *status_ret)
{
    uint16_t prot;         /* (-0x2,A6) */
    uint16_t asid;         /* (-0x4,A6) */
    boolean modified;      /* (-0x6,A6) */
    boolean active;        /* (-0x8,A6) */

    /* 0x00E4401C .. 0x00E44020: `move.w (A0),(-0x4,A6)` */
    asid = *asid_p;

    /* 0x00E44024 .. 0x00E44044 */
    MST_$GET_VA_INFO(&asid,
                     va_ptr,
                     uid_out,
                     adjusted_va,
                     &prot,
                     &active,
                     &modified,
                     status_ret);
}
