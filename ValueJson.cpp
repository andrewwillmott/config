//
// ValueJson.cpp
//
// Support for reading/writing json to/from Value
//
// Andrew Willmott
//

#define _CRT_SECURE_NO_WARNINGS

#include "ValueJson.hpp"

#include "ValueJsonInternal.hpp"

#include "Value.hpp"

#include <math.h>

#if HL_WINDOWS
    #define sscanf sscanf_s
#endif

using namespace HL;

//
// JsonReader implementation
//

namespace
{
    inline bool In(char c, const char* s)
    {
        do
        {
            if (*s == c)
                return true;
        }
        while (*++s);

        return false;
    }

#ifdef HL_VALUE_COMMENTS
    bool ContainsNewLine(const char* begin, const char* end)
    {
        for (; begin < end; ++begin)
            if (*begin == '\n' || *begin == '\r')
                return true;

        return false;
    }
#endif

    inline bool IsStartTokenChar(char c)
    {
        return isalpha(c) || c == '_' || c == '@';
    }

    inline bool IsTokenChar(char c)
    {
        return isalnum(c) || In(c, "_@.-+=");     // in particular, NOT : or , or []{} etc.
    }
}

JsonReader::JsonReader(HL::StringTable* st)
#ifdef HL_STRING_TABLE_HPP
    : mStringTable(st)
#endif
{
}

bool JsonReader::Read(const char* document, Value* root)
{
    const char* begin = document;
    const char* end   = begin + strlen(document);

    return Read(begin, end, root);
}

bool JsonReader::Read(const char* beginDoc, const char* endDoc, Value* root)
{
    mBegin = beginDoc;
    mEnd = endDoc;
    mCurrent = mBegin;
    mLastValueEnd = 0;
    mLastValue = 0;
    mCommentsBefore.clear();
    mErrors.clear();
    mNodes.clear();

#ifdef HL_STRING_TABLE_HPP
    if (!mStringTable && (mUseStringTableForKey || mUseStringTableForValue))
        mStringTable = CreateStringTable();
#endif

    root->MakeNull();
    mNodes.push_back(root);
    bool successful = ReadValue();
    mNodes.pop_back();

    SkipSpaces();

    if (mErrors.empty() && mCurrent != mEnd)
    {
        AddError("trailing garbage", { kTokenEndOfStream, mCurrent, mEnd });

        successful = false;
    }

#ifdef HL_VALUE_COMMENTS
    if (mCollectComments && !mCommentsBefore.empty())
        root->SetComment(mCommentsBefore, kCommentAfter);
#endif

    return successful;
}

bool JsonReader::ReadValue()
{
    Token token;
    ReadNonCommentToken(token);

    return ReadValue(token);
}

bool JsonReader::ReadValue(Token& token)
{
    bool successful = true;

#ifdef HL_VALUE_COMMENTS
    if (mCollectComments && !mCommentsBefore.empty())
    {
        mNodes.back()->SetComment(mCommentsBefore, kCommentBefore);
        mCommentsBefore.clear();
    }
#endif

    switch (token.mType)
    {
    case kTokenObjectBegin:
        successful = ReadObject(token);
        break;
    case kTokenArrayBegin:
        successful = ReadArray(token);
        break;
    case kTokenNumber:
        successful = DecodeNumber(token);
        break;
    case kTokenString:
        successful = DecodeString(token);
        break;
    case kTokenMinusInfinity:
        *mNodes.back() = -INFINITY;
        break;
    case kTokenInfinity:
        *mNodes.back() = INFINITY;
        break;
    case kTokenNaN:
        *mNodes.back() = NAN;
        break;
    case kTokenTrue:
        *mNodes.back() = true;
        break;
    case kTokenFalse:
        *mNodes.back() = false;
        break;
    case kTokenNull:
        *mNodes.back() = Value();
        break;
    default:
        return AddError("Syntax error: value, object or array expected.", token);
    }

    if (mCollectComments)
    {
        mLastValueEnd = mCurrent;
        mLastValue = mNodes.back();
    }

    return successful;
}

bool JsonReader::ReadNonCommentToken(Token& token)
{
    bool success;

    do
    {
        success = ReadToken(token);
    }
    while (success && token.mType == kTokenComment);

    return success;
}

bool JsonReader::ExpectToken(TokenType type, Token& token, const char* message)
{
    ReadToken(token);

    if (token.mType != type)
        return AddError(message, token);

    return true;
}

