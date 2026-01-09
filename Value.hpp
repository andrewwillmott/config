//
//  Value.hpp
//
//  Represents classic bool/int/float/string/array/object value
//
//  Andrew Willmott
//

#ifndef HL_VALUE_H
#define HL_VALUE_H

// #define HL_VALUE_COMMENTS  // Whether to allow preservation of comments from source files

#include "RefCount.hpp"
#include "String.hpp"
#include "vector_map.hpp"

#include <stdint.h>

namespace HL
{
    struct EnumInfo;

    // Type of the value held by a Value object.
    enum ValueType : uint8_t
    {
        kValueNull,    // null value, can be converted to any other type on assignment
        kValueBool,    // bool value
        kValueInt,     // signed 32-bit integer value   (range: INT32_MIN .. INT32_MAX)
        kValueUInt,    // unsigned 32-bit integer value (range: 0 .. UINT32_MAX)
        kValueInt64,   // signed 64-bit integer value   (range: INT64_MIN .. INT64_MAX)
        kValueUInt64,  // unsigned 64-bit integer value (range: 0 .. UINT64_MAX)
        kValueDouble,  // floating point value
        kValueString,  // UTF-8 string value
        kValueArray,   // array value
        kValueObject   // object value (key -> value map).
    };

    typedef const char* ValueKey;

#ifdef HL_VALUE_COMMENTS
    enum CommentPlacement : uint8_t
    {
        kCommentBefore = 0,       // a comment placed on the line before a value
        kCommentAfterOnSameLine,  // a comment just after a value on the same line
        kCommentAfter,            // a comment on the line after a value (only make sense for root value)
        kNumberOfCommentPlacements
    };
#endif


    // --- Value --------------------------------------------------------------

    class StringValue;
    class ArrayValue;
    class ObjectValue;
    typedef ObjectValue Members;
    class Value;
    typedef std::vector<Value> Values;

