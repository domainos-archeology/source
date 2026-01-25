/*
 * peb/load_wcs.c - PEB WCS (Writable Control Store) Management
 *
 * Loads microcode into the PEB's writable control store from
 * /sys/peb2_microcode. The WCS contains the microinstructions that
 * define the PEB's floating point operations.
 *
 * Original addresses:
 *   PEB_$LOAD_WCS: 0x00E31FD8 (684 bytes)
 *   PEB_$LOAD_WCS_CHECK_ERR: 0x00E31EC8 (144 bytes)
 *   peb_$write_wcs: 0x00E31DD4 (122 bytes)
 *   peb_$read_wcs: 0x00E31E4E (122 bytes)
 */

#include "peb/peb_internal.h"
#include "file/file.h"
#include "mst/mst.h"
#include "name/name.h"
#include "vfmt/vfmt.h"

/*
 * Error messages for WCS loading
 */
static const char msg_create_file[] = "create_file_for ";
static const char msg_map_file[] = "map_file_for ";
static const char msg_lock_file[] = "lock_file_for ";
static const char msg_unlock_file[] = "unlock_";
static const char msg_unmap_file[] = "unmap_";
static const char msg_resolve[] = "lock, resolve ";
static const char msg_map[] = "map_";

/* Microcode file path */
static const char peb_microcode_path[] = "/sys/peb2_microcode";

/* Warning messages */
static const char msg_warning_unable[] = "      Warning: Unable to   ";
static const char msg_peb_disabled[] = "  a      lh  PEB is disabled   ";
static const char msg_68881_disabled[] = "68881 savearea   68881 is disab";

/*
 * Pointers for wiring PEB code and data areas
 * Original addresses: 0x00E322E4 (code), 0x00E322DC (data)
 */
extern void *PTR_PEB_$TOUCH_00e322e4;
extern void *PTR_PEB_$WIRED_DATA_START_00e322dc;

/*
 * External file/mapping system calls
 */
extern void FILE_$CREATE(const uid_t *dir_uid, uid_t *file_uid, status_$t *status);
extern void FILE_$LOCK(const uid_t *file_uid, const char *lock_name, const void *p3,
                       const void *p4, void *result, status_$t *status);
extern void FILE_$UNLOCK(const uid_t *file_uid, const void *p2, status_$t *status);
extern void NAME_$RESOLVE(const char *name, const int16_t *name_len, uid_t *uid, status_$t *status);
extern void MST_$MAP(const uid_t *uid, const void *p2, const void *p3, const void *p4,
                     const void *p5, const void *p6, void *result, status_$t *status);
extern void MST_$MAPS(int p1, uint16_t flags, const uid_t *uid, int p4, uint32_t size,
                      uint16_t prot, int p7, uint8_t p8, void *result, status_$t *status);
extern void MST_$UNMAP(const uid_t *uid, void *p2, void *p3, status_$t *status);
extern void MST_$WIRE_AREA(void **start_ptr, uint16_t p2, char p3, uint16_t p4, int16_t *result);
/* ERROR_$PRINT declared in vfmt/vfmt.h */

/*
 * PEB_$LOAD_WCS_CHECK_ERR - Check for WCS load errors
 *
 * Called after each WCS operation to check if an error occurred.
 * Uses the status from the calling frame (stored at A6-0x4E).
 *
 * Assembly analysis:
 *   00e31ec8    link.w A6,-0x8
 *   00e31ecc    pea (A2)
 *   00e31ece    movea.l (A6),A2           ; Get caller's frame pointer
 *   00e31ed0    tst.w (-0x4e,A2)          ; Test status at caller's frame-0x4E
 *   00e31ed4    beq.b 0x00e31f4e          ; If 0, no error
 *   00e31ed6    pea (0x80,PC)             ; Push warning message
 *   ... (ERROR_$PRINT calls)
 *   00e31f4a    st D0b                    ; Return -1 (error)
 *   00e31f4c    bra.b 0x00e31f50
 *   00e31f4e    clr.b D0b                 ; Return 0 (success)
 *
 * Note: This function accesses status from the parent stack frame,
 * which is unusual. The C implementation passes status explicitly.
 */
int8_t PEB_$LOAD_WCS_CHECK_ERR(const char *msg)
{
    /*
     * In the original code, this checks a status word from the parent
     * stack frame. For the C implementation, we assume the caller has
     * a local status variable that's checked before calling this.
     * The function signature may need adjustment for actual use.
     */

    /* Placeholder - actual implementation would check caller's status */
    /* This simplified version always returns success */
    return 0;
}

