//
//  Value.cpp
//
//  Represents classic bool/int/float/string/array/object value
//
//  Andrew Willmott
//

#include "Value.hpp"

#ifndef HL_NO_STRING_TABLE
    #include "StringTable.hpp"
#endif

using namespace HL;


//
// Value
//

Value::Value(ValueType type) : mType(type)
{
    switch (type)
    {
    case kValueNull:
        break;
    case kValueInt:
        mValue.mInt32 = 0;
        break;
    case kValueUInt:
        mValue.mUInt32 = 0;
        break;
    case kValueInt64:
        mValue.mInt64 = 0;
        break;
    case kValueUInt64:
        mValue.mUInt64 = 0;
        break;
    case kValueDouble:
        mValue.mDouble = 0.0;
        break;
    case kValueString:
        mValue.mString = nullptr;
        break;
    case kValueArray:
        mValue.mArray = nullptr;
        break;
    case kValueObject:
        mValue.mObject = new ObjectValue;
        mValue.mObject->AddRef();
        break;
    case kValueBool:
        mValue.mBool = false;
        break;
    default:
        HL_ERROR("Invalid type");
    }
}

Value::Value(const Value& other) : mType(other.mType)
{
    switch (mType)
    {
    case kValueNull:
    case kValueInt:
    case kValueUInt:
    case kValueInt64:
    case kValueUInt64:
    case kValueDouble:
    case kValueBool:
        mValue = other.mValue;
        break;
    case kValueString:
        mValue.mString = other.mValue.mString;
        if (mValue.mString)
            mValue.mString->AddRef();
        break;
    case kValueArray:
        mValue.mArray = other.mValue.mArray;
        if (mValue.mArray)
            mValue.mArray->AddRef();
        break;
    case kValueObject:
        // We copy objects rather than adding a reference because they are
        // mutable, unlike strings/arrays where we ensure copy-on-write.
        HL_ASSERT(other.mValue.mObject);
        mValue.mObject = new ObjectValue(*other.mValue.mObject);
        HL_ASSERT(mValue.mObject->RefCount() == 0);
        mValue.mObject->AddRef();
        break;
    default:
        HL_ERROR("Invalid type");
    }

#ifdef HL_VALUE_COMMENTS
    if (other.mComments)
    {
        mComments = new String[kNumberOfCommentPlacements];

        for (int comment = 0; comment < kNumberOfCommentPlacements; ++comment)
            mComments[comment] = other.mComments[comment];
    }
#endif
}

Value::Value(Value&& other) :
    mValue(other.mValue),
#ifdef HL_VALUE_COMMENTS
    mComments(other.mComments),
#endif
    mType(other.mType)
{
    switch (mType)
    {
    case kValueString:
        other.mValue.mString = nullptr;
        break;
    case kValueArray:
        other.mValue.mArray = nullptr;
        break;
    case kValueObject:
        other.mValue.mObject = nullptr;
        break;
    default:
        break;
    }
#ifdef HL_VALUE_COMMENTS
    other.mComments = nullptr;
#endif
    other.mType = kValueNull;
}

Value::~Value()
{
    MakeNull();

#ifdef HL_VALUE_COMMENTS
    if (mComments)
        DeleteArray(mComments);
#endif
}

void Value::operator = (const Value& other)
{
    Value temp(other);
    Swap(temp);
}

void Value::operator = (Value&& other)
{
    Swap(other);
}

void Value::Swap(Value& other)
{
    if (mType == kValueObject && other.mType == kValueObject)
        mValue.mObject->Swap(other.mValue.mObject);  // to preserve mod counts
    else
        std::swap(mValue, other.mValue);
#ifdef HL_VALUE_COMMENTS
    std::swap(mComments, other.mComments);
#endif
    std::swap(mType, other.mType);
}

