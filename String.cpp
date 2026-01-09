//
// String.cpp
//
// String utilities
//
// Andrew Willmott
//

#include "String.hpp"
#include <vector>
#include <string.h>
#include <stdarg.h>

#if HL_WINDOWS
    #define strtok_r strtok_s
#endif

using namespace HL;

int HL::VFormat(String* s, const char* format, va_list args)
{
    va_list argsCopy;
    va_copy(argsCopy, args);

    char bufferStack[1024];
    int length = vsnprintf(bufferStack, HL_SIZE(bufferStack), format, args);

    if (length <= 0)
        s->clear();
    else if (length < HL_SIZE(bufferStack))      // length does not include terminator, so <
        s->assign(bufferStack);
    else
    {
        s->resize(length);
        length = vsnprintf(&s->front(), length + 1, format, argsCopy);
    }

    return length;
}

int HL::Format(String* s, const char* format, ...)
{
    va_list args;
    va_start(args, format);

    int result = VFormat(s, format, args);

    va_end(args);

    return result;
}

int HL::AppendVFormat(String* s, const char* format, va_list args)
{
    va_list argsCopy;
    va_copy(argsCopy, args);

    char bufferStack[1024];
    int length = vsnprintf(bufferStack, HL_SIZE(bufferStack), format, args);

    if (length <= 0)
        ;
    else if (length < HL_SIZE_I(bufferStack))      // length does not include terminator, so <
        s->append(bufferStack);
    else
    {
        size_t origLength = s->length();
        s->resize(origLength + length);
        length = vsnprintf(&s->front() + origLength, length + 1, format, argsCopy);
    }

    return length;
}

int HL::AppendFormat(String* s, const char* format, ...)
{
    va_list args;
    va_start(args, format);

    int result = AppendVFormat(s, format, args);
    va_end(args);

    return result;
}

String HL::Format(const char* format, ...)
{
    String result;
    va_list args;
    va_start(args, format);
    VFormat(&result, format, args);
    va_end(args);
    return result;
}

String HL::VFormat(const char* format, va_list args)
{
    String result;
    VFormat(&result, format, args);
    return result;
}

bool HL::StartsWith(const char* s, const char* prefix)
{
    size_t prefixLen = strlen(prefix);
    return strncmp(s, prefix, prefixLen) == 0;
}

Strings HL::Split(const char* line, const char* separators)
{
    Strings result;
    std::vector<char> scratch;
    const char* s = line;

    size_t len = strlen(line);
    scratch.insert(scratch.begin(), s, s + len + 1);

    char* savePtr = 0;
    s = strtok_r(scratch.data(), separators, &savePtr);
    if (!s)
        return result;

    result.push_back(s);

    while ((s = strtok_r(0, separators, &savePtr)))
        result.push_back(s);

    return result;
}
