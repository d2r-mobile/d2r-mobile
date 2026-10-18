// Generated ARM64 entry points are not C functions and have no UBSAN function
// type metadata at entry-8. Suppress only the function-type check at the exact
// test driver that enters generated code; all ASAN/UBSAN memory checks remain.
#pragma once
#if defined(__clang__)
#define DM_NO_UBSAN_FUNCTION __attribute__((no_sanitize("function")))
#else
#define DM_NO_UBSAN_FUNCTION
#endif