    class Value
    // Represents a generically typed value, of type Type() = ValueType.
    // Note: this is designed to fail gracefully rather than asserting or crashing.
    // - A null object will be returned for bogus queries. (Non-existent array value or object member.)
    // - An array/object write will fail silently on the wrong kind of value.
    // The idea is to avoid having to write a lot of error-checking code.
    {
    public:
        Value();
        Value(const Value& other);
        Value(Value&& other);

        explicit Value(ValueType          type);   // Default value of given type
        explicit Value(bool               value);
        explicit Value(int32_t            value);
        explicit Value(uint32_t           value);
        explicit Value(int64_t            value);
        explicit Value(uint64_t           value);
        explicit Value(double             value);
        explicit Value(const char*        value);
        explicit Value(const std::string& value);
        explicit Value(StringValue*       value);  // Adds a reference to 'value' rather than copying.
        explicit Value(ArrayValue*        value);  // Adds a reference to 'value' rather than copying.
        explicit Value(ObjectValue*       value);  // Adds a reference to 'value' rather than copying.

        ~Value();

        void operator = (const Value&       value);
        void operator = (Value&&            value);

        void operator = (bool               value);
        void operator = (int32_t            value);
        void operator = (uint32_t           value);
        void operator = (int64_t            value);
        void operator = (uint64_t           value);
        void operator = (double             value);

        void operator = (const char*        value);
        void operator = (const std::string& value);
        void operator = (const Values&      value);
        void operator = (const StringValue& value);
        void operator = (const ArrayValue&  value);
        void operator = (const ObjectValue& value);

        void operator = (StringValue*       value);  // Adds a reference to 'value' rather than copying.
        void operator = (ArrayValue*        value);  // Adds a reference to 'value' rather than copying.
        void operator = (ObjectValue*       value);  // Adds a reference to 'value' rather than copying.

        void Swap(Value& other);  // Swap values. Currently, comments are intentionally not swapped, for both logic and efficiency.

        ValueType      Type() const;

        bool           IsNull()     const;
        bool           IsBool()     const;
        bool           IsInt()      const;
        bool           IsUInt()     const;
        bool           IsIntegral() const;     // true if bool, int, or uint
        bool           IsFloat()    const;
        bool           IsDouble()   const;
        bool           IsNumeric()  const;     // true if integral or double
        bool           IsString()   const;
        bool           IsArray()    const;     // true if an array or null (convertible to an array on write)
        bool           IsObject()   const;     // true if an object or null (convertible to an object on write)

        bool           IsConvertibleTo(ValueType other) const;  // returns true if 'this' is losslessly convertible to 'other'

        bool           AsBool   (bool        defaultValue = false  ) const;
        int32_t        AsInt    (int32_t     defaultValue = 0      ) const;
        uint32_t       AsUInt   (uint32_t    defaultValue = 0      ) const;
        int64_t        AsInt64  (int64_t     defaultValue = 0      ) const;
        uint64_t       AsUInt64 (uint64_t    defaultValue = 0      ) const;
        float          AsFloat  (float       defaultValue = 0.0f   ) const;
        double         AsDouble (double      defaultValue = 0.0    ) const;

        const char*    AsString (const char* defaultValue = ""     ) const;  // Safe to assign to e.g. std::string class
        const char*    AsCString(const char* defaultValue = 0      ) const;  // Returns 0 if not a string.
        uint32_t       AsID     (uint32_t    defaultValue = kIDNull) const;  // IDs can be strings or 32-bit integer IDs

        const ArrayValue&  AsArray () const;   // Returns array or kNullArrayValue if not an array
        const ObjectValue& AsObject() const;   // Returns object or kNullObjectValue if not an object

        // Array API
        const Value&   Elt(int index) const; // Access an array element
        Value&         Elt(int index);       // Access an array element
        int            NumElts() const;      // Returns number of elements in array

        // Object API
        const Value&   Member         (ValueKey key) const;  // Return the member if it exists, kNullValue otherwise.
        Value&         UpdateMember   (ValueKey key);        // Return the member if it exists, otherwise insert kNullValue and return that.
        const Value*   MemberPtr      (ValueKey key) const;  // Return the member if it exists, nullptr otherwise
        Value*         UpdateMemberPtr(ValueKey key);        // Return the member if it exists, nullptr otherwise

        void           SetMember      (ValueKey key, const Value& v);  // Set given member
        bool           RemoveMember   (ValueKey key);        // Remove the named member, or return false if it doesn't exist.
        bool           HasMember      (ValueKey key) const;  // Return true if the object has a member named key.

        int            NumMembers() const;         // Returns number of members, or 0 if is not an object.
        const char*    MemberName (int i) const;   // Returns i'th member name
        uint32_t       MemberID   (int i) const;   // Returns i'th member id
        const Value&   MemberValue(int i) const;   // Returns i'th member value

        // Syntactic sugar for Member/UpdateMember(key1, key, key3). See also MemberPath.
        template<typename... T> const Value&  Member      (ValueKey key, T... args) const;
        template<typename... T> Value&        UpdateMember(ValueKey key, T... args);

        // Arrays + Objects, STL-alike API
        const Value&  operator [] (size_t index) const; // Access an array element. Note: [0] is ambiguous with [nullptr] for the member version below, use Elt(0) or [size_t(0)] instead.
        Value&        operator [] (size_t index);       // Access an array element

        // Note: the traditional STL map[key] API has the drawback that it is easy to accidentally insert a key when you only meant to query its existence.
        // We retain v[key] to mean a const query for 'key', but add v(key) to mean 'return a writable value', which will auto-insert under the key if it doesn't exist yet.

        const Value&  operator [] (ValueKey key) const; // Access an object value by name, returns null if there is no member with that name.
        Value&        operator () (ValueKey key);       // Write to the given key -- if it doesn't exist a null value will be auto-created ready to modify.

        const Value&  operator [] (const std::string& key) const;  // STL string for convenience
        Value&        operator () (const std::string& key);        // STL string for convenience

        size_t        size() const;         // Number of values in array or object
        bool          empty() const;        // Return true if empty array, empty object, or null; otherwise, false.
        void          clear();              // Remove all object members, string characters, or array elements.

    #ifdef HL_VALUE_COMMENTS
        // Comments
        void SetComment(const char* comment, CommentPlacement placement);  // Comments must be //... or /* ... */
        bool HasComment(CommentPlacement placement) const;                 // Returns true if a comment exists in the given place.

        const char* Comment(CommentPlacement placement) const;             // Include delimiters and embedded newlines.
    #endif

        // Comparisons
        bool operator <  (const Value& other) const;
        bool operator <= (const Value& other) const;
        bool operator >= (const Value& other) const;
        bool operator >  (const Value& other) const;

        operator bool    () const;                    // Shortcut for !IsNull()
        bool operator !  () const;                    // Shortcut for IsNull()
        bool operator == (const Value& other) const;  // Note: this is an exact comparison including type, so double(0) != int(0) for instance.
        bool operator != (const Value& other) const;

        int Compare(const Value& other) const;        // Trivalue comparison -- returns -1, 0, or 1

        // Utilities
        void         MakeNull();            // Clears value back to null
        ArrayValue&  MakeArray(int n);      // Convert to an array of the given size
        ArrayValue&  MakeArray(size_t n);   // Convert to an array of the given size
        ObjectValue& MakeObject();          // Convert to an object

        bool         ToArray();      // If null, convert to a null array, returns true in this case or if array already
        bool         ToObject();     // If null, convert to an object, returns true in this case or if object already

        ArrayValue*  AsArrayPtr();  // Returns modifiable array or nullptr. Be aware arrays may be shared between objects.
        ArrayValue*  ToArrayPtr();  // Returns modifiable array or nullptr, auto-converting null value if necessary

        ObjectValue* AsObjectPtr();  // Returns modifiable object or nullptr. Be aware objects may be shared between objects.
        ObjectValue* ToObjectPtr();  // Returns modifiable object or nullptr, auto-converting null value if necessary

        void Merge(const Value& overrides);  // Merge contents of 'overrides' into this if both are objects, otherwise 'overrides' replaces 'this', unless it is null, in which case there is no change.

        // Helpers to auto-convert to value...
        template<class T> void SetMember  (ValueKey key, const T& value) { SetMember(key, Value(value)); }
        template<class T> void SetMemberAs(ValueKey key, const T& value) { SetMember(key, AsValue(value)); }

        // Data
        union ValueHolder
        {
            int32_t      mInt32;
            uint32_t     mUInt32;
            int64_t      mInt64;
            uint64_t     mUInt64;
            double       mDouble;
            bool         mBool;
            StringValue* mString;
            ArrayValue*  mArray;
            ObjectValue* mObject;
        };

        ValueHolder mValue = { 0 };  // mValue is public for efficient access when type is known. Use the AsXXX() methods when the type is unknown, as they handle, e.g., bool/int/float conversions.

    protected:
    #ifdef HL_VALUE_COMMENTS
        String*     mComments = 0;
    #endif
        ValueType   mType = kValueNull;
        uint8_t     mDummy[3] = {0};
    };

