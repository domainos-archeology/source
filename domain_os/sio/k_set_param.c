/*
 * SIO_$K_SET_PARAM - Set a line's serial parameters
 *
 * Works on a frame copy of the descriptor's parameter block.  For each
 * selector set in *change_mask_ptr the caller's value is validated and
 * compared with the copy: an unchanged value drops its selector from the
 * mask, a changed one is merged into the copy.  If any selector survives,
 * the driver's set_params is called with the copy and the reduced mask,
 * and on success the copy is written back to the descriptor.  Each
 * validation failure stores status_$sio_invalid_param and returns
 * without touching the driver or the descriptor.
 *
 * Original address: 0x00E680AC, 638 bytes (SAU2 map: SIO module at
 * 0xE67D9C)
 *
 * Frame: -0x18 flags1, -0x14 flags2, -0x10 break_mask, -0xC baud_rate,
 *        -0x8 char_size, -0x6 stop_bits, -0x4 parity (the copy)
 *
 *   00e680ac    link.w A6,-0x28
 *   00e680b0    movem.l {A4 A3 A2 D2},-(SP)
 *   00e680b4    movea.l (0xc,A6),A2            ; arg 2 params
 *   00e680b8    movea.l (0x14,A6),A3           ; arg 4 status_ret
 *   00e680bc    subq.l #0x2,SP ; pea (A3) ; move.w (*line_ptr),-(SP)
 *   00e680c6    jsr 0x00e667c6.l               ; SIO_$I_GET_DESC -> A0
 *   00e680d0    tst.l (A3) ; bne exit
 *   00e680d6    movea.l (0x10,A6),A0 ; move.l (A0),D2   ; mask = *change_mask_ptr
 *   00e680de    lea (0x4c,A4),A1 ; lea (-0x18,A6),A0 ; 5 longs + 1 word copy
 *   --- each selector: btst.l #n,D2 / compare copy with caller / clear or merge ---
 *   00e680f0    bit 5:  flags1 bit 0            00e68118    bit 6:  flags1 bit 3
 *   00e68140    bit 11: flags2 bit 2            00e6816a    bit 12: flags2 bit 3
 *   00e68194    bit 9:  flags2 bit 0            00e681be    bit 10: flags2 bit 1
 *   00e681e8    bit 14: flags2 bit 6
 *   00e68212    bits 1|0: both words of baud_rate <= 0x10 (sls/sls/and.b, bpl
 *               -> invalid); whole longword compared / copied (0x00E68234)
 *   00e6824c    bit 2:  parity (+0x14) <= 3 unsigned, else invalid
 *   00e68272    bit 3:  stop_bits (+0x12) - 1 <= 2 unsigned, else invalid
 *   00e682a0    bit 4:  char_size (+0x10) <= 3 unsigned, else invalid
 *   00e682c6    bit 13: break_mask (+0x08) & 0xFFFFFFC0 must be 0, else invalid;
 *               the "unchanged" arm branches straight to the beq at 0x00E682F4
 *               with the flags of the andi.l
 *   00e682d4    move.l #0x360002,(A3) ; bra exit
 *   00e682f2    tst.l D2 ; beq exit
 *   00e682f6    pea (A3) ; move.l D2,-(SP) ; pea (-0x18,A6) ; move.l (A4),-(SP)
 *   00e68300    movea.l (0x40,A4),A0 ; jsr (A0)   ; set_params(context, &copy, mask, status)
 *   00e6830a    tst.l (A3) ; bne exit
 *   00e6830e    copy -> desc->params (5 longs + 1 word)
 *   00e68320    movem.l (-0x38,A6),{D2 A2 A3 A4} ; unlk ; rts
 */

#include "sio/sio_internal.h"

/*
 * One flag-bit selector (0x00E680F0 and the six that follow it): compare
 * bit `bit` of *field with the caller's, clear `selector` from the mask if
 * equal, otherwise copy the caller's bit in.
 */
static uint32_t sio_$merge_flag_bit(uint32_t *mask, uint32_t selector,
                                    uint32_t field, uint32_t caller_value,
                                    uint32_t bit)
{
    uint32_t mine = field & bit;
    uint32_t theirs = caller_value & bit;

    if (mine == theirs) {
        *mask &= ~selector;
        return field;
    }
    return (field & ~bit) | theirs;
}

