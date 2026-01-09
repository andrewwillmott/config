//
// Defs.hpp
//
// Basic definitions and macros for Config library
//
// Andrew Willmott
//

#ifndef HL_DEFS_HPP
#define HL_DEFS_HPP

#include <stdint.h>  // IWYU pragma: keep, for int32 etc.

#define HL_ASSERT(X)
#define HL_ASSERT_F(...)
#define HL_ERROR(X)

#define HL_SIZE(M_ARRAY) sizeof(M_ARRAY) / sizeof(M_ARRAY[0]) // size of static array
#define HL_SIZE_I(M_ARRAY) int(sizeof(M_ARRAY) / sizeof(M_ARRAY[0])) // size of static array as int

#if defined(_WIN32) || defined(_WIN64)
    #define HL_WINDOWS 1
#else
    #define HL_UNIX 1
#endif

namespace HL
{
    template <typename T> int size_i(const T& container) { return (int) container.size(); }
}

#endif
