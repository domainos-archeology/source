/*
 * misc/string.h - String/memory function wrappers
 *
 * Provides memset/memcpy using compiler builtins for freestanding environment.
 * These work without libc since they're handled by the compiler.
 */

#ifndef MISC_STRING_H
#define MISC_STRING_H

/*
 * Use compiler builtins for memory operations.
 * -fno-builtin prevents automatic recognition of memset/memcpy calls,
 * but __builtin_* versions are always available.
 */
/*
 * A host build that has already seen <string.h> may have these as macros of
 * its own (macOS spells them __memcpy_chk_func); leave those alone so the
 * unit tests do not have to fight a redefinition warning.  The freestanding
 * m68k build has no such definitions, so it always gets the builtins.
 */
#ifndef memset
#define memset(dst, val, n)   __builtin_memset((dst), (val), (n))
#endif
#ifndef memcpy
#define memcpy(dst, src, n)   __builtin_memcpy((dst), (src), (n))
#endif
#ifndef memmove
#define memmove(dst, src, n)  __builtin_memmove((dst), (src), (n))
#endif

#endif /* MISC_STRING_H */
