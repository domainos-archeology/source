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
 */

#include "mst.h"

/* External process ID */
extern int16_t PROC1_$AS_ID;

/* Internal mapping function */
extern void FUN_00e43182(uint32_t addr_hint,
                         uid_t *uid,
                         uint32_t start_va,
                         uint32_t length,
                         uint32_t area_size,
                         int16_t asid,
                         uint16_t area_id,
                         uint16_t touch_count,
                         uint8_t access_rights,
                         int16_t direction,
                         int32_t *mapped_length,
                         status_$t *status_ret);

/*
 * MST_$MAP - Map memory at any available private address
 *
 * @param uid           Object UID for the mapping
 * @param start_va_ptr  Pointer to starting virtual address
 * @param length_ptr    Pointer to length to map
 * @param area_id_ptr   Pointer to area identifier
 * @param area_size_ptr Pointer to area size
 * @param rights_ptr    Pointer to access rights byte
 * @param mapped_len    Output: actual length mapped
 * @param status_ret    Output: status code
 */
void MST_$MAP(uid_t *uid,
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
 * MST_$MAP_TOP - Map memory at top of private address space
 *
 * Currently identical to MST_$MAP - both search from top of space.
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
