/*
 * smd/ws_init.c - SMD_$WS_INIT implementation
 *
 * Workstation initialization - sets up display context for rendering.
 *
 * Original address: 0x00E6DE58
 *
 * Assembly analysis:
 * This function initializes a workstation context structure with:
 * - Font pointer
 * - Font HDM position
 * - Display base address
 * - Hardware info pointer
 *
 * The context is populated from the current process's display unit data.
 */

#include "smd/smd_internal.h"

/*
 * Workstation context structure
 * Size: 0x18 (24) bytes
 */
typedef struct smd_ws_ctx_t {
    void        *font_ptr;      /* 0x00: Font data pointer */
    void        *pad_04;        /* 0x04: Reserved/padding */
    uint32_t    display_base;   /* 0x08: Display memory base address */
    void        *hw_ptr;        /* 0x0C: Hardware info pointer */
    status_$t   status;         /* 0x10: Status code */
    uint32_t    font_hdm_pos;   /* 0x14: Font HDM position */
    uint16_t    font_index;     /* 0x18: Font index */
} smd_ws_ctx_t;

/*
 * SMD_$WS_INIT - Workstation initialization
 *
 * Initializes a workstation context structure with font and display
 * information for the current process.
 *
 * Parameters:
 *   ctx - Pointer to workstation context structure to initialize
 *
 * The context structure receives:
 *   ctx[0]  = font pointer (from font table)
 *   ctx[5]  = font HDM position
 *   ctx[2]  = display base address
 *   ctx[3]  = hardware info pointer
 *   ctx[4]  = status
 */
void SMD_$WS_INIT(smd_ws_ctx_t *ctx)
{
    uint16_t unit;
    int32_t unit_offset;
    smd_display_unit_t *display_unit;
    smd_font_entry_t *font_table;
    uint16_t font_index;

    /* Get current process's display unit */
    unit = SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID];

    if (unit == 0) {
        /* No display associated */
        ctx->status = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    /* Calculate unit offset: unit * 0x10C */
    unit_offset = (int32_t)unit * SMD_DISPLAY_UNIT_SIZE;
    display_unit = (smd_display_unit_t *)((uint8_t *)&SMD_EC_1 + unit_offset);

    /* Get font index from context (caller provides this at offset 0x18) */
    font_index = ctx->font_index;

    /* Get font table pointer from display unit */
    font_table = (smd_font_entry_t *)((uint8_t *)display_unit);

    /* Look up font entry: font_table + font_index * 8 */
    ctx->font_ptr = font_table[font_index].font_ptr;

    if (ctx->font_ptr == NULL) {
        /* Font not loaded */
        ctx->status = status_$display_font_not_loaded;
        return;
    }

    /* Get font HDM position */
    ctx->font_hdm_pos = *(uint32_t *)&font_table[font_index].hdm_offset;

    /* Get display base address from unit offset + 8 */
    ctx->display_base = *(uint32_t *)((uint8_t *)&SMD_EC_1 + unit_offset + 8);

    /* Get hardware info pointer */
    ctx->hw_ptr = (void *)((uint8_t *)SMD_UNIT_AUX_BASE + unit_offset);

    ctx->status = status_$ok;
}
