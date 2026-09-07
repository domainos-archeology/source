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
 * Size: 0x1A bytes (the caller supplies font_index at +0x18)
 *
 * Assembly (every instruction accounted for):
 *   00e6de58    link.w A6,-0x8
 *   00e6de5c    movem.l {  A5 A4 A3 A2 D2},-(SP)
 *   00e6de60    lea (0xe82b8c).l,A5
 *   00e6de66    move.w (0x00e2060a).l,D0w      ; D0 = PROC1_$AS_ID
 *   00e6de6c    movea.l (0x8,A6),A0            ; A0 = ctx
 *   00e6de70    add.w D0w,D0w
 *   00e6de72    move.w (0x48,A5,D0w*0x1),D0w   ; asid_to_unit[asid]
 *   00e6de76    bne.b 0x00e6de82
 *   00e6de78    move.l #0x130004,(0x10,A0)     ; ctx->status = invalid_use
 *   00e6de80    bra.b 0x00e6deca
 *   00e6de82    move.w D0w,D1w
 *   00e6de84    movea.l #0xe2e3fc,A1
 *   00e6de8a    muls.w #0x10c,D1
 *   00e6de8e    move.w (0x18,A0),D2w           ; D2 = ctx->font_index
 *   00e6de92    lea (0x0,A1,D1*0x1),A2         ; A2 = biased unit record
 *   00e6de96    lsl.w #0x3,D2w                 ; D2 = font_index * 8
 *   00e6de98    movea.l (A2),A3                ; A3 = rec->font_table (+0xF4)
 *   00e6de9a    lea (0x0,A3,D2w*0x1),A1
 *   00e6de9e    move.l (-0x8,A1),(A0)          ; ctx->font_ptr =
 *                                              ;   font_table[font_index-1].font_ptr
 *   00e6dea2    bne.b 0x00e6deae
 *   00e6dea4    move.l #0x130002,(0x10,A0)     ; ctx->status = font_not_loaded
 *   00e6deac    bra.b 0x00e6deca
 *   00e6deae    move.l (-0x4,A1),(0x14,A0)     ; ctx->font_hdm_pos =
 *                                              ;   the longword at entry+4
 *   00e6deb4    movea.l #0xe2e3fc,A4
 *   00e6deba    move.l (0x8,A4,D1*0x1),(0x8,A0); ctx->ctrl_regs = rec->ctrl_regs
 *   00e6dec0    move.l (-0xf4,A2),(0xc,A0)     ; ctx->hw = rec->hw (the VALUE)
 *   00e6dec6    clr.l (0x10,A0)                ; ctx->status = 0
 *   00e6deca    movem.l (-0x1c,A6),{  D2 A2 A3 A4 A5}
 *   00e6ded0    unlk A6
 *   00e6ded2    rts
 */
typedef struct smd_ws_ctx_t {
    void              *font_ptr;     /* 0x00: Font data pointer */
    void              *pad_04;       /* 0x04: never written */
    SMD_HW_REG_PTR     ctrl_regs;    /* 0x08: rec->ctrl_regs (record +0xFC) */
    smd_display_hw_t  *hw;           /* 0x0C: rec->hw (record +0x00) */
    status_$t          status;       /* 0x10: Status code */
    uint32_t           font_hdm_pos; /* 0x14: longword at font entry +4 */
    uint16_t           font_index;   /* 0x18: caller-supplied, 1-based */
} smd_ws_ctx_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(smd_ws_ctx_t, ctrl_regs) == 0x08, "ws ctrl");
_Static_assert(offsetof(smd_ws_ctx_t, hw) == 0x0C, "ws hw");
_Static_assert(offsetof(smd_ws_ctx_t, status) == 0x10, "ws status");
_Static_assert(offsetof(smd_ws_ctx_t, font_hdm_pos) == 0x14, "ws hdm");
_Static_assert(offsetof(smd_ws_ctx_t, font_index) == 0x18, "ws idx");
#endif

/*
 * SMD_$WS_INIT - Workstation initialization
 *
 * Initializes a workstation context structure with font and display
 * information for the current process.
 *
 * Parameters:
 *   ctx - Pointer to workstation context structure; the caller must have
 *         filled in ctx->font_index (1-based) before the call.
 */
void SMD_$WS_INIT(smd_ws_ctx_t *ctx)
{
    int16_t unit;
    smd_display_unit_t *rec;
    smd_font_entry_t *entry;

    /* 0x00e6de66-0x00e6de72 */
    unit = (int16_t)SMD_GLOBALS.asid_to_unit[PROC1_$AS_ID];

    if (unit == 0) {
        /* 0x00e6de78 */
        ctx->status = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    /* 0x00e6de84-0x00e6de9a: the font table is 1-based on the font index. */
    rec = smd_$unit_rec(unit);
    entry = &rec->font_table[ctx->font_index - 1];

    /* 0x00e6de9e */
    ctx->font_ptr = entry->font_ptr;

    if (ctx->font_ptr == NULL) {
        /* 0x00e6dea4 */
        ctx->status = status_$display_font_not_loaded;
        return;
    }

    /* 0x00e6deae: one longword covering hdm_offset and the word after it */
    ctx->font_hdm_pos = *(uint32_t *)&entry->hdm_pos;

    /* 0x00e6deba / 0x00e6dec0 */
    ctx->ctrl_regs = rec->ctrl_regs;
    ctx->hw = rec->hw;

    /* 0x00e6dec6 */
    ctx->status = status_$ok;
}