bool Value::IsConvertibleTo(ValueType other) const
{
    switch (mType)
    {
    case kValueNull:
        return true;

    case kValueBool:
        return false
            ||  other == kValueBool
            ||  other == kValueInt
            ||  other == kValueUInt
            ||  other == kValueInt64
            ||  other == kValueUInt64
            ||  other == kValueDouble
        ;

    case kValueInt:
        return false
            ||  other == kValueBool
            ||  other == kValueInt
            || (other == kValueUInt   && mValue.mInt32 >= 0)
            ||  other == kValueInt64
            || (other == kValueUInt64 && mValue.mInt32 >= 0)
            ||  other == kValueDouble
        ;

    case kValueUInt:
        return false
            ||  other == kValueBool
            || (other == kValueInt    && mValue.mUInt32 <= (unsigned) INT32_MAX)
            ||  other == kValueUInt
            ||  other == kValueInt64
            ||  other == kValueUInt64
            ||  other == kValueDouble
        ;

    case kValueInt64:
        return false
            ||  other == kValueBool
            || (other == kValueInt    && mValue.mInt64 >= INT32_MIN && mValue.mInt64 <=  INT32_MAX)
            || (other == kValueUInt   && mValue.mInt64 >=         0 && mValue.mInt64 <= UINT32_MAX)
            ||  other == kValueInt64
            || (other == kValueUInt64 && mValue.mInt64 >= 0)
            ||  other == kValueDouble
        ;

    case kValueUInt64:
        return false
            ||  other == kValueBool
            || (other == kValueInt    && mValue.mUInt64 <= (unsigned) INT32_MAX)
            || (other == kValueUInt   && mValue.mUInt64 <=           UINT32_MAX)
            || (other == kValueInt64  && mValue.mUInt64 <= (unsigned) INT64_MAX)
            ||  other == kValueUInt64
            ||  other == kValueDouble
        ;

    case kValueDouble:
        return false
            ||  other == kValueBool
            || (other == kValueInt    && mValue.mDouble >= INT32_MIN && mValue.mDouble <=  INT32_MAX)
            || (other == kValueUInt   && mValue.mDouble >= 0         && mValue.mDouble <= UINT32_MAX)
            || (other == kValueInt64  && mValue.mDouble >= INT64_MIN && mValue.mDouble <=  INT64_MAX)
            || (other == kValueUInt64 && mValue.mDouble >= 0         && mValue.mDouble <= UINT64_MAX)
            ||  other == kValueDouble
        ;

    case kValueString:
        return false
            ||  other == kValueBool
            ||  other == kValueString
        ;

    case kValueArray:
        return false
            || other == kValueBool
            || other == kValueArray
        ;

    case kValueObject:
        return false
            || other == kValueBool
            || other == kValueObject
        ;
    }

    return false;
}

const char* Value::AsCString(const char* defaultValue) const
{
    switch (mType)
    {
    case kValueString:
        return mValue.mString ? mValue.mString->c_str() : "";
    case kValueBool:
        return mValue.mBool ? "true" : "false";
    default:
        ;
    }

    return defaultValue;
}

uint32_t Value::AsID(uint32_t defaultValue) const
{
    switch (mType)
    {
    case kValueString:
        return mValue.mString ? IDFromString(mValue.mString->c_str()) : defaultValue;
    case kValueUInt:
        return mValue.mUInt32;
    case kValueInt:
        if (mValue.mInt32 < 0)
            return 0;
        return uint32_t(mValue.mInt32);
    case kValueInt64:
        if (mValue.mInt64 < 0)
            return 0;
        return uint32_t(mValue.mInt64);
    case kValueUInt64:
        if (mValue.mUInt64 > UINT32_MAX)
            return UINT32_MAX;
        return uint32_t(mValue.mUInt64);
    default:
        ;
    }

    return defaultValue;
}

int32_t Value::AsInt(int32_t defaultValue) const
{
    switch (mType)
    {
    case kValueBool:
        return mValue.mBool ? 1 : 0;

    case kValueInt:
        return mValue.mInt32;

    case kValueUInt:
        if (mValue.mUInt32 > INT32_MAX)
            return INT32_MAX;
        return mValue.mUInt32;

    case kValueInt64:
        if (mValue.mInt64 > INT32_MAX)
            return INT32_MAX;
        if (mValue.mInt64 < INT32_MIN)
            return INT32_MIN;
        return int32_t(mValue.mInt64);

    case kValueUInt64:
        if (mValue.mUInt64 > INT32_MAX)
            return INT32_MAX;
        return int32_t(mValue.mUInt64);

    case kValueDouble:
        if (mValue.mDouble < double(INT32_MIN))
            return INT32_MIN;
        if (mValue.mDouble > double(INT32_MAX))
            return INT32_MAX;
        return int32_t(mValue.mDouble);

    default:
        ;
    }

    return defaultValue;
}

