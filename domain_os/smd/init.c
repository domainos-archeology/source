/*
 * smd/init.c - SMD_$INIT
 *
 * Initialisation of the SMD (Screen Management Display) subsystem.
 *
 * Original address: 0x00E34D2C, size 308 bytes.
 *
 * Structure of the original:
 *   0x00E34D34  EC_$INIT on the two standalone eventcounts SMD_EC_1/SMD_EC_2
 *   0x00E34D50  clr.w D0 / cmpi.w #4,D0 / bcc / jump table at 0x00E34D62
 *               D0 is *always* 0, so case 0 (0x00E34D6A) always runs and then
 *               falls into the per-unit loop; cases 1..3 all point straight at
 *               the loop (all three table entries are 0x0050).
 *   0x00E34DB2  per-unit loop; `clr.w D2w` + `dbf D2w` gives exactly ONE
 *               iteration, for unit 1.
 *   0x00E34E82  SMD_$INTERRUPT_INIT, the two globals, ML_$EXCLUSION_INIT
 */

#include "smd/smd_internal.h"
#include "prom/prom.h"

/*
 * Controller type word passed by reference to io_$probe.
 * Original: `pea (0xdc,PC)` at 0x00E34DD2 -> 0x00E34EB0, which holds the
 * 16-bit constant 0x0001.
 */
static const uint16_t smd_probe_type = 1;

/*
 * Values the case-0 prologue plants in the unit record.  The two buffer
 * pointers are formed as `lea (0x1748,A1)` / `lea (0x1788,A1)` with
 * A1 = &SMD_GLOBALS (0x00E34D6A, 0x00E34D7A).
 */
#define SMD_UNIT1_BUF_A_OFFSET 0x1748
#define SMD_UNIT1_BUF_B_OFFSET 0x1788

/* SAU2 display controller register base (0x00E34D8A move.l #0xff9800,...). */
#define SMD_SAU2_CTRL_REGS ((SMD_HW_REG_PTR)0x00FF9800u)

/* SAU2 display memory base (0x00E34D82 move.l #0xfc0000,...). */
#define SMD_SAU2_DISPLAY_BASE 0x00FC0000u

/*
 * SMD_$INIT - Initialise the SMD subsystem.
 */
