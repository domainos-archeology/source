/*
 * DIR_$ALLOC_HANDLE - Allocate directory handle slot
 *
 * Allocates a handle from the free list. If no handles are available,
 * waits on DIR_$WT_FOR_HDNL_EC (unless the process is a server
 * process type 9, in which case it returns NULL immediately).
 *
 * If no free handle is available and the current process already owns
 * a handle (checked via bitmap + owner matching), and the "extra" bit
 * (bit 0 at offset 0x203F) is not set, reuses offset 0x1880 as an
 * emergency handle and sets the extra bit.
 *
 * Initializes the handle fields:
 *   +0x08: Owner process ID (PROC1_$CURRENT)
 *   +0x0A: Lock mode (cleared to 0)
 *   +0x0E: Flags (cleared to 0)
 *   +0x20: Mapped flag (cleared to 0)
 *   +0x1C: max_slots (set to 2)
 *   +0x34: Lock entry pointer (cleared to 0)
 *
 * Returns: handle pointer (NULL if unavailable for server processes)
 *
 * Original address: 0x00E4B86E
 * Original size: 274 bytes
 */

#include "dir/dir_internal.h"

void *DIR_$ALLOC_HANDLE(void)
{
    char *base = (char *)__A5_BASE();
    uint8_t *handle = NULL;

    while (1) {
        ML_$EXCLUSION_START(&DIR_$MUTEX);

        handle = *(uint8_t **)(base + 0x2038);

        if (handle != NULL) {
            /* Got a handle from free list */
            uint16_t slot_idx = *(uint16_t *)(handle + 0x38);

            /* Set bit in active bitmap */
            *(uint32_t *)(base + 0x203C) |= (1u << (slot_idx & 0x1F));

            /* Remove from free list */
            *(uint32_t *)(base + 0x2038) = *(uint32_t *)(handle + 0x30);

            break;
        }

        /* No free handle */

        /* Server processes (type 9) don't wait.
         * 0xE4B898: `cmpi.w #0x9,(-0x2,A0,D0w*1)` with A0 = 0xE2612C and
         * D0 = PROC1_$CURRENT*2, i.e. PROC1_$TYPE[PROC1_$CURRENT] with the
         * 0xE2612A base that proc1.h declares. */
        if (PROC1_$TYPE[PROC1_$CURRENT] == 9) {
            goto done;
        }

        /* Check if current process already owns a handle */
        {
            int16_t count = 0x1F;
            uint16_t idx = 0;
            char *scan = base;
            boolean found = false;

            do {
                /* 0xE4B8B8: the bitmap is re-read on every iteration. */
                uint32_t bitmap = *(uint32_t *)(base + 0x203C);
                if ((bitmap & (1u << (idx & 0x1F))) != 0) {
                    if (*(int16_t *)(scan + 0x1888) == (int16_t)PROC1_$CURRENT) {
                        found = true;   /* 0xE4B8C8: st D0b */
                        break;
                    }
                }
                idx++;
                scan += 0x3C;
                count--;
            } while (count != -1);

            if (found < 0) {    /* 0xE4B8D6: tst.b D0b / bpl */
                /* We own a handle - try emergency slot if not already used */
                if ((*(uint8_t *)(base + 0x203F) & 1) == 0) {
                    handle = (uint8_t *)(base + 0x1880);
                    *(uint8_t *)(base + 0x203F) |= 1;
                    goto done;
                }
            }
        }

        /* Wait for a handle to become available */
        {
            /* 0xE4B8EE */
            int32_t wait_val = (int32_t)DIR_$WT_FOR_HDNL_EC.value + 1;
            *(uint32_t *)(base + 0x2020) += 1;      /* 0xE4B8FA */

            ML_$EXCLUSION_STOP(&DIR_$MUTEX);        /* 0xE4B8FE */

            /*
             * 0xE4B90C-0xE4B926: EC_$WAIT takes two 3-element arrays BY
             * VALUE (24 bytes on the stack, popped with `lea (0x18,SP),SP`).
             * Only slot 0 is used here: ecs = { &DIR_$WT_FOR_HDNL_EC, NULL,
             * NULL }, vals = { wait_val, 0, 0 }.  The returned index is
             * discarded.
             */
            EC_$WAIT((ec_$wait_ecs_t){{ &DIR_$WT_FOR_HDNL_EC, NULL, NULL }},
                     (ec_$wait_vals_t){{ wait_val, 0, 0 }});
        }
        /* 0xE4B92A: loop back to the ML_$EXCLUSION_START at 0xE4B878 */
    }

done:
    ML_$EXCLUSION_STOP(&DIR_$MUTEX);

    if (handle != NULL) {
        *(int16_t *)(handle + 0x08) = PROC1_$CURRENT;
        *(int16_t *)(handle + 0x0A) = 0;
        handle[0x0E] = 0;
        handle[0x20] = 0;
        *(int16_t *)(handle + 0x1C) = 2;
        *(uint32_t *)(handle + 0x34) = 0;
    }

    return handle;
}