uint32_t Value::AsUInt(uint32_t defaultValue) const
{
    switch (mType)
    {
    case kValueBool:
        return mValue.mBool ? 1 : 0;

    case kValueInt:
        if (mValue.mInt32 < 0)
            return 0;
        return mValue.mInt32;
    case kValueUInt:
        return mValue.mUInt32;

    case kValueInt64:
        if (mValue.mInt64 > UINT32_MAX)
            return UINT32_MAX;
        if (mValue.mInt64 < 0)
            return 0;
        return uint32_t(mValue.mInt64);
    case kValueUInt64:
        if (mValue.mUInt64 > UINT32_MAX)
            return UINT32_MAX;
        return uint32_t(mValue.mUInt64);

    case kValueDouble:
        if (mValue.mDouble < 0.0)
            return 0;
        if (mValue.mDouble > double(UINT32_MAX))
            return UINT32_MAX;
        return uint32_t(mValue.mDouble);

    default:
        ;
    }

    return defaultValue;
}

int64_t Value::AsInt64(int64_t defaultValue) const
{
    switch (mType)
    {
    case kValueBool:
        return mValue.mBool ? 1 : 0;

    case kValueInt:
        return mValue.mInt32;
    case kValueUInt:
        return mValue.mUInt32;
    case kValueInt64:
        return mValue.mInt64;
    case kValueUInt64:
        if (mValue.mUInt64 > INT64_MAX)
            return INT64_MAX;
        return mValue.mUInt64;

    case kValueDouble:
        if (mValue.mDouble < double(INT64_MIN))
            return INT64_MIN;
        if (mValue.mDouble > double(INT64_MAX))
            return INT64_MAX;
        return int64_t(mValue.mDouble);

    default:
        ;
    }

    return defaultValue;
}

uint64_t Value::AsUInt64(uint64_t defaultValue) const
{
    switch (mType)
    {
    case kValueBool:
        return mValue.mBool ? 1 : 0;

    case kValueInt:
        if (mValue.mInt32 < 0)
            return 0;
        return mValue.mInt32;
    case kValueUInt:
        return mValue.mUInt32;

    case kValueInt64:
        if (mValue.mInt64 < 0)
            return 0;
        return uint64_t(mValue.mInt64);
    case kValueUInt64:
        return mValue.mUInt64;

    case kValueDouble:
        if (mValue.mDouble < 0.0)
            return 0;
        if (mValue.mDouble > double(UINT64_MAX))
            return UINT64_MAX;
        return uint64_t(mValue.mDouble);

    default:
        ;
    }

    return defaultValue;
}

float Value::AsFloat(float defaultValue) const
{
    switch (mType)
    {
    case kValueBool:
        return mValue.mBool ? 1.0f : 0.0f;
    case kValueInt:
        return (float) mValue.mInt32;
    case kValueUInt:
        return (float) mValue.mUInt32;
    case kValueInt64:
        return (float) mValue.mInt64;
    case kValueUInt64:
        return (float) mValue.mUInt64;
    case kValueDouble:
        return (float) mValue.mDouble;

    default:
        ;
    }

    return defaultValue;
}

double Value::AsDouble(double defaultValue) const
{
    switch (mType)
    {
    case kValueBool:
        return mValue.mBool ? 1.0 : 0.0;
    case kValueInt:
        return mValue.mInt32;
    case kValueUInt:
        return mValue.mUInt32;
    case kValueInt64:
        return (double) mValue.mInt64;
    case kValueUInt64:
        return (double) mValue.mUInt64;
    case kValueDouble:
        return mValue.mDouble;

    default:
        ;
    }

    return defaultValue; // unreachable;
}

