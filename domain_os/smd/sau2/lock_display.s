/*
 * smd/sau2/lock_display.s - SMD_$LOCK_DISPLAY implementation
 *
 * Low-level display locking with interrupt disable.
 * This function must be in assembly because it manipulates the SR register.
 *
 * Original address: 0x00E15CCE
 *
 * Lock state machine:
 *   0 -> 5 (unlocked -> initial lock)
 *   3 -> 4 if param2[0] == 1 (scroll -> post-scroll)
 *   other -> return false (0x00)
 *
 * Parameters:
 *   (4,SP) = lock_data - pointer to display lock structure
 *   (8,SP) = param2 - pointer to additional params
 *
 * Returns:
 *   D0.b = 0xFF if locked successfully, 0x00 if not
 */

    .text
    .globl SMD_$LOCK_DISPLAY
    .type  SMD_$LOCK_DISPLAY, @function

SMD_$LOCK_DISPLAY:
    movea.l (4,%sp),%a0          /* A0 = lock_data */

    /* Disable interrupts (ori #0x700,SR) */
    ori     #0x700,%sr

    /* Get current lock state */
    move.w  (2,%a0),%d0          /* D0 = lock_data->lock_state */

    /* Check for unlocked state (0) */
    cmp.w   #0,%d0
    bne.b   check_scroll_state

    /* State 0 -> 5: Acquire lock */
    move.w  #5,(2,%a0)           /* lock_data->lock_state = 5 */
    bra.b   lock_success

check_scroll_state:
    /* Check for scroll state (3) */
    cmp.w   #3,%d0
    bne.b   lock_fail

    /* Verify param2[0] == 1 */
    movea.l (8,%sp),%a1          /* A1 = param2 */
    cmpi.w  #1,(%a1)
    bne.b   lock_fail

    /* State 3 -> 4: Scroll complete */
    move.w  #4,(2,%a0)           /* lock_data->lock_state = 4 */
    clr.w   (0x24,%a1)           /* param2[0x12] = 0 */

lock_success:
    /* Re-enable interrupts (andi #-0x701,SR = andi #0xF8FF,SR) */
    andi    #0xF8FF,%sr
    st      %d0                  /* D0.b = 0xFF (true) */
    rts

lock_fail:
    /* Re-enable interrupts */
    andi    #0xF8FF,%sr
    clr.b   %d0                  /* D0.b = 0x00 (false) */
    rts

    .size SMD_$LOCK_DISPLAY, .-SMD_$LOCK_DISPLAY
