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
#define memset(dst, val, n)   __builtin_memset((dst), (val), (n))
#define memcpy(dst, src, n)   __builtin_memcpy((dst), (src), (n))
#define memmove(dst, src, n)  __builtin_memmove((dst), (src), (n))

#endif /* MISC_STRING_H */