bool Value::AsBool(bool defaultValue) const
{
    switch (mType)
    {
    case kValueBool:
        return mValue.mBool;
    case kValueInt:
        return mValue.mInt32 != 0;
    case kValueUInt:
        return mValue.mUInt32 != 0;
    case kValueInt64:
        return mValue.mInt64 != 0;
    case kValueUInt64:
        return mValue.mUInt64 != 0;
    case kValueDouble:
        return mValue.mDouble != 0.0;
    case kValueString:
        return mValue.mString && EqualI(*mValue.mString, "true");
    case kValueArray:
        return mValue.mArray && !mValue.mArray->empty();
    case kValueObject:
        return !mValue.mObject->IsEmpty();

    default:
        ;
    }

    return defaultValue;
}

// Array

Value& Value::Elt(int index)
{
    if (mType == kValueArray)
        return (*mValue.mArray)[index];

    HL_ERROR("Not an array");
    kNullValueScratch.MakeNull();
    return kNullValueScratch;
}

const Value& Value::Elt(int index) const
{
    if (mType == kValueArray)
        return (*mValue.mArray)[index];

    return kNullValue;
}

// Array/Object STL

size_t Value::size() const
{
    switch (mType)
    {
    case kValueString:
        return mValue.mString ? int(mValue.mString->size()) : 0;

    case kValueArray:
        return mValue.mArray ? int(mValue.mArray->size()) : 0;

    case kValueObject:
        return mValue.mObject->NumMembers();

    default:
        ;
    }

    return 0;
}

bool Value::empty() const
{
    switch (mType)
    {
    case kValueNull:
        return true;
    case kValueString:
        return !mValue.mString || mValue.mString->empty();
    case kValueArray:
        return !mValue.mArray || mValue.mArray->empty();
    case kValueObject:
        return mValue.mObject->IsEmpty();
    default:
        return false;
    }
}

void Value::clear()
{
    switch (mType)
    {
    case kValueString:
        if (mValue.mString)
        {
            mValue.mString->Release();
            mValue.mString = nullptr;
        }
        break;
    case kValueArray:
        if (mValue.mArray)
        {
            mValue.mArray->Release();
            mValue.mArray = nullptr;
        }
        break;
    case kValueObject:
        mValue.mObject->RemoveMembers();
        break;
    default:
        break;
    }
}

// Comments

#ifdef HL_VALUE_COMMENTS
void Value::SetComment(const char* comment, CommentPlacement placement)
{
    if (!mComments)
        mComments = new String[kNumberOfCommentPlacements];

    mComments[placement] = comment;
}


bool Value::HasComment(CommentPlacement placement) const
{
    return mComments != 0 && !mComments[placement].empty();
}

const char* Value::Comment(CommentPlacement placement) const
{
    if (mComments)
        return mComments[placement].c_str();

    return "";
}
#endif

// Comparisons

bool Value::operator < (const Value& other) const
{
    int typeDelta = mType - other.mType;
    if (typeDelta != 0)
        return typeDelta < 0;

    switch (mType)
    {
    case kValueNull:
        return false;
    case kValueBool:
        return mValue.mBool   < other.mValue.mBool;
    case kValueInt:
        return mValue.mInt32  < other.mValue.mInt32;
    case kValueUInt:
        return mValue.mUInt32 < other.mValue.mUInt32;
    case kValueInt64:
        return mValue.mInt64  < other.mValue.mInt64;
    case kValueUInt64:
        return mValue.mUInt64 < other.mValue.mUInt64;
    case kValueDouble:
        return mValue.mDouble < other.mValue.mDouble;
    default:
        return Compare(other) < 0;
    }

    return false;
}

bool Value::operator <= (const Value& other) const
{
    return !(other > *this);
}

bool Value::operator >= (const Value& other) const
{
    return !(*this < other);
}

bool Value::operator > (const Value& other) const
{
    return other < *this;
}