bool JsonReader::ReadToken(Token& token)
{
    SkipSpaces();

    token.mStart = mCurrent;
    char c = GetNextChar();
    bool ok = true;
    bool validUnquoted = false;

    switch (c)
    {
    case '{':
        token.mType = kTokenObjectBegin;
        break;
    case '}':
        token.mType = kTokenObjectEnd;
        break;
    case '[':
        token.mType = kTokenArrayBegin;
        break;
    case ']':
        token.mType = kTokenArrayEnd;
        break;
    case '"':
        token.mType = kTokenString;
        ok = ReadString();
        break;
    case '/':
        token.mType = kTokenComment;
        ok = ReadComment();
        break;
    case '-':
        if (Match("Infinity", sizeof("Infinity") - 1) || Match("inf", sizeof("inf") - 1))
        {
            token.mType = kTokenMinusInfinity;
            break;
        }
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '+':
        token.mType = kTokenNumber;
        ReadNumber();
        break;
    case 'I':
        token.mType = kTokenInfinity;
        validUnquoted = true;
        ok = Match("nfinity", sizeof("nfinity") - 1);
        break;
    case 'i':
        token.mType = kTokenInfinity;
        validUnquoted = true;
        ok = Match("nf", sizeof("nf") - 1);
        break;
    case 'N':
        token.mType = kTokenNaN;
        validUnquoted = true;
        ok = Match("aN", sizeof("aN") - 1);
        break;
    case 't':
        token.mType = kTokenTrue;
        validUnquoted = true;
        ok = Match("rue", 3);
        break;
    case 'f':
        token.mType = kTokenFalse;
        validUnquoted = true;
        ok = Match("alse", 4);
        break;
    case 'n':
        validUnquoted = true;
        if (Match("ull", 3))
            token.mType = kTokenNull;
        else if (Match("an", 2))
            token.mType = kTokenNaN;
        else
            ok = false;
        break;
    case ',':
        token.mType = kTokenArraySeparator;
        break;
    case ':':
        token.mType = kTokenMemberSeparator;
        break;
    case 0:
        token.mType = kTokenEndOfStream;
        break;
    default:
        validUnquoted = IsStartTokenChar(c);
        ok = false;
        break;
    }

    if (!ok && mAllowUnquotedStrings && validUnquoted)
    {
        token.mType = kTokenString;
        ok = ReadUnquotedString();
    }

    if (!ok)
        token.mType = kTokenError;

    token.mEnd = mCurrent;

    return true;
}

void JsonReader::SkipSpaces()
{
    while (mCurrent != mEnd)
    {
        char c = *mCurrent;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            ++mCurrent;
        else
            break;
    }
}

bool JsonReader::Match(Location pattern, int patternLength)
{
    if (mEnd - mCurrent < patternLength)
        return false;

    for (int i = 0; i < patternLength; i++)
        if (mCurrent[i] != pattern[i])
            return false;

    if (mCurrent + patternLength + 1 < mEnd && IsTokenChar(mCurrent[patternLength]))
        return false;

    mCurrent += patternLength;

    return true;
}

bool JsonReader::ReadComment()
{
#ifdef HL_VALUE_COMMENTS
    Location commentBegin = mCurrent - 1;
#endif

    char c = GetNextChar();
    bool successful = false;
    if (c == '*')
        successful = ReadCStyleComment();
    else if (c == '/')
        successful = ReadCppStyleComment();
    if (!successful)
        return false;

#ifdef HL_VALUE_COMMENTS
    if (mCollectComments)
    {
        CommentPlacement placement = kCommentBefore;

        if (mLastValueEnd && !ContainsNewLine(mLastValueEnd, commentBegin))
        {
            if (c != '*' || !ContainsNewLine(commentBegin, mCurrent))
                placement = kCommentAfterOnSameLine;
        }

        AddComment(commentBegin, mCurrent, placement);
    }
#endif

    return true;
}

bool JsonReader::ReadCStyleComment()
{
    while (mCurrent != mEnd)
    {
        char c = GetNextChar();
        if (c == '*' && *mCurrent == '/')
            break;
    }
    return GetNextChar() == '/';
}

bool JsonReader::ReadCppStyleComment()
{
    while (mCurrent != mEnd)
    {
        char c = GetNextChar();

        if ( c == '\r' || c == '\n')
            break;
    }

    return true;
}

void JsonReader::ReadNumber()
{
    while (mCurrent != mEnd)
    {
        if (!(*mCurrent >= '0' && *mCurrent <= '9')
                && !In(*mCurrent, ".eE+-"))
            break;

        ++mCurrent;
    }
}

