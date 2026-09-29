/*
 * ast/ast_data.c - AST Subsystem Global Data Definitions
 *
 * Module data blocks AST_$DATA / AST_$AOT: Claude Opus 5.5 (source-gmxj).
 *
 * The AST module's data (docs/design-per-process-data.md; the addresses are
 * the SAU2 image's, used as ordering keys, not link addresses):
 *
 *   0x00E1DC80  AST_$DATA  map "D E1DC80 AST_ size = 498": the A5 block
 *   0x00EC5400  AST_$AOT   map "D00 EC5400 AST_AOT ... size = F960": the
 *                          ASTE (AST) and AOTE (AOT) tables
 *
 * plus the AST_ cells that live in the image's code region and the wired
 * zero page.
 */

#include "ast/ast_internal.h"

/*
 * PTR_AST_$SET_TROUBLE_00e07272 - cell holding AST_$SET_TROUBLE's address
 *
 * AST_$SAVE_CLOBBERED_UID (0x00E07220) builds a DXM callback with
 *
 *   00e0725a    pea (0x16,PC)          ; -> 0x00e0725a + 2 + 0x16 = 0x00e07272
 *   00e0725e    move.l #0xe2adc4,-(SP) ; DXM_$UNWIRED_Q
 *   00e07264    jsr 0x00e16fe0.l       ; DXM_$ADD_CALLBACK
 *
 * so it hands DXM the ADDRESS of this cell, and the cell itself holds the
 * 4-byte code address.  The image has
 *
 *   00e07272  00 e0 71 ea            ; = AST_$SET_TROUBLE (0x00e071ea)
 *
 * DXM_$DEFINE_CALLBACK_CELL keeps the cell a 32-bit word on m68k and a
 * registry handle on a 64-bit host, so a dxm_entry_t stays 16 bytes
 * (see dxm/dxm.h, source-wy9y).
 */
DXM_$DEFINE_CALLBACK_CELL(PTR_AST_$SET_TROUBLE_00e07272, AST_$SET_TROUBLE);

/*
 * ============================================================================
 * AST_$DATA - the AST_ module block, 0x00E1DC80..0x00E1E117
 * ============================================================================
 *
 * Layout, biases and asserts in ast/ast.h.  Image contents
 * (`gsk read 0xE1DC80 0x498`); every byte not listed is zero:
 *
 *   +0x3F0 0xE1E070  00 ec 7b 60                   aote_scan_pos = AOT
 *   +0x3F4 0xE1E074  00 ec 7b 60                   aote_limit    = AOT
 *   +0x3FC 0xE1E07C  00 ec 54 00                   aste_scan_pos = AST
 *   +0x400 0xE1E080  00 ec 54 00                   aste_limit    = AST
 *   +0x408 0xE1E088  00 00 00 00 00 e1 e0 88 00 e1 e0 88   dism_ec
 *   +0x428 0xE1E0A8  00 00 00 00 00 e1 e0 a8 00 e1 e0 a8   ast_in_trans_ec
 *   +0x44C 0xE1E0CC  00 00 00 00 00 e1 e0 cc 00 e1 e0 cc   pmap_in_trans_ec
 *   +0x46C 0xE1E0EC  00 08                          grow_ahead_cnt = 8
 *   +0x484 0xE1E104  00 ec 7b 60                   update_scan   = AOT
 *   +0x488 0xE1E108  ff ff                          update_timestamp
 *   +0x48C 0xE1E10C  02 78 30 1c                   attr_timestamp_mask
 *
 * The table cells are the link-time addresses of AST_$AOT's two arrays
 * (the image values, 0xEC7B60 / 0xEC5400, are the AOT / AST symbols); they
 * are pointer fields, so the initialiser is the address itself rather than
 * ARCH_PTR_TO_VA_STATIC, and a host test gets its own tables.  The
 * eventcounts are the EC_$INIT state: an empty circular waiter list whose
 * head and tail point back at the eventcount.
 */
MODULE_DATA_DEFINE_INIT(ast_$data_t, AST_$DATA, 0x00E1DC80, {
    .aote_scan_pos = &AST_$AOT.aote[0],
    .aote_limit = &AST_$AOT.aote[0],
    .aste_scan_pos = &AST_$AOT.aste[0],
    .aste_limit = &AST_$AOT.aste[0],
    .dism_ec = {
        .value = 0,
        .waiter_list_head = (ec_$eventcount_waiter_t *)&AST_$DATA.dism_ec,
        .waiter_list_tail = (ec_$eventcount_waiter_t *)&AST_$DATA.dism_ec,
    },
    .ast_in_trans_ec = {
        .value = 0,
        .waiter_list_head = (ec_$eventcount_waiter_t *)&AST_$DATA.ast_in_trans_ec,
        .waiter_list_tail = (ec_$eventcount_waiter_t *)&AST_$DATA.ast_in_trans_ec,
    },
    .pmap_in_trans_ec = {
        .value = 0,
        .waiter_list_head = (ec_$eventcount_waiter_t *)&AST_$DATA.pmap_in_trans_ec,
        .waiter_list_tail = (ec_$eventcount_waiter_t *)&AST_$DATA.pmap_in_trans_ec,
    },
    .grow_ahead_cnt = 8,
    .update_scan = &AST_$AOT.aote[0],
    .update_timestamp = 0xFFFF,
    .attr_timestamp_mask = 0x0278301Cu,
});

/*
 * ============================================================================
 * AST_$AOT - the ASTE and AOTE tables, 0x00EC5400..0x00ED4D5F
 * ============================================================================
 *
 * Layout in ast/ast.h.  No bytes in the image (AST_$INIT builds both tables
 * at boot), so zero-filled.  The map places AST_AOT after AUDIT_LIST
 * (0xEC4800) and before AST_PMAPS (PMAP_$SEGMAP, 0xED5000).
 */
MODULE_DATA_DEFINE(ast_$aot_t, AST_$AOT, 0x00EC5400);

/*
 * ============================================================================
 * Wired buffers and in-code constants
 * ============================================================================
 */

/*
 * AST_$ZERO_BUFF - the wired all-zero page AST_$FETCH_PMAP_PAGE maps and
 * copies from.  Not part of the loaded image (it is a wired page in the I/O
 * region), so it starts zero.
 *
 * Original address: 0xFF8C00 (0x400 bytes)
 */
uint32_t AST_$ZERO_BUFF[256];

/*
 * status_$t_00e2f1d0 - AST_$ACTIVATE_AOTE_CANNED's crash status.
 * Image bytes at 0x00E2F1D0: 80 03 00 03.
 *
 * Original address: 0xE2F1D0
 */
status_$t status_$t_00e2f1d0 = (status_$t)0x80030003;