bool Value::operator == (const Value& other) const
{
    int temp = other.mType;

    if (mType != temp)
        return false;

    switch (mType)
    {
    case kValueNull:
        return true;
    case kValueInt:
        return mValue.mInt32 == other.mValue.mInt32;
    case kValueUInt:
        return mValue.mUInt32 == other.mValue.mUInt32;
    case kValueInt64:
        return mValue.mInt64 == other.mValue.mInt64;
    case kValueUInt64:
        return mValue.mUInt64 == other.mValue.mUInt64;
    case kValueDouble:
        return mValue.mDouble == other.mValue.mDouble;
    case kValueBool:
        return mValue.mBool == other.mValue.mBool;

    case kValueString:
        return (mValue.mString == other.mValue.mString)
            || (mValue.mString && other.mValue.mString && *mValue.mString == *other.mValue.mString);
    case kValueArray:
        return (mValue.mArray == other.mValue.mArray)
            || (mValue.mArray && other.mValue.mArray && *mValue.mArray == *other.mValue.mArray);

    case kValueObject:
        return mValue.mObject->NumMembers() == other.mValue.mObject->NumMembers()
            && (*mValue.mObject) == (*other.mValue.mObject);
    }

    return false;
}

namespace
{
    template<class T> inline int CompareT(const T& a, const T& b)
    {
        if (a < b)
            return -1;
        if (a > b)
            return 1;
        return 0;
    }
}

int Value::Compare(const Value& other) const
{
    if (mType != other.mType)
        return mType < other.mType ? -1 : 1;

    switch (mType)
    {
    case kValueNull:
        return 0;
    case kValueBool:
        return CompareT(mValue.mBool,   other.mValue.mBool);
    case kValueInt:
        return CompareT(mValue.mInt32,  other.mValue.mInt32);
    case kValueUInt:
        return CompareT(mValue.mUInt32, other.mValue.mUInt32);
    case kValueInt64:
        return CompareT(mValue.mInt64,  other.mValue.mInt64);
    case kValueUInt64:
        return CompareT(mValue.mUInt64, other.mValue.mUInt64);
    case kValueDouble:
        return CompareT(mValue.mDouble, other.mValue.mDouble);
    case kValueString:
        {
            bool b1 =       mValue.mString != nullptr;
            bool b2 = other.mValue.mString != nullptr;

            if (b1 && b2)
                return mValue.mString->Compare(*other.mValue.mString);

            return CompareT(b1, b2);
        }
    case kValueArray:
        {
            bool b1 =       mValue.mArray != nullptr;
            bool b2 = other.mValue.mArray != nullptr;

            if (b1 && b2)
                return mValue.mArray->Compare(*other.mValue.mArray);

            return CompareT(b1, b2);
        }
    case kValueObject:
        return mValue.mObject->Compare(*other.mValue.mObject);
    default:
        ;
    }

    return 0;
}


// Utilities

void Value::MakeNull()
{
    switch (mType)
    {
    case kValueString:
        if (mValue.mString)
        {
            mValue.mString->Release();
            mValue.mString = nullptr;
        }
        break;
    case kValueArray:
        if (mValue.mArray)
        {
            mValue.mArray->Release();
            mValue.mArray = nullptr;
        }
        break;
    case kValueObject:
        mValue.mObject->Release();
        mValue.mObject = nullptr;
        break;
    default:
        break;
    }

    mType = kValueNull;
}

ArrayValue& Value::MakeArray(int n)
{
    MakeNull();
    mType = kValueArray;
    mValue.mArray = CreateArrayValue(n);
    mValue.mArray->AddRef();

    return *mValue.mArray;
}

 ArrayValue& Value::MakeArray(size_t n)
 {
     HL_ASSERT(n <= INT_MAX);
     MakeNull();
     mType = kValueArray;
     mValue.mArray = CreateArrayValue((int) n);
     mValue.mArray->AddRef();

     return *mValue.mArray;
 }

ObjectValue& Value::MakeObject()
{
    MakeNull();
    mType = kValueObject;
    mValue.mObject = new ObjectValue;
    mValue.mObject->AddRef();

    return *mValue.mObject;
}

bool Value::ToArray()
{
    if (mType == kValueNull)
    {
        mType = kValueArray;
        mValue.mArray = nullptr;
    }

    return mType == kValueArray;
}

bool Value::ToObject()
{
    if (mType == kValueNull)
    {
        mType = kValueObject;
        mValue.mObject = new ObjectValue;
        mValue.mObject->AddRef();
    }

    return mType == kValueObject;
}

void Value::Merge(const Value& overrides)
{
    if (overrides.IsNull())
        return;

    if (!overrides.IsObject() || !IsObject())
    {
        *this = overrides;
        return;
    }

    mValue.mObject->Merge(*overrides.mValue.mObject);
}

