/*
 * os/init.c - OS_$INIT
 *
 * Original address: 0x00E337F4, 4054 bytes.
 *
 * The whole of Domain/OS boot: it copies the two records the bootstrap hands
 * it, installs the exception vectors the boot info table describes, brings up
 * memory management, discovers whether this node is diskless, mounts the boot
 * volume (or fetches its parameters from the mother node), maps and wires the
 * OS image, starts the system processes, and finally hands off to proc2.
 *
 * The module base is a data table, not this function: `lea (0xe351f4).l,A5`
 * at 0x00E337FC makes A5 = BOOT_INFO_TABLE.
 *
 * Every basic block of the original is accounted for; the addresses in the
 * comments say which instructions each statement stands for.
 */

#include "os/os_internal.h"

/*
 * ============================================================================
 * Constant cells in the OS_$INIT code region, passed by `pea (d,PC)`
 * ============================================================================
 *
 * Domain Pascal passes `var`/`const` parameters by address, so a literal
 * argument becomes a cell in the code region whose address is pushed.  Each
 * of these is one such cell; the address is where it lives in the image.
 */

/* 0x00E33774: a zero longword.  Handed to IO_$INIT as its first argument,
 * to CRASH_SYSTEM as a status, to FILE_$SET_REFCNT as the new refcount, and
 * to VTOP_OR_CRASH as the virtual address to translate (i.e. page 0). */
static const status_$t os_$init_zero_long = 0;

/* 0x00E337D2: the word 1.  The length of the "/" path handed to
 * NAME_$SET_WDIR, io_$probe's width, and SMD_$INQ_DISP_TYPE's unit number. */
static const int16_t os_$init_one_word = 1;

/* 0x00E347CA: the word 0.  RING_$GET_ID's argument, IO_$GET_DCTE's ctype and
 * cnum, VOLX_$MOUNT's bus number, OS_$CHKSUM's first and third arguments. */
static const uint16_t os_$init_zero_word = 0;

/* 0x00E348E2: the word 4 -- FILE_$LOCK's lock mode. */
static const uint16_t os_$init_four_word = 4;

/* 0x00E348E4: the word 2 -- OS_$CHKSUM's second argument and the second
 * display unit handed to SMD_$INQ_DISP_TYPE. */
static const uint16_t os_$init_two_word = 2;

/* 0x00E349B2: the Domain boolean TRUE (0xFF).  VOLX_$MOUNT's salvage_ok on
 * the retry, FILE_$LOCK's third argument, OS_$INSTALL_DISPLAY_ASTE's touch
 * flag. */
static const boolean os_$init_true = true;

/* 0x00E349DA: the Domain boolean FALSE (0x00).  VOLX_$MOUNT's salvage_ok on
 * the first attempt and its write_prot on both, FILE_$LOCK's fourth
 * argument. */
static const boolean os_$init_false = false;

/* 0x00E34AD8 and 0x00E34ADC: the first two arguments CAL_$VERIFY (0x00E68380)
 * is given.  Their raw contents are 0x00192549 and 0x35202000; the widths the
 * callee reads them at are not established (0x00E34ADA also reads as the vfmt
 * string "%I5  ", so the two cells may overlap a shared literal).
 * TODO(source-rcd6): decode CAL_$VERIFY's first two parameters. */
static const uint32_t os_$init_cal_verify_p1 = 0x00192549;
static const uint32_t os_$init_cal_verify_p2 = 0x35202000;

/* 0x00E34A9C / 0x00E34AA0: the display aperture handed to io_$probe and
 * OS_$INSTALL_DISPLAY_ASTE -- 128 KB at physical 0x00FC0000. */
static const uint32_t os_$init_display_size = 0x00020000;
static const uint32_t os_$init_display_addr = 0x00FC0000;

/* 0x00E347CC: the working directory path.  One character; its length comes
 * from os_$init_one_word above. */
static const char os_$init_root_path[] = "/";

/*
 * ============================================================================
 * Status constants in the code region
 * ============================================================================
 */
/* 0x00E349DC */
static const status_$t os_$init_wired_too_big_err = 0x00040003;
/* 0x00E34B10 */
static const status_$t os_$init_no_calendar_err = 0x001B0004;

/*
 * ============================================================================
 * Message strings (vfmt format strings; %/ = newline, %. = flush, %$ = prompt)
 * ============================================================================
 */
/*
 * 0x00E349B4.  Note the terminating NUL is the byte at 0x00E349DA, which is
 * also the FALSE cell os_$init_false names -- the compiler shared them.
 */
static const char msg_salvage[] = "%/%/%/%/BOOT VOLUME NEEDS SALVAGING.%.";

/*
 * 0x00E34AE4.  Its NUL is the first byte of the status at 0x00E34B10
 * (os_$init_no_calendar_err), likewise shared.
 */
static const char msg_proceed_risk[] =
    "Proceed to bring up OS (and risk volume)? %$";

/* 0x00E34AE0: {0x0001, 0x0005}, the status the salvage refusal crashes with */
static const status_$t os_$init_salvage_err = 0x00010005;

/*
 * The "no paging file on the boot device" banner, in the order OS_$INIT
 * prints it (0x00E33EF6 .. 0x00E33F3E).  The first three run into one
 * another: 0x00E34978's text has no NUL of its own and simply continues to
 * 0x00E349B1, and the two run-of-newlines strings are tails of it.
 */
static const char msg_nopf_1[] = /* 0x00E349A0 */
    "%/%/%/%/%/%/%/%/%.";
static const char msg_nopf_2[] = /* 0x00E3499E */
    "%.%/%/%/%/%/%/%/%/%.";
static const char msg_nopf_3[] = /* 0x00E34978 */
    "Boot device has no OS paging file.%/%.%.%/%/%/%/%/%/%/%/%.";
static const char msg_nopf_4[] = /* 0x00E34936 */
    "see the Installation Procedures chapter in the Release Document%.";
static const char msg_nopf_5[] = /* 0x00E34AA4 */
    "for information on how to correct this problem.%/%.";
static const char msg_nopf_6[] = /* 0x00E348F4 */
    "For now, the OS will NOT page, which may cause some performance%.";
static const char msg_nopf_7[] = /* 0x00E348E6 */
    "degradation%.";

/*
 * The "paging file too small" banner, in the order OS_$INIT prints it
 * (0x00E344A2 .. 0x00E344FA).  msg_small_1 is 38 "%/" pairs followed by
 * "%." (0x00E34894 .. 0x00E348E1); msg_small_3 and msg_proceed_prompt have no NUL
 * of their own, each ending at the "%." that starts the next literal.
 */
static const char msg_small_1[] = /* 0x00E34894 */
    "%/%/%/%/%/%/%/%/%/%/%/%/%/%/%/%/%/%/%/"
    "%/%/%/%/%/%/%/%/%/%/%/%/%/%/%/%/%/%/%/%.";
static const char msg_small_2[] = /* 0x00E34A58 */
    "Unable to initialize DOMAIN_OS - The OS paging file is too small.%.";
static const char msg_small_3[] = /* 0x00E34856 */
    "For information on how to correct this, see the Installation%.";
