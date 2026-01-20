/*
 * AS_$GET_INFO - Get address space information
 *
 * Reverse engineered from Domain/OS at address 0x00E583A0
 *
 * Copies the AS info structure to the caller's buffer.
 * The amount copied is bounded by both the requested size
 * and the actual info size (AS_$INFO_SIZE).
 */

#include "as/as.h"

void AS_$GET_INFO(void *buffer, int16_t *req_size, int16_t *actual_size)
{
    int16_t copy_size;
    int16_t i;
    uint8_t *src;
    uint8_t *dst;

    /*
     * Check for invalid request size
     * Assembly: tst.w (A1); bgt.b valid; clr.w (A0); bra.b done
     */
    if (*req_size < 1) {
        *actual_size = 0;
        return;
    }

    /*
     * Determine actual copy size - minimum of requested and available
     * Assembly:
     *   move.w (A1),D0w
     *   cmp.w (0x00e2b970).l,D0w  ; compare with AS_$INFO_SIZE
     *   bgt.b use_info_size
     *   move.w D0w,(A0)           ; use requested size
     *   bra.b do_copy
     * use_info_size:
     *   move.w (0x00e2b970).l,(A0) ; use AS_$INFO_SIZE
     */
    if (*req_size <= AS_$INFO_SIZE) {
        copy_size = *req_size;
    } else {
        copy_size = AS_$INFO_SIZE;
    }
    *actual_size = copy_size;

    /*
     * Copy bytes from AS_$INFO to caller's buffer
     * Uses 1-based indexing in original assembly:
     * Assembly:
     *   movea.l #0xe2b914,A1      ; source = AS_$INFO
     *   movea.l (0x8,A6),A2       ; dest = buffer
     *   move.w (A0),D0w           ; count = actual_size
     *   subq.w #0x1,D0w           ; adjust for dbf
     *   bmi.b done
     *   moveq #0x1,D1             ; index starts at 1
     * loop:
     *   move.b (-0x1,A1,D1w*0x1),(-0x1,A2,D1w*0x1)
     *   addq.w #0x1,D1w
     *   dbf D0w,loop
     *
     * Note: The assembly uses 1-based indexing with -1 offset,
     * which is equivalent to 0-based indexing.
     */
    src = (uint8_t *)&AS_$INFO;
    dst = (uint8_t *)buffer;

    for (i = 0; i < copy_size; i++) {
        dst[i] = src[i];
    }
}