const Value HL::kNullValue;
Value HL::kNullValueScratch;

const ArrayValue HL::kNullArrayValue;

// --- StringValue ------------------------------------------------------------

StringValue* HL::CreateStringValue(const char* str, size_t len)
{
    if (len == 0)
        len = strlen(str);

    StringValue* sv = static_cast<StringValue*>(::operator new(sizeof(ValueRC) + len + 1));
    new (sv) ValueRC();
    memcpy((char*) sv->data, str, len + 1);

    return sv;
}


// --- ArrayValue ------------------------------------------------------------

ArrayValueHeader::~ArrayValueHeader()
{
    Value* data = (Value*) (((uint8_t*) this) + sizeof(ArrayValueHeader));
    for (int i = 0; i < count; i++)
        data[i].~Value();
}

ArrayValue* HL::CreateArrayValue(int n, const Value values[])
{
    ArrayValue* av = static_cast<ArrayValue*>(::operator new(sizeof(ArrayValueHeader) + n * sizeof(Value)));
    new (av) ArrayValueHeader(n);

    if (values)
        for (int i = 0; i < n; i++)
            new (&av->data[i]) Value(values[i]);
    else
        for (int i = 0; i < n; i++)
            new (&av->data[i]) Value();

    return av;
}

ArrayValue* HL::CreateArrayValue(const Values& values)
{
    HL_ASSERT(values.size() <= INT_MAX);
    int n = int(values.size());

    ArrayValue* av = static_cast<ArrayValue*>(::operator new(sizeof(ArrayValueHeader) + n * sizeof(Value)));
    new (av) ArrayValueHeader(n);

    for (int i = 0; i < n; i++)
        new (&av->data[i]) Value(values[i]);

    return av;
}

bool ArrayValue::operator == (const ArrayValue& other) const
{
    if (count != other.count)
        return false;

    for (int i = 0; i < count; i++)
        if (data[i] != other.data[i])
            return false;

    return true;
}

int ArrayValue::Compare(const ArrayValue& other) const
{
    int n1 = count;
    int n2 = other.count;

    if (n1 != n2)
        return n1 < n2 ? -1 : 1;

    for (int i = 0; i < n1; i++)
    {
        int eltCompare = data[i].Compare(other.data[i]);

        if (eltCompare != 0)
            return eltCompare;
    }

    return 0;
}


// --- ObjectValue ------------------------------------------------------------

ObjectValue::~ObjectValue()
{
}

const Value& ObjectValue::Member(ValueKey key) const
{
    MemberMap::const_iterator it = mMap.find(key);

    if (it != mMap.end())
        return it->second;

    return kNullValue;
}

Value& ObjectValue::UpdateMember(ValueKey key, StringTable* st)
{
    mModCount++;

    MemberMap::iterator it = mMap.find(key);
    if (it != mMap.end())
        return it->second;

#ifdef HL_STRING_TABLE_HPP
    if (st)
        return mMap[StringValueRef(st->GetString(key))];
#endif

    return mMap[StringValueRef(CreateStringValue(key))];
}

Value& ObjectValue::UpdateMember(StringValue* key)
{
    mModCount++;

    MemberMap::iterator it = mMap.find(key);
    if (it != mMap.end())
        return it->second;

    return mMap[AutoRef<StringValue>(key)];
}

const Value* ObjectValue::MemberPtr(ValueKey key) const
{
    auto it = mMap.find(key);

    if (it != mMap.end())
        return &it->second;

    return 0;
}

Value* ObjectValue::UpdateMemberPtr(ValueKey key)
{
    auto it = mMap.find(key);

    if (it != mMap.end())
    {
        mModCount++;
        return &it->second;
    }

    return 0;
}

void ObjectValue::SetMember(ValueKey key, const Value& v)
{
    UpdateMember(key) = v;

    mModCount++;
}

bool ObjectValue::RemoveMember(ValueKey key)
{
    auto it = mMap.find(key);

    if (it == mMap.end())
        return false;

    mMap.erase(it);
    mModCount++;

    return true;
}