bool JsonReader::ReadString()
{
    char c = 0;

    while (mCurrent != mEnd)
    {
        c = GetNextChar();

        if (c == '\\')
            GetNextChar();
        else if (c == '"')
            break;
    }

    return c == '"';
}

bool JsonReader::ReadUnquotedString()
{
    while (mCurrent != mEnd)
    {
        if (!IsTokenChar(*mCurrent))
            break;

        ++mCurrent;
    }

    return true;
}

bool JsonReader::ReadObject(Token& tokenStart)
{
    Token tokenName;
    String name;

    Value* destValue = mNodes.back();

    ObjectValue& objectValue = destValue->MakeObject();

    while (ReadNonCommentToken(tokenName))
    {
        if (tokenName.mType == kTokenObjectEnd && (name.empty() || mAllowTrailingCommas))  // empty object
            break;

        if (tokenName.mType != kTokenString)
            return AddErrorAndRecover("Object member name isn't a String", tokenName, kTokenObjectEnd);

        name.clear();
        if (!DecodeString(tokenName, name))
            return RecoverFromError(kTokenObjectEnd);

        Token colon;
        if (!ReadNonCommentToken(colon) || colon.mType != kTokenMemberSeparator)
            return AddErrorAndRecover("Missing ':' after object member name", colon, kTokenObjectEnd);

    #ifdef HL_STRING_TABLE_HPP
        Value* value = &objectValue.UpdateMember(name, mUseStringTableForKey ? mStringTable : 0);
    #else
        Value* value = &objectValue.UpdateMember(name);
    #endif

        mNodes.push_back(value);
        bool ok = ReadValue();
        mNodes.pop_back();

        if (!ok) // error already set
            return RecoverFromError(kTokenObjectEnd);

        Token comma;
        if
        (      !ReadNonCommentToken(comma)
            || (comma.mType != kTokenObjectEnd && comma.mType != kTokenArraySeparator)
        )
        {
            return AddErrorAndRecover("Missing ',' or '}' in object declaration", comma, kTokenObjectEnd);
        }

        if (comma.mType == kTokenObjectEnd)
            break;
    }

    return true;
}

bool JsonReader::ReadArray(Token& tokenStart)
{
    Values array;

    int index = 0;
    Token token;

    while (true)
    {
        if (!ReadNonCommentToken(token))
            return AddErrorAndRecover("Missing remainder of array", token, kTokenArrayEnd);

        // Allow ] next if empty array or we support trailing commas
        if (token.mType == kTokenArrayEnd && (mAllowTrailingCommas || index == 0))
            break;

        array.push_back({});

        mNodes.push_back(&array.back());
        bool ok = ReadValue(token);
        mNodes.pop_back();

        if (!ok) // error already set
            return RecoverFromError(kTokenArrayEnd);

        if (!ReadNonCommentToken(token))
            return AddErrorAndRecover("Missing remainder of array", token, kTokenArrayEnd);

        if (token.mType == kTokenArrayEnd)
            break;

        if (token.mType != kTokenArraySeparator)
            return AddErrorAndRecover("Expecting ',' in array declaration", token, kTokenArrayEnd);

        if (token.mType == kTokenArrayEnd)
            break;
    }

    *mNodes.back() = array;

    return true;
}

bool JsonReader::DecodeNumber(Token& token)
{
    bool isDouble = false;

    for (Location inspect = token.mStart; inspect != token.mEnd; ++inspect)
    {
        isDouble = isDouble
                   || In(*inspect, ".eE+")
                   || (*inspect == '-' && inspect != token.mStart);
    }

    if (isDouble)
        return DecodeDouble(token);

    Location current = token.mStart;

    bool isNegative = false;
    while (*current == '-' || *current == '+')
    {
        if (*current == '-')
            isNegative = !isNegative;
        ++current;
    }

    uint64_t value = 0;

    while (current < token.mEnd)
    {
        if (value > (UINT64_MAX / 10))
            return DecodeDouble(token);

        char c = *current++;

        if (c < '0' || c > '9')
            return AddError(("'" + String(token.mStart, token.mEnd) + "' is not a number.").c_str(), token);

        c -= '0';

        value = value * 10;
        if (value > (UINT64_MAX - c))
            return DecodeDouble(token);

        value += c;
    }

    if (isNegative)
    {
        if (value <= 2147483648)  // -INT32_MIN
        *mNodes.back() = -int32_t(value);
        else if (value <= 9223372036854775808u) // -INT64_MIN
            *mNodes.back() = -int64_t(value);
        else
            *mNodes.back() = -double(value);
    }
    else if (value <= INT32_MAX)
        *mNodes.back() = int32_t(value);
    else if (value <= UINT32_MAX)
        *mNodes.back() = uint32_t(value);
    else if (value <= INT64_MAX)
        *mNodes.back() = int64_t(value);
    else
        *mNodes.back() = value;

    return true;
}