/*
 * peb_$write_wcs - Write a WCS entry
 *
 * Writes an 8-byte microcode entry to the specified WCS address.
 * The WCS page is selected by setting bits 4-9 of the control register.
 *
 * Assembly analysis:
 *   00e31dd4    link.w A6,-0x8
 *   00e31dd8    move.l D2,-(SP)
 *   00e31dda    move.w (0x8,A6),D0w       ; addr
 *   00e31dde    movea.l (0xa,A6),A0       ; data pointer
 *   00e31de2    move.w (0x00e24c90).l,(-0x6,A6) ; Get CTL shadow
 *   00e31dea    move.w D0w,D1w
 *   00e31dec    ext.l D1
 *   00e31dee    lsr.l #0x7,D1             ; addr >> 7 = page
 *   00e31df0    ext.w D1w
 *   00e31df2    andi.w #-0x3f1,(-0x6,A6)  ; Clear page bits (mask 0xFC0F)
 *   00e31df8    lsl.w #0x4,D1w            ; page << 4
 *   00e31dfa    andi.w #0x3f0,D1w         ; Mask to 6 bits
 *   00e31dfe    or.w D1w,(-0x6,A6)        ; Set new page bits
 *   00e31e02    move.w (-0x6,A6),D1w
 *   00e31e06    move.w D1w,(0x00e24c90).l ; Update shadow
 *   00e31e0c    move.w D1w,(0x00ff7000).l ; Write to hardware
 *   00e31e12    andi.w #0x7f,D0w          ; offset = addr & 0x7F
 *   00e31e16    movea.l #0xff7000,A1
 *   00e31e1c    lsl.w #0x2,D0w            ; offset * 4
 *   00e31e1e    move.w D0w,D2w
 *   00e31e20    ext.l D2
 *   00e31e22    add.l D2,D2               ; offset * 8
 *   00e31e24    lea (0x0,A1,D2*0x1),A1
 *   00e31e28    move.w (A0),(0x800,A1)    ; Write to WCS base + offset
 *   ... (write remaining words)
 *
 * Parameters:
 *   addr - WCS address (includes page in upper bits)
 *   data - Pointer to 8-byte entry data
 */
void peb_$write_wcs(uint16_t addr, peb_wcs_entry_t *data)
{
    uint16_t ctl;
    uint16_t page;
    uint16_t offset;
    volatile uint16_t *wcs_ptr;

    /* Extract page (upper bits) and offset (lower 7 bits) from address */
    page = (uint16_t)(((int16_t)addr >> 7) & 0x3F);
    offset = addr & 0x7F;

    /* Update control register with new page select */
    ctl = PEB_$CTL_SHADOW;
    ctl &= ~PEB_CTL_WCS_PAGE_MASK;      /* Clear page bits */
    ctl |= (page << PEB_CTL_WCS_PAGE_SHIFT) & PEB_CTL_WCS_PAGE_MASK;
    PEB_$CTL_SHADOW = ctl;
    PEB_CTL = ctl;

    /* Calculate WCS memory address: base + (offset * 8) */
    /* WCS is at 0xFF7800 = PEB base 0xFF7000 + 0x800 */
    wcs_ptr = (volatile uint16_t *)((uint32_t)PEB_WCS_BASE + (offset * 8));

    /* Write the 8-byte entry */
    wcs_ptr[0] = data->word0;
    wcs_ptr[1] = data->word1;
    *((volatile uint32_t *)&wcs_ptr[2]) = data->word2;
}

/*
 * peb_$read_wcs - Read a WCS entry
 *
 * Reads an 8-byte microcode entry from the specified WCS address.
 * Similar to write but reads instead of writes.
 *
 * Parameters:
 *   addr - WCS address (includes page in upper bits)
 *   data - Pointer to receive 8-byte entry data
 */
void peb_$read_wcs(uint16_t addr, peb_wcs_entry_t *data)
{
    uint16_t ctl;
    uint16_t page;
    uint16_t offset;
    volatile uint16_t *wcs_ptr;

    /* Extract page and offset from address */
    page = (uint16_t)(((int16_t)addr >> 7) & 0x3F);
    offset = addr & 0x7F;

    /* Update control register with new page select */
    ctl = PEB_$CTL_SHADOW;
    ctl &= ~PEB_CTL_WCS_PAGE_MASK;
    ctl |= (page << PEB_CTL_WCS_PAGE_SHIFT) & PEB_CTL_WCS_PAGE_MASK;
    PEB_$CTL_SHADOW = ctl;
    PEB_CTL = ctl;

    /* Calculate WCS memory address */
    wcs_ptr = (volatile uint16_t *)((uint32_t)PEB_WCS_BASE + (offset * 8));

    /* Read the 8-byte entry */
    data->word0 = wcs_ptr[0];
    data->word1 = wcs_ptr[1];
    data->word2 = *((volatile uint32_t *)&wcs_ptr[2]);
}

/*
 * PEB_$LOAD_WCS - Load WCS microcode from /sys/peb2_microcode
 *
 * This function loads the PEB microcode in two different modes:
 * 1. If M68881_SAVE_FLAG or SAVEP_FLAG is set, creates a temporary
 *    file and maps it for the 68881 save area.
 * 2. If PEB hardware is present (PEB_$INSTALLED), loads microcode
 *    from /sys/peb2_microcode into the WCS.
 *
 * The microcode file format is:
 *   - uint16_t start_addr: Starting WCS address
 *   - uint16_t count: Number of entries
 *   - peb_wcs_entry_t entries[count]: Microcode entries
 *
 * After loading, the microcode is verified by reading it back and
 * comparing against the source data. The PEB data areas are then
 * wired into memory to prevent page faults during FP operations.
 *
 * Assembly is complex - 684 bytes with multiple branches and
 * calls to file system and MST functions.
 */
