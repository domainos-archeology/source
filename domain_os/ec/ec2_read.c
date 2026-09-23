/*
 * EC2_$READ - Read a Level 2 Event Count value
 *
 * Above 0x3E8 the argument is the eventcount's address and its value
 * longword is returned directly.  Otherwise it is an EC2 index: it is
 * copied into a local longword and EC2_$GET_VAL resolves it, its status
 * going to a local the caller never sees (so a bad index reads as
 * 0x7FFFFFFF).
 *
 * Parameters:
 *   ec - EC2 pointer or index (argument 1, (0x8,A6))
 *
 * Returns:
 *   Current value of the eventcount.
 *
 * Original address: 0x00e42c7c (map: EC2U, 0xE42C60 size 0x8C)
 * Re-emitted from the disassembly 0x00E42C7C-0x00E42CAC.
 */

#include "ec/ec_internal.h"

int32_t EC2_$READ(ec2_$eventcount_t *ec)
{
    ec2_$eventcount_t index_cell;   /* (-0x8,A6): the index longword */
    status_$t         status;       /* (-0x4,A6): discarded          */
    int32_t           value;        /* D0                            */

    /* 0x00E42C82-0x00E42C8C: `cmpi.l #0x3e8,D2 / bls` - UNSIGNED. */
    if (ARCH_PTR_TO_VA(ec) > 0x3E8) {
        /* 0x00E42C8E-0x00E42C92: direct read. */
        value = ec->value;
    } else {
        /* 0x00E42C94-0x00E42CA0: EC2_$GET_VAL(&index_cell, &status). */
        index_cell.value = (int32_t)ARCH_PTR_TO_VA(ec);
        value = EC2_$GET_VAL(&index_cell, &status);
    }

    /* 0x00E42CA6-0x00E42CAC */
    return value;
}