bool JsonReader::DecodeDouble(Token& token)
{
    double value = 0;
    const int bufferSize = 32;

    int count;
    int length = int(token.mEnd - token.mStart);

    if (length < bufferSize)
    {
        char buffer[bufferSize];
        memcpy(buffer, token.mStart, length);
        buffer[length] = 0;
        count = sscanf(buffer, "%lf", &value);
    }
    else
    {
        String buffer(token.mStart, token.mEnd);
        count = sscanf(buffer.c_str(), "%lf", &value);
    }

    if (count != 1)
        return AddError(("'" + String(token.mStart, token.mEnd) + "' is not a number.").c_str(), token);

    *mNodes.back() = value;
    return true;
}

bool JsonReader::DecodeString(Token& token)
{
    String decoded;

    if (!DecodeString(token, decoded))
        return false;

#ifdef HL_STRING_TABLE_HPP
    if (mUseStringTableForValue && mStringTable)
        *mNodes.back() = mStringTable->GetString(decoded);
    else
#endif
        *mNodes.back() = decoded.c_str();

    return true;
}

bool JsonReader::DecodeString(Token& token, String& decoded)
{
    Location current = token.mStart;
    Location end = token.mEnd;

    bool quoted = *current == '"';

    if (quoted)
    {
        HL_ASSERT(*(end - 1) == '"');
        current++;
        end--;
    }

    decoded.reserve(end - current);

    while (current != end)
    {
        char c = *current++;

        if (quoted)
        {
            if (c == '"')
                break;
        }
        else
        {
            if (!IsTokenChar(c))
                break;
        }

        if (c == '\\')
        {
            if (current == end)
                return AddError("Empty escape sequence in string", token, current);

            char escape = *current++;

            switch (escape)
            {
            case '"':
                decoded += '"' ;
                break;
            case '/':
                decoded += '/' ;
                break;
            case '\\':
                decoded += '\\';
                break;
            case 'b':
                decoded += '\b';
                break;
            case 'f':
                decoded += '\f';
                break;
            case 'n':
                decoded += '\n';
                break;
            case 'r':
                decoded += '\r';
                break;
            case 't':
                decoded += '\t';
                break;
            case 'u':
            {
                unsigned int unicode;

                if (!DecodeUnicodeEscapeSequence(token, current, end, unicode))
                    return false;

                // @todo encode unicode as utf8.
                // @todo remember to alter the writer too.
            }
            break;

            default:
                return AddError("Bad escape sequence in string", token, current);
            }
        }
        else
            decoded += c;
    }

    return true;
}

bool JsonReader::DecodeUnicodeEscapeSequence(Token& token, Location& current, Location end, unsigned int& unicode)
{
    if (end - current < 4)
        return AddError("Bad unicode escape sequence in string: four digits expected.", token, current);

    unicode = 0;

    for (int index = 0; index < 4; index++)
    {
        char c = *current++;

        unicode <<= 4;

        if      (c >= '0' && c <= '9')
            unicode += c - '0';
        else if (c >= 'a' && c <= 'f')
            unicode += c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            unicode += c - 'A' + 10;
        else
            return AddError("Bad unicode escape sequence in string: hexadecimal digit expected.", token, current);
    }

    return true;
}

bool JsonReader::AddError(const char* message, const Token& token, Location extra)
{
    ErrorInfo info;

    info.mToken = token;
    info.mMessage = message;
    info.mExtra = extra;

    mErrors.push_back(info);

    return false;
}

bool JsonReader::RecoverFromError(TokenType skipUntilToken)
{
    int errorCount = size_i(mErrors);
    Token skip;

    while (true)
    {
        if (!ReadToken(skip))
            mErrors.resize(errorCount); // discard errors caused by recovery

        if (skip.mType == skipUntilToken || skip.mType == kTokenEndOfStream)
            break;
    }

    mErrors.resize(errorCount);
    return false;
}

bool JsonReader::AddErrorAndRecover(const String& message, Token& token, TokenType skipUntilToken)
{
    AddError(message.c_str(), token);
    return RecoverFromError(skipUntilToken);
}

