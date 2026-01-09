//
// ValueYaml.hpp
//
// Support for reading/writing (simple) yaml files to Value
//
// Andrew Willmott
//

#ifndef HL_VALUE_YAML_H
#define HL_VALUE_YAML_H

#include "String.hpp"
#include <stdio.h>

namespace HL
{
    struct StringTable;
    class Value;

    bool LoadYamlFile(const char* path, Value* value, String* errors = 0, StringTable* st = 0);
    bool LoadYamlFile(FILE*       file, Value* value, String* errors = 0, StringTable* st = 0);
    bool LoadYamlText(const char* text, Value* value, String* errors = 0, StringTable* st = 0);

    String AsYaml(const Value& v, int indent = 2);

    bool SaveAsYaml(const char*  path, const Value& v, int indent = 2);
    bool SaveAsYaml(FILE*        out,  const Value& v, int indent = 2);
    void SaveAsYaml(String*      text, const Value& v, int indent = 2);
}

#endif