    extern const Value kNullValue;
    extern Value       kNullValueScratch;


    // --- Helpers -------------------------------------------------------------

    const char* TypeName(ValueType type);  // Returns name for given type

    int  SetFromValue(const Value& value, int nv, int32_t  v[]);   // Fills given array from value, returns number of elts set, which will be <= nv
    int  SetFromValue(const Value& value, int nv, uint32_t v[]);
    int  SetFromValue(const Value& value, int nv, float    v[]);

    // These are implemented for the base types: bool, int, float, double, const char*/String, ID
    template<class T> bool SetFromValue(const Value& v, std::vector<T>* array);   // Pull array v out into dense array -- returns false if not convertible.
    template<class T> void SetFromArray(int n, const T array[]     , Value* v);  // Store the given array in 'v'
    template<class T> void SetFromArray(const std::vector<T>& array, Value* v);  // Store the given array in 'v'

    int                 AsEnum(const Value& value, const EnumInfo enumInfo[], int32_t defaultValue = -1);
    template<class T> T AsEnum(const Value& value, const EnumInfo enumInfo[], T defaultValue = T(0));

    bool MemberIsHidden(const char* memberName);
    #define HL_HIDDEN_KEY(M_NAME) "_" M_NAME

    const Value& MemberPath(const Value& v, const char* path);  // v.Member, but handles extended objects/array lookup, e.g. "a.b.c", "a.b[2]"
    Value& UpdateMemberPath(      Value& v, const char* path);  // v.Member, but handles extended objects/array lookup, e.g. "a.b.c", "a.b[2]"


