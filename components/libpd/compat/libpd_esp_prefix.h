/* Force-included into every libpd/Pd translation unit (see ../CMakeLists.txt).
 * Fills gaps between Pd's POSIX assumptions and ESP-IDF's newlib/lwIP. */
#pragma once

/* IDF builds with _GNU_SOURCE, which makes newlib define _LARGEFILE64_SOURCE.
 * Pd then maps open/lseek/stat to the *64 variants, which newlib lacks.
 * off_t is already 64-bit here, so the plain calls are fine. */
#include <sys/features.h>
#undef _LARGEFILE64_SOURCE

/* lwIP provides getaddrinfo() but not gai_strerror(). */
#define gai_strerror(err) "getaddrinfo failed"