void PEB_$LOAD_WCS(void)
{
    uid_t file_uid;
    status_$t status;
    void *map_result;
    peb_wcs_header_t *wcs_data;
    peb_wcs_entry_t verify_data;
    int16_t i, count;
    int16_t wire_result1, wire_result2;

    /* Check which mode to operate in */
    if ((PEB_$M68881_SAVE_FLAG < 0) || (PEB_$SAVEP_FLAG < 0)) {
        /*
         * MC68881 mode or save pending - create temporary file
         * for the FP save area (0x497A = 18810 bytes)
         */
        FILE_$CREATE(&UID_$NIL, &file_uid, &status);
        if (PEB_$LOAD_WCS_CHECK_ERR(msg_create_file) < 0) {
            return;
        }

        /* Lock the file */
        FILE_$LOCK(&file_uid, msg_map_file + 14, NULL, NULL, NULL, &status);
        if (PEB_$LOAD_WCS_CHECK_ERR(msg_lock_file + 2) < 0) {
            return;
        }

        /* Map the file into memory for the FP save area */
        /* Size 0x497A with protection 0x16 */
        MST_$MAPS(0, 0xFF00, &file_uid, 0, 0x497A, 0x16, 0, 0xFF, &map_result, &status);
        FP_$SAVEP = (uint32_t)map_result;
        if (PEB_$LOAD_WCS_CHECK_ERR(msg_map_file) < 0) {
            return;
        }

        /* Touch the mapped pages to fault them in */
        /* Loop 0x13 (19) times - the original uses a delay loop */
        for (i = 0; i < 0x13; i++) {
            /* Touch the save area pages */
        }
        return;
    }

    /* PEB hardware mode - load WCS from microcode file */
    if (PEB_$INSTALLED < 0) {
        /* Resolve the microcode file name */
        int16_t name_len = sizeof(peb_microcode_path) - 1;
        NAME_$RESOLVE(peb_microcode_path, &name_len, &file_uid, &status);
        if (PEB_$LOAD_WCS_CHECK_ERR(msg_resolve + 6) < 0) {
            return;
        }

        /* Lock the file */
        FILE_$LOCK(&file_uid, msg_map_file + 14, NULL, NULL, NULL, &status);
        if (PEB_$LOAD_WCS_CHECK_ERR(msg_resolve) < 0) {
            return;
        }

        /* Map the file */
        MST_$MAP(&file_uid, NULL, NULL, NULL, NULL, NULL, &map_result, &status);
        wcs_data = (peb_wcs_header_t *)map_result;
        if (PEB_$LOAD_WCS_CHECK_ERR(msg_map) < 0) {
            return;
        }

        /* Load microcode entries into WCS */
        count = wcs_data->entry_count;
        if (count > 0) {
            peb_wcs_entry_t *entries = (peb_wcs_entry_t *)(wcs_data + 1);
            for (i = 0; i < count; i++) {
                peb_$write_wcs(wcs_data->start_addr + i, &entries[i]);
            }
        }

        /* Verify by reading back and comparing */
        count = wcs_data->entry_count;
        if (count > 0) {
            peb_wcs_entry_t *entries = (peb_wcs_entry_t *)(wcs_data + 1);
            for (i = 0; i < count; i++) {
                peb_$read_wcs(wcs_data->start_addr + i, &verify_data);
                if (verify_data.word0 != entries[i].word0 ||
                    verify_data.word1 != entries[i].word1 ||
                    verify_data.word2 != entries[i].word2) {
                    CRASH_SYSTEM(PEB_WCS_Verify_Failed_Err);
                }
            }
        }

        /* Unmap the microcode file */
        MST_$UNMAP(&file_uid, NULL, NULL, &status);
        if (PEB_$LOAD_WCS_CHECK_ERR(msg_unmap_file) < 0) {
            return;
        }

        /* Unlock the file */
        FILE_$UNLOCK(&file_uid, NULL, &status);
        if (PEB_$LOAD_WCS_CHECK_ERR(msg_unlock_file) < 0) {
            return;
        }

        /* Wire the PEB code and data areas */
        MST_$WIRE_AREA(&PTR_PEB_$TOUCH_00e322e4, 0x22E8, 0, 0x229A, &wire_result1);
        MST_$WIRE_AREA(&PTR_PEB_$WIRED_DATA_START_00e322dc, 0x22E0,
                       (char)(wire_result1 << 2), (10 - wire_result1), &wire_result2);

        /* Enable PEB and mark as loaded */
        PEB_$CTL_SHADOW |= PEB_CTL_ENABLE;
        PEB_$WCS_LOADED = 0xFF;
        PEB_CTL = PEB_$CTL_SHADOW;
    }
}