    // Non-scalar values: String/Array/ObjectValue

    typedef RefCountedMT ValueRC;

    // --- StringValue --------------------------------------------------------

    class StringValue : public ValueRC  // Represents a fixed-size UTF8 string
    {
    public:
        const char data[1];

        operator const char*() const { return data; }

        const char* c_str() const { return data; }
        size_t      size()  const { return strlen(data); }
        bool        empty() const { return data[0] == 0; }

        bool operator == (const StringValue& other) const;
        bool operator != (const StringValue& other) const { return !(*this == other); }

        int Compare(const StringValue& other) const;  // Trivalue comparison -- returns -1, 0, or 1
    };

    typedef AutoRef<StringValue> StringValueRef;

    StringValue* CreateStringValue(const char* str, size_t len = 0); // Creates a new StringValue object


    // --- ArrayValue --------------------------------------------------------

    struct ArrayValueHeader : public ValueRC
    {
        int count = 0;

        ArrayValueHeader(int n = 0) : count(n) {}
        ~ArrayValueHeader();
    };

    class ArrayValue : public ArrayValueHeader  // Represents a fixed-size array of values
    {
    public:
        Value data[1];  // Actually of size 'count'

        const Value& operator [] (int index) const { return data[index]; }
        Value&       operator [] (int index)       { return data[index]; }

        const Value& at(int index) const { return data[index]; }
        Value&       at(int index)       { return data[index]; }

        size_t size()  const { return count; }
        bool   empty() const { return count == 0; }

        const Value* begin() const { return data; }
        const Value* end  () const { return data + count; }

        Value*       begin()       { return data; }
        Value*       end  ()       { return data + count; }

        bool operator == (const ArrayValue& other) const;
        bool operator != (const ArrayValue& other) const { return !(*this == other); }

        int Compare(const ArrayValue& other) const;  // Trivalue comparison -- returns -1, 0, or 1

        operator Values () const { return Values(data, data + count); }  // make it easy to pull out data to editable form.
    };

    typedef AutoRef<ArrayValue> ArrayValueRef;

    ArrayValue* CreateArrayValue(int count, const Value values[] = 0);  // Creates a new array of the given size, with optional source values
    ArrayValue* CreateArrayValue(const Values& values);  // Creates a copy of resizable array 'values'. Use ArrayValue.operator Values() for going the other way.

    extern const ArrayValue kNullArrayValue;


    // --- ObjectValue --------------------------------------------------------

    struct ConstNameValue { const char* name; const Value& value; };
    struct NameValue      { const char* name;       Value& value; };
    struct ConstMemberIterator;
    struct MemberIterator;
    struct StringTable;

    class ObjectValue : public ValueRC  // Represents an object -- a map from keys to values
    {
    public:
        typedef ValueKey Key;
        ~ObjectValue();

        const Value& operator [] (Key key) const;

        const Value&    Member         (Key key) const;  // Return member for the given key if it exists, kNullValue otherwise.
        Value&          UpdateMember   (Key key, StringTable* st = 0);  // Add given member if it doesn't exist already, returns for writing.
        const Value*    MemberPtr      (Key key) const;  // Returns member for writing, or 0 if it doesn't exist
        Value*          UpdateMemberPtr(Key key);        // Returns member for writing, or 0 if it doesn't exist

        Value&          UpdateMember   (StringValue* key);  // Variant that shares 'key' on insertion

        void            SetMember      (Key key, const Value& v);  // Set given member
        bool            RemoveMember   (Key key);        // Remove the named member, or return false if it doesn't exist.
        bool            HasMember      (Key key) const;  // Return true if the object has a member named key.

        void            Merge(const ObjectValue& overrides);  // Merge contents of 'overrides' into this

        bool            IsEmpty() const;             // Returns true if the object has no members.

