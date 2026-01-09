//
// ValueJson.hpp
//
// Support for reading/writing json to/from Value
//
// Andrew Willmott
//

#ifndef HL_VALUE_JSON_H
#define HL_VALUE_JSON_H

#include "String.hpp"

namespace HL
{
    class Value;
    struct StringTable;

    // File/string loading
    bool LoadJsonFile(const char* path, Value* value, String* errors = 0, StringTable* st = 0);
    bool LoadJsonFile(FILE*       file, Value* value, String* errors = 0, StringTable* st = 0);
    bool LoadJsonText(const char* text, Value* value, String* errors = 0, StringTable* st = 0);
    bool LoadJsonText(const char* textBegin, const char* textEnd, Value* value, String* errors = 0, StringTable* st = 0);

    // File/string saving

    enum InfNanType { kInfNanC, kInfNanJS, kInfNanNull };  // How to emit floating point specials: inf/nan (C), Infinity/NaN (Javascript), or as a null value.

    struct JsonFormat
    {
        int  indent        = 2;      // Indent level. -1 = single line, -2 = single line with spaces removed.
        bool quoteKeys     = false;  // Whether to quote all keys (strict json) or use bare keys where possible (json5 etc.)
        int  arrayMargin   = 74;     // Margin to use in wrapping arrays
        int  maxPrecision  = 6;      // Max precision to use for reals
        bool trimZeroes    = true;   // Remove trailing zeroes for a minimal text representation

        InfNanType infNaN  = kInfNanJS;  // How to emit floating point specials
    };

    extern JsonFormat kJsonFormatDefault; // json5 compatible
    extern JsonFormat kJsonFormatStrict;  // Can be parsed by 'strict' json parsers

    String AsJson(const Value& v, int indent = -1, JsonFormat format = kJsonFormatDefault);  // quick conversion to string

    bool SaveAsJson(const char*  path, const Value& v, const JsonFormat& format = kJsonFormatDefault);
    bool SaveAsJson(FILE*        out,  const Value& v, const JsonFormat& format = kJsonFormatDefault);
    void SaveAsJson(String*      text, const Value& v, const JsonFormat& format = kJsonFormatDefault);
}

#endif
