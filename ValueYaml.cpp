//
// ValueYaml.cpp
//
// Support for reading/writing (simple) yaml files to Value
//
// Andrew Willmott
//

#define _CRT_SECURE_NO_WARNINGS

#include "ValueYaml.hpp"

#include "Value.hpp"
#include "ValueJson.hpp"

#ifndef HL_NO_STRING_TABLE
    #include "StringTable.hpp"
#endif

#ifdef HL_ALLOC
    #include "Memory.hpp"
#endif

#include "external/yaml.h"

using namespace HL;

namespace
{
    enum YamlResult { kYamlOk, kYamlEnd, kYamlError };

    struct YamlReader
    {
        yaml_parser_t             mParser;
        StringTable*              mStringTable = 0;
        vector_map<String, Value> mAnchors;
        String                    mLocalError;

        YamlReader(const char* text, StringTable* st) : mStringTable(st)
        {
            yaml_parser_initialize(&mParser);
            yaml_parser_set_input_string(&mParser, (const unsigned char*)text, strlen(text));
        }

        YamlReader(FILE* file, StringTable* st) : mStringTable(st)
        {
            yaml_parser_initialize(&mParser);
            yaml_parser_set_input_file(&mParser, file);
        }

        ~YamlReader()
        {
            yaml_parser_delete(&mParser);
        }

        YamlResult ParseScalar  (Value* value);
        YamlResult ParseSequence(Values* array);
        YamlResult ParseMapping (ObjectValue* object);

        void AppendError(String* errors);
    };

    YamlResult YamlReader::ParseScalar(Value* scalar)
    {
        bool done = false;
        YamlResult result = kYamlOk;

        do
        {
            yaml_event_t event;

            if (!yaml_parser_parse(&mParser, &event))
            {
                yaml_event_delete(&event);
                return kYamlError;
            }

            switch (event.type)
            {
            case YAML_NO_EVENT:
            case YAML_STREAM_START_EVENT:
            case YAML_DOCUMENT_START_EVENT:
                break;

            case YAML_STREAM_END_EVENT:
            case YAML_DOCUMENT_END_EVENT:
                result = kYamlOk;
                done = true;
                break;

            case YAML_SEQUENCE_START_EVENT:
                {
                    Values array;
                    result = ParseSequence(&array);
                    *scalar = array;

                    if (event.data.scalar.anchor)
                        mAnchors[String((const char*) event.data.scalar.anchor)] = scalar->AsArrayPtr();
                }
                done = true;
                break;

            case YAML_MAPPING_START_EVENT:
                result = ParseMapping(scalar->ToObjectPtr());

                if (event.data.mapping_start.anchor)
                    mAnchors[String((const char*) event.data.mapping_start.anchor)] = scalar->AsObjectPtr();

                done = true;
                break;

            case YAML_SEQUENCE_END_EVENT:
            case YAML_MAPPING_END_EVENT:
                result = kYamlEnd;
                done = true;
                break;

            case YAML_ALIAS_EVENT:
                {
                    const char* anchor = (const char*) event.data.alias.anchor;

                    auto it = mAnchors.find(anchor);

                    if (it != mAnchors.end())
                        *scalar = it->second;
                    else
                    {
                        mLocalError = Format("unknown anchor '%s'", anchor);
                        mParser.problem = mLocalError;
                        mParser.problem_mark = mParser.mark;
                        result = kYamlError;
                    }
                }
                done = true;
                break;

            case YAML_SCALAR_EVENT:
                {
                    static_assert(sizeof(yaml_char_t) == sizeof(char), "mismatch");

                    result = kYamlOk;
                    String valueStr(event.data.scalar.value, event.data.scalar.value + event.data.scalar.length);

                    // TODO: I guess we're supposed to handle explicit 'tag' types here. But how many
                    // people even know they exist?! Ridiculous format.

                    if (event.data.scalar.style == YAML_PLAIN_SCALAR_STYLE)
                    {
                        done = true;

                        if (*valueStr == 0 || EqualI(valueStr, "null") || Equal(valueStr, "~"))
                            scalar->MakeNull();

                        else if (EqualI(valueStr, "true"))
                            *scalar = Value(true);

                        else if (EqualI(valueStr, "false"))
                            *scalar = Value(false);

                        else if (Equal(valueStr, "-.inf"))
                            *scalar = -INFINITY;

                        else if (Equal(valueStr, ".inf"))
                            *scalar = INFINITY;

                        else if (Equal(valueStr, ".nan"))
                            *scalar = NAN;
                        else
                            done = false;

                        if (!done)
                        {
                            String numberStr(valueStr);
                            // in yaml you can use '_' in numbers as a separator, because why not :
                            for (auto it = numberStr.begin(); it != numberStr.end(); )
                                if (*it == '_')
                                    it = numberStr.erase(it);
                                else
                                    ++it;

                            // handle 0o for octal because why not just make up a new number format for such a heavily-used category?
                            if (StartsWith(numberStr, "0o"))
                                numberStr.erase(1, 1);

                            char* last = 0;
                            long intResult = strtol(numberStr, &last, 0);

                            if (last[0] == 0)
                            {
                                *scalar = (int) intResult;
                                done = true;
                            }
                            else
                            {
                                last = 0;
                                double doubleResult = strtod(numberStr, &last);

                                if (last[0] == 0)
                                {
                                    *scalar = doubleResult;
                                    done = true;
                                }
                            }
                        }
                    }
                    else
                        done = false;

                    if (!done)
                    {
                        // Well, I guess it's a string then.
                        if (mStringTable)
                            *scalar = Value(mStringTable->GetString(valueStr));
                        else
                            *scalar = Value(valueStr);

                        done = true;
                    }

                    if (event.data.scalar.anchor)
                        mAnchors[String((const char*) event.data.scalar.anchor)] = scalar;
                }
                break;
            }

            yaml_event_delete(&event);
        }
        while (!done);

        return result;
    }