inline char JsonReader::GetNextChar()
{
    if (mCurrent == mEnd)
        return 0;

    return *mCurrent++;
}

void JsonReader::GetLocationLineAndColumn(Location location, int& line, int& column) const
{
    Location current = mBegin;
    Location lastLineStart = current;

    line = 0;

    while (current < location && current != mEnd)
    {
        char c = *current++;

        if (c == '\r')
        {
            if (*current == '\n')
                ++current;
            lastLineStart = current;
            ++line;
        }
        else if (c == '\n')
        {
            lastLineStart = current;
            ++line;
        }
    }

    // column & line start at 1
    column = int(location - lastLineStart) + 1;
    ++line;
}

void JsonReader::AddLocationLineAndColumn(Location location, String* str) const
{
    int line, column;
    GetLocationLineAndColumn(location, line, column);

    char buffer[18 + 16 + 16 + 1];
    snprintf(buffer, sizeof(buffer), "Line %d, Column %d", line, column);

    *str += buffer;
}

void JsonReader::GetErrors(String* errorString) const
{
    errorString->clear();

    for (Errors::const_iterator itError = mErrors.begin(); itError != mErrors.end(); ++itError)
    {
        const ErrorInfo& error = *itError;

        *errorString += "* ";
        AddLocationLineAndColumn(error.mToken.mStart, errorString);
        *errorString += "\n  ";

        *errorString += error.mMessage;
        *errorString += '\n';

        if (error.mExtra)
        {
            *errorString += "See ";
            AddLocationLineAndColumn(error.mExtra, errorString);
            *errorString += " for detail.\n";
        }
    }
}

int JsonReader::GetFirstErrorLine() const
{
    auto it = mErrors.begin();

    if (it != mErrors.end())
    {
        int line, column;
        GetLocationLineAndColumn(it->mToken.mStart, line, column);

        return line;
    }

    return -1;
}

#ifdef HL_VALUE_COMMENTS
void JsonReader::AddComment(Location begin, Location end, int placement)
{
    HL_ASSERT(mCollectComments);

    if (placement == kCommentAfterOnSameLine)
    {
        HL_ASSERT(mLastValue != 0);
        mLastValue->SetComment(String(begin, end), CommentPlacement(placement));
    }
    else
    {
        mCommentsBefore += String(begin, end);
    }
}
#endif


//
// JsonWriter implementation
//

namespace
{
    inline bool IsControlCharacter(char ch)
    {
        return ch > 0 && ch <= 0x1F;
    }

    bool ContainsControlCharacter(const char* str)
    {
        while (*str)
            if (IsControlCharacter(*(str++)))
                return true;

        return false;
    }

    bool IsToken(const char* name)
    {
        if (!IsStartTokenChar(*name))
            return false;

        name++;
        while (*++name != 0)
            if (!IsTokenChar(*name))
                return false;

        return true;
    }

    void UIntToString(uint64_t value, char*& current)
    {
        *--current = 0;

        do
        {
            *--current = (value % 10) + '0';
            value /= 10;
        }
        while (value != 0);
    }

    void ValueToString(int64_t value, String* outString)
    {
        char buffer[32];
        char* current = buffer + sizeof(buffer);
        bool isNegative = value < 0;
        if (isNegative)
            value = -value;
        UIntToString(uint64_t(value), current);
        if (isNegative)
            *--current = '-';
        HL_ASSERT(current >= buffer);
        *outString += current;
    }

    void ValueToString(uint64_t value, String* outString)
    {
        char buffer[32];
        char* current = buffer + sizeof(buffer);
        UIntToString(value, current);
        HL_ASSERT(current >= buffer);
        *outString += current;
    }

    void ValueToString(double value, String* outString, const JsonFormat& jf)
    {
        if (jf.infNaN == kInfNanJS)
        {
            if (isinf(value))
            {
                *outString = value < 0.0f ? "-Infinity" : "Infinity";
                return;
            }
            else if (isnan(value))
            {
                *outString = "NaN";
                return;
            }
        }
        else if (jf.infNaN == kInfNanNull && (isinf(value) || isnan(value)))
        {
            *outString = "null";
            return;
        }

        char formatStr[6 + 11 + 1];
        snprintf(formatStr, HL_SIZE(formatStr), "%%##0.%ig", jf.maxPrecision);

        char buffer[64];
        snprintf(buffer, HL_SIZE(buffer), formatStr, value);

        if (!jf.trimZeroes)
        {
            outString->assign((const char*) buffer);
            return;
        }

        const char* dot = strchr(buffer, '.');
        if (!dot)
        {
            *outString += buffer;
            return;
        }

        const char* last = dot;
        while (last[1])
            ++last;

        while (*last == '0' && last > dot)
            last--;

        if (last == dot)  // remove trailing dot either due to trimming or format output
            last--;

        outString->assign((const char*) buffer, last + 1);
    }