        // index-based
        int             NumMembers() const;          // Returns number of members, or 0 if is not an object.
        int             MemberIndex(Key key) const;  // Returns index of member with given key, or -1 if not found
        const char*     MemberName (int i) const;    // Returns i'th member name
        uint32_t        MemberID   (int i) const;    // Returns i'th member id
        ValueKey        MemberKey  (int i) const;    // Returns i'th member key
        const Value&    MemberValue(int i) const;    // Returns i'th member value
        Value&          MemberValue(int i);          // Returns i'th member value
        ConstNameValue  MemberInfo (int i) const;    // Return combined name/value
        NameValue       MemberInfo (int i);          // Return combined name/value
        void            RemoveMembers();             // Remove all members

        uint32_t        ModCount() const;
        void            IncModCount();

        void            Swap(ObjectValue* other);

        // ranged for
        ConstMemberIterator begin() const;
        ConstMemberIterator end  () const;
        MemberIterator      begin();
        MemberIterator      end  ();

        // comparison
        bool operator == (const ObjectValue& other) const;
        bool operator != (const ObjectValue& other) const { return !(*this == other); }

        int Compare(const ObjectValue& other) const;  // Trivalue comparison -- returns -1, 0, or 1

    protected:
        // Data
        struct MemberMapEqual
        {
            bool operator () (const AutoRef<StringValue>& a, const AutoRef<StringValue>& b) const { return strcmp(a->c_str(), b->c_str()) < 0; }
            bool operator () (const AutoRef<StringValue>& a, const char*                 b) const { return strcmp(a->c_str(), b         ) < 0; }
            bool operator () (const char*                 a, const AutoRef<StringValue>& b) const { return strcmp(a         , b->c_str()) < 0; }
        };

        typedef vector_map<AutoRef<StringValue>, Value, MemberMapEqual> MemberMap;

        MemberMap mMap;

        uint32_t mModCount = 0;
    };

    typedef AutoRef<ObjectValue>       ObjectRef;
    typedef AutoRef<const ObjectValue> ConstObjectRef;

    extern const ObjectValue kNullObjectValue;

    struct ConstMemberIterator
    {
        const ObjectValue* mObject;
        int                mIndex;

        bool operator!=(ConstMemberIterator it) const { return mIndex != it.mIndex; }
        void operator++()                             { ++mIndex; }
        ConstNameValue operator*() const              { return { mObject->MemberInfo(mIndex) }; }
    };

    struct MemberIterator
    {
        ObjectValue* mObject;
        int          mIndex;

        bool operator!=(MemberIterator it) const { return mIndex != it.mIndex; }
        void operator++()                        { ++mIndex; }
        NameValue operator*() const              { return { mObject->MemberInfo(mIndex) }; }
    };

    // --- Function style ------------------------------------------------------

    // For consistency with non-built-in AsXXX functions
    inline const char* AsString (const Value& v, const char* defaultValue = ""     ) { return v.AsString (defaultValue); }
    inline const char* AsCString(const Value& v, const char* defaultValue = 0      ) { return v.AsCString(defaultValue); }
    inline uint32_t    AsID     (const Value& v, uint32_t    defaultValue = kIDNull) { return v.AsID     (defaultValue); }
    inline int32_t     AsInt    (const Value& v, int32_t     defaultValue = 0      ) { return v.AsInt    (defaultValue); }
    inline uint32_t    AsUInt   (const Value& v, uint32_t    defaultValue = 0      ) { return v.AsUInt   (defaultValue); }
    inline int64_t     AsInt64  (const Value& v, int64_t     defaultValue = 0      ) { return v.AsInt64  (defaultValue); }
    inline uint64_t    AsUInt64 (const Value& v, uint64_t    defaultValue = 0      ) { return v.AsUInt64 (defaultValue); }
    inline float       AsFloat  (const Value& v, float       defaultValue = 0.0f   ) { return v.AsFloat  (defaultValue); }
    inline double      AsDouble (const Value& v, double      defaultValue = 0.0    ) { return v.AsDouble (defaultValue); }
    inline bool        AsBool   (const Value& v, bool        defaultValue = false  ) { return v.AsBool   (defaultValue); }


    // --- Inlines -------------------------------------------------------------

    inline Value::Value()
    {}

