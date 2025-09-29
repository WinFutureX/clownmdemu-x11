/* for compatibility with musl libc */
#if defined(__unix__) && defined(__linux__)
#define sigjmp_buf jmp_buf
#endif

#include "common/unity.c"