    void ValueToString(bool value, String* outString)
    {
        *outString += (value ? "true" : "false");
    }

    void ValueToStringInternal(const char* value, String* outString, bool quoted)
    {
        if (strpbrk(value, "\"\\\b\f\n\r\t") == NULL && !ContainsControlCharacter(value))
        {
            if (quoted)
                *outString += '"';
            *outString += value;
            if (quoted)
                *outString += '"';
            return;
        }
        // We have to walk value and escape any special characters.
        // Appending to String is not efficient, but this should be rare.
        // (Note: forward slashes are *not* rare, but I am not escaping them.)
        size_t maxsize = strlen(value) * 2 + 3; // assume all escaped + quotes + \0

        outString->reserve(maxsize + outString->size());
        if (quoted)
            *outString += '"';

        for (const char* c = value; *c != 0; ++c)
        {
            switch (*c)
            {
            case '\"':
                *outString += "\\\"";
                break;
            case '\\':
                *outString += "\\\\";
                break;
            case '\b':
                *outString += "\\b";
                break;
            case '\f':
                *outString += "\\f";
                break;
            case '\n':
                *outString += "\\n";
                break;
            case '\r':
                *outString += "\\r";
                break;
            case '\t':
                *outString += "\\t";
                break;
            default:
                if (IsControlCharacter(*c))
                {
                    char buffer[7];
                    snprintf(buffer, sizeof(buffer), "\\u%04x", int(*c));
                    *outString += buffer;
                }
                else
                    *outString += *c;
                break;
            }
        }
        if (quoted)
            *outString += '"';
    }

    void ValueToString(const char* value,  String* outString)
    {
        ValueToStringInternal(value, outString, false);
    }

    void ValueToQuotedString(const char* value,  String* outString)
    {
        ValueToStringInternal(value, outString, true);
    }

#ifdef HL_VALUE_COMMENTS
    void NormalizeEOL(const char* text, String* outString)
    {
        size_t textLength = strlen(text);
        outString->reserve(outString->size() + textLength);
        const char* end = text + textLength;
        const char* current = text;
        while (current != end)
        {
            char c = *current++;
            if (c == '\r')   // mac or dos EOL
            {
                if (*current == '\n') // convert dos EOL
                    ++current;
                *outString += '\n';
            }
            else   // handle unix EOL & other char
                *outString += c;
        }
    }
#endif
}

JsonWriter::JsonWriter()
{
}

JsonWriter::JsonWriter(const JsonFormat& format) :
    mFormat(format)
{
}

void JsonWriter::Write(const Value& root, String* outString)
{
    mDocument.clear();

    mAddChildValues = false;
    mIndent = 0;

#ifdef HL_VALUE_COMMENTS
    WriteCommentBeforeValue(root);
#endif
    WriteValue(root);
#ifdef HL_VALUE_COMMENTS
    WriteCommentAfterValueOnSameLine(root);
#endif

    mDocument.swap(*outString);
    mDocument.clear();
}

const char* JsonWriter::Write(const Value& root)
{
    mDocument.clear();

    mAddChildValues = false;
    mIndent = 0;

#ifdef HL_VALUE_COMMENTS
    WriteCommentBeforeValue(root);
#endif
    WriteValue(root);
#ifdef HL_VALUE_COMMENTS
    WriteCommentAfterValueOnSameLine(root);
#endif

    return mDocument.c_str();
}

