/*
 * misc/set_lites_loc.c - SET_LITES_LOC implementation
 *
 * Sets the location for memory-mapped status lights. These lights
 * are 16 indicator blocks displayed on screen to show system activity.
 *
 * Original address: 0x00e0c4dc
 *
 * Assembly:
 *   00e0c4dc    link.w A6,-0x4
 *   00e0c4e0    pea (A5)
 *   00e0c4e2    lea (0xe2327c).l,A5      ; LITES_LOC global base
 *   00e0c4e8    movea.l (0x8,A6),A0      ; Load loc_p parameter
 *   00e0c4ec    move.l (A0),(-0x4,A6)    ; Copy *loc_p to local
 *   00e0c4f0    tst.l (A5)               ; Test if LITES_LOC is 0
 *   00e0c4f2    bne.b 0x00e0c506         ; If not zero, skip init
 *   00e0c4f4    movea.l (-0x4,A6),A1     ; Load new value
 *   00e0c4f8    cmpa.w #0x0,A1           ; Compare to 0
 *   00e0c4fc    beq.b 0x00e0c506         ; If zero, skip init
 *   00e0c4fe    move.l A1,(A5)           ; Store new value
 *   00e0c500    bsr.w 0x00e0c43c         ; Call START_MEM_LITES
 *   00e0c504    bra.b 0x00e0c50a
 *   00e0c506    move.l (-0x4,A6),(A5)    ; Store new value (else case)
 *   00e0c50a    movea.l (-0x8,A6),A5
 *   00e0c50e    unlk A6
 *   00e0c510    rts
 */

#include "misc/misc.h"

/*
 * Global variable for the lights display location.
 * Address 0xe2327c in the original binary.
 */
extern int32_t LITES_LOC;

/*
 * START_MEM_LITES - Start the memory lights update process
 *
 * Creates a process that periodically updates the status lights
 * to reflect memory and system activity.
 *
 * Original address: 0x00e0c43c
 */
extern void START_MEM_LITES(void);

/*
 * SET_LITES_LOC - Set the status lights location
 *
 * This function sets the display memory address where status lights
 * should be drawn. If the lights were previously disabled (LITES_LOC
 * was 0) and a valid new location is provided, it also starts the
 * MEM_LITES process to begin updating the lights.
 *
 * The status lights are 16 small indicator blocks that show various
 * system activities like disk I/O, network activity, and CPU usage.
 *
 * Parameters:
 *   loc_p - Pointer to the new lights location value
 */
void SET_LITES_LOC(int32_t *loc_p)
{
    int32_t new_loc;
    int was_disabled;

    /* Get the new location value */
    new_loc = *loc_p;

    /* Check if lights were previously disabled */
    was_disabled = (LITES_LOC == 0);

    /* Store the new location */
    LITES_LOC = new_loc;

    /*
     * If lights were disabled and new location is valid,
     * start the memory lights update process.
     */
    if (was_disabled && new_loc != 0) {
        START_MEM_LITES();
    }
}
