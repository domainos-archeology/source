/*
 * AST Internal Header
 *
 * This header contains internal function prototypes and data structures
 * used only within the AST subsystem. External code should use ast.h.
 */

#ifndef AST_INTERNAL_H
#define AST_INTERNAL_H

#include "ast/ast.h"

/*
 * Internal helper functions
 */

/* Look up AOTE by UID - returns AOTE pointer */
aote_t *ast_$lookup_aote_by_uid(uid_t *uid);

/* Force lookup/activate AOTE for segment - returns AOTE pointer */
aote_t *ast_$force_activate_segment(uid_t *uid, uint16_t segment, status_$t *status, int8_t force);

/* Look up existing ASTE for AOTE/segment */
aste_t* ast_$lookup_aste(aote_t *aote, int16_t segment);

/* Look up or create ASTE for AOTE/segment */
aste_t* ast_$lookup_or_create_aste(aote_t *aote, int16_t segment, status_$t *status);

/* Wait for page transition to complete */
void ast_$wait_for_page_transition(void);

/* Allocate pages - returns count, takes count_flags and ppn_array */
int16_t ast_$allocate_pages(uint32_t count_flags, uint32_t *ppn_array);

/* Clear transition bits in segment map */
void ast_$clear_transition_bits(uint32_t *segmap, uint16_t count);

/* Setup page read */
void ast_$setup_page_read(aste_t *aste, uint32_t *segmap, uint16_t page,
                          uint16_t count);

/* Count valid pages in segment map */
uint16_t ast_$count_valid_pages(uint32_t *segmap, uint16_t count);

/* Read area pages from disk */
void ast_$read_area_pages(aste_t *aste, uint32_t *segmap, uint32_t *ppn_array,
                          uint16_t count, status_$t *status);

/* Read area pages from network */
void ast_$read_area_pages_network(aste_t *aste, uint32_t *segmap, uint32_t *ppn_array,
                                  uint16_t count, status_$t *status);

/* Process AOTE flags/flush */
void ast_$process_aote(aote_t *aote, uint8_t flags1, uint16_t flags2,
                       uint16_t flags3, status_$t *status);

/* Free/release AOTE */
void ast_$release_aote(aote_t *aote);

/* Allocate new AOTE */
aote_t* ast_$allocate_aote(void);

/* Purify/flush AOTE */
void ast_$purify_aote(aote_t *aote, uint16_t flags, status_$t *status);

/* Update ASTE/segment map */
void ast_$update_aste(aste_t *aste, segmap_entry_t *segmap, uint16_t flags,
                      status_$t *status);

/* Invalidate pages with wait */
status_$t ast_$invalidate_with_wait(uint16_t end_page);

/* Invalidate pages without wait */
void ast_$invalidate_no_wait(uint16_t end_page);

/* Flush installed pages */
void ast_$flush_installed_pages(void);

/* Set attribute on object */
void ast_$set_attribute_internal(uid_t *uid, int16_t attr_id, uint32_t value,
                                 uint8_t flags, uint32_t *clock, status_$t *status);

/* Validate UID and return status */
status_$t ast_$validate_uid(uid_t *uid, uint32_t flags);

/*
 * Internal global variables
 */

/* Volume info count at 0xE1E0A0 (offset 0x420 from globals) */
extern uint16_t ast_$vol_info_count;
#define DAT_00e1e0a0 ast_$vol_info_count

/* Unknown at 0xE1E088 (offset 0x408) */
extern uint32_t ast_$unknown_e1e088;
#define DAT_00e1e088 ast_$unknown_e1e088

/* Volume index array at 0xE1E092 */
extern int16_t ast_$vol_indices[];
#define DAT_00e1e092 ast_$vol_indices

/* Clobbered UID storage at 0xE1E110 (offset 0x490) */
extern uid_t ast_$clobbered_uid;
#define DAT_00e1e110 ast_$clobbered_uid

/* Dismount failed AOTE pointer */
extern aote_t* AST_$DISMOUNT_FAILED_PTR;

/* Set trouble callback pointer */
extern void *PTR_AST_$SET_TROUBLE_00e07272;

/* Zero buffer for page operations (1KB = 256 uint32_t) */
extern uint32_t AST_$ZERO_BUFF[256];

/* Duplicate AOTE error status */
extern status_$t status_$_00e2f1d0;

#endif /* AST_INTERNAL_H */
