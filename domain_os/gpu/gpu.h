/*
 * gpu/gpu.h - GPU Subsystem Public Interface
 *
 * Graphics Processing Unit initialization and termination.
 * On SAU2 hardware, this is a stub that returns "device not present".
 */

#ifndef GPU_H
#define GPU_H

#include "base/base.h"
#include "ec/ec.h"

/*
 * ============================================================================
 * Status Codes
 * ============================================================================
 */

/* GPU subsystem ID (0x2d = 45) */
#define STATUS_GPU_SUBSYSTEM        0x2d

/* GPU device not present on this hardware */
#define status_$gpu_device_not_present  0x2d0001

/*
 * ============================================================================
 * Public Functions
 * ============================================================================
 */

/*
 * GPU_$INIT - Initialize GPU subsystem
 *
 * Initializes the GPU subsystem. On SAU2 hardware without a GPU,
 * this simply returns status_$gpu_device_not_present.
 *
 * Parameters:
 *   status_ret - Pointer to receive initialization status
 *
 * Original address: 0x00E707FC (18 bytes)
 */
void GPU_$INIT(status_$t *status_ret);

/*
 * GPU_$TERM - Terminate GPU subsystem
 *
 * Terminates the GPU subsystem and releases resources.
 * On SAU2 hardware without a GPU, this is a no-op.
 *
 * Original address: 0x00E7080E (2 bytes)
 */
void GPU_$TERM(void);

/* GPU data */
/*
 * The GPU_ASM data segment (SAU2 map: D E27500 GPU_ASM size = 10):
 *   +0x0  GPU_$PRESENT   int8_t boolean in the high byte of a word; the
 *                        only reader is ASKNODE_$INTERNET_INFO's
 *                        `tst.b (0x00e27500).l` / bpl at 0x00E64D28
 *   +0x2  GPU_$PAGE_EC   an eventcount (0xC bytes), no user in the tree yet
 *   +0xE  pad to the segment size
 * The image holds zero throughout (no GPU on the SAU2).
 */
typedef struct gpu_$asm_data_t {
    int8_t           present;      /* +0x0  GPU_$PRESENT */
    int8_t           _pad_1;       /* +0x1  */
    ec_$eventcount_t page_ec;      /* +0x2  GPU_$PAGE_EC */
    int8_t           _pad_e[2];    /* +0xE  */
} gpu_$asm_data_t;
_Static_assert(__builtin_offsetof(gpu_$asm_data_t, present) == 0x0, "gpu present");
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(gpu_$asm_data_t, page_ec) == 0x2, "gpu page_ec");
_Static_assert(sizeof(gpu_$asm_data_t) == 0x10, "GPU_ASM segment size");
#endif
MODULE_DATA_DECLARE(gpu_$asm_data_t, GPU_$ASM_DATA, 0x00E27500);
#define GPU_$PRESENT (GPU_$ASM_DATA.present)


#endif /* GPU_H */
