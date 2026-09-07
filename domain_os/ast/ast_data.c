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

/*
 * ============================================================================
 * AST_ module data block cells (A5 = 0xE1DC80, `D E1DC80 AST_ size = 498`)
 * ============================================================================
 */

/*
 * ast_$vol_indices - per-volume activated-AOTE counts, block + 0x412.
 * Seven words, closed above by ast_$vol_info_count at block + 0x420.
 * Zero in the image.
 *
 * Original address: 0xE1E092
 */
int16_t ast_$vol_indices[AST_VOL_INDEX_SLOTS];

/*
 * ast_$vol_info_count - per-volume "dismount in progress" bit set,
 * block + 0x420.  Zero in the image.
 *
 * Original address: 0xE1E0A0
 */
uint16_t ast_$vol_info_count;

/*
 * AST_$DISMOUNT_FAILED_PTR - the AOTE whose flush failed during the last
 * AST_$DISMOUNT (0x00E06AC0 stores it).  Named by the SAU2 map.  NULL in the
 * image.
 *
 * Original address: 0xE1E0B8 (block + 0x438)
 */
aote_t *AST_$DISMOUNT_FAILED_PTR;

/*
 * ast_$clobbered_uid - the UID AST_$SAVE_CLOBBERED_UID copies aside before it
 * queues AST_$SET_TROUBLE.  Zero in the image.
 *
 * Original address: 0xE1E110 (block + 0x490)
 */
uid_t ast_$clobbered_uid;

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
