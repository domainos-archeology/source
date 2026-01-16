/*
 * FILE_$CREATE_IT - Create a typed file
 *
 * Original address: 0x00E5D79E
 *
 * Creates a file with a specified type. Only types 0, 4, and 5 are valid.
 */

#include "file/file_internal.h"
#include "uid/uid.h"

/* Status code for invalid argument */
#define status_$file_invalid_arg    0x000F0014

/*
 * FILE_$CREATE_IT
 *
 * Assembly at 0x00E5D79E:
 *   link.w A6,-0x4
 *   movem.l { A3 A2},-(SP)
 *   movea.l (0x8,A6),A2         ; type_ptr
 *   movea.l (0x1c,A6),A3        ; status_ret
 *   cmpi.w #0x5,(A2)            ; check if type == 5
 *   seq D0b                     ; D0 = (type == 5)
 *   cmpi.w #0x4,(A2)            ; check if type == 4
 *   seq D1b                     ; D1 = (type == 4)
 *   or.b D1b,D0b                ; D0 |= D1
 *   tst.w (A2)                  ; check if type == 0
 *   seq D1b                     ; D1 = (type == 0)
 *   or.b D1b,D0b                ; D0 |= D1
 *   bmi.b valid_type            ; if any bit set (negative), type is valid
 *   move.l #0xf0014,(A3)        ; status = invalid_arg
 *   bra.b done
 * valid_type:
 *   ... call FILE_$PRIV_CREATE ...
 */
uint8_t FILE_$CREATE_IT(int16_t *type_ptr, uid_t *type_uid, uid_t *dir_uid,
                        uint32_t *size_ptr, uid_t *file_uid_ret, status_$t *status_ret)
{
    int16_t file_type;
    uint8_t result;

    file_type = *type_ptr;

    /*
     * Validate file type: only 0, 4, and 5 are allowed.
     *
     * The original code uses a clever trick: it sets bits in a byte
     * for each valid type, then checks if the result is negative
     * (has high bit set). This works because seq sets 0xFF on true.
     *
     * We use a simpler explicit check.
     */
    if (file_type == 0 || file_type == 4 || file_type == 5) {
        result = (uint8_t)FILE_$PRIV_CREATE(file_type, type_uid, dir_uid,
                                             file_uid_ret, *size_ptr, 0, NULL, status_ret);
    } else {
        *status_ret = status_$file_invalid_arg;
        result = 0;
    }

    return result;
}