    YamlResult YamlReader::ParseSequence(Values* array)
    {
        while (true)
        {
            Value item;

            YamlResult result = ParseScalar(&item);

            if (result == kYamlEnd)
                return kYamlOk;

            if (result == kYamlError)
                return kYamlError;

            array->push_back(item);
        }
    }

    YamlResult YamlReader::ParseMapping(ObjectValue* object)
    {
        YamlResult result;

        while (true)
        {
            yaml_event_t event;

            if (!yaml_parser_parse(&mParser, &event))
            {
                result = kYamlError;
                break;
            }

            if (event.type == YAML_MAPPING_END_EVENT)
            {
                result = kYamlOk;
                break;
            }
            else if (event.type != YAML_SCALAR_EVENT)
            {
                result = kYamlError;
                mParser.problem = "expecting scalar value for key";
                mParser.problem_mark = mParser.mark;
                break;
            }
            else
            {
                String key(event.data.scalar.value, event.data.scalar.value + event.data.scalar.length);

                if (key == "<<")
                {
                    Value mergeValue;
                    result = ParseScalar(&mergeValue);

                    if (result == kYamlOk)
                    {
                        bool success = true;
                        if (mergeValue.IsObject())
                            object->Merge(mergeValue.AsObject());
                        else if (mergeValue.IsArray())
                        {
                            // This exists pretty much just to support [*anchor1, *anchor2, ...] syntax :P

                            for (const Value& av : mergeValue.AsArray())
                                if (av.IsObject())
                                    object->Merge(av.AsObject());
                                else
                                    success = false;
                        }

                        if (!success)
                        {
                            result = kYamlError;
                            mParser.problem = "can't merge non-mapping";
                            mParser.problem_mark = mParser.mark;
                        }
                    }
                }
                else
                {
                    Value& newMember = object->UpdateMember(key, mStringTable);

                    result = ParseScalar(&newMember);
                }

                if (result != kYamlOk)
                    break;
            }

            yaml_event_delete(&event);
        }

        return result;
    }

    void YamlReader::AppendError(String* errors)
    {
        *errors += mParser.problem;
        *errors += Format(" in line %d, col %d\n", mParser.problem_mark.line, mParser.problem_mark.column);
    }
}

bool HL::LoadYamlText(const char* text, Value* value, String* errors, StringTable* st)
{
    YamlReader reader(text, st);

    value->MakeNull();

    YamlResult result = reader.ParseScalar(value);

    if (result == kYamlError && errors)
        reader.AppendError(errors);

    return result != kYamlError;
}

bool HL::LoadYamlFile(FILE* file, Value* value, String* errors, StringTable* st)
{
    YamlReader reader(file, st);

    value->MakeNull();
    YamlResult result = reader.ParseScalar(value);

    if (result == kYamlError && errors)
        reader.AppendError(errors);

    return result != kYamlError;
}

bool HL::LoadYamlFile(const char* path, Value* value, String* errors, StringTable* st)
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

    bool result = LoadYamlFile(file, value, errors, st);

    fclose(file);
    return result;
}

namespace
{
    template <typename OutDest, typename OutFunc>
    void SaveAsYaml(OutFunc outFunc, OutDest* out, const Value& v, int tab = 2, int indent = 0)
    {
        switch (v.Type())
        {
        case kValueObject:
            if (indent > 0)
                outFunc(out, "\n");

            for (ConstNameValue item : v.AsObject())
            {
                outFunc(out, "%*s%s: ", indent, "", item.name);
                SaveAsYaml(outFunc, out, item.value, tab, indent + tab);
            }
            break;

        case kValueArray:
            if (indent > 0)
                outFunc(out, "\n");

            for (const Value& av : v.AsArray())
            {
                outFunc(out, "%*s", indent + tab, "- ");
                SaveAsYaml(outFunc, out, av, tab, indent + tab);
            }
            break;
        default:
            outFunc(out, "%s\n", AsJson(v).c_str());
            break;
        }
    }
}

String HL::AsYaml(const Value& v, int indent)
{
    String s;
    ::SaveAsYaml(&s, v, indent);
    return s;
}

bool HL::SaveAsYaml(const char* path, const Value& v, int indent)
{
    FILE* file = fopen(path, "w");

    if (!file)
    {
        return false;
    }

    ::SaveAsYaml(fprintf, file, v, indent);
    fclose(file);

    return true;
}

bool HL::SaveAsYaml(FILE* out, const Value& v, int indent)
{
    ::SaveAsYaml(fprintf, out, v, indent);
    return ferror(out) == 0;
}

void HL::SaveAsYaml(String* out, const Value& v, int indent)
{
    ::SaveAsYaml(AppendFormat, out, v, indent);
}

#ifdef HL_ALLOC

extern "C"
{

YAML_DECLARE(void *) yaml_malloc(size_t size)
{
    return HL::Allocate(size ? size : 1);
}

YAML_DECLARE(void *) yaml_realloc(void *ptr, size_t size)
{
    return HL::Reallocate(ptr, size ? size : 1);
}

YAML_DECLARE(void) yaml_free(void *ptr)
{
    HL::Deallocate(ptr);
}

YAML_DECLARE(yaml_char_t *) yaml_strdup(const yaml_char_t *str)
{
    if (!str)
        return NULL;

    size_t len = strlen((const char*) str) + 1;
    yaml_char_t* newString = (yaml_char_t*) HL::Allocate(len);
    if (newString)
        memcpy(newString, str, len);
    return newString;
}

}

#endif