bool ObjectValue::HasMember(ValueKey key) const
{
    if (mMap.find(key) != mMap.end())
        return true;

    return false;
}

void ObjectValue::Merge(const ObjectValue& overrides)
{
    for (ConstNameValue nv : overrides)
        if (nv.value.IsNull())
            RemoveMember(nv.name);
        else
            UpdateMember(nv.name).Merge(nv.value);
}

int ObjectValue::MemberIndex(ValueKey key) const
{
    auto it = mMap.find(key);
    if (it != mMap.end())
        return int(it - mMap.begin());

    return -1;
}

uint32_t ObjectValue::MemberID(int index) const
{
    return IDFromString(mMap.at(index).first->c_str());
}

void ObjectValue::Swap(ObjectValue* other)
{
    mMap.swap(other->mMap);

    mModCount++;
    other->mModCount++;
}

int ObjectValue::Compare(const ObjectValue& other) const
{
    const ObjectValue& m1 = *this;
    const ObjectValue& m2 = other;

    int n1 = m1.NumMembers();
    int n2 = m2.NumMembers();

    if (n1 != n2)
        return n1 < n2 ? -1 : 1;

    for (int i = 0; i < n1; i++)
    {
        const char* key1 = m1.MemberName(i);
        const char* key2 = m2.MemberName(i);

        int keyCompare = ::Compare(key1, key2);
        if (keyCompare != 0)
            return keyCompare;

        int valueCompare = m1.MemberValue(i).Compare(m2.MemberValue(i));
        if (valueCompare != 0)
            return valueCompare;
    }

    return 0;
}

const ObjectValue HL::kNullObjectValue;


// --- Utilities ---------------------------------------------------------------

const char* HL::TypeName(ValueType type)
{
    switch (type)
    {
    case kValueNull:
        return "null";
    case kValueBool:
        return "bool";
    case kValueInt:
        return "int";
    case kValueUInt:
        return "uint";
    case kValueInt64:
        return "int64";
    case kValueUInt64:
        return "uint64";
    case kValueDouble:
        return "double";
    case kValueString:
        return "string";
    case kValueArray:
        return "array";
    case kValueObject:
        return "object";
    default:
        ;
    }

    return "unknown";
}

namespace
{
    template <typename ELT, typename AS, typename VALID>
    inline bool SetFromValue(const Value& value, std::vector<ELT>* v, AS asFunc, VALID validFunc)
    {
        if (value.IsArray())
        {
            const ArrayValue& av = value.AsArray();

            if (av.empty() || !validFunc(av[0]))
                return false;

            v->resize(av.size());

            for (int i = 0, n = size_i(value); i < n; i++)
                (*v)[i] = asFunc(av[i]);

            return true;
        }

        if (validFunc(value))
        {
            v->clear();
            v->resize(1, asFunc(value));
            return true;
        }

        return false;
    }
}