void JsonWriter::WriteValue(const Value& value)
{
    switch (value.Type())
    {
    case kValueNull:
        PushValue("null");
        break;
    case kValueInt:
        ValueToString((int64_t) value.mValue.mInt32, &mScratch);
        PushValue(mScratch.c_str());
        mScratch.clear();
        break;
    case kValueUInt:
        ValueToString((uint64_t) value.mValue.mUInt32, &mScratch);
        PushValue(mScratch.c_str());
        mScratch.clear();
        break;
    case kValueInt64:
        ValueToString(value.mValue.mInt64, &mScratch);
        PushValue(mScratch.c_str());
        mScratch.clear();
        break;
    case kValueUInt64:
        ValueToString(value.mValue.mUInt64, &mScratch);
        PushValue(mScratch.c_str());
        mScratch.clear();
        break;
    case kValueDouble:
        ValueToString(value.AsDouble(), &mScratch, mFormat);
        PushValue(mScratch.c_str());
        mScratch.clear();
        break;
    case kValueString:
        ValueToQuotedString(value.AsString(), &mScratch);
        PushValue(mScratch.c_str());
        mScratch.clear();
        break;
    case kValueBool:
        ValueToString(value.AsBool(), &mScratch);
        PushValue(mScratch.c_str());
        mScratch.clear();
        break;
    case kValueArray:
        WriteArrayValue(value);
        break;
    case kValueObject:
        {
            if (value.empty())
            {
                PushValue("{}");
                break;
            }

            WriteWithIndent("{");
            Indent();

            const ObjectValue& objectValue = value.AsObject();

            for (int i = 0, n = objectValue.NumMembers(); i < n; i++)
            {
                const char*  name       = objectValue.MemberName(i);
                const Value& childValue = objectValue.MemberValue(i);

            #ifdef HL_VALUE_COMMENTS
                WriteCommentBeforeValue(childValue);
            #endif

                WriteIndent();
                if (mFormat.quoteKeys || !IsToken(name))
                    ValueToQuotedString(name, &mDocument);
                else
                    ValueToString(name, &mDocument);

                if (mFormat.indent < -1)
                    mDocument += ':';
                else
                    mDocument += ": ";

                WriteValue(childValue);

                if (i < n - 1)
                    mDocument += ',';

            #ifdef HL_VALUE_COMMENTS
                WriteCommentAfterValueOnSameLine(childValue);
            #endif
            }

            Unindent();

            WriteWithIndent("}");
        }
        break;
    }
}

void JsonWriter::WriteArrayValue(const Value& value)
{
    int size = size_i(value);

    if (size == 0)
    {
        PushValue("[]");
        return;
    }

    bool isArrayMultiLine = mFormat.indent >= 0 && IsMultiLineArray(value);
    bool hasChildValues = !mChildValues.empty();

    if (isArrayMultiLine)
    {
        WriteWithIndent("[");

        Indent();

        for (int i = 0; ; i++)
        {
            const Value& childValue = value[i];

        #ifdef HL_VALUE_COMMENTS
            WriteCommentBeforeValue(childValue);
        #endif

            if (hasChildValues)
                WriteWithIndent(mChildValues[i].c_str());
            else
            {
                WriteIndent();
                WriteValue(childValue);
            }

            if (i + 1 == size)
            {
            #ifdef HL_VALUE_COMMENTS
                WriteCommentAfterValueOnSameLine(childValue);
            #endif
                break;
            }

            mDocument += ',';
        #ifdef HL_VALUE_COMMENTS
            WriteCommentAfterValueOnSameLine(childValue);
        #endif
        }

        Unindent();

        WriteWithIndent("]");
    }
    else
    {
        HL_ASSERT(!hasChildValues || size_i(mChildValues) == size);

        mDocument += '[';

        for (int i = 0; i < size; i++)
        {
            if (i > 0)
            {
                mDocument += ',';
                if (mFormat.indent >= -1)
                    mDocument += ' ';
            }

            if (hasChildValues)
                mDocument += mChildValues[i];
            else
                WriteValue(value[i]);
        }

        mDocument += ']';
    }

    mChildValues.clear();
}

bool JsonWriter::IsMultiLineArray(const Value& value)
{
    if (mFormat.arrayMargin == 0)
        return true;

    int size = size_i(value);
    bool isMultiLine = size * 3 >= mFormat.arrayMargin;

    for (int index = 0; index < size && !isMultiLine; index++)
    {
        const Value& childValue = value[index];

    #ifdef HL_VALUE_COMMENTS
        if (childValue.HasComment(kCommentBefore) || childValue.HasComment(kCommentAfterOnSameLine) || childValue.HasComment(kCommentAfter))
            isMultiLine = true;
        else
    #endif
        if ((childValue.IsArray() || childValue.IsObject()) && !childValue.empty())
            isMultiLine = true;
    }

    if (!isMultiLine)
    {
        // check if line length > max line length
        mChildValues.reserve(size);
        mAddChildValues = true;

        int lineLength = 2 + (size - 1) * 2; // '[' + ', ' * (n - 1) + ']'

        for (int i = 0; i < size; i++)
        {
            WriteValue(value[i]);
            lineLength += int(mChildValues[i].length());
        }

        mAddChildValues = false;
        isMultiLine = lineLength >= mFormat.arrayMargin;
    }

    return isMultiLine;
}

