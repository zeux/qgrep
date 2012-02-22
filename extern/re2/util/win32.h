#ifndef RE2_UTIL_WIN32_H_
#define RE2_UTIL_WIN32_H_

// Required libraries, note that the #pragma preprocessor directive is only valid for Microsoft C++ Compilers.
// Ergo, you need to specify additional linkage manually with other compilers such as MinGW or Cygwin.
#pragma comment(lib,"Kernel32.lib")
#pragma comment(lib,"psapi.lib")

// Ommit seldom used headers.
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN

/**
 * In util/mutex.h:33 there's a check to test for TryEnterCriticalSection.
 * This seems fairly odd since the implementation is already done,
 * so let's move that out.
 * -
 * # ifdef GMUTEX_TRYLOCK
 *   // We need Windows NT or later for TryEnterCriticalSection().  If you
 *   // don't need that functionality, you can remove these _WIN32_WINNT
 *   // lines, and change TryLock() to assert(0) or something.
 * #   ifndef _WIN32_WINNT
 * #     define _WIN32_WINNT 0x0400
 * #   endif
 * # endif
 **/

#define NOGDI    // GDI defines the ERROR macro which collides with the Google logging mechanism.
#define NOMINMAX

#include <Windows.h>
#include <Psapi.h>

// POSIX replecation features
#ifndef va_copy
#define va_copy(s,d) ((s)=(d))
#endif // va_copy

#ifndef snprintf
#define snprintf _snprintf
#endif // snprintf

#ifndef strtof
#define strtof strtod
#endif // strtof

#ifndef strtoll
#define strtoll _strtoi64
#endif // strtoll

#ifndef strtoull
#define strtoull _strtoui64
#endif // strtoull

#ifndef _OFF_T_DEFINED
typedef long off_t;
#endif // _OFF_T_DEFINED

#ifndef _SYS_MMAN_H_
#define _SYS_MMAN_H
/* Minimal POSIX-like memory mapping implementation */
#define PROT_READ  0x01
#define PROT_WRITE 0x02

#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x0800

void *mmap(void *, size_t, int, int, int, off_t);
int munmap(void *, size_t);

#endif // _SYS_MMAN_H_

#endif // RE2_UTIL_WIN32_H_