    inline Value::Value(bool value) : mType(kValueBool)
    {
        mValue.mBool = value;
    }
    inline Value::Value(int32_t value) : mType(kValueInt)
    {
        mValue.mInt32 = value;
    }
    inline Value::Value(uint32_t value) : mType(kValueUInt)
    {
        mValue.mUInt32 = value;
    }
    inline Value::Value(int64_t value) : mType(kValueInt64)
    {
        mValue.mInt64 = value;
    }
    inline Value::Value(uint64_t value) : mType(kValueUInt64)
    {
        mValue.mUInt64 = value;
    }
    inline Value::Value(double value) : mType(kValueDouble)
    {
        mValue.mDouble = value;
    }
    inline Value::Value(const char* value) : mType(kValueString)
    {
        mValue.mString = CreateStringValue(value);
        mValue.mString->AddRef();
    }
    inline Value::Value(const std::string& value) : mType(kValueString)
    {
        mValue.mString = CreateStringValue(value.c_str());
        mValue.mString->AddRef();
    }
    inline Value::Value(StringValue* value) : mType(kValueString)
    {
        mValue.mString = value;
        mValue.mString->AddRef();
    }
    inline Value::Value(ArrayValue* value) : mType(kValueArray)
    {
        mValue.mArray = value;
        mValue.mArray->AddRef();
    }
    inline Value::Value(ObjectValue* value) : mType(kValueObject)
    {
        mValue.mObject = value;
        mValue.mObject->AddRef();
    }

    inline void Value::operator = (bool value)
    {
        MakeNull();
        mType = kValueBool;
        mValue.mBool = value;
    }
    inline void Value::operator = (int32_t value)
    {
        MakeNull();
        mType = kValueInt;
        mValue.mInt32 = value;
    }
    inline void Value::operator = (uint32_t value)
    {
        MakeNull();
        mType = kValueUInt;
        mValue.mUInt32 = value;
    }
    inline void Value::operator = (int64_t value)
    {
        MakeNull();
        mType = kValueInt64;
        mValue.mInt64 = value;
    }
    inline void Value::operator = (uint64_t value)
    {
        MakeNull();
        mType = kValueUInt64;
        mValue.mUInt64 = value;
    }
    inline void Value::operator = (double value)
    {
        MakeNull();
        mType = kValueDouble;
        mValue.mDouble = value;
    }
    inline void Value::operator = (const char* value)
    {
        MakeNull();
        mType = kValueString;
        mValue.mString = CreateStringValue(value);
        mValue.mString->AddRef();
    }
    inline void Value::operator = (const std::string& s)
    {
        MakeNull();
        mType = kValueString;
        mValue.mString = CreateStringValue(s.c_str());
        mValue.mString->AddRef();
    }
    inline void Value::operator = (const Values& value)
    {
        MakeNull();
        mType = kValueArray;
        mValue.mArray = CreateArrayValue(value);
        mValue.mArray->AddRef();
    }

    inline void Value::operator = (const ArrayValue& array)
    {
        MakeNull();
        mType = kValueArray;
        mValue.mArray = CreateArrayValue(array.count, array.data);
        mValue.mArray->AddRef();
    }

    inline void Value::operator = (const ObjectValue& object)
    {
        MakeObject() = object;
    }

    inline void Value::operator = (StringValue* value)
    {
        MakeNull();
        mType = kValueString;
        mValue.mString = value;
        mValue.mString->AddRef();
    }

    inline void Value::operator = (ArrayValue* array)
    {
        MakeNull();
        mType = kValueArray;
        mValue.mArray = array;
        mValue.mArray->AddRef();
    }

    inline void Value::operator = (ObjectValue* object)
    {
        MakeNull();
        mType = kValueObject;
        mValue.mObject = object;
        mValue.mObject->AddRef();
    }

    inline ValueType Value::Type() const
    {
        return mType;
    }

