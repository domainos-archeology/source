/*
 * pmap_$wait_in_transit - Drop the PMAP lock until a page write completes
 *
 * 0x00E12D38 - 0x00E12D7C (70 bytes).  Verified against the disassembly
 * 2026-09-27; the earlier emission was faithful.
 *
 * wait_val = AST_$PMAP_IN_TRANS_EC.value + 1 (0x00E12D3C); ML_$UNLOCK(0x14);
 * EC_$WAITN(ecs, &wait_val, 1) where `ecs' is the constant cell at
 * 0x00E12D80 (`pea (0x1e,PC)`, image bytes 00 e1 e0 cc = the address of
 * AST_$PMAP_IN_TRANS_EC), i.e. a one-element eventcount-pointer array
 * that lives in this code segment; then ML_$LOCK(0x14).  The result slot
 * of EC_$WAITN is discarded.
 */

#include "pmap/pmap_internal.h"
#include "ast/ast.h"
#include "ec/ec.h"

/* 0x00E12D80: the one-entry ecs array the image keeps in code */
static ec_$eventcount_t *pmap_$in_trans_ecs_00e12d80 = &AST_$PMAP_IN_TRANS_EC;

void pmap_$wait_in_transit(void)
{
    int32_t wait_val;                                   /* (-0x8,A6) */

    wait_val = AST_$PMAP_IN_TRANS_EC.value + 1;         /* 0x00E12D3C */
    ML_$UNLOCK(PMAP_LOCK_ID);                           /* 0x00E12D4E */
    EC_$WAITN(&pmap_$in_trans_ecs_00e12d80, &wait_val, 1); /* 0x00E12D64 */
    ML_$LOCK(PMAP_LOCK_ID);                             /* 0x00E12D74 */
}
