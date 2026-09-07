/*
 * FIM_$GET_USER_PC - read the longword sitting at the top of the user stack
 *
 * The name comes from the SR10.4 kernel link maps
 * (sr10.4-install/sau7/domain_os.map), which list the FIM_ module's entries
 * in address order: INIT_FF_POOL, DISPOSE_FF, RESTORE_FF, BUILD_DF,
 * ACKNOWLEDGE, INSTALL, GET_FIM_ADDR, INIT_PID, FREE_PID, GET_USER_PC (the SR10.2 SAU2 map for this
 * image, ~/src/domainos-archeology/sau2-maps/domain_os.10.2.map, spells the
 * two as FIM_$INIT_ASID / FIM_$FREE_ASID, the names used here).  In
 * the SAU2 image the corresponding tail of the module is 0x00E0A96C
 * (ACKNOWLEDGE), 0x00E0A9C2 (INSTALL), 0x00E0AA04 (GET_FIM_ADDR),
 * 0x00E0AA24 (INIT_PID) and 0x00E0AA6C (FREE_PID); this object begins
 * immediately after FREE_PID's rts and is the last one in the module, which
 * pins it as FIM_$GET_USER_PC.  (The SAU7 build of the same routine is
 * 0x22 bytes rather than 0x16; every FIM_ entry differs in size between the
 * two SAUs -- FREE_PID is 0x46 bytes there against 0x3A here -- so only the
 * order carries over.)
 *
 * Original address: 0x00E0AAA6
 * Size: 22 bytes
 *
 * Assembly (0x00E0AAA6):
 *   00e0aaa6  link.w   A6,-0x8          ; 8-byte frame, only (-0x4,A6) used
 *   00e0aaaa  jsr      0x00e20f0c.l     ; PROC1_$GET_USP()
 *   00e0aab0  move.l   D0,(-0x4,A6)     ; local := usp
 *   00e0aab4  movea.l  D0,A0
 *   00e0aab6  move.l   (A0),D0          ; result := *(long *)usp
 *   00e0aab8  unlk     A6
 *   00e0aaba  rts
 *
 * Parameter count: none.  The frame is entered with link.w and nothing is
 * ever read from (0x8,A6) or above, so the routine takes no arguments; the
 * only stack slot it touches is the local at (-0x4,A6), which merely holds
 * the value PROC1_$GET_USP returned in D0 across the two instructions that
 * dereference it.  The store is dead as far as the result is concerned, but
 * it is what the compiler emitted and it is reproduced here as the local
 * variable `usp`.
 *
 * The value returned is whatever longword the user stack pointer currently
 * addresses.  Domain/OS enters the kernel for a fault or a trap with the
 * user's return address on top of the user stack, so for a process stopped
 * in the kernel this is that process's user-mode PC -- hence the map name.
 * No validity check of any kind is performed: if the USP is bad the read
 * faults, which is what FIM_$BUS_ERR is for.
 *
 * There are no callers of this entry in the SAU2 image ("gsk xrefs to
 * 0x00E0AAA6" is empty); it is an exported module entry reached from
 * outside the kernel image.
 */

#include "fim/fim_internal.h"

uint32_t FIM_$GET_USER_PC(void)
{
    uint32_t *usp;

    /* 0x00E0AAAA / 0x00E0AAB0: the USP is fetched and parked in the frame. */
    usp = (uint32_t *)PROC1_$GET_USP();

    /* 0x00E0AAB4 / 0x00E0AAB6 */
    return *usp;
}