    inline bool Value::IsNull() const
    {
        return mType == kValueNull;
    }
    inline bool Value::IsBool() const
    {
        return mType == kValueBool;
    }
    inline bool Value::IsInt() const
    {
        return mType == kValueInt || mType == kValueInt64;
    }
    inline bool Value::IsUInt() const
    {
        return mType == kValueUInt || mType == kValueUInt64;
    }
    inline bool Value::IsIntegral() const
    {
        return mType == kValueBool || mType == kValueInt || mType == kValueUInt || mType == kValueInt64 || mType == kValueUInt64;
    }
    inline bool Value::IsFloat() const
    {
        return mType == kValueDouble;
    }
    inline bool Value::IsDouble() const
    {
        return mType == kValueDouble;
    }
    inline bool Value::IsNumeric() const
    {
        return mType == kValueBool || mType == kValueInt || mType == kValueUInt || mType == kValueInt64 || mType == kValueUInt64 || mType == kValueDouble;
    }
    inline bool Value::IsString() const
    {
        return mType == kValueString;
    }
    inline bool Value::IsArray() const
    {
        return mType == kValueArray;
    }
    inline bool Value::IsObject() const
    {
        return mType == kValueObject;
    }

    inline const char* Value::AsString(const char* defaultValue) const
    {
        HL_ASSERT(defaultValue != nullptr);
        return AsCString(defaultValue);
    }

    inline const Value& Value::operator [] (const std::string& key) const
    {
        return operator [] (key.c_str());
    }
    inline Value& Value::operator () (const std::string& key)
    {
        return operator () (key.c_str());
    }

    inline const ArrayValue& Value::AsArray() const
    {
        if (mType == kValueArray)
            return *mValue.mArray;

        return kNullArrayValue;
    }

    inline const ObjectValue& Value::AsObject() const
    {
        if (mType == kValueObject)
            return *mValue.mObject;

        return kNullObjectValue;
    }

    inline int Value::NumElts() const
    {
        if (mType == kValueArray)
            return mValue.mArray ? int(mValue.mArray->size()) : 0;

        return 0;
    }

    inline const Value& Value::Member(ValueKey key) const
    {
        if (mType == kValueObject)
            return mValue.mObject->Member(key);

        return kNullValue;
    }

    inline Value& Value::UpdateMember(ValueKey key)
    {
        if (ToObject())
            return mValue.mObject->UpdateMember(key);

        HL_ERROR("Can't insert a member on a non-object");
        kNullValueScratch.MakeNull();
        return kNullValueScratch;
    }

    inline const Value* Value::MemberPtr(ValueKey key) const
    {
        if (IsObject())
            return mValue.mObject->MemberPtr(key);

        return nullptr;
    }

    inline Value* Value::UpdateMemberPtr(ValueKey key)
    {
        if (ToObject())
            return mValue.mObject->UpdateMemberPtr(key);

        return nullptr;
    }

    inline void Value::SetMember(ValueKey key, const Value& v)
    {
        if (ToObject())
            mValue.mObject->SetMember(key, v);
    }

    inline bool Value::RemoveMember(ValueKey key)
    {
        if (mType == kValueObject)
            return mValue.mObject->RemoveMember(key);

        return false;
    }

    inline bool Value::HasMember(ValueKey key) const
    {
        if (mType == kValueObject)
            return mValue.mObject->HasMember(key);

        return false;
    }

    inline int Value::NumMembers() const
    {
        if (mType == kValueObject)
            return mValue.mObject->NumMembers();

        return 0;
    }

    inline const char* Value::MemberName(int index) const
    {
        if (mType == kValueObject)
            return mValue.mObject->MemberName(index);

        return 0;
    }

    inline uint32_t Value::MemberID(int index) const
    {
        if (mType == kValueObject)
            return mValue.mObject->MemberID(index);

        return 0;
    }

    inline const Value& Value::MemberValue(int index) const
    {
        if (mType == kValueObject)
            return mValue.mObject->MemberValue(index);

        return kNullValue;
    }

    template<typename... T> inline const Value& Value::Member(ValueKey key, T... args) const
    {
        return Member(key).Member(args...);
    }

    template<typename... T> inline Value& Value::UpdateMember(ValueKey key, T... args)
    {
        return UpdateMember(key).UpdateMember(args...);
    }

    inline Value& Value::operator[](size_t index)
    {
        return Elt((int) index);
    }

    inline const Value& Value::operator[](size_t index) const
    {
        return Elt((int) index);
    }

    inline const Value& Value::operator[](ValueKey key) const
    {
        if (mType == kValueObject)
            return mValue.mObject->Member(key);

        return kNullValue;
    }

