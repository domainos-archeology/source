/*
 * MST_$MAP, MST_$MAP_TOP, MST_$MAP_GLOBAL - Map memory regions
 *
 * These functions are public wrappers around the internal mapping function.
 * They set up parameters for different mapping scenarios:
 *
 * - MST_$MAP: Map at any available address in private space
 * - MST_$MAP_TOP: Map at top of address space (same as MAP for now)
 * - MST_$MAP_GLOBAL: Map in global (shared) address space
 *
 * All functions call the internal FUN_00e43182 with appropriate parameters.
 *
 * Original analysis from 0x00E4386C assembly:
 *   - Returns mapped address in A0 register
 *   - Parameters are dereferenced and passed to FUN_00e43182
 *   - param_7 (map_info) is passed through as output buffer
 */

#include "mst_internal.h"

/*
 * MST_$MAP - Map memory at any available private address
 *
 * Maps a file object into the current address space at any available
 * address. Returns the mapped address.
 *
 * @param uid           Object UID for the mapping
 * @param start_ptr     Pointer to starting offset in file
 * @param length_ptr    Pointer to length to map
 * @param mode_ptr      Pointer to mapping mode/area identifier
 * @param extend_ptr    Pointer to extend value/area size
 * @param concur_ptr    Pointer to concurrency flags/access rights byte
 * @param map_info      Output: mapping info (passed through to FUN_00e43182,
 *                      also used as input to MST_$UNMAP)
 * @param status_ret    Output: status code
 *
 * Returns:
 *   Mapped virtual address (from A0 register in original assembly)
 *
 * Original address: 0x00E4386C
 */
void *MST_$MAP(uid_t *uid,
               uint32_t *start_ptr,
               uint32_t *length_ptr,
               uint16_t *mode_ptr,
               uint32_t *extend_ptr,
               uint8_t *concur_ptr,
               void *map_info,
               status_$t *status_ret)
{
    return FUN_00e43182(0x7fffffff,       /* addr_hint = search from top */
                        uid,
                        *start_ptr,
                        *length_ptr,
                        *extend_ptr,
                        PROC1_$AS_ID,     /* Current process ASID */
                        *mode_ptr,
                        MST_$TOUCH_COUNT, /* Touch-ahead count */
                        *concur_ptr,
                        0,                /* direction = 0 (forward) */
                        map_info,
                        status_ret);
}

/*
 * MST_$MAP_TOP - Map memory at top of private address space
 *
 * Currently identical to MST_$MAP - both search from top of space.
 *
 * Original address: 0x00E438CC
 */
void MST_$MAP_TOP(uid_t *uid,
                  uint32_t *start_va_ptr,
                  uint32_t *length_ptr,
                  uint16_t *area_id_ptr,
                  uint32_t *area_size_ptr,
                  uint8_t *rights_ptr,
                  int32_t *mapped_len,
                  status_$t *status_ret)
{
    FUN_00e43182(0x7fffffff,       /* addr_hint = search from top */
                 uid,
                 *start_va_ptr,
                 *length_ptr,
                 *area_size_ptr,
                 PROC1_$AS_ID,     /* Current process ASID */
                 *area_id_ptr,
                 MST_$TOUCH_COUNT, /* Touch-ahead count */
                 *rights_ptr,
                 0,                /* direction = 0 (forward) */
                 mapped_len,
                 status_ret);
}

/*
 * MST_$MAP_GLOBAL - Map memory in global (shared) address space
 *
 * Uses ASID 0 for global mappings and touch-ahead of 1.
 *
 * Original address: 0x00E43918
 */
void MST_$MAP_GLOBAL(uid_t *uid,
                     uint32_t *start_va_ptr,
                     uint32_t *length_ptr,
                     uint16_t *area_id_ptr,
                     uint32_t *area_size_ptr,
                     uint8_t *rights_ptr,
                     int32_t *mapped_len,
                     status_$t *status_ret)
{
    FUN_00e43182(0,                /* addr_hint = 0 for global */
                 uid,
                 *start_va_ptr,
                 *length_ptr,
                 *area_size_ptr,
                 0,                /* ASID 0 for global space */
                 *area_id_ptr,
                 1,                /* Touch-ahead = 1 for global */
                 *rights_ptr,
                 0,                /* direction = 0 (forward) */
                 mapped_len,
                 status_ret);
}