void JsonWriter::PushValue(const char* value)
{
    if (mAddChildValues)
        mChildValues.push_back(value);
    else
        mDocument += value;
}

void JsonWriter::WriteIndent()
{
    if (mFormat.indent < 0)
    {
        if (mFormat.indent == -1 && !mDocument.empty())
            mDocument += ' ';
        return;
    }

    if (!mDocument.empty())
    {
        char last = mDocument.back();
        if (last == ' ')  // already indented
            return;
        if (last != '\n') // comments may add new-line
            mDocument += '\n';
    }

    mDocument.insert(mDocument.size(), mIndent, ' ');
}

void JsonWriter::WriteWithIndent(const char* value)
{
    WriteIndent();
    mDocument += value;
}

void JsonWriter::Indent()
{
    if (mFormat.indent >= 0)
        mIndent += mFormat.indent;
}

void JsonWriter::Unindent()
{
    HL_ASSERT(mIndent >= mFormat.indent);
    if (mFormat.indent >= 0)
        mIndent -= mFormat.indent;
}

#ifdef HL_VALUE_COMMENTS
void JsonWriter::WriteCommentBeforeValue(const Value& root)
{
    if (!root.HasComment(kCommentBefore))
        return;

    NormalizeEOL(root.Comment(kCommentBefore), &mDocument);
}

void JsonWriter::WriteCommentAfterValueOnSameLine(const Value& root)
{
    if (root.HasComment(kCommentAfterOnSameLine))
        mDocument += ' ';

    if (root.HasComment(kCommentAfter))
        NormalizeEOL(root.Comment(kCommentAfter), &mDocument);
}
#endif

// External API

// JsonReader wrappers

bool HL::LoadJsonFile(const char* path, Value* value, String* errors, StringTable* st)
{
    FILE* file = fopen(path, "rb");  // TODO: windows returns fread < fsize when it does auto-cr translation, hence rb to defeat this

    if (!file)
    {
        if (errors)
        {
            *errors += "Couldn't read ";
            *errors += path;
            *errors += '\n';
        }
        return false;
    }

    bool result = LoadJsonFile(file, value, errors, st);
    return fclose(file) == 0 && result;
}

bool HL::LoadJsonFile(FILE* file, Value* value, String* errors, StringTable* st)
{
    long fileSize = 0;

    if (fseek(file, 0, SEEK_END) == 0)
        fileSize = ftell(file);

    if (fseek(file, 0, SEEK_SET) != 0)
        fileSize = 0;

    String text;
    text.resize(fileSize);

    if (fread((char*) text.data(), 1, fileSize, file) != (size_t) fileSize)
    {
        if (errors)
            *errors += "Couldn't read file data\n";
        return false;
    }

    return LoadJsonText(text.c_str(), value, errors, st);
}

bool HL::LoadJsonText(const char* text, Value* value, String* errors, StringTable* st)
{
    JsonReader reader(st);

    if (reader.Read(text, value))
        return true;

    if (errors)
        reader.GetErrors(errors);

    return false;
}

bool HL::LoadJsonText(const char* textBegin, const char* textEnd, Value* value, String* errors, StringTable* st)
{
    JsonReader reader(st);

    if (reader.Read(textBegin, textEnd, value))
        return true;

    if (errors)
        reader.GetErrors(errors);

    return false;
}


JsonFormat HL::kJsonFormatDefault;
JsonFormat HL::kJsonFormatStrict = { 2, true, 0, 6, true, kInfNanNull };

String HL::AsJson(const Value& v, int indent, JsonFormat format)
{
    format.indent = indent;
    JsonWriter writer(format);

    String result;
    writer.Write(v, &result);
    return result;
}

bool HL::SaveAsJson(const char* path, const Value& v, const JsonFormat& format)
{
    FILE* file = fopen(path, "w");
    if (!file)
        return false;

    bool result = SaveAsJson(file, v, format);
    return fclose(file) == 0 && result;
}

bool HL::SaveAsJson(FILE* file, const Value& v, const JsonFormat& format)
{
    JsonWriter writer(format);
    String out;
    writer.Write(v, &out);
    return fwrite(out.data(), out.size(), 1, file) == 1;
}

void HL::SaveAsJson(String* text, const Value& v, const JsonFormat& format)
{
    JsonWriter writer(format);
    writer.Write(v, text);
}
