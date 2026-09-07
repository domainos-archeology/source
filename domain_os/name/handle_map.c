/*
 * name/handle_map.c - host-side directory-handle registry
 *
 * NAME_$LOCK_DIR hands back the virtual address at which a directory is
 * mapped and stores it in a 32-bit word, because m68k pointers are 32 bits
 * wide.  Turning that word back into a pointer (0xE54B06:
 * `movea.l (A0),A1 ; cmpi.w #0x1,(A1)`) is an identity cast on m68k, so this
 * whole file compiles to nothing there.  A host whose pointers are wider
 * needs a translation instead, which is what lives here; dir/ uses the same
 * pair for the directory handle that dir_$add_entry receives at A6+0x08.
 */

#include "name/name.h"

#if !defined(ARCH_M68K)
/*
 * Host builds only: a kernel virtual address is 32 bits wide, so a host
 * pointer does not survive the round trip through a directory handle.  Keep a
 * tiny registry so NAME_$HANDLE_TO_PTR / NAME_$PTR_TO_HANDLE are inverses of
 * one another on the host.  On m68k both macros are identity casts and none of
 * this exists.
 */
#define NAME_$HANDLE_MAP_SIZE   16

static struct {
    uint32_t    handle;
    void       *ptr;
} name_$handle_map[NAME_$HANDLE_MAP_SIZE];

static uint32_t name_$handle_next = 0x00100000;

uint32_t name_$ptr_to_handle(const void *ptr)
{
    int i;

    if (ptr == NULL) {
        return 0;
    }
    for (i = 0; i < NAME_$HANDLE_MAP_SIZE; i++) {
        if (name_$handle_map[i].ptr == ptr) {
            return name_$handle_map[i].handle;
        }
    }
    for (i = 0; i < NAME_$HANDLE_MAP_SIZE; i++) {
        if (name_$handle_map[i].ptr == NULL) {
            name_$handle_map[i].ptr = (void *)(uintptr_t)ptr;
            name_$handle_map[i].handle = name_$handle_next;
            name_$handle_next += 0x10000;
            return name_$handle_map[i].handle;
        }
    }
    return 0;
}

void *name_$handle_to_ptr(uint32_t handle)
{
    int i;

    for (i = 0; i < NAME_$HANDLE_MAP_SIZE; i++) {
        if (name_$handle_map[i].ptr != NULL &&
            name_$handle_map[i].handle == handle) {
            return name_$handle_map[i].ptr;
        }
    }
    return NULL;
}
#endif /* !ARCH_M68K */
