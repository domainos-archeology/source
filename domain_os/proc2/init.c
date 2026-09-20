/*
 * PROC2_$INIT - Initialise the PROC2 subsystem
 *
 * Re-emitted from the image (0x00E303D8..0x00E30890, 1210 bytes).
 *
 * Builds the UID table, the pid map, the process-group table and the
 * free list, sets up entry 1 as the init process, maps its creation
 * record and initial stack, records the boot flags, hands control to the
 * tape/floppy boot probes, resolves `node_data/proc_dir, and finally
 * locks, samples and re-maps /sys/boot_shell.
 *
 * Frame (link.w A6,-0x50; A5 = 0xE35034 is loaded and never used):
 *   (0x8,A6)  boot_flags  -> D4  pointer to the caller's ws_mode word
 *   (0xC,A6)  status_ret  -> D3  handed to every callee that reports
 *   A6-0x8    boot shell UID          A6-0x18  10-byte boot shell header
 *   A6-0x1C   tape/floppy entry point A6-0x24  FILE_$LOCK info (8 bytes)
 *   A6-0x2C   MST map info (8 bytes)  A6-0x30  mapped header VA
 *   A6-0x36/-0x38 min/max priority    A6-0x4E  boot probe result byte
 *
 * Return (D0): see the prototype comment in proc2/proc2.h.  Sole caller:
 * OS_$INIT 0x00E34698.
 *
 * Original address: 0x00e303d8
 */

#include "proc2/proc2_internal.h"

/*
 * Constant pool in the code region, 0x00E30892..0x00E3094F (cell = pea
 * address + 2 + displacement; bytes read from the image):
 *
 *   0x00E30892  00 00        boolean FALSE / rights 0 / concurrency 0
 *   0x00E30894  00 03        word 3   (MST_$MAP / MST_$MAP_AT mode)
 *   0x00E30896  00 00        word 0   (FILE_$LOCK lock index)
 *   0x00E30898  00 01        word 1   (FILE_$LOCK lock mode)
 *   0x00E3089A  "unable to resolve%"
 *   0x00E308AC  00 0f        word 15  (length of "/sys/boot_shell")
 *   0x00E308AE  00 13        word 19  (length of "`node_data/proc_dir")
 *   0x00E308B0  00 0c        word 12  (length of "initial area")
 *   0x00E308B2  ff 00        boolean TRUE
 *   0x00E308B4  "unable to map%"
 *   0x00E308C2  00 14        word 20  (length of "creation record area")
 *   0x00E308C4  00 00 40 00  longword 0x4000 (MST_$MAP_AREA_AT arg 3)
 *   0x00E308C8  7f ff ff ff  longword 0x7FFFFFFF (MST_$MAP_AT length)
 *   0x00E308CC  "unable to unmap%"
 *   0x00E308DC  "unable to map for read%"
 *   0x00E308F4  00 00 00 0a  longword 10 (MST_$MAP length: the header)
 *   0x00E308F8  00 00 00 00  longword 0 (start offset / extend)
 *   0x00E308FC  "unable to lock%"
 *   0x00E3090C  "/sys/boot_shell"
 *   0x00E3091C  "`node_data/proc_dir"   (backquote: the node_data root)
 *   0x00E30930  "initial area"
 *   0x00E3093C  "creation record area"
 *
 * The '%' is OS_$BOOT_ERRCHK's format terminator; the paths and area
 * names are passed with a separate length word.
 */