void SMD_$INIT(void)
{
    smd_display_unit_t *rec;
    smd_display_hw_t *hw;
    uint16_t disp_type;
    uint32_t probe_result;     /* (-0x4,A6) */
    int8_t probe_status;       /* D0 */
    int16_t unit_loop;         /* D2 */
    int16_t unit_num;
    int16_t i;                 /* D0 in the inner dbf loop */

    /* 0x00E34D34 / 0x00E34D42 */
    EC_$INIT(&SMD_EC_1);
    EC_$INIT(&SMD_EC_2);

    /*
     * Case-0 prologue (0x00E34D6A-0x00E34DB0).  A0 is 0x00E2E3FC and every
     * store is at A0 + 0x10C + k, i.e. into the record for unit 1 at offset
     * 0xF4 + k (see smd_display_unit_t), except the store at (0x18,A0) which
     * is the record's +0x00 field, the hw pointer.
     *
     * The dispatch value is `clr.w D0`, so this is unconditional; the switch
     * only exists because the Pascal source selected between SAU flavours.
     */
    rec = smd_$unit_rec(1);

    /* 0x00E34D6E move.l A2,(0x10c,A0) with A2 = lea (0x1748,A1): the unit's
     * 8-entry font table (see smd_$reset_unit_display, 0x00E6D76C). */
    rec->font_table =
        (smd_font_entry_t *)((uint8_t *)&SMD_GLOBALS + SMD_UNIT1_BUF_A_OFFSET);
    /* 0x00E34D7E move.l A2,(0x110,A0) with A2 = lea (0x1788,A1) */
    rec->hdm_list =
        (smd_hdm_list_t *)((uint8_t *)&SMD_GLOBALS + SMD_UNIT1_BUF_B_OFFSET);
    /* 0x00E34D82 move.l #0xfc0000,(0x120,A0) */
    rec->display_base = SMD_SAU2_DISPLAY_BASE;
    /* 0x00E34D8A move.l #0xff9800,(0x114,A0) */
    rec->ctrl_regs = SMD_SAU2_CTRL_REGS;
    /*
     * 0x00E34D96 move.l #0xe27376,(0x18,A0)
     * The per-display hardware record and the display info entry are the same
     * 0x60-byte object; the image keeps only one copy at 0x00E27376.
     */
    rec->hw = (smd_display_hw_t *)&SMD_DISPLAY_INFO[0];
    /* 0x00E34D9E movea.l (0x18,A0),A2 / clr.w (A2) */
    rec->hw->display_type = 0;
    /* 0x00E34DA2 movea.l #0xe173d4,A2 / move.l (A2)+,(0x118,A0)
     *                                 / move.l (A2)+,(0x11c,A0) */
    rec->display_uid.high = smd_$unit_init_params[0];
    rec->display_uid.low = smd_$unit_init_params[1];

    /*
     * Per-unit loop (0x00E34DB2-0x00E34E7E).  A2 starts at 0x00E2E3FC + 0x10C,
     * i.e. the slot for unit 1, and D2 is cleared before the `dbf`, so the body
     * runs exactly once.
     */
    unit_num = 1;
    unit_loop = 0;
    do {
        rec = smd_$unit_rec(unit_num);

        /* 0x00E34DC2 movea.l (-0xf4,A3),A4 */
        hw = rec->hw;

        /*
         * 0x00E34DC6 clr.l (-0xf0,A3): a single longword clear that covers
         * both of the record's ASID words (+0x04 owner, +0x06 borrowed).
         */
        rec->owner_asid = 0;
        rec->borrowed_asid = 0;

        /*
         * 0x00E34DCA pea (-0x4,A6) / pea (0x8,A3) / pea (0xdc,PC) / jsr
         * io_$probe -- the middle argument is the address of the record's
         * controller register field, which io_$probe rewrites.
         */
        probe_status = io_$probe((void *)&smd_probe_type,
                                 (void *)&rec->ctrl_regs,
                                 &probe_result);

        /* 0x00E34DE0 tst.b D0b / bpl / move.w #2,(A4) */
        if (probe_status < 0) {
            hw->display_type = 2;
        }

        /* 0x00E34DE8 clr.w (0x52,A4) / clr.w (0x4e,A4) */
        hw->min_y = 0;
        hw->min_x = 0;

        /* 0x00E34DF0 move.w (A4),D0w / cmpi.w #1 / cmpi.w #2 */
        disp_type = hw->display_type;
        if (disp_type == SMD_DISP_TYPE_MONO_LANDSCAPE) {
            /* 0x00E34E00: 800 wide x 1024 tall */
            hw->max_y = 0x3FF;
            hw->max_x = 0x31F;
        } else if (disp_type == SMD_DISP_TYPE_MONO_PORTRAIT) {
            /* 0x00E34E0E: 1024 wide x 800 tall */
            hw->max_y = 0x31F;
            hw->max_x = 0x3FF;
        }
        /* Any other type keeps whatever bounds it already has. */

        /* 0x00E34E1A clr.w (-0xea,A3) */
        rec->field_0a = 0;

        /* 0x00E34E1E clr.w (0x2,A4) */
        hw->lock_state = 0;

        /* 0x00E34E22 / 0x00E34E2E */
        EC_$INIT(&hw->lock_ec);
        EC_$INIT(&hw->op_ec);

        /* 0x00E34E3A clr.l (0x1c,A4) */
        hw->field_1c = 0;
        /* 0x00E34E3E clr.b (0x3c,A4) */
        hw->tracking_enabled = 0;
        /* 0x00E34E42 clr.b (0x20,A4) */
        hw->field_20 = 0;
        /* 0x00E34E46 move.l #0x10000,(0x22,A4): one longword store covering
         * video_flags (0x22) = 0x0001 and field_24 (0x24) = 0x0000. */
        hw->video_flags = 0x0001;
        hw->field_24 = 0x0000;
        /* 0x00E34E4E clr.b (0x3e,A4) */
        hw->field_3e = 0;
        /* 0x00E34E52 clr.w (0x5e,A4) */
        hw->field_5e = 0;

        /* 0x00E34E56 pea (0x40,A4) / jsr EC_$INIT */
        EC_$INIT(&hw->cursor_ec);

        /* 0x00E34E62 clr.w (0x4c,A4) */
        hw->field_4c = 0;

        /*
         * 0x00E34E66 moveq #0x38,D0 / moveq #4,D1
         * 0x00E34E6C lea (0,A3,D1*1),A0 / clr.l (-0xe8,A0) / addq.l #4,D1
         *            dbf D0w -> 57 iterations, clearing A3-0xE4 .. A3-0x04,
         *            i.e. the record's mapped_addresses[0..56].
         */
        for (i = 0; i < 57; i++) {
            rec->mapped_addresses[i] = 0;
        }

        /* 0x00E34E7A lea (0x10c,A2),A2 / dbf D2w */
        unit_num++;
    } while (unit_loop-- != 0);

    /* 0x00E34E82 */
    SMD_$INTERRUPT_INIT();

    /* 0x00E34E88 movea.l #0xe82b8c,A0 / clr.l (0xc8,A0)
     *                                 / move.l #0xd69,(0xd8,A0) */
    SMD_GLOBALS.blank_time = 0;
    SMD_GLOBALS.blank_timeout = 0xD69; /* 3433 */

    /* 0x00E34E9A pea (0xe2e520).l / jsr ML_$EXCLUSION_INIT */
    ML_$EXCLUSION_INIT(&ml_$exclusion_t_00e2e520);
}