    inline Value& Value::operator () (ValueKey key)
    {
        if (ToObject())
            return mValue.mObject->UpdateMember(key);

        HL_ERROR("Can't insert a member on a non-object");
        kNullValueScratch.MakeNull();
        return kNullValueScratch;
    }

    inline Value::operator bool() const
    {
        return mType != kValueNull;
    }

    inline bool Value::operator!() const
    {
        return mType == kValueNull;
    }

    inline bool Value::operator !=(const Value& other) const
    {
        return !(*this == other);
    }

    inline ArrayValue* Value::AsArrayPtr()
    {
        return mType == kValueArray ? mValue.mArray : nullptr;
    }

    inline ArrayValue* Value::ToArrayPtr()
    {
        return ToArray() ? mValue.mArray : nullptr;
    }

    inline ObjectValue* Value::AsObjectPtr()
    {
        return mType == kValueObject ? mValue.mObject : nullptr;
    }

    inline ObjectValue* Value::ToObjectPtr()
    {
        return ToObject() ? mValue.mObject : nullptr;
    }

    // --- StringValue --------------------------------------------------------

    inline bool StringValue::operator == (const StringValue& other) const
    {
        return strcmp(data, other.data) == 0;
    }

    inline int StringValue::Compare(const StringValue& other) const
    {
        return strcmp(data, other.data);
    }

    // --- ObjectValue --------------------------------------------------------

    inline const Value& ObjectValue::operator[](ValueKey key) const
    {
        return Member(key);
    }

    inline bool ObjectValue::IsEmpty() const
    {
        return mMap.empty();
    }

    inline int ObjectValue::NumMembers() const
    {
        return size_i(mMap);
    }

    inline const char* ObjectValue::MemberName(int index) const
    {
        return mMap.at(index).first->c_str();
    }

    inline ValueKey ObjectValue::MemberKey(int i) const
    {
        return mMap.at(i).first->c_str();
    }

    inline const Value& ObjectValue::MemberValue(int i) const
    {
        return mMap.at(i).second;
    }

    inline Value& ObjectValue::MemberValue(int i)
    {
        return mMap.at(i).second;
    }

    inline ConstNameValue ObjectValue::MemberInfo(int i) const
    {
        return { mMap.at(i).first->c_str(), mMap.at(i).second };
    }

    inline NameValue ObjectValue::MemberInfo(int i)
    {
        return { mMap.at(i).first->c_str(), mMap.at(i).second };
    }

    inline void ObjectValue::RemoveMembers()
    {
        if (!mMap.empty())
        {
            mModCount++;
            mMap.clear();
        }
    }

    inline uint32_t ObjectValue::ModCount() const
    {
        return mModCount;
    }

    inline void ObjectValue::IncModCount()
    {
        mModCount++;
    }

    inline bool ObjectValue::operator ==(const ObjectValue& other) const
    {
        return mMap == other.mMap;
    }

    inline ConstMemberIterator ObjectValue::begin() const
    {
        return { this, 0 };
    }

    inline ConstMemberIterator ObjectValue::end() const
    {
        return { this, NumMembers() };
    }

    inline MemberIterator ObjectValue::begin()
    {
        return { this, 0 };
    }

    inline MemberIterator ObjectValue::end()
    {
        return { this, NumMembers() };
    }


    // --- Utilities -----------------------------------------------------------

    inline int SetFromValue(const Value& value, int nv, int32_t v[])
    {
        int n = std::min(size_i(value), nv);

        for (int i = 0; i < n; i++)
            v[i] = value[i].AsInt();

        return n;
    }

    inline int SetFromValue(const Value& value, int nv, uint32_t v[])
    {
        int n = std::min(size_i(value), nv);

        for (int i = 0; i < n; i++)
            v[i] = value[i].AsUInt();

        return n;
    }

    inline int SetFromValue(const Value& value, int nv, float v[])
    {
        int n = std::min(size_i(value), nv);

        for (int i = 0; i < n; i++)
            v[i] = value[i].AsFloat();

        return n;
    }

    template<class T> inline T AsEnum(const Value& value, const EnumInfo enumInfo[], T defaultValue)
    {
        return T(AsEnum(value, enumInfo, int(defaultValue)));
    }

    inline bool MemberIsHidden(const char* key)
    {
        return key[0] == '_';
    }
}

#endif