static const char msg_small_4[] = /* 0x00E34828 */
    "Procedures chapter in the Release Document.%.";
static const char msg_small_5[] = /* 0x00E34A14 */
    "The OS will NOT page, which may cause some performance degradation%.";

/*
 * 0x00E3480E.  Printed both as the last line of the "too small" banner
 * (0x00E344D2) and after the node-number mismatch (0x00E3463C).
 */
static const char msg_proceed_prompt[] = "Do you want to proceed? %$";

/* The node-number mismatch (0x00E3460E .. 0x00E34640) */
static const char msg_node_1[] = /* 0x00E347E4 */
    "%/The node number of this node differs %$";
static const char msg_node_2[] = /* 0x00E347CE */
    "from that stored on%.";
static const char msg_node_3[] = /* 0x00E349E0, the ERROR_$PRINT format */
    "the boot volume.  Prom node #%h, stored node #%h.%.";

/*
 * ============================================================================
 * The two records the bootstrap passes
 * ============================================================================
 */

/*
 * Boot record (param_1): nine longwords.  OS_$INIT copies it twice --
 * 0x00E33802 into a scratch copy at A6-0x158, then 0x00E33822 from there into
 * the working copy at A6-0x28 -- and reads the first four words of the
 * working copy.
 */
typedef struct boot_params_t {
    int16_t device;        /* +0x00 (A6-0x28) */
    int16_t ctlr;          /* +0x02 (A6-0x26); the longword here is stored
                            *        whole into OS_$BOOT_DEVICE+4 */
    int16_t unit;          /* +0x04 (A6-0x24) */
    uint16_t flags;        /* +0x06 (A6-0x22); bit tests use (A6-0x21), the
                            *        low byte, so `btst.b #n` is bit n of the
                            *        word */
    uint32_t reserved[7];  /* +0x08 .. +0x23 */
} boot_params_t;

#if defined(ARCH_M68K)
_Static_assert(sizeof(boot_params_t) == 36, "boot_params_t is 9 longwords");
_Static_assert(__builtin_offsetof(boot_params_t, device) == 0, "device");
_Static_assert(__builtin_offsetof(boot_params_t, ctlr) == 2, "ctlr");
_Static_assert(__builtin_offsetof(boot_params_t, unit) == 4, "unit");
_Static_assert(__builtin_offsetof(boot_params_t, flags) == 6, "flags");
#endif

/* Boot flag bits, from the `btst.b #n,(-0x21,A6)` tests and the two word
 * masks applied to (-0x22,A6). */
#define BOOT_FLAG_TERM_SELECT 0x0001 /* 0x00E33C60 */
#define BOOT_FLAG_TERM_MODE 0x0002   /* 0x00E33C68 */
#define BOOT_FLAG_TERM_CTRL 0x0010   /* 0x00E33C50 */
#define BOOT_FLAG_TYPE_MASK 0x8004   /* 0x00E339F2: andi.w #-0x7ffc */
#define BOOT_FLAG_TYPE_VALUE 0x0004  /* 0x00E33A0C */
#define BOOT_FLAG_IO_VERBOSE 0x8000  /* 0x00E33C0A: tst.w / smi */

/*
 * The VTOCE the boot volume's paging file resolves to.  The original clears
 * exactly 0x90 bytes at A6-0x128 (0x00E34018: `moveq #0x23` + `dbf` = 36
 * longwords), which is also all the frame has room for, so this is 0x90
 * bytes and not the 0x150 that vtoc/vtoc.h gives vtoce_$result_t.
 * TODO(source-eb9k): reconcile vtoce_$result_t's declared size with the
 * buffer OS_$INIT actually provides.
 */
typedef struct os_$init_vtoce_t {
    uint32_t reserved00;                  /* +0x00 */
    uid_t file_uid;                       /* +0x04 (A6-0x124) */
    uint32_t reserved0c;                  /* +0x0C */
    uint32_t reserved10;                  /* +0x10 */
    uint32_t length;                      /* +0x14 (A6-0x114) */
    uint8_t reserved18[0x74 - 0x18];      /* +0x18 */
    uint16_t field74;                     /* +0x74 (A6-0xB4) */
    uint8_t reserved76[0x88 - 0x76];      /* +0x76 */
    uid_t acl_uid;                        /* +0x88 (A6-0xA0) */
} os_$init_vtoce_t;

#if defined(ARCH_M68K)
_Static_assert(sizeof(os_$init_vtoce_t) == 0x90, "os_$init_vtoce_t size");
_Static_assert(__builtin_offsetof(os_$init_vtoce_t, file_uid) == 0x04, "uid");
_Static_assert(__builtin_offsetof(os_$init_vtoce_t, length) == 0x14, "length");
_Static_assert(__builtin_offsetof(os_$init_vtoce_t, field74) == 0x74, "f74");
_Static_assert(__builtin_offsetof(os_$init_vtoce_t, acl_uid) == 0x88, "acl");
_Static_assert(sizeof(vtoc_$lookup_req_t) == 0x20, "vtoc_$lookup_req_t size");
#endif

/*
 * ============================================================================
 * Fixed addresses OS_$INIT builds the address space around
 * ============================================================================
 *
 * All of these come from `move.l #<link-time constant>,Dn` followed by
 * `andi.l #-0x8000,Dn` (round down to 32 KB) or `andi.w #-0x400,Dn`
 * (round down to 1 KB within the low word).  They are the section
 * boundaries of the OS image as the linker laid it out.
 */
#define OS_WIRED_LOW 0x00D00000   /* 0x00E33996 */
#define OS_WIRED_HIGH 0x00DA8000  /* 0x00E3399C, 0xdac7ff rounded down */
#define OS_DATA_LOW 0x00E00000    /* 0x00E339AE */
#define OS_DATA_WIRED_BASE 0x00E1DC00 /* 0x00E33B7E: the start of
                                       * OS_DATA_WIRED */
#define OS_DATA_RO_LOW 0x00E00800     /* 0x00E33B78, 0xe00bff & ~0x3ff */
#define OS_DATA_HIGH 0x00E38000   /* 0x00E339B4 */
#define OS_TEXT_LOW 0x00E78000    /* 0x00E339C0, 0xe78400 rounded down */
#define OS_TEXT_HIGH 0x00EB0000   /* 0x00E339D6, 0xeb1683 rounded down */
#define OS_MST_BASE 0x00EF6400    /* 0x00E339FA */
#define OS_WIRED_LIMIT 0x00FC0000 /* 0x00E33982 */
#define OS_WIRED_END 0x00F4FC00   /* 0x00E3397C: the link-time end of the
                                   * wired region, compared against the
                                   * limit above */

/* Page ranges that stay wired on a disked node (0x00E3450A-0x00E34528) */
#define OS_KEEP_WIRED1_LOW 0x00E38000  /* 0xe3824c & ~0x3ff */
#define OS_KEEP_WIRED1_HIGH 0x00E3E800 /* 0xe3eb45 & ~0x3ff */
#define OS_KEEP_WIRED2_LOW 0x00E78400  /* 0xe784d0 & ~0x3ff */
#define OS_KEEP_WIRED2_HIGH 0x00E7B400 /* 0xe7b443 & ~0x3ff */

