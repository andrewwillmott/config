//
// StringTable.cpp
//
// Basic string table for reference-counted strings
//
// Andrew Willmott
//

#include "StringTable.hpp"

#include "RefCount.hpp"

#include "external/unordered_dense.h" // requires 64-bit well-mixed hash

using namespace HL;

namespace
{
    const uint32_t kHashOffset32 = UINT32_C(0x811C9DC5);
    const uint32_t kHashPrime32 = 0x01000193;

    uint32_t StrHashU32 (const char* s, uint32_t hashValue = kHashOffset32); // Basic string hash. 'hashValue' can be used to chain hashes.

    inline uint32_t StrHashU32(const char* s, uint32_t hashValue)
    {
        const uint8_t* data = (const uint8_t*) s;
        uint32_t c;

        while ((c = *data++) != 0)
            hashValue = (hashValue ^ c) * kHashPrime32;

        return hashValue;
    }

    // We use this both for convenience and to support delayed creation of
    // a StringValue from a const char* on lookup.
    struct STStringRef : public AutoRef<StringValue>
    {
        using AutoRef<StringValue>::AutoRef;

        STStringRef(const char* s) : AutoRef<StringValue>(CreateStringValue(s)) {}
    };

    struct StringSetHash
    {
        typedef size_t HashType;

        using is_transparent = void;
        using is_avalanching = void;

        HashType operator () (const StringValueRef& s) const
        {
            return operator()(s->c_str());
        }

        HashType operator () (const char* s) const
        {
            HashType hash = StrHashU32(s) * UINT64_C(0x9ddfea08eb382d69);
            return hash;
        }
    };

    struct StringSetEqual
    {
        using is_transparent = void;

        bool operator () (const StringValueRef& a, const StringValueRef& b) const { return strcmp(a->c_str(), b->c_str()) == 0; }
        bool operator () (const StringValueRef& a, const char*           b) const { return strcmp(a->c_str(), b) == 0; }
        bool operator () (const char*           a, const StringValueRef& b) const { return strcmp(a, b->c_str()) == 0; }
    };

    struct StringSetLess
    {
        using is_transparent = void;

        bool operator () (const StringValueRef& a, const StringValueRef& b) const { return strcmp(a->c_str(), b->c_str()) < 0; }
        bool operator () (const StringValueRef& a, const char*           b) const { return strcmp(a->c_str(), b) < 0; }
        bool operator () (const char*           a, const StringValueRef& b) const { return strcmp(a, b->c_str()) < 0; }
    };
}

namespace HL
{
    struct StringTableImpl : public StringTable
    {
        typedef STStringRef StringKey;

        ankerl::dense_hash_set<StringKey, StringSetHash, StringSetEqual> mStrings;

        ~StringTableImpl()
        {
            mStrings.clear();
        }
    };
}

StringValue* StringTable::GetString(const char* str)
{
    StringTableImpl* self = static_cast<StringTableImpl*>(this);

    auto it = self->mStrings.insert(str).first;

    return *it;
}

void StringTable::Flush()
{
    StringTableImpl* self = static_cast<StringTableImpl*>(this);

    // Remove unreferenced strings
    for (auto it = self->mStrings.begin(); it != self->mStrings.end(); )
    {
        if ((*it)->RefCount() == 1)
            self->mStrings.erase(it++);
        else
            ++it;
    }
}

void StringTable::Clear()
{
    StringTableImpl* self = static_cast<StringTableImpl*>(this);
    self->mStrings.clear();
}

StringTable* HL::CreateStringTable()
{
    return new StringTableImpl;
}
