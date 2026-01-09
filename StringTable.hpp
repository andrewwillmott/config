//
// StringTable.hpp
//
// Basic string table for reference-counted strings
//
// Andrew Willmott
//

#ifndef HL_STRING_TABLE_HPP
#define HL_STRING_TABLE_HPP

#include "Value.hpp"  // for StringValue... hmm.

namespace HL
{
    struct StringTableData;

    struct StringTable : RefCounted
    {
        StringValue* GetString(const char* str);  // Returns ref-counted string from table, adding if necessary

        void Flush();  // Flushes all entries that are unreferenced
        void Clear();  // Clears all entries

    protected:
        StringTable() {} // CreateStringTable() please
    };

    typedef AutoRef<StringTable> StringTableRef;

    StringTable* CreateStringTable();  // Creates a new string table instance
}

#endif