/* Above this virtual address the init pages are freed outright rather than
 * mapped (0x00E34452) */
#define OS_INIT_FREE_ABOVE 0x00EA9684

#define OS_PAGE_SIZE 0x400   /* 1 KB pages */
#define OS_SEG_SIZE 0x8000   /* 32 KB segments, 32 pages each */

/* MST_$MAP_CANNED_AT flag words (0x00E33A20 etc.) */
#define OS_MAP_RW 0x00170001 /* read/write */
#define OS_MAP_RO 0x00130001 /* read-only */

/* MMU_$SET_PROT protection codes (0x00E33BAC / 0x00E33BB4) */
#define OS_PROT_RO 0x13
#define OS_PROT_RW 0x17

/*
 * ============================================================================
 * Static helpers lifted out of the body so they can be unit tested
 * ============================================================================
 */

/*
 * os_$install_vectors - 0x00E33876-0x00E338D6
 *
 * The boot info table is a flat list.  Entry i (1-based, A5+4 is entry 1)
 * carries, 55 longwords further on (byte offset 0xDC), a descriptor whose
 * high word is the first exception vector number and whose low word is the
 * count.  The `count` entries that FOLLOW entry i -- the pointers are
 * advanced before the value is read, which is why entry i itself is never
 * installed -- each hold one vector value, again 0xDC bytes on.  A zero
 * value leaves the vector alone.
 *
 * The outer loop runs while the entry index is <= 0x28 (`cmpi.w #0x28,D0w /
 * ble`), and the index advances once per consumed entry, so the walk stops
 * after 40 entries however they are grouped.
 */
static void os_$install_vectors(uint32_t *table)
{
    uint32_t *entry;  /* A0/A1/A2, all kept equal */
    int16_t index;    /* D0 */

    entry = &table[1]; /* 0x00E33876-0x00E33880: A5 + 4 */
    index = 1;         /* 0x00E33878 */

    /* 0x00E33882: the test is at the bottom */
    while (index <= 0x28) {
        /* 0x00E33888-0x00E33892: D1 = start + count - 1 - start */
        int16_t start = (int16_t)(entry[0x37] >> 16);
        int16_t count = (int16_t)(entry[0x37] & 0xFFFF);
        int16_t remaining = (int16_t)(start + count - 1 - start);

        if (remaining >= 0) { /* 0x00E33896: bmi skips the inner loop */
            /* 0x00E33898-0x00E338AC: the vector cell for `start` */
            uint32_t vecnum = (uint32_t)(int32_t)start;

            /* 0x00E338B0-0x00E338C6: `dbf` runs remaining + 1 times */
            do {
                index++;
                entry++;
                /* 0x00E338B8: tst.l (0xdc,A0) -- after the increment */
                if (entry[0x37] != 0) {
                    OS_$VECTOR_TABLE[vecnum] = entry[0x37];
                }
                vecnum++;
                remaining--;
            } while (remaining >= 0);
        }

        /* 0x00E338CA-0x00E338D0 */
        index++;
        entry++;
    }
}

/*
 * os_$boot_ws_mode - 0x00E338D8-0x00E338EE
 *
 * Boot device 1 means "workstation mode 2, no physical device"; the device
 * number is cleared in that case.  The returned word is what PROC2_$INIT
 * eventually receives by reference.
 */
static int16_t os_$boot_ws_mode(int16_t *device)
{
    int16_t ws_mode = 0;    /* 0x00E338D8: clr.w (-0x1ca,A6) */
    boolean is_one;         /* D0 */

    /* 0x00E338DC-0x00E338E4: seq then a signed byte test */
    is_one = (*device == 1) ? true : false;
    if (is_one < 0) {
        ws_mode = 2;   /* 0x00E338E8 */
        *device = 0;   /* 0x00E338EE */
    }
    return ws_mode;
}

/*
 * os_$boot_term_params - 0x00E33C4A-0x00E33C8C
 *
 * Turns the boot flags into the (mode, ctrl) word pair TERM_$INIT and
 * DTTY_$INIT both take by reference.
 */
static void os_$boot_term_params(uint16_t flags, int16_t *mode, uint16_t *ctrl)
{
    *ctrl = 1; /* 0x00E33C4A */
    if ((flags & BOOT_FLAG_TERM_CTRL) != 0) {
        *ctrl = 2; /* 0x00E33C58 */
    }
    if ((flags & BOOT_FLAG_TERM_SELECT) != 0) {
        if ((flags & BOOT_FLAG_TERM_MODE) != 0) {
            *mode = 1; /* 0x00E33C70; jumps past the ctrl reset */
        } else {
            *mode = 2; /* 0x00E33C78 */
            *ctrl = 1; /* 0x00E33C84 */
        }
    } else {
        *mode = 0; /* 0x00E33C80 */
        *ctrl = 1; /* 0x00E33C84 */
    }
}

/*
 * ============================================================================
 * OS_$INIT
 * ============================================================================
 */
