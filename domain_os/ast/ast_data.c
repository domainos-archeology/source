/*
 * ast/ast_data.c - AST Subsystem Global Data Definitions
 *
 * Holds the AST data cells that live in the image's own code/data region
 * and that no other AST translation unit owns.
 *
 * Original addresses:
 *   PTR_AST_$SET_TROUBLE_00e07272:   0x00e07272
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
