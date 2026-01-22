//
// String.hpp
//
// String utilities
//
// Andrew Willmott
//

#ifndef HL_STRING_HPP
#define HL_STRING_HPP

#include "Defs.hpp"
#include <string>
#include <vector>

namespace HL
{
    // String
    struct String : public std::string
    {
        using std::string::string;
        String(const std::string& s) : std::string(s) {}  // constructor inheritance above doesn't work -- templating?

        void operator =(const std::string& s) { std::string::operator=(s); }
        void operator =(const char* s) { std::string::operator=(s); }
        operator const char*() const { return c_str(); }
    };
    typedef std::vector<String> Strings;

    int Format (String* s, const char* format, ...);
    int VFormat(String* s, const char* format, va_list args);

    int AppendFormat (String* s, const char* format, ...);
    int AppendVFormat(String* s, const char* format, va_list args);

    String VFormat(const char* format, va_list args);
    String Format (const char* format, ...);

    int Compare (const char* s0, const char* s1);
    int CompareI(const char* s0, const char* s1);

    bool Equal (const char* s0, const char* s1);
    bool EqualI(const char* s0, const char* s1);

    bool StartsWith(const char* s, const char* prefix);

    // String ID (hash)
    typedef uint32_t ID;
    constexpr ID kIDNull = 0;
    constexpr ID kIDInvalid = ~0u;

    ID IDFromString(const char* s);

    Strings Split(const char* line, const char* separators = " \t");  // variant of split()
}


//
// Inlines
//

namespace
{
    constexpr uint32_t kFNVPrime32  = 0x01000193;
    constexpr uint32_t kFNVOffset32 = 0x811C9DC5;
}

inline HL::ID HL::IDFromString(const char* s)
{
    ID hashValue = kFNVOffset32;
    char c;

    while ((c = *s++) != 0)
        hashValue = (hashValue ^ tolower(c)) * kFNVPrime32;

    return hashValue | 0x80000000;
}

inline bool HL::Equal(const char* s0, const char* s1)
{
    while (*s0 == *s1)
    {
        if (*s0 == 0)
            return true;

        s0++;
        s1++;
    }

    return false;
}

inline bool HL::EqualI(const char* s0, const char* s1)
{
    while (tolower(*s0) == tolower(*s1))
    {
        if (*s0 == 0)
            return true;

        s0++;
        s1++;
    }

    return false;
}

inline int HL::Compare(const char* s0, const char* s1)
{
    int c0, c1;

    while (true)
    {
        c0 = *s0++;
        c1 = *s1++;

        if (c0 != c1 || c0 == 0)
            break;
    }

    return c0 - c1;
}

inline int HL::CompareI(const char* s0, const char* s1)
{
    int c0, c1;

    while (true)
    {
        c0 = tolower(*s0++);
        c1 = tolower(*s1++);

        if (c0 != c1 || c0 == 0)
            break;
    }

    return c0 - c1;
}

#endif
