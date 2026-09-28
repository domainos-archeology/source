/*
 * MST_$UNMAP - Unmap a memory region from the current address space
 *
 * Original address: 0x00E4472E (SAU2 map: MST_UNWIRED, MST_$UNMAP at E4472E)
 * Size: 54 bytes (0x00E4472E .. 0x00E44763)
 *
 * A forwarding wrapper around MST_$UNMAP_PRIVI (0x00E448B0) with mode 2 and
 * the current ASID.  A5 is saved and set to 0xE7CF0C (the module data base)
 * but never dereferenced.
 *
 * Frame (link.w A6,0x0):
 *   (0x8,A6)   uid           pointer, pushed as-is        (0x00E44750)
 *   (0xc,A6)   start_va_ptr  pointer, dereferenced `move.l (A1),-(SP)` (0x00E4474E)
 *   (0x10,A6)  length_ptr    pointer, dereferenced `move.l (A0),-(SP)` (0x00E44748)
 *   (0x14,A6)  status_ret    pointer, pushed as-is        (0x00E4473A)
 *
 * The ASID pushed is the word at 0xE2060A, PROC1_$AS_ID (0x00E4473E); the
 * mode is `move.w #0x2,-(SP)` (0x00E44754).  No addq after the bsr: unlk
 * discards the 20 bytes of arguments.
 *
 * Verified against the disassembly 2026-09-19; the body was already faithful.
 */

#include "mst/mst_internal.h"

/*
 * @param uid           Object UID to match
 * @param start_va_ptr  Pointer to the starting virtual address
 * @param length_ptr    Pointer to the length to unmap
 * @param status_ret    Output: status code
 */
void MST_$UNMAP(uid_t *uid,
                uint32_t *start_va_ptr,
                uint32_t *length_ptr,
                status_$t *status_ret)
{
    /* 0x00E4473A .. 0x00E44758 */
    MST_$UNMAP_PRIVI(2,
                     uid,
                     *start_va_ptr,
                     *length_ptr,
                     PROC1_$AS_ID,
                     status_ret);
}
