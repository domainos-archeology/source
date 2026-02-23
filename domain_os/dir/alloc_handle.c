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

        /* Server processes (type 9) don't wait */
        if (((int16_t *)PROC1_$TYPE)[(int16_t)(PROC1_$CURRENT * 2)] == 9) {
            goto done;
        }

        /* Check if current process already owns a handle */
        {
            int16_t count = 0x1F;
            uint16_t idx = 0;
            uint32_t bitmap = *(uint32_t *)(base + 0x203C);
            char *scan = base;
            int8_t found = 0;

            do {
                if ((bitmap & (1u << idx)) != 0) {
                    if (*(int16_t *)(scan + 0x1888) == PROC1_$CURRENT) {
                        found = -1;
                        break;
                    }
                }
                idx++;
                scan += 0x3C;
                count--;
            } while (count != -1);

            if (found < 0) {
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
            ulong wait_val = DIR_$WT_FOR_HDNL_EC.value + 1;
            *(uint32_t *)(base + 0x2020) += 1;

            ML_$EXCLUSION_STOP(&DIR_$MUTEX);

            {
                ec_$eventcount_t *wait_ecs[3];
                wait_ecs[0] = NULL;
                wait_ecs[1] = NULL;
                wait_ecs[2] = &DIR_$WT_FOR_HDNL_EC;

                EC_$WAIT(wait_ecs, &wait_val);
            }
        }
        /* Loop back to try again */
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