static const int8_t   proc2_init_false_00e30892     = 0;
static const uint16_t proc2_init_mode_3_00e30894    = 3;
static const uint16_t proc2_init_lock_index_00e30896 = 0;
static const uint16_t proc2_init_lock_mode_00e30898 = 1;
static const char     proc2_init_msg_resolve_00e3089a[] = "unable to resolve%";
static const int16_t  proc2_init_len_15_00e308ac    = 15;
static const int16_t  proc2_init_len_19_00e308ae    = 19;
static const int16_t  proc2_init_len_12_00e308b0    = 12;
static const int8_t   proc2_init_true_00e308b2      = (int8_t)0xFF;
static const char     proc2_init_msg_map_00e308b4[] = "unable to map%";
static const int16_t  proc2_init_len_20_00e308c2    = 20;
static const uint32_t proc2_init_size_00e308c4      = 0x00004000;
static const uint32_t proc2_init_maxlen_00e308c8    = 0x7FFFFFFF;
static const char     proc2_init_msg_unmap_00e308cc[] = "unable to unmap%";
static const char     proc2_init_msg_map_read_00e308dc[] = "unable to map for read%";
static const uint32_t proc2_init_hdr_len_00e308f4   = 10;
static const uint32_t proc2_init_zero_00e308f8      = 0;
static const char     proc2_init_msg_lock_00e308fc[] = "unable to lock%";
static const char     proc2_init_boot_shell_path[]  = "/sys/boot_shell";       /* 0x00E3090C */
static const char     proc2_init_proc_dir_path[]    = "`node_data/proc_dir";   /* 0x00E3091C */
static const char     proc2_init_initial_area[]     = "initial area";          /* 0x00E30930 */
static const char     proc2_init_cr_area[]          = "creation record area";  /* 0x00E3093C */

/*
 * The first ten bytes of /sys/boot_shell, copied out of the read-only
 * mapping at 0x00E307F8..0x00E30802 (long, long, word) and handed by
 * reference to MST_$MAP_AT as its start record.
 */
typedef struct __attribute__((packed)) proc2_boot_shell_hdr_t {
    uint32_t word_0;    /* A6-0x18 */
    uint32_t word_4;    /* A6-0x14: the value PROC2_$INIT returns */
    uint16_t word_8;    /* A6-0x10 */
} proc2_boot_shell_hdr_t;
_Static_assert(sizeof(proc2_boot_shell_hdr_t) == 10, "proc2_boot_shell_hdr_t is 10 bytes");

/* Process-table extents (bead source-nm9e): slots 2..70 go on the free list. */
#define P2_FIRST_FREE_ENTRY     2
#define P2_LAST_ENTRY           P2_INFO_TABLE_SIZE

/*
 * 0x00E3047A-0x00E3048A clears the ref_count word of pgroup slots 1..70:
 * A0 = 0xEA551C + 8, `clr.w (0x3f30,A0)` = 0xEA9454 = PGROUP_TABLE[1],
 * `moveq #0x45` against the dbf = 70 iterations.  The table therefore has
 * 71 slots (0xEA944C + 71*8 = 0xEA9684 = the end of PROC2_$DATA).
 */
#define P2_PGROUP_LAST_ENTRY    70
_Static_assert(P2_PGROUP_LAST_ENTRY < PGROUP_TABLE_SIZE, "pgroup slot 70 must exist");

