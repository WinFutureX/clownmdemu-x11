/* 
 * some linux distros define __unix__ alongside __linux__ 
 * we'll throw away the __unix__ define so we can force the use of setjmp instead of sigsetjmp
 */
#if defined(__unix__) && defined(__linux__)
#undef __unix__
#endif

#ifdef __GNUC__
/* to shut up "unused function" warnings from dr_flac, dr_mp3, dr_wav and libchdr */
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#include "common/unity.c"
