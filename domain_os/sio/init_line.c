/*
 * SIO_$INIT_LINE - Initialize SIO TTY line descriptor
 *
 * Sets up a TTY descriptor from SIO port configuration data.
 * Copies the line identifier, hardware register addresses,
 * I/O buffer pointers, and then calls TTY_$I_INIT to complete
 * the initialization.
 *
 * Assembly analysis:
 *   param_1 (A2) = TTY descriptor pointer
 *   param_2 (A3) = SIO port data pointer
 *   param_3 (A0) = Pointer to line config (dereferenced for line ID)
 *   param_4 (A1) = Hardware info pointer
 *
 * Layout:
 *   desc[0x000]       = *param_3 (line ID, uint32_t)
 *   desc[0x2B4..0x2C3] = param_4[0x08..0x17] (16 bytes hardware regs)
 *   desc[0x2A4]       = param_2 + 0x0C (input buffer pointer)
 *   desc[0x2A8]       = param_2 + 0x18 (output buffer pointer)
 *   param_2[0x24]     = desc (back-link from port to TTY)
 *
 * Parameters:
 *   desc      - TTY descriptor to initialize
 *   port_data - SIO port data structure
 *   config    - Pointer to line configuration (dereferenced)
 *   hw_info   - Hardware info block
 *
 * Original address: 0x00E32B26
 * Size: 80 bytes
 */

#include "sio/sio_internal.h"
#include "tty/tty.h"

void SIO_$INIT_LINE(void *desc, void *port_data, void **config, void *hw_info)
{
    uint32_t *d = (uint32_t *)desc;
    char *port = (char *)port_data;
    char *hw = (char *)hw_info;

    /* Copy line ID from config */
    d[0] = **(uint32_t **)config;

    /* Copy 16 bytes of hardware register addresses from hw_info+8 to desc+0x2B4 */
    uint32_t *dst = (uint32_t *)((char *)desc + 0x2B4);
    uint32_t *src = (uint32_t *)(hw + 0x08);
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];

    /* Set I/O buffer pointers */
    *(void **)((char *)desc + 0x2A4) = port + 0x0C;   /* Input buffer */
    *(void **)((char *)desc + 0x2A8) = port + 0x18;   /* Output buffer */

    /* Initialize TTY subsystem for this descriptor */
    TTY_$I_INIT(desc);

    /* Back-link: store TTY descriptor pointer in port data */
    *(void **)(port + 0x24) = desc;
}