uint32_t PROC2_$INIT(uint16_t *boot_flags, status_$t *status_ret)
{
    uint32_t result;                 /* D0 at the exits */
    int16_t i;
    proc2_info_t *entry;
    proc2_info_t *init_entry;        /* A2 = 0xEA551C = entry(1) */
    uint16_t min_pri, max_pri;       /* A6-0x36 / A6-0x38 */
    int8_t mmu_mode;                 /* D0b */
    uint8_t dtty_bit;
    uid_t boot_shell_uid;            /* A6-0x8 */
    uint32_t boot_entry_point;       /* A6-0x1C */
    int8_t probe_result;             /* A6-0x4E */
    uint8_t lock_info[8];            /* A6-0x24 */
    uint8_t map_info[8];             /* A6-0x2C */
    uint32_t mapped_va;              /* A6-0x30 */
    proc2_boot_shell_hdr_t hdr;      /* A6-0x18 */
    proc2_boot_shell_hdr_t *mapped;  /* A2, reused */
    uint16_t *stack_word;

    /* 0x00E303F4-0x00E30410: UID_$GEN(0xE7BE8C) and UID_$GEN(0xE7BE9C) */
    UID_$GEN(&proc2_system_uid);
    UID_$GEN(&PROC2_$UID[1]);

    /* 0x00E30412-0x00E30434: both words 0x10, `st` = SET on PROC1_$CURRENT;
     * pushes (-0x38,A6) then (-0x36,A6), so arg 3 is A6-0x36, arg 4 A6-0x38 */
    min_pri = 0x10;
    max_pri = 0x10;
    PROC1_$SET_PRIORITY(PROC1_$CURRENT, PROC1_SET_PRIORITY_SET, &min_pri, &max_pri);

    /* 0x00E30438-0x00E30446: PROC2_$UID[0] = proc2_system_uid */
    PROC2_$UID[0].high = proc2_system_uid.high;
    PROC2_$UID[0].low = proc2_system_uid.low;

    /*
     * 0x00E3044A-0x00E30462: moveq #0x37 / dbf = 56 iterations starting at
     * (0x10,A0) with A0 = 0xE7BE94 -> slots 2..57.
     */
    for (i = 2; i <= 57; i++) {
        PROC2_$UID[i].high = proc2_system_uid.high;
        PROC2_$UID[i].low = proc2_system_uid.low;
    }

    /*
     * 0x00E30466-0x00E30476: moveq #0x3e / dbf = 63 iterations; the first
     * word is 0xEA5520 + 0x3EB6 = 0xEA93D6 = P2_PID_TO_INDEX(2), so pids
     * 2..64 are cleared.
     */
    for (i = 2; i <= 64; i++) {
        PROC2_$PID_TO_INDEX[i] = 0;
    }

    /* 0x00E3047A-0x00E3048A: pgroup slots 1..70, ref_count only */
    for (i = 1; i <= P2_PGROUP_LAST_ENTRY; i++) {
        PGROUP_ENTRY(i)->ref_count = 0;
    }

    /* 0x00E3048E-0x00E30494: P2_FREE_LIST_HEAD (0xE7C066) = 2 */
    P2_FREE_LIST_HEAD = P2_FIRST_FREE_ENTRY;

    /*
     * 0x00E3049A-0x00E304D4: moveq #0x44 / dbf = 69 iterations, D1 = 2..70,
     * A1 = entry(i) + 0xE4 (starts at 0xEA551C + 0x1C8).
     */
    for (i = P2_FIRST_FREE_ENTRY; i <= P2_LAST_ENTRY; i++) {
        entry = P2_INFO_ENTRY(i);

        /* 0x00E304B4/0x00E304B6: entry+0x12 = i + 1, UNCONDITIONALLY */
        entry->next_index = (uint16_t)(i + 1);

        /* 0x00E304BA-0x00E304C0: entry+0x08 = UID_$NIL */
        entry->parent_uid.high = UID_$NIL.high;
        entry->parent_uid.low = UID_$NIL.low;

        /* 0x00E304C4: andi.w #-0x181 -> clears exactly 0x0180 */
        entry->flags &= (uint16_t)~PROC2_FLAG_VALID;

        /* 0x00E304CA: entry+0x1C = i (slot identity, bead source-e8c8) */
        entry->self_index = (uint16_t)i;
    }

    /* 0x00E304D8: clr.w 0xEA92A2 = entry(70)+0x12 -- terminates the list */
    P2_INFO_ENTRY(P2_LAST_ENTRY)->next_index = 0;

    /* 0x00E304DE-0x00E304E4: P2_INFO_ALLOC_PTR (0xE7C064) = 1 */
    P2_INFO_ALLOC_PTR = 1;

    /* 0x00E304EA: 0xEA93D4 = P2_PID_TO_INDEX(1) = 1 */
    PROC2_$PID_TO_INDEX[1] = 1;

    /* 0x00E304F2: A2 = 0xEA551C = entry(1) */
    init_entry = P2_INFO_ENTRY(1);

    init_entry->next_index = 0;                 /* 0x00E304F8: clr.l (0x12) */
    init_entry->pad_14 = 0;
    init_entry->asid = 1;                       /* 0x00E304FC */
    init_entry->self_index = 1;                 /* 0x00E30502 */

    /* 0x00E30508-0x00E3050E: entry+0x00 = 0xE7BE9C = PROC2_$UID[1] */
    init_entry->uid.high = PROC2_$UID[1].high;
    init_entry->uid.low = PROC2_$UID[1].low;

    init_entry->level1_pid = PROC1_$CURRENT;    /* 0x00E30512 */
    init_entry->cleanup_flags = 0;              /* 0x00E3051A */
    init_entry->first_child_idx = 0;            /* 0x00E3051E: clr.l (0x20) */
    init_entry->next_child_sibling = 0;
    init_entry->parent_pgroup_idx = 0;          /* 0x00E30522 */
    init_entry->first_debug_target_idx = 0;     /* 0x00E30526: clr.l (0x24) */
    init_entry->debugger_idx = 0;
    init_entry->next_debug_target_idx = 0;      /* 0x00E3052A */
    init_entry->upid = 1;                       /* 0x00E3052E */
    init_entry->session_id = 0;                 /* 0x00E30534 */
    init_entry->pgroup_table_idx = 0;           /* 0x00E30538 */

    /* 0x00E3053C-0x00E3054E: 0x70, 0x74, 0x78, 0x7C, 0x80, 0x84 */
    init_entry->sig_pending = 0;
    init_entry->sig_blocked_1 = 0;
    init_entry->sig_blocked_2 = 0;
    init_entry->sig_mask_3 = 0;
    init_entry->sig_mask_2 = 0;
    init_entry->sig_mask_1 = 0;

    /* 0x00E30552: andi.b #-0x45 on the HIGH byte -> flags &= 0xBBFF */
    init_entry->flags &= 0xBBFF;

    init_entry->pad_18[0] = 0;                  /* 0x00E30558: clr.l (0x18) */
    init_entry->pad_18[1] = 0;
    init_entry->pad_94 = 0;                     /* 0x00E3055C */

    /* 0x00E30560: bclr.b #5 high byte; 0x00E30566: bset.b #7 high byte;
     * 0x00E3056C: andi.w #-0x1a51 */
    init_entry->flags &= (uint16_t)~0x2000;
    init_entry->flags |= 0x8000;
    init_entry->flags &= 0xE5AF;

    init_entry->name_len = 0x21;                /* 0x00E30572 */

    /* 0x00E30578: entry+0x6C = AS_$CR_REC (0xE2B930) */
    init_entry->cr_rec_2 = AS_$CR_REC;

    /* 0x00E30580-0x00E3058A: entry+0x60 = UID_$NIL */
    init_entry->tty_uid.high = UID_$NIL.high;
    init_entry->tty_uid.low = UID_$NIL.low;

    /* 0x00E3058E-0x00E30598: entry+0x4C = UID_$NIL; 0x00E3059C: +0x54 = 0 */
    init_entry->acct_uid.high = UID_$NIL.high;
    init_entry->acct_uid.low = UID_$NIL.low;
    init_entry->acct_info_len = 0;

    /* 0x00E305A0-0x00E305D4: the slot's EC pair, indexed by entry+0x1C */
    EC_$INIT(PROC_FORK_EC(init_entry->self_index));
    EC_$INIT(PROC_CR_REC_EC(init_entry->self_index));

    /*
     * 0x00E305D6-0x00E305F6, pushes right to left:
     *   D3, pea (0x8,A2), &0x00E30892, &0x00E308C4, #0xE2B96C, #0xE2B930
     * = MST_$MAP_AREA_AT(&AS_$CR_REC, &AS_$CR_REC_FILE_SIZE, &0x4000,
     *                    &FALSE, &entry->parent_uid, status_ret)
     */
    MST_$MAP_AREA_AT(&AS_$INFO.cr_rec, &AS_$INFO.cr_rec_file_size,
                     (void *)&proc2_init_size_00e308c4,
                     (void *)&proc2_init_false_00e30892,
                     &init_entry->parent_uid, status_ret);

    /* 0x00E305FA-0x00E30614: ERRCHK("unable to map%", "creation record area", &20) */
    result = (uint32_t)OS_$BOOT_ERRCHK(proc2_init_msg_map_00e308b4, proc2_init_cr_area,
                                       (short *)&proc2_init_len_20_00e308c2, status_ret);
    if ((int8_t)result >= 0) {
        return result;                          /* 0x00E30614: bpl.w exit */
    }

    /*
     * 0x00E30618-0x00E30638: MST_$MAP_AREA_AT(&AS_$STACK_FILE_LOW,
     *   &AS_$INIT_STACK_FILE_SIZE, &0x4000, &TRUE, &entry->stack_uid, status_ret)
     */
    MST_$MAP_AREA_AT(&AS_$INFO.stack_file_low, &AS_$INFO.init_stack_file_size,
                     (void *)&proc2_init_size_00e308c4,
                     (void *)&proc2_init_true_00e308b2,
                     &init_entry->stack_uid, status_ret);

    /* 0x00E3063C-0x00E30656: ERRCHK("unable to map%", "initial area", &12) */
    result = (uint32_t)OS_$BOOT_ERRCHK(proc2_init_msg_map_00e308b4, proc2_init_initial_area,
                                       (short *)&proc2_init_len_12_00e308b0, status_ret);
    if ((int8_t)result >= 0) {
        return result;
    }

    /* 0x00E3065A: ori.w #0x180 */
    init_entry->flags |= PROC2_FLAG_VALID;

    /* 0x00E30660: entry+0x68 = AS_$STACK_HIGH (0xE2B950) */
    init_entry->cr_rec = AS_$STACK_HIGH;

    /*
     * 0x00E30668-0x00E306A0: boot flags word at 0xE7C068.
     *   andi.w #-0x4000          -> keep bits 15..14
     *   MMU_$NORMAL_MODE -> D0b; andi.b #0x7f (high byte); or.b (D0 & 0x80)
     *                            -> bit 15 = bit 7 of the mode byte
     *   DTTY_$USE_DTTY: not.b; lsr.b #7; andi.b #-0x41 (high byte); lsl.b #6; or.b
     *                            -> bit 14 = NOT bit 7 of DTTY_$USE_DTTY
     */
    proc2_boot_flags = (int16_t)(proc2_boot_flags & 0xC000);
    mmu_mode = MMU_$NORMAL_MODE();
    proc2_boot_flags = (int16_t)((proc2_boot_flags & 0x7FFF) |
                                 (((uint16_t)mmu_mode & 0x80) << 8));
    dtty_bit = (uint8_t)((uint8_t)~(uint8_t)DTTY_$USE_DTTY >> 7);
    proc2_boot_flags = (int16_t)((proc2_boot_flags & 0xBFFF) |
                                 ((uint16_t)dtty_bit << 14));

    /*
     * 0x00E306A4-0x00E306B6: A0 = entry+0x68 (the stack top) - 6;
     * clr.l (2,A0); move.w boot_flags,(A0).
     */
    stack_word = (uint16_t *)ARCH_VA_TO_PTR(init_entry->cr_rec - 6);
    stack_word[1] = 0;
    stack_word[2] = 0;
    stack_word[0] = (uint16_t)proc2_boot_flags;

    /*
     * 0x00E306BA-0x00E306DE: btst.b #0,(0x1,A1) -- bit 0 of the low byte of
     * the caller's word.  TAPE_$BOOT(&entry_point, status_ret) returns a
     * Domain boolean in D0b (no result slot); TRUE exits with D0 = the
     * entry point.
     */
    if ((*boot_flags & 0x0001) != 0) {
        probe_result = TAPE_$BOOT(&boot_entry_point, status_ret);
        result = boot_entry_point;              /* 0x00E306D6 */
        if (probe_result < 0) {
            return result;                      /* 0x00E306DE: bmi.w exit */
        }
    }

    /* 0x00E306E2-0x00E30706: the same shape for bit 1 and FLOP_$BOOT */
    if ((*boot_flags & 0x0002) != 0) {
        probe_result = FLOP_$BOOT(&boot_entry_point, status_ret);
        result = boot_entry_point;              /* 0x00E306FE */
        if (probe_result < 0) {
            return result;
        }
    }

    /*
     * 0x00E3070A-0x00E30738: NAME_$RESOLVE("`node_data/proc_dir", &19,
     * &proc2_proc_dir_uid (0xE7BE84), status_ret); on any error the UID
     * becomes UID_$NIL.
     */
    NAME_$RESOLVE((char *)proc2_init_proc_dir_path, (int16_t *)&proc2_init_len_19_00e308ae,
                  &proc2_proc_dir_uid, status_ret);
    if (*status_ret != status_$ok) {
        proc2_proc_dir_uid.high = UID_$NIL.high;
        proc2_proc_dir_uid.low = UID_$NIL.low;
    }

    /* 0x00E3073C-0x00E30750: NAME_$RESOLVE("/sys/boot_shell", &15, &uid, status) */
    NAME_$RESOLVE((char *)proc2_init_boot_shell_path, (int16_t *)&proc2_init_len_15_00e308ac,
                  &boot_shell_uid, status_ret);

    /* 0x00E30754-0x00E3076E: ERRCHK("unable to resolve%", "/sys/boot_shell", &15) */
    result = (uint32_t)OS_$BOOT_ERRCHK(proc2_init_msg_resolve_00e3089a, proc2_init_boot_shell_path,
                                       (short *)&proc2_init_len_15_00e308ac, status_ret);
    if ((int8_t)result >= 0) {
        return result;
    }

    /* 0x00E30772-0x00E3078E: FILE_$LOCK(&uid, &0, &1, &rights 0, &lock_info, status) */
    FILE_$LOCK(&boot_shell_uid, &proc2_init_lock_index_00e30896, &proc2_init_lock_mode_00e30898,
               (const uint8_t *)&proc2_init_false_00e30892, lock_info, status_ret);

    /* 0x00E30792-0x00E307AC: ERRCHK("unable to lock%", "/sys/boot_shell", &15) */
    result = (uint32_t)OS_$BOOT_ERRCHK(proc2_init_msg_lock_00e308fc, proc2_init_boot_shell_path,
                                       (short *)&proc2_init_len_15_00e308ac, status_ret);
    if ((int8_t)result >= 0) {
        return result;
    }

    /*
     * 0x00E307B0-0x00E307D8: A2 = MST_$MAP(&uid, &start 0, &length 10,
     * &mode 3, &extend 0, &concurrency 0, &map_info, status_ret) -- maps
     * just the ten-byte header for reading.
     */
    mapped = (proc2_boot_shell_hdr_t *)MST_$MAP(&boot_shell_uid,
                                                (uint32_t *)&proc2_init_zero_00e308f8,
                                                (uint32_t *)&proc2_init_hdr_len_00e308f4,
                                                (uint16_t *)&proc2_init_mode_3_00e30894,
                                                (uint32_t *)&proc2_init_zero_00e308f8,
                                                (uint8_t *)&proc2_init_false_00e30892,
                                                map_info, status_ret);

    /* 0x00E307DA-0x00E307F4: ERRCHK("unable to map for read%", "/sys/boot_shell", &15) */
    result = (uint32_t)OS_$BOOT_ERRCHK(proc2_init_msg_map_read_00e308dc, proc2_init_boot_shell_path,
                                       (short *)&proc2_init_len_15_00e308ac, status_ret);
    if ((int8_t)result >= 0) {
        return result;
    }

    /* 0x00E307F8-0x00E30802: ten bytes of the header; 0x00E30804: A6-0x30 = A2 */
    for (i = 0; i < 10; i++) {
        ((uint8_t *)&hdr)[i] = ((uint8_t *)mapped)[i];
    }
    mapped_va = ARCH_PTR_TO_VA(mapped);

    /* 0x00E30808-0x00E3081C: MST_$UNMAP(&uid, &mapped_va, &map_info, status) */
    MST_$UNMAP(&boot_shell_uid, &mapped_va, (uint32_t *)map_info, status_ret);

    /* 0x00E30820-0x00E3083A: ERRCHK("unable to unmap%", "/sys/boot_shell", &15) */
    result = (uint32_t)OS_$BOOT_ERRCHK(proc2_init_msg_unmap_00e308cc, proc2_init_boot_shell_path,
                                       (short *)&proc2_init_len_15_00e308ac, status_ret);
    if ((int8_t)result >= 0) {
        return result;
    }

    /*
     * 0x00E3083C-0x00E30864: MST_$MAP_AT(&hdr, &uid, &start 0,
     * &length 0x7FFFFFFF, &mode 3, &extend 0, &concurrency 0, &map_info,
     * status_ret) -- the header record says where the shell wants to live.
     */
    MST_$MAP_AT(&hdr, &boot_shell_uid,
                (void *)&proc2_init_zero_00e308f8,
                (void *)&proc2_init_maxlen_00e308c8,
                (void *)&proc2_init_mode_3_00e30894,
                (void *)&proc2_init_zero_00e308f8,
                (void *)&proc2_init_false_00e30892,
                map_info, status_ret);

    /* 0x00E30868-0x00E30882: ERRCHK("unable to map%", "/sys/boot_shell", &15) */
    result = (uint32_t)OS_$BOOT_ERRCHK(proc2_init_msg_map_00e308b4, proc2_init_boot_shell_path,
                                       (short *)&proc2_init_len_15_00e308ac, status_ret);
    if ((int8_t)result >= 0) {
        return result;
    }

    /* 0x00E30884: D0 = A6-0x14, the header's second longword */
    return hdr.word_4;
}