void OS_$INIT(uint32_t *param_1, uint32_t *param_2)
{
    /* --- frame (link.w A6,-0x1fc) ----------------------------------- */
    uint32_t boot_scratch[9];      /* A6-0x158 */
    uint32_t diskless_info[12];    /* A6-0x58  */
    boot_params_t boot;            /* A6-0x28  */
    status_$t status;              /* A6-0x1b8 */
    uint32_t map_desc;             /* A6-0x1bc: read before it is ever
                                    * written -- see the comment below */
    uint32_t vtop_va;              /* A6-0x1c4 */
    uint32_t vtop_ppn;             /* A6-0x1c8 */
    uint16_t ws_mode;              /* A6-0x1ca */
    uint16_t term_ctrl;            /* A6-0x1cc */
    int16_t term_mode;             /* A6-0x1ce */
    int16_t lv_num;                /* A6-0x1dc */
    int16_t vol_idx;               /* A6-0x1e4; its low byte is A6-0x1e3 */
    boolean time_flags;            /* A6-0x1ea */
    boolean disk_chksum_arg;       /* A6-0x1ee */
    boolean is_type4;              /* A6-0x1f0 */
    boolean io_verbose;            /* A6-0x1f8, reused as a temporary at
                                    * 0x00E3451C */
    uint32_t file_set_len_arg;     /* A6-0x1fc */
    uid_t vol_root_uid;            /* A6-0x60 */
    uid_t vol_node_uid;            /* A6-0x68 */
    uid_t mount_dir_uid;           /* A6-0x130 */
    uint32_t os_text_low;          /* A6-0x198 */
    uint32_t os_text_high;         /* A6-0x194 */
    uint32_t keep_wired1_low;      /* A6-0x18c */
    uint32_t keep_wired1_high;     /* A6-0x188 */
    uint32_t block_hint;           /* A6-0x1b4 */
    uint16_t asid;                 /* A6-0x70 */
    uint32_t proc2_result;         /* A6-0x74 */
    uint32_t stack_high;           /* A6-0x78 */
    clock_t boot_clock;            /* A6-0x178 */
    clock_t rounded_clock;         /* A6-0x170 */
    uint32_t prom_node;            /* A6-0x164 */
    uint32_t stored_node;          /* A6-0x160 */
    int16_t probe_result;          /* A6-0x1d8 */
    uint32_t lock_result;          /* A6-0x1ac */
    vtoc_$lookup_req_t vtoce_req;  /* A6-0x98  */
    os_$init_vtoce_t vtoce;        /* A6-0x128 */

    /*
     * D4: TRUE (0xFF) when IO_$GET_DCTE found a calendar device.  Every
     * later test is `>= 0`, i.e. "there is NO calendar".
     */
    boolean calendar_present;
    uint32_t mst_high;    /* D6, after it stops holding OS_TEXT_LOW */
    int16_t i;

    /* --- 0x00E33802-0x00E3382E: three copies of the two records ------- */
    for (i = 0; i <= 8; i++) { /* moveq #8 / dbf = 9 longwords */
        boot_scratch[i] = param_1[i];
    }
    for (i = 0; i <= 11; i++) { /* moveq #0xb / dbf = 12 longwords */
        diskless_info[i] = param_2[i];
    }
    /* 0x00E33822: the working copy the rest of the function reads */
    for (i = 0; i <= 8; i++) {
        ((uint32_t *)&boot)[i] = boot_scratch[i];
    }

    /* --- 0x00E33832-0x00E33844 --------------------------------------- */
    PMAP_$SHUTTING_DOWN_FLAG = 0;
    MST_$PRE_INIT();
    MMU_$INIT();
    AS_$INIT();

    /*
     * 0x00E3384A-0x00E3386A: drop the MMU entries for the two pages at
     * 0x101400 and 0x101800 so MMAP_$INIT sees them as free.  The page
     * number is computed as 0x101400 >> 10.
     */
    {
        uint32_t ppn = 0x101400u >> 10; /* lsr.l #8 then lsr.l #2 */
        MMU_$REMOVE(ppn);
        MMU_$REMOVE(ppn + 1);
    }

    /* --- 0x00E3386C-0x00E338D6 --------------------------------------- */
    MMAP_$INIT(BOOT_INFO_TABLE);
    os_$install_vectors(BOOT_INFO_TABLE);

    /* --- 0x00E338D8-0x00E33900: the boot device ---------------------- */
    ws_mode = (uint16_t)os_$boot_ws_mode(&boot.device);

    OS_$BOOT_DEVICE.device = boot.device;   /* 0x00E338F8 */
    OS_$BOOT_DEVICE.reserved = 0;           /* 0x00E338FC: clr.w (0x2,A3) */
    /* 0x00E33900: the longword at boot+2, i.e. {ctlr, unit}, in one move */
    OS_$BOOT_DEVICE.ctlr = boot.ctlr;
    OS_$BOOT_DEVICE.unit = boot.unit;

    MMU_$SET_SYSREV(); /* 0x00E33906 */

    /* --- 0x00E3390C-0x00E3391C --------------------------------------- */
    NETWORK_$DISKLESS = NET_IO_$BOOT_DEVICE(boot.device, boot.ctlr);

    if (NETWORK_$DISKLESS < 0) {
        /* 0x00E33924-0x00E33948: diskless -- everything comes from the
         * mother node's reply that the bootstrap already fetched. */
        NETWORK_$MOTHER_NODE = diskless_info[0];
        NETWORK_$PAGING_FILE_UID.high = diskless_info[1];
        NETWORK_$PAGING_FILE_UID.low = diskless_info[2];
        vol_root_uid.high = diskless_info[3];
        vol_root_uid.low = diskless_info[4];
        vol_node_uid.high = diskless_info[5]; /* A6-0x44 */
        vol_node_uid.low = diskless_info[6];
    } else {
        /* 0x00E3394E-0x00E33966: both UIDs are nil on a disked node */
        vol_root_uid = UID_$NIL;
        vol_node_uid = UID_$NIL;
    }

    /* --- 0x00E3396A-0x00E33976 --------------------------------------- */
    PROC1_$INHIBIT_BEGIN();
    MST_$INIT();
    DXM_$INIT();

    /*
     * 0x00E3397C-0x00E33994: a link-time assertion.  With the values the
     * linker produced (0x00F4FC00 <= 0x00FC0000) the branch is always
     * taken and the crash never happens; it is kept because the constants
     * are what the image contains.
     */
    if (OS_WIRED_END > OS_WIRED_LIMIT) {
        CRASH_SYSTEM(&os_$init_wired_too_big_err);
    }

    /* --- 0x00E33996-0x00E33A12: section boundaries ------------------- */
    os_text_low = OS_TEXT_LOW;   /* D6 -> A6-0x198 */
    os_text_high = OS_TEXT_HIGH; /* D7 -> A6-0x194 */

    /*
     * 0x00E339E6-0x00E33A0A: the top of the MST region, one 32 KB segment
     * per MST page limit unit above OS_MST_BASE, rounded up to 32 KB.
     */
    mst_high = ((uint32_t)MST_$MST_PAGES_LIMIT * OS_PAGE_SIZE) + OS_MST_BASE;
    mst_high = (mst_high + 0x7FFF) & 0xFFFF8000u;

    /* 0x00E339EC-0x00E33A12 */
    is_type4 =
        ((boot.flags & BOOT_FLAG_TYPE_MASK) == BOOT_FLAG_TYPE_VALUE) ? true
                                                                     : false;

    /*
     * map_desc (A6-0x1bc) is read by the five MST_$MAP_CANNED_AT calls below
     * but is not written until 0x00E33E88 / 0x00E33FC2, well after them: the
     * original passes an uninitialised stack longword here.  Reproduced by
     * leaving the local uninitialised.
     */

    /* --- 0x00E33A16-0x00E33A50: map the wired OS image --------------- */
    MST_$MAP_CANNED_AT(OS_WIRED_LOW, &OS_WIRED_$UID, 0,
                       OS_WIRED_HIGH - OS_WIRED_LOW, OS_MAP_RW, false, false,
                       map_desc, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /* --- 0x00E33A52-0x00E33A8C: map the OS data section -------------- */
    MST_$MAP_CANNED_AT(OS_DATA_LOW, &OS_WIRED_$UID, 0,
                       OS_DATA_HIGH - OS_DATA_LOW, OS_MAP_RW, false, false,
                       map_desc, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /*
     * --- 0x00E33A8E-0x00E33AE6: map the rest of the data section.
     * A type-4 boot maps it read-only.
     */
    MST_$MAP_CANNED_AT(OS_DATA_HIGH, &OS_WIRED_$UID, 0,
                       os_text_low - OS_DATA_HIGH,
                       (is_type4 < 0) ? OS_MAP_RO : OS_MAP_RW, true, false,
                       map_desc, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /* --- 0x00E33AE8-0x00E33B2C: map the OS text --------------------- */
    MST_$MAP_CANNED_AT(os_text_low, &OS_WIRED_$UID,
                       os_text_low - OS_DATA_HIGH, os_text_high - os_text_low,
                       OS_MAP_RW, true, false, map_desc, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /* --- 0x00E33B2E-0x00E33B6C: map the MST region ------------------ */
    MST_$MAP_CANNED_AT(os_text_high, &OS_WIRED_$UID, 0,
                       mst_high - os_text_high, OS_MAP_RW, false, false,
                       map_desc, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /*
     * --- 0x00E33B6E-0x00E33BFE: on a type-4 boot, walk the two mapped
     * data ranges page by page and set the MMU protection explicitly.
     */
    if (is_type4 < 0) {
        uint32_t va;

        /*
         * 0x00E33B88-0x00E33BCA: [OS_DATA_LOW, OS_DATA_HIGH).  Pages inside
         * [0x00E00800, 0x00E1D800) -- i.e. above the first kilobyte of the
         * data section and below OS_DATA_WIRED -- are read-only; the rest
         * is read/write.
         */
        for (va = OS_DATA_LOW; va < OS_DATA_HIGH; va += OS_PAGE_SIZE) {
            uint16_t prot;

            vtop_va = va;
            vtop_ppn = VTOP_OR_CRASH(&vtop_va);

            /* 0x00E33B9C: cmp.l D2,D3 / bgt -- signed, D3 = 0x00E00800 */
            if ((int32_t)OS_DATA_RO_LOW > (int32_t)va) {
                prot = OS_PROT_RW;
            } else if (OS_DATA_WIRED_BASE <= va) {
                /* 0x00E33BA0-0x00E33BA8: `movea.l #0xe1dc00,A2` then
                 * `andi.w #-0x400,D7w`, which leaves 0x00E1DC00 unchanged;
                 * the compare is unsigned (`bls`) */
                prot = OS_PROT_RW;
            } else {
                prot = OS_PROT_RO;
            }
            MMU_$SET_PROT(vtop_ppn, prot);
        }

        /* 0x00E33BCC-0x00E33BFE: [OS_DATA_HIGH, os_text_low) is all
         * read-only. */
        for (va = OS_DATA_HIGH; va < os_text_low; va += OS_PAGE_SIZE) {
            vtop_va = va;
            vtop_ppn = VTOP_OR_CRASH(&vtop_va);
            MMU_$SET_PROT(vtop_ppn, OS_PROT_RO);
        }
    }

    /* --- 0x00E33C00-0x00E33C36: the I/O subsystem -------------------- */
    PEB_$INIT();
    /* 0x00E33C0A-0x00E33C10: `tst.w (-0x22,A6) / smi` -- the sign bit of the
     * boot flags word */
    io_verbose = ((int16_t)boot.flags < 0) ? true : false;
    IO_$INIT((void *)&os_$init_zero_long, &io_verbose, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /* --- 0x00E33C38-0x00E33C48 --------------------------------------- */
    NODE_$ME = RING_$GET_ID((void *)&os_$init_zero_word);

    /* --- 0x00E33C4A-0x00E33C9A --------------------------------------- */
    os_$boot_term_params(boot.flags, &term_mode, &term_ctrl);
    TERM_$INIT(&term_mode, (short *)&term_ctrl);

    /*
     * --- 0x00E33C9C-0x00E33CDE: is there a calendar?  Both IO_$GET_DCTE
     * arguments are the same zero-word cell (`move.l (SP),-(SP)` duplicates
     * the one just pushed).  A disked node without one cannot boot.
     */
    IO_$GET_DCTE((uint16_t *)&os_$init_zero_word,
                 (uint16_t *)&os_$init_zero_word, &status);
    calendar_present = (status == status_$ok) ? true : false;

    if (NETWORK_$DISKLESS >= 0 && calendar_present >= 0) {
        CRASH_SYSTEM(&os_$init_no_calendar_err);
    }

    time_flags = calendar_present;
    TIME_$INIT((uint8_t *)&time_flags);

    /* --- 0x00E33CE0-0x00E33D06 --------------------------------------- */
    UID_$INIT();
    _NULL_PC = (void *)NULLPROC;
    PROC1_$INIT();
    PROC1_$SET_TYPE(PROC1_$CURRENT, 1);

    /* --- 0x00E33D08-0x00E33D2A --------------------------------------- */
    SMD_$INIT();
    TPAD_$INIT();
    DTTY_$INIT(&term_mode, &term_ctrl);
    EC2_$INIT_S();
    PRINT_BUILD_TIME();

    /* --- 0x00E33D30-0x00E33D5A --------------------------------------- */
    OS_$VECTOR_TABLE[0x7C / 4] = (uint32_t)(uintptr_t)FIM_$PARITY_TRAP;
    ACL_$INIT();
    AST_$INIT();
    AREA_$INIT();
    if (NETWORK_$DISKLESS >= 0) {
        DISK_$INIT();
        DBUF_$INIT();
    }

    CAL_$BOOT_VOLX = 0; /* 0x00E33D60 */

    if (NETWORK_$DISKLESS >= 0) {
        /* ======================================================
         * 0x00E33D70-0x00E33FBE: disked boot
         * ====================================================== */
        void *lv_label;

        /* 0x00E33D70-0x00E33D78: unit 0 means logical volume 1 */
        lv_num = boot.unit;
        if (lv_num == 0) {
            lv_num = 1;
        }

        /* 0x00E33D7C-0x00E33DA6 */
        VOLX_$MOUNT(&boot.device, (int16_t *)&os_$init_zero_word, &boot.ctlr,
                    &lv_num, (int8_t *)&os_$init_false,
                    (int8_t *)&os_$init_false, &UID_$NIL, &mount_dir_uid,
                    &status);

        /* 0x00E33DAA-0x00E33E28: offer to mount anyway if it needs salvaging */
        if (status == status_$disk_needs_salvaging) {
            OS_$PRINT_INIT_ERROR(msg_salvage);
            if (MMU_$NORMAL_MODE() >= 0) {
                /* 0x00E33DC6-0x00E33DD4: no console to ask on */
                CRASH_SYSTEM(&status);
            }
            OS_$PRINT_INIT_ERROR(msg_proceed_risk);
            if (prompt_for_yes_or_no() >= 0) {
                CRASH_SYSTEM(&os_$init_salvage_err);
            }
            /* 0x00E33DF6-0x00E33E26: retry with salvage_ok = TRUE */
            lv_num = boot.unit;
            if (lv_num == 0) {
                lv_num = 1;
            }
            VOLX_$MOUNT(&boot.device, (int16_t *)&os_$init_zero_word,
                        &boot.ctlr, &lv_num, (int8_t *)&os_$init_true,
                        (int8_t *)&os_$init_false, &UID_$NIL, &mount_dir_uid,
                        &status);
        }

        if (status != status_$ok) { /* 0x00E33E2A */
            CRASH_SYSTEM(&status);
        }

        vol_idx = 1;        /* 0x00E33E3C */
        CAL_$BOOT_VOLX = 1; /* 0x00E33E42 */

        /*
         * 0x00E33E4A-0x00E33E86: check the calendar against the volume.
         * CAL_$VERIFY returns a boolean; a refusal with a bad-time status
         * shuts the volume down and crashes.
         */
        if (CAL_$VERIFY((int *)&os_$init_cal_verify_p1,
                        (void *)&os_$init_cal_verify_p2,
                        (char *)&os_$init_true, &status) >= 0 &&
            status == status_$cal_refused) {
            status = (status_$t)VOLX_$SHUTDOWN();
            CRASH_SYSTEM(&os_$init_zero_long);
        }

        /*
         * 0x00E33E88-0x00E33E9C: build the location descriptor for the
         * paging file.  bclr then `andi.l #0x80000000` leaves nothing but
         * the bit just cleared, so the result is simply 1; the sequence is
         * kept because that is what the code does.
         */
        map_desc &= ~0x80000000u;
        map_desc &= 0x80000000u;
        map_desc |= 0x00000001u;

        /* 0x00E33E9E-0x00E33EBE: read the logical-volume label */
        lv_label = DBUF_$GET_BLOCK((uint16_t)vol_idx, 0, &LV_LABEL_$UID, 0, 0,
                                   &status);
        if (status != status_$ok) {
            CRASH_SYSTEM(&status);
        }

        /* 0x00E33ED2-0x00E33EEE */
        block_hint = *(uint32_t *)((uint8_t *)lv_label + 0x5C);
        DBUF_$SET_BUFF(lv_label, 8, &status);

        if ((block_hint >> 4) == 0) {
            /* 0x00E33EF6-0x00E33F5C: no paging file on the boot volume */
            OS_$PRINT_INIT_ERROR(msg_nopf_1);
            OS_$PRINT_INIT_ERROR(msg_nopf_2);
            OS_$PRINT_INIT_ERROR(msg_nopf_3);
            OS_$PRINT_INIT_ERROR(msg_nopf_4);
            OS_$PRINT_INIT_ERROR(msg_nopf_5);
            OS_$PRINT_INIT_ERROR(msg_nopf_6);
            OS_$PRINT_INIT_ERROR(msg_nopf_7);
            NETWORK_$PAGING_FILE_UID = OS_WIRED_$UID;
            goto after_paging_file; /* 0x00E33F5C: bra 0x00E34062 */
        }

        /*
         * 0x00E33F60-0x00E33FBE: look the paging file's VTOCE up.  Only
         * three fields are written -- unlike the diskless path there is no
         * clear loop here, so the rest of the request record is whatever
         * the stack held.  Reproduced as written.
         */
        vtoce_req.uid = UID_$NIL;
        vtoce_req.block_hint = block_hint;
        vtoce_req.vol_idx = (uint8_t)vol_idx; /* the low byte, A6-0x1e3 */

        VTOCE_$READ(&vtoce_req, (vtoce_$result_t *)&vtoce, &status);
        if (status != status_$ok) {
            CRASH_SYSTEM(&status);
        }

        vtoce_req.uid = vtoce.file_uid;
        NETWORK_$PAGING_FILE_UID = vtoce.file_uid;
    } else {
        /* ======================================================
         * 0x00E33FC2-0x00E34050: diskless boot
         * ====================================================== */

        /* 0x00E33FC2-0x00E33FDC: the location descriptor names the mother
         * node.  `andi.w` touches the high word only. */
        map_desc |= 0x80000000u;
        map_desc = (map_desc & 0xFC0F0000u) | (map_desc & 0x0000FFFFu);
        map_desc &= 0xFFF00000u;
        map_desc |= diskless_info[0];

        /* 0x00E33FDE-0x00E34012: the lookup request */
        for (i = 0; i <= 7; i++) { /* 0x00E33FE0: moveq #7 / dbf */
            ((uint32_t *)&vtoce_req)[i] = 0;
        }
        vtoce_req.uid = NETWORK_$PAGING_FILE_UID;
        vtoce_req.port = 0;
        vtoce_req.node = diskless_info[0];
        vtoce_req.flags_1d = (uint8_t)((vtoce_req.flags_1d & 0xF0) | 0x01);
        /* 0x00E34012: ori.w #0xc0 on the word at +0x1C, i.e. bits 6 and 7 of
         * the flags_1d byte */
        vtoce_req.flags_1d |= 0xC0;

        /* 0x00E34018-0x0E3404C: the VTOCE the mother node's file stands in
         * for */
        for (i = 0; i <= 0x23; i++) { /* 0x00E34018: moveq #0x23 / dbf */
            ((uint32_t *)&vtoce)[i] = 0;
        }
        vtoce.file_uid = NETWORK_$PAGING_FILE_UID;
        vtoce.acl_uid = ACL_$FNDWRX;
        vtoce.length = os_text_high - OS_DATA_HIGH;
        vtoce.field74 = 1;
    }

    /* 0x00E34052-0x00E34060: both paths converge here */
    AST_$ACTIVATE_AOTE_CANNED((uint32_t *)&vtoce, (uint32_t *)&vtoce_req);

after_paging_file:
    /* --- 0x00E34062-0x00E340A4 --------------------------------------- */
    NETWORK_$REALLY_DISKLESS = NETWORK_$DISKLESS;
    os_$free_va_page(0x00EB0000);
    os_$free_va_page(0x00EB0800);
    os_$free_va_page(0x00EB2000);

    /* 0x00E34096-0x00E340A2: zero the 1 KB interrupt stack, downwards */
    {
        char *p = INT_STACK_BASE;
        for (i = 0x3FF; i >= 0; i--) {
            *--p = 0;
        }
    }

    /*
     * --- 0x00E340A6-0x00E340D4: free the 56 boot-time page frames the
     * boot info table's first 56 entries describe.  A zero table entry
     * means the frame was never allocated.
     */
    {
        int16_t idx = 1;
        for (i = 0x37; i >= 0; i--) { /* moveq #0x37 / dbf = 56 */
            if (BOOT_INFO_TABLE[idx - 1] != 0) {
                os_$free_va_page(0x00EB4800u +
                                 (uint32_t)(idx - 1) * OS_PAGE_SIZE);
            }
            idx++;
        }
    }

    /* --- 0x00E340D8-0x00E3412A: optional checksum passes -------------- */
    if (NETWORK_$DO_CHKSUM < 0) {
        OS_$CHKSUM((void *)&os_$init_zero_word, (void *)&os_$init_two_word,
                   (void *)&os_$init_zero_word, &NETWORK_$DO_CHKSUM, &status);
    }
    if (DISK_$DO_CHKSUM < 0) {
        DISK_$DO_CHKSUM = 0;
        disk_chksum_arg = true; /* 0x00E3410E: st (-0x1ee,A6) */
        OS_$CHKSUM((void *)&os_$init_zero_word, (void *)&os_$init_zero_word,
                   (void *)&os_$init_zero_word, &disk_chksum_arg, &status);
    }

    /* --- 0x00E3412C-0x00E34160: install the display, if there is one -- */
    if (io_$probe((void *)&os_$init_one_word,
                  (void *)&os_$init_display_addr, &probe_result) < 0) {
        OS_$INSTALL_DISPLAY_ASTE(&DISPLAY1_$UID,
                                 (void *)&os_$init_display_addr,
                                 (int *)&os_$init_display_size,
                                 (char *)&os_$init_true);
    }

    /* --- 0x00E34162-0x00E34172 --------------------------------------- */
    if (calendar_present >= 0) { /* no calendar of our own */
        ML_$LOCK(1);
    }

    /* --- 0x00E34174-0x00E341F6: the three daemon processes ------------ */
    PROC1_$CREATE_P((void *)PMAP_$PURIFIER_L, 0x0C000005, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }
    PROC1_$CREATE_P((void *)PMAP_$PURIFIER_R, 0x0C000005, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }
    PROC1_$CREATE_P((void *)DXM_$HELPER_UNWIRED, 0x0C000006, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /* --- 0x00E341F8-0x00E34238 --------------------------------------- */
    asid = MST_$ALLOC_ASID(&status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }
    PROC1_$SET_ASID(asid);
    PROC1_$SET_TYPE(PROC1_$CURRENT, 0x0B);

    /* --- 0x00E3423A-0x00E34246 --------------------------------------- */
    ACL_$ENTER_SUPER();
    SOCK_$INIT();
    NETWORK_$INIT();

    /*
     * --- 0x00E3424C-0x00E342DA: with no calendar of our own, take the
     * time from the mother node and derive the boot time from it.
     */
    if (calendar_present >= 0) { /* no calendar of our own */
        uint32_t remainder; /* D7 */

        network_$fetch_diskless_info(2, NETWORK_$MOTHER_NODE);

        TIME_$CURRENT_CLOCKH = TIME_$CLOCKH;
        TIME_$BOOT_TIME = TIME_$CLOCKH;

        /* 0x00E34276-0x00E3427A: the 48-bit clock {low long, high word} */
        boot_clock.high = TIME_$CLOCKH;
        boot_clock.low = 0;

        TIME_$CURRENT_TIME = CAL_$CLOCK_TO_SEC(&boot_clock);
        CAL_$SEC_TO_CLOCK((uint *)&TIME_$CURRENT_TIME, &rounded_clock);

        /* 0x00E342AA: boot_clock -= rounded_clock */
        SUB48(&boot_clock, &rounded_clock);

        /*
         * 0x00E342B2-0x00E342B8: `move.l (-0x176,A6),D7` reads the
         * longword two bytes into the six-byte clock, i.e. the low half of
         * clock.high followed by clock.low, then scales it by 4.  Written
         * with shifts so it does not depend on the host's byte order.
         */
        remainder = ((boot_clock.high & 0x0000FFFFu) << 16) |
                    (uint32_t)boot_clock.low;
        TIME_$CURRENT_USEC = remainder << 2;

        /* 0x00E342BE: the Domain-to-Unix epoch offset */
        TIME_$CURRENT_TIME += 0x12CEA600u;

        UID_$INIT();
        ML_$UNLOCK(1);
    }

    /* --- 0x00E342DC-0x00E34322 --------------------------------------- */
    PROC1_$INIT_LOADAV();
    FILE_$LOCK_INIT();
    OS_$VECTOR_TABLE[0x08 / 4] = (uint32_t)(uintptr_t)FIM_$BUS_ERR;

    PROC1_$CREATE_P((void *)DXM_$HELPER_WIRED, 0x08000004, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /* --- 0x00E3431E-0x00E34332 --------------------------------------- */
    HINT_$INIT_CACHE();
    NAME_$INIT(&vol_root_uid, &vol_node_uid);

    /* --- 0x00E34334-0x00E34354 --------------------------------------- */
    NETWORK_$ADD_REQUEST_SERVERS((int16_t *)&os_$init_one_word, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /* --- 0x00E34356-0x00E3438A: lock the paging file ------------------ */
    /*
     * 0x00E34356-0x00E34376: six arguments -- the paging file's UID, the
     * zero-word lock index, the word 4 as the lock mode, the FALSE byte as
     * the rights, an 8-byte lock-info buffer and the status.
     */
    FILE_$LOCK(&NETWORK_$PAGING_FILE_UID, (uint16_t *)&os_$init_zero_word,
               (uint16_t *)&os_$init_four_word, (uint8_t *)&os_$init_false,
               &lock_result, &status);
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /* --- 0x00E3438C-0x00E343AA --------------------------------------- */
    if (NETWORK_$DISKLESS < 0) {
        FILE_$SET_REFCNT(&NETWORK_$PAGING_FILE_UID,
                         (uint32_t *)&os_$init_zero_long, &status);
    }

    /*
     * --- 0x00E343AC-0x00E345E6: unless the paging file IS the wired OS
     * image, grow it and wire the OS into it segment by segment.
     */
    if (NETWORK_$PAGING_FILE_UID.high != OS_WIRED_$UID.high ||
        NETWORK_$PAGING_FILE_UID.low != OS_WIRED_$UID.low) {
        uint32_t va;    /* D2 */
        int16_t seg;    /* D4 */
        boolean warned; /* D5, byte */

        /* 0x00E343C4-0x00E343E2 */
        file_set_len_arg = os_text_high - OS_DATA_HIGH;
        FILE_$SET_LEN(&NETWORK_$PAGING_FILE_UID, &file_set_len_arg, &status);

        /* 0x00E343E6-0x00E34414 */
        keep_wired1_low = OS_KEEP_WIRED1_LOW;
        keep_wired1_high = OS_KEEP_WIRED1_HIGH;

        seg = 0;
        va = OS_DATA_HIGH;

        do { /* 0x00E34426 */
            aste_t *aste;

            /*
             * 0x00E34426-0x00E34438: activate (or find) the ASTE for this
             * segment of the paging file.  The result comes back in A0.
             */
            aste = AST_$ACTIVATE_CANNED_SEG(&NETWORK_$PAGING_FILE_UID,
                                            (uint16_t)seg);
            warned = false; /* 0x00E3443A: clr.b D5b */

            for (i = 0x1F; i >= 0; i--) { /* 32 pages per segment */
                vtop_va = va;
                vtop_ppn = VTOP_OR_CRASH(&vtop_va);

                if (va >= OS_INIT_FREE_ABOVE) {
                    /* 0x00E34538: past the last page worth keeping */
                    os_$free_va_page(va);
                } else {
                    /* 0x00E3445C-0x00E3447E */
                    /*
                     * 0x00E34462/0x00E34464: two `st -(SP)`, i.e. two
                     * boolean TRUE arguments in word slots (the 0xFF lands
                     * in each slot's high byte).
                     */
                    AST_$PMAP_ASSOC(aste,
                                    (uint16_t)((va & 0x7FFF) / OS_PAGE_SIZE),
                                    vtop_ppn, 0x00FF, 0x00FF, &status);

                    if (status != status_$ok) {
                        /* 0x00E34488: only "file too small" is survivable */
                        if (status != 0x00050006) {
                            CRASH_SYSTEM(&status);
                        }
                        if (warned >= 0) {
                            /* 0x00E344A2-0x00E34502: warn once */
                            OS_$PRINT_INIT_ERROR(msg_small_1);
                            OS_$PRINT_INIT_ERROR(msg_small_2);
                            OS_$PRINT_INIT_ERROR(msg_small_3);
                            OS_$PRINT_INIT_ERROR(msg_small_4);
                            OS_$PRINT_INIT_ERROR(msg_proceed_prompt);
                            if (prompt_for_yes_or_no() >= 0) {
                                status = status_$ok;
                                OS_$SHUTDOWN(&status);
                            }
                            OS_$PRINT_INIT_ERROR(msg_small_5);
                            warned = true;
                        }
                    } else {
                        /*
                         * 0x00E34506-0x00E34530: on a disked node the two
                         * ranges that must stay resident keep their wiring;
                         * everything else -- and everything on a diskless
                         * node -- is unwired now that it is backed.
                         */
                        boolean keep;

                        if (NETWORK_$REALLY_DISKLESS < 0) {
                            keep = false;
                        } else {
                            boolean in1;
                            boolean in2;

                            in1 = ((va >= keep_wired1_low) ? true : false) &
                                  ((va < keep_wired1_high) ? true : false);
                            io_verbose = (va >= OS_KEEP_WIRED2_LOW) ? true
                                                                    : false;
                            in2 = ((va < OS_KEEP_WIRED2_HIGH) ? true : false) &
                                  io_verbose;
                            keep = (boolean)(in1 | in2);
                        }
                        if (keep >= 0) {
                            MMAP_$UNWIRE(vtop_ppn);
                        }
                    }
                }
                va += OS_PAGE_SIZE; /* 0x00E34542 */
            }

            /*
             * --- 0x00E3454C-0x00E345C4: map the segment just filled.  A
             * type-4 boot maps the segments below the text section
             * read-only.
             */
            {
                uint32_t seg_va = va - OS_SEG_SIZE;
                uint32_t flags;

                if (is_type4 < 0 && seg_va < os_text_low) {
                    flags = OS_MAP_RO; /* 0x00E3456C */
                } else {
                    flags = OS_MAP_RW; /* 0x00E34596 */
                }
                MST_$MAP_CANNED_AT(seg_va, (uid_t *)&NETWORK_$PAGING_FILE_UID,
                                   (uint32_t)(uint16_t)seg * OS_SEG_SIZE,
                                   OS_SEG_SIZE, flags, true, true, map_desc,
                                   &status);
            }
            if (status != status_$ok) {
                CRASH_SYSTEM(&status);
            }

            seg++; /* 0x00E345D8 */
            /* 0x00E345DA-0x00E345E2: stop early once the warning fired */
        } while (warned >= 0 && va < os_text_high);
    }

    /*
     * --- 0x00E345E6-0x00E34678: does the node number in the PROM agree
     * with the one recorded on the boot volume?
     */
    if (NETWORK_$DISKLESS >= 0) {
        stored_node = NAME_$NODE_UID.low & 0x000FFFFFu;
        prom_node = NODE_$ME;

        if (stored_node != prom_node) {
            OS_$PRINT_INIT_ERROR(msg_node_1);
            OS_$PRINT_INIT_ERROR(msg_node_2);
            ERROR_$PRINT(msg_node_3, &prom_node, &stored_node);
            OS_$PRINT_INIT_ERROR(msg_proceed_prompt);
            if (prompt_for_yes_or_no() >= 0) {
                status = status_$ok;
                OS_$SHUTDOWN(&status);
            }
        }

        /* 0x00E34660-0x00E34678: the test is repeated after the prompt */
        if (NETWORK_$DISKLESS >= 0) {
            VOLX_$REC_ENTRY(&vol_idx, &NAME_$NODE_UID);
        }
    }

    /* --- 0x00E3467A-0x00E3468C --------------------------------------- */
    NAME_$SET_WDIR((char *)os_$init_root_path, (int16_t *)&os_$init_one_word,
                   &status);

    /* --- 0x00E34690-0x00E346B4 --------------------------------------- */
    /* 0x00E346A0 stores D0 into (-0x74,A6); nothing ever reads it back */
    proc2_result = PROC2_$INIT(&ws_mode, &status);
    (void)proc2_result;
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /*
     * 0x00E346B6: the stack top os_$start_proc2 is eventually handed is
     * read here, into a local, and it is the local's address that is
     * passed at 0x00E347B6 -- not AS_$STACK_HIGH's.
     */
    stack_high = AS_$STACK_HIGH;

    /* --- 0x00E346BE-0x00E346F0: hints --------------------------------- */
    if (NETWORK_$DISKLESS < 0) {
        /*
         * 0x00E346C6-0x00E346EA: 0x00E2E0A0 carries both the ROUTE_$PORT
         * and NETWORK_$ME labels.  HINT_$INIT may clear it, in which case
         * the saved value is put back and registered as a network.
         */
        uint32_t saved_port = ROUTE_$PORT; /* D2 */

        HINT_$INIT();
        if (ROUTE_$PORT == 0) {
            ROUTE_$PORT = saved_port;
            HINT_$ADD_NET(saved_port);
        }
    } else {
        HINT_$INIT();
    }

    /* --- 0x00E346F2-0x00E34724: diskless time zone and routing -------- */
    if (NETWORK_$DISKLESS < 0) {
        network_$fetch_diskless_info(8, diskless_info[0]);
        CAL_$TIMEZONE.drift.high = 0; /* 0x00E3470A: clr.l */
        CAL_$TIMEZONE.drift.low = 0;  /* 0x00E34710: clr.w */
        network_$fetch_diskless_info(0x37, diskless_info[0]);
    }

    /* --- 0x00E34726-0x00E3473E --------------------------------------- */
    LOG_$INIT();
    XPD_$INIT();
    PCHIST_$INIT();
    NETWORK_$LOAD();
    PEB_$LOAD_WCS();

    /*
     * --- 0x00E34744-0x00E3475E: with every vector now installed, make the
     * page holding the exception vector table read-only.  VTOP_OR_CRASH
     * takes the address of the virtual address, and the cell it is given
     * holds zero -- page 0.
     */
    MMU_$SET_PROT(VTOP_OR_CRASH((uint32_t *)&os_$init_zero_long), OS_PROT_RO);

    /*
     * --- 0x00E34760-0x00E3479A: display type 3 selects a different
     * diskless configuration.  Both unit numbers are passed by address.
     */
    {
        uint16_t disp_type;

        disp_type = SMD_$INQ_DISP_TYPE((uint16_t *)&os_$init_one_word);
        if (disp_type == 0) {
            disp_type = SMD_$INQ_DISP_TYPE((uint16_t *)&os_$init_two_word);
        }
        MST_$DISKLESS_INIT((disp_type == 3) ? -1 : 0, NETWORK_$MOTHER_NODE,
                           NODE_$ME);
    }

    /* --- 0x00E3479E-0x00E347B0 --------------------------------------- */
    SMD_$INIT_BLINK();
    PACCT_$INIT();
    AUDIT_$INIT();
    PROC1_$INHIBIT_END();

    /* --- 0x00E347B6-0x00E347BA: never returns ------------------------- */
    os_$start_proc2(&stack_high);
}
