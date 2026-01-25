/*
 * FIM_$ADVANCE_SIGNAL_DELIVERY - Advance signal delivery mechanism
 *
 * Updates the quit value for the current address space from the quit
 * event counter, clears the quit inhibit flag, and advances the
 * delivery event counter to signal that signal delivery can proceed.
 *
 * Called during signal acknowledge and signal delivery operations.
 *
 * Original address: 0x00e0a96c
 */

#include "fim/fim.h"
#include "mmu/mmu.h"

/*
 * FIM data structure addresses (M68K specific)
 */
#if defined(M68K)
    /* FIM_$QUIT_EC - Quit event counter array, indexed by AS_ID * 12 */
    #define FIM_QUIT_EC_BASE        0xE22002
    #define FIM_QUIT_EC(asid)       ((ec_$eventcount_t*)(FIM_QUIT_EC_BASE + (asid) * 12))

    /* FIM_$QUIT_VALUE - Quit value array, indexed by AS_ID * 4 */
    #define FIM_QUIT_VALUE_BASE     0xE222BA
    #define FIM_QUIT_VALUE(asid)    (*(uint32_t*)(FIM_QUIT_VALUE_BASE + (asid) * 4))

    /* FIM_$QUIT_INH - Quit inhibit flags array, indexed by AS_ID */
    #define FIM_QUIT_INH_BASE       0xE2248A
    #define FIM_QUIT_INH(asid)      (*(uint8_t*)(FIM_QUIT_INH_BASE + (asid)))

    /* FIM_$DELIV_EC - Delivery event counter array, indexed by AS_ID * 12 */
    #define FIM_DELIV_EC_BASE       0xE224C4
    #define FIM_DELIV_EC(asid)      ((ec_$eventcount_t*)(FIM_DELIV_EC_BASE + (asid) * 12))
#else
    /* Non-M68K stubs */
    static ec_$eventcount_t fim_dummy_ec;
    static uint32_t fim_dummy_value;
    static uint8_t fim_dummy_inh;
    #define FIM_QUIT_EC(asid)       (&fim_dummy_ec)
    #define FIM_QUIT_VALUE(asid)    (fim_dummy_value)
    #define FIM_QUIT_INH(asid)      (fim_dummy_inh)
    #define FIM_DELIV_EC(asid)      (&fim_dummy_ec)
#endif

void FIM_$ADVANCE_SIGNAL_DELIVERY(void)
{
    int16_t asid;

    /* Get current address space ID */
    asid = PROC1_$AS_ID;

    /* Copy current value from quit EC to quit value array */
    FIM_QUIT_VALUE(asid) = *(uint32_t*)FIM_QUIT_EC(asid);

    /* Clear quit inhibit flag */
    FIM_QUIT_INH(asid) = 0;

    /* Advance the delivery event counter to allow signal delivery */
    EC_$ADVANCE(FIM_DELIV_EC(asid));
}