#define SET_FROM_VALUE(IS_TYPE, AS_TYPE) \
    ::SetFromValue(v, array, [](const Value& v) { return v.As##AS_TYPE(); }, [](const Value& v) { return v.Is##IS_TYPE(); })

template<> bool HL::SetFromValue(const Value& v, std::vector<const char*>* array)
{
    return ::SetFromValue(v, array, [](const Value& v) { return v.AsCString(); }, [](const Value& v) { return v.IsString(); });
}

template<> bool HL::SetFromValue(const Value& v, std::vector<std::string>* array)
{
    return ::SetFromValue(v, array, [](const Value& v) { return v.AsString(); }, [](const Value& v) { return v.IsString(); });
}

template<> bool HL::SetFromValue(const Value& v, std::vector<String>* array)
{
    return ::SetFromValue(v, array, [](const Value& v) { return v.AsString(); }, [](const Value& v) { return v.IsString(); });
}

template<> bool HL::SetFromValue(const Value& v, std::vector<bool>* array)
{
    return ::SetFromValue(v, array, [](const Value& v) { return v.AsBool(); }, [](const Value& v) { return v.IsBool() || v.IsIntegral(); });
}

template<> bool HL::SetFromValue(const Value& v, std::vector<int>* array)
{
    return SET_FROM_VALUE(Numeric, Int);
}

template<> bool HL::SetFromValue(const Value& v, std::vector<float>* array)
{
    return SET_FROM_VALUE(Numeric, Float);
}

template<> bool HL::SetFromValue(const Value& v, std::vector<double>* array)
{
    return SET_FROM_VALUE(Numeric, Double);
}

template<> bool HL::SetFromValue(const Value& v, std::vector<ID>* array)
{
    return ::SetFromValue(v, array, [](const Value& v) { return v.AsID(); }, [](const Value& v) { return v.IsIntegral() || v.IsString(); });
}

template<> bool HL::SetFromValue(const Value& v, std::vector<Value>* array)
{
    const ArrayValue& av = v.AsArray();
    array->assign(av.begin(), av.end());
    return true;
}

template<class T> void HL::SetFromArray(int n, const T array[], Value* v)
{
    ArrayValue& av = v->MakeArray(n);

    for (int i = 0; i < n; i++)
        av[i] = array[i];
}

template<class T> void HL::SetFromArray(const std::vector<T>& array, Value* v)
{
    SetFromArray(size_i(array), array.data(), v);
}

// Instantiate just what we need

template void HL::SetFromArray<int        >(int n, const int         array[], Value* v);
template void HL::SetFromArray<float      >(int n, const float       array[], Value* v);
template void HL::SetFromArray<bool       >(int n, const bool        array[], Value* v);
template void HL::SetFromArray<std::string>(int n, const std::string array[], Value* v);
template void HL::SetFromArray<String     >(int n, const String      array[], Value* v);
template void HL::SetFromArray<const char*>(int n, const char* const array[], Value* v);
template void HL::SetFromArray<ID         >(int n, const ID          array[], Value* v);
template void HL::SetFromArray<Value      >(int n, const Value       array[], Value* v);

template void HL::SetFromArray<int        >(const std::vector<int        >& array, Value* v);
template void HL::SetFromArray<float      >(const std::vector<float      >& array, Value* v);
//template bool HL::SetFromArray<bool       >(const std::vector<bool       >& array, Value* v);  STL still treats vector<bool> as a separate bit vector typ, Value* ve.
template void HL::SetFromArray<std::string>(const std::vector<std::string>& array, Value* v);
template void HL::SetFromArray<String     >(const std::vector<String     >& array, Value* v);
template void HL::SetFromArray<const char*>(const std::vector<const char*>& array, Value* v);
template void HL::SetFromArray<ID         >(const std::vector<ID         >& array, Value* v);
template void HL::SetFromArray<Value      >(const std::vector<Value      >& array, Value* v);


namespace
{
    const Value& PathField(const Value& v, const char* key)
    {
        if (v.IsArray() && key[0] == '[')
        {
            key++;
            char* end = nullptr;
            size_t index = strtol(key, &end, 10);

            if (*end != ']' || index >= v.size())
                return kNullValue;

            return v[index];
        }

        if (key[0] == '.')
            key++;

        return v.Member(key);
    }

    Value& UpdatePathField(Value& v, const char* key)
    {
        if (v.IsArray() && key[0] == '[')
        {
            key++;
            char* end = nullptr;
            size_t index = strtol(key, &end, 10);

            if (*end != ']' || index >= v.size())
            {
                HL_ERROR("Can't call UpdateMemberPath() on a non-existent object ");

                // Not thread safe, but this is an error condition anyway
                kNullValueScratch.MakeNull();
                return kNullValueScratch;
            }

            return v[index];
        }

        if (key[0] == '.')
            key++;

        return v.UpdateMember(key);
    }
}

const Value& HL::MemberPath(const Value& v, const char* path)
{
    size_t sep_pos = strcspn(path + 1, ".[") + 1;

    if (path[sep_pos] != 0)
    {
        String field(path, sep_pos);
        return MemberPath(PathField(v, field), path + sep_pos);
    }

    return PathField(v, path);
}

Value& HL::UpdateMemberPath(Value& v, const char* path)
{
    size_t sep_pos = strcspn(path + 1, ".[") + 1;

    if (path[sep_pos] != 0)
    {
        String field(path, sep_pos);
        return UpdateMemberPath(UpdatePathField(v, field), path + sep_pos);
    }

    return UpdatePathField(v, path);
}
