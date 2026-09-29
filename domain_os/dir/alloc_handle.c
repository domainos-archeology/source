/*
 * DIR_$ALLOC_HANDLE - Allocate a directory handle slot
 *
 * Takes the head of the handle free list at A5+0x2038 and marks the slot in
 * the in-use bitmap at A5+0x203C.  When the list is empty:
 *   - a naming-server helper (PROC1_$TYPE == 9) gives up at once and
 *     returns NULL;
 *   - a process that already holds a handle may fall back on the RESERVE
 *     slot, handle table entry 0 at A5+0x1880, provided bit 0 of the in-use
 *     bitmap is still clear (the image spells that test as a byte operation
 *     on A5+0x203F, the bitmap's least significant byte);
 *   - everyone else counts a wait in DIR_$HNDL_WAITS and blocks on
 *     DIR_$WT_FOR_HDNL_EC, then retries from the top.
 *
 * Returns: the handle, or NULL.
 *
 * Original address: 0x00E4B86E
 * Original size: 274 bytes
 */

#include "dir/dir_internal.h"

/* `cmpi.w #0x9,(-0x2,A0,D0w*0x1)` at 0x00E4B8A0 against PROC1_$TYPE. */
#define DIR_PROC_TYPE_NS_HELPER     9

/* Bit 0 of DIR_$HANDLE_IN_USE marks handle table slot 0, the reserve slot
 * `lea (0x1880,A5),A2` hands out at 0x00E4B8E2. */
#define DIR_HANDLE_RESERVE_BIT      0x00000001u

void *DIR_$ALLOC_HANDLE(void)
{
    char          *blk = DIR_$BLOCK;
    dir_$handle_t *handle = NULL;       /* A2 */

    while (1) {
        ML_$EXCLUSION_START(&DIR_$MUTEX);       /* 0x00E4B878 */

        /* 0x00E4B886 */
        handle = (dir_$handle_t *)ARCH_VA_TO_PTR(DIR_HANDLE_FREE_OF(blk));

        if (handle != NULL) {
            /* 0x00E4B92E-0x00E4B93A: mark it busy and unlink it. */
            DIR_HANDLE_IN_USE_OF(blk) |=
                1u << ((uint32_t)handle->slot_index & 0x1F);
            DIR_HANDLE_FREE_OF(blk) = handle->next;
            break;
        }

        /*
         * 0x00E4B892-0x00E4B8A6: a naming-server helper never waits for a
         * handle.
         */
        if (PROC1_$DATA.type[PROC1_$CURRENT] == DIR_PROC_TYPE_NS_HELPER) {
            goto done;
        }

        /* 0x00E4B8AA-0x00E4B8D2: does this process already hold a handle? */
        {
            int16_t  count = DIR_SLOT_COUNT - 1;    /* D1, the dbf counter */
            uint16_t idx = 0;                       /* D2 */
            boolean  found = false;                 /* D0 */

            do {
                /* 0x00E4B8B8: the bitmap is re-read on every iteration. */
                if ((DIR_HANDLE_IN_USE_OF(blk) &
                     (1u << ((uint32_t)idx & 0x1F))) != 0) {
                    /* 0x00E4B8C0: `(0x1888,A1)` with A1 = A5 + idx*0x3C is
                     * handle table entry idx's owner word. */
                    if (DIR_HANDLE_TAB_OF(blk)[idx].owner ==
                        (int16_t)PROC1_$CURRENT) {
                        found = true;   /* 0x00E4B8C8 `st D0b` */
                        break;
                    }
                }
                idx++;
                count--;
            } while (count != -1);

            if (found < 0) {    /* 0x00E4B8D6 `tst.b D0b` / `bpl` */
                /* 0x00E4B8DA-0x00E4B8EC: hand out the reserve slot once. */
                if ((DIR_HANDLE_IN_USE_OF(blk) & DIR_HANDLE_RESERVE_BIT) == 0) {
                    handle = &DIR_HANDLE_TAB_OF(blk)[0];
                    DIR_HANDLE_IN_USE_OF(blk) |= DIR_HANDLE_RESERVE_BIT;
                    goto done;
                }
            }
        }

        /* 0x00E4B8EE-0x00E4B92A: wait for somebody to free a handle. */
        {
            int32_t wait_val = (int32_t)DIR_$WT_FOR_HDNL_EC.value + 1;
            DIR_HNDL_WAITS_OF(blk) += 1;            /* 0x00E4B8FA */

            ML_$EXCLUSION_STOP(&DIR_$MUTEX);        /* 0x00E4B8FE */

            /*
             * 0x00E4B90C-0x00E4B926: EC_$WAIT takes two 3-element arrays BY
             * VALUE (24 bytes on the stack, popped with `lea (0x18,SP),SP`).
             * Only slot 0 is used here: ecs = { &DIR_$WT_FOR_HDNL_EC, NULL,
             * NULL }, vals = { wait_val, 0, 0 }.  The returned index is
             * discarded.
             */
            EC_$WAIT((ec_$wait_ecs_t){{ &DIR_$WT_FOR_HDNL_EC, NULL, NULL }},
                     (ec_$wait_vals_t){{ wait_val, 0, 0 }});
        }
        /* 0x00E4B92A: `bra.w 0x00e4b878` - back to the mutex acquire. */
    }

done:
    ML_$EXCLUSION_STOP(&DIR_$MUTEX);                /* 0x00E4B940 */

    /* 0x00E4B950-0x00E4B972 */
    if (handle != NULL) {
        handle->owner      = (int16_t)PROC1_$CURRENT;   /* 0x00E4B958 */
        handle->lock_mode  = 0;                         /* 0x00E4B960 */
        handle->split_busy = 0;                         /* 0x00E4B964 byte */
        handle->mapped     = 0;                         /* 0x00E4B968 byte */
        handle->max_slots  = 2;                         /* 0x00E4B96C */
        handle->lock_entry = 0;                         /* 0x00E4B972 */
    }

    return handle;
}
