//
// ValueJsonInternal.hpp
//
// Reader and writer class declarations
//
// Andrew Willmott
//

#include "ValueJson.hpp"
#include "RefCount.hpp"
#include <vector>

#ifndef HL_NO_STRING_TABLE
    #include "StringTable.hpp"
#endif

namespace HL
{
    class Value;

    //
    // Json parser. Supports:
    // - C++-style comments a la original json or json5
    // - Proper line/column error messages
    // - Optionally trailing commas and bare-word (unquoted) keys.
    //
    // Originally based off jsoncpp (public domain), Baptiste Lepilleur
    //

    class JsonReader
    {
    public:
        JsonReader(StringTable* st = 0);

        bool Read(const char* document, Value* value);
        // Read json from given UTF8 document string and store the results in 'value'.
        // Returns false if an error occurred, although an attempt is made to recover even in the
        // presence of errors. Call GetErrors() to retrieve the corresponding error(s).

        bool Read(const char* beginDoc, const char* endDoc, Value* value);  // Read a Value from a string range.

        void GetErrors(String* formattedMessage) const;  // Returns a string that lists any errors in the parsed document.

        int GetFirstErrorLine() const;  // Returns line number of the first error, or -1 if none.

    protected:
        typedef const char* Location;

        enum TokenType
        {
            kTokenEndOfStream = 0,
            kTokenObjectBegin,
            kTokenObjectEnd,
            kTokenArrayBegin,
            kTokenArrayEnd,
            kTokenString,
            kTokenNumber,
            kTokenMinusInfinity,
            kTokenInfinity,
            kTokenNaN,
            kTokenTrue,
            kTokenFalse,
            kTokenNull,
            kTokenArraySeparator,
            kTokenMemberSeparator,
            kTokenComment,
            kTokenError
        };

        struct Token
        {
            TokenType   mType;
            const char* mStart;
            const char* mEnd;
        };

        struct ErrorInfo
        {
            Token    mToken;
            String   mMessage;
            Location mExtra;
        };

        typedef std::vector<ErrorInfo> Errors;
        typedef std::vector<Value*> Nodes;

        // Utils
        bool ExpectToken(TokenType type, Token& token, const char* message);
        bool ReadToken(Token& token);
        void SkipSpaces();
        bool Match(Location pattern, int patternLength);

        bool ReadComment();
        bool ReadCStyleComment();
        bool ReadCppStyleComment();
        bool ReadString();
        bool ReadUnquotedString();
        void ReadNumber();
        bool ReadValue();

        bool ReadValue(Token& token);
        bool ReadObject(Token& token);
        bool ReadArray(Token& token);

        bool DecodeNumber(Token& token);
        bool DecodeString(Token& token);
        bool DecodeString(Token& token, String& decoded);
        bool DecodeDouble(Token& token);
        bool DecodeUnicodeEscapeSequence(Token& token, Location& current, Location end, uint32_t& unicode);

        bool AddError(const char* message, const Token& token, Location extra = 0);
        bool RecoverFromError(TokenType skipUntilToken);
        bool AddErrorAndRecover(const String& message, Token& token, TokenType skipUntilToken);

        char GetNextChar();
        void GetLocationLineAndColumn(Location location, int& line, int& column) const;
        void AddLocationLineAndColumn(Location location, String* str) const;
        bool ReadNonCommentToken(Token& token);

    #ifdef HL_VALUE_COMMENTS
        void AddComment(Location begin, Location end, int placement);
    #endif

    protected:
        // Data
        Nodes       mNodes;
        Errors      mErrors;
        Location    mBegin   = 0;
        Location    mEnd     = 0;
        Location    mCurrent = 0;

    #ifdef HL_STRING_TABLE_HPP
        AutoRef<StringTable> mStringTable;
    #endif

        // Comments handling
        Location    mLastValueEnd = 0;
        Value*      mLastValue = 0;
        String      mCommentsBefore;

        // Options
        bool        mCollectComments      = false;
        bool        mAllowUnquotedStrings = true;
        bool        mAllowTrailingCommas  = true;
        bool        mUseStringTableForKey   = true;
        bool        mUseStringTableForValue = true;
    };


    // JsonWriter

    class JsonWriter
    {
    public:
        JsonWriter();
        JsonWriter(const JsonFormat& format);  // Initialise with the given formatting options

        void        Write(const Value& value, String* jsonText);  // Fill 'jsonText' with the json representation of 'value'
        const char* Write(const Value& value);  // Return the json representation of 'value' as a C string, valid until the next Write() call.

    protected:
        void WriteValue(const Value& value);
        void WriteArrayValue(const Value& value);
        bool IsMultiLineArray(const Value& value);
        void PushValue(const char* value);
        void WriteIndent();
        void WriteWithIndent(const char* value);
        void Indent();
        void Unindent();

    #ifdef HL_VALUE_COMMENTS
        void WriteCommentedValue(const Value& value);
        void WriteCommentBeforeValue(const Value& root);
        void WriteCommentAfterValueOnSameLine(const Value& root);
    #endif

        // Data
        String  mDocument;
        int     mIndent = 0;

        bool    mAddChildValues = false;
        Strings mChildValues;

        JsonFormat mFormat;

        String mScratch;
    };
}