void SIO_$K_SET_PARAM(int16_t *line_ptr, sio_params_t *params,
                      const uint32_t *change_mask_ptr, status_$t *status_ret)
{
    sio_desc_t *desc;
    uint32_t mask;              /* D2 */
    sio_params_t copy;          /* (-0x18,A6) */
    uint16_t hi, lo;

    /* 0x00E680BC-0x00E680D2 */
    desc = SIO_$I_GET_DESC(*line_ptr, status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    /* 0x00E680D6-0x00E680EE */
    mask = *change_mask_ptr;
    memcpy(&copy, &desc->params, sizeof(sio_params_t));

    /* 0x00E680F0-0x00E68210: the seven single-bit selectors */
    if (mask & 0x0020) copy.flags1 = sio_$merge_flag_bit(&mask, 0x0020, copy.flags1, params->flags1, 0x01);
    if (mask & 0x0040) copy.flags1 = sio_$merge_flag_bit(&mask, 0x0040, copy.flags1, params->flags1, 0x08);
    if (mask & 0x0800) copy.flags2 = sio_$merge_flag_bit(&mask, 0x0800, copy.flags2, params->flags2, 0x04);
    if (mask & 0x1000) copy.flags2 = sio_$merge_flag_bit(&mask, 0x1000, copy.flags2, params->flags2, 0x08);
    if (mask & 0x0200) copy.flags2 = sio_$merge_flag_bit(&mask, 0x0200, copy.flags2, params->flags2, 0x01);
    if (mask & 0x0400) copy.flags2 = sio_$merge_flag_bit(&mask, 0x0400, copy.flags2, params->flags2, 0x02);
    if (mask & 0x4000) copy.flags2 = sio_$merge_flag_bit(&mask, 0x4000, copy.flags2, params->flags2, 0x40);

    /* 0x00E68212-0x00E6824A: speed - both 16-bit halves at most 0x10 */
    if (mask & 0x0003) {
        hi = (uint16_t)(params->baud_rate >> 16);
        lo = (uint16_t)(params->baud_rate & 0xFFFF);
        if (!(lo <= 0x10 && hi <= 0x10)) {
            *status_ret = status_$sio_invalid_param;
            return;
        }
        if (copy.baud_rate == params->baud_rate) {
            mask &= ~(uint32_t)0x0003;
        } else {
            copy.baud_rate = params->baud_rate;
        }
    }

    /* 0x00E6824C-0x00E68270: parity 0..3 */
    if (mask & SIO_PARAM_PARITY) {
        if ((uint16_t)params->parity > 3) {
            *status_ret = status_$sio_invalid_param;
            return;
        }
        if (copy.parity == params->parity) {
            mask &= ~(uint32_t)SIO_PARAM_PARITY;
        } else {
            copy.parity = params->parity;
        }
    }

    /* 0x00E68272-0x00E6829E: stop bits 1..3 */
    if (mask & SIO_PARAM_STOP_BITS) {
        if ((uint32_t)((uint16_t)params->stop_bits - 1) > 2) {
            *status_ret = status_$sio_invalid_param;
            return;
        }
        if (copy.stop_bits == params->stop_bits) {
            mask &= ~(uint32_t)SIO_PARAM_STOP_BITS;
        } else {
            copy.stop_bits = params->stop_bits;
        }
    }

    /* 0x00E682A0-0x00E682C4: character size 0..3 */
    if (mask & SIO_PARAM_CHAR_SIZE) {
        if ((uint16_t)params->char_size > 3) {
            *status_ret = status_$sio_invalid_param;
            return;
        }
        if (copy.char_size == params->char_size) {
            mask &= ~(uint32_t)SIO_PARAM_CHAR_SIZE;
        } else {
            copy.char_size = params->char_size;
        }
    }

    /* 0x00E682C6-0x00E682F0: bits 6..31 of the caller's word must be clear */
    if (mask & 0x2000) {
        if ((params->break_mask & 0xFFFFFFC0u) != 0) {
            *status_ret = status_$sio_invalid_param;
            return;
        }
        if (params->break_mask == copy.break_mask) {
            mask &= ~(uint32_t)0x2000;
        } else {
            copy.break_mask = params->break_mask;
        }
    }

    /* 0x00E682F2-0x00E6831E */
    if (mask != 0) {
        ((sio_set_params_fn_t)ARCH_VA_TO_PTR(desc->set_params))(
            desc->context, &copy, mask, status_ret);
        if (*status_ret == status_$ok) {
            memcpy(&desc->params, &copy, sizeof(sio_params_t));
        }
    }
}
