//
// Config.cpp
//
// Utilities for managing Value-based configurations, e.g., preferences,
// game system tuning, model definitions...
//
// Andrew Willmott
//

#include "Config.hpp"

#include "ValueJson.hpp"
#include "ValueYaml.hpp"

#include "Path.hpp"

#include "math.h"

using namespace HL;

namespace
{
    const char* const kJsonExtensions[] = { ".json", ".jsn", ".json5", 0 };
#ifdef HL_VALUE_YAML_H
    const char* const kYamlExtensions[] = { ".yaml", ".yml", 0 };
#endif

    // Templates support
    bool ApplyTemplatesInternal(Value& objects, Value* target, String* errors)
    {
        const Value& templateValue = target->Member("template");

        if (!templateValue)
            return true;

        const char* templateKey = templateValue.AsString();
        Value* templateTarget = objects.UpdateMemberPtr(templateKey);

        if (!templateTarget)
        {
            if (errors)
            {
                *errors += "Unknown template key: ";
                *errors += templateKey;
                *errors += '\n';
            }
            return false;
        }

        // Ensure we've expanded any templates in the source first
        if (!ApplyTemplatesInternal(objects, templateTarget, errors))
            return false;

        target->RemoveMember("template");

        Value newValue(*templateTarget);
        newValue.Merge(*target);

        target->Swap(newValue);
        return true;
    }

    bool ApplyTemplates(Value* objects, String* errors)
    {
        bool success = true;

        // Apply at this level. Note that this may add some template directives to
        // our children, which will result in redundant expansions below. However,
        // if we don't do it in this order, our children can't template on an
        // object brought in by our template argument, which is useful behaviour sometimes.

        ObjectValue* ov = objects->AsObjectPtr();
        if (ov)
        {
            for (NameValue member : *ov)
                if (member.value.IsObject() && !ApplyTemplatesInternal(*objects, &member.value, errors))
                    success = false;

            for (NameValue member : *ov)
                if (!ApplyTemplates(&member.value, errors))
                    success = false;
        }

        ArrayValue* av = objects->AsArrayPtr();
        if (av)
            for (Value& v : *av)
                if (!ApplyTemplates(&v, errors))
                    success = false;

        return success;
    }
}

namespace
{
    // Imports support
    typedef bool FileLoader(const char* path, Value* value, String* errors, StringTable* st);

    bool AddImports(FileLoader loader, const char* basePath, Value* value, String* errors, ConfigInfo* info);

    bool LoadImport
    (
        const Value& importPathValue,
        FileLoader   loader,
        const char*  basePath,
        Value*       value,
        String*      errors,
        ConfigInfo*  info
    )
    {
        const char* relativeImportPath = importPathValue.AsCString();

        if (!relativeImportPath)
        {
            if (errors)
            {
                *errors += "Expecting import path in '";
                *errors += AsJson(importPathValue);
                *errors += "'\n";
            }
            return false;
        }

        String importPath = PathFull(relativeImportPath, basePath);

        bool success = false;
        bool importExists = false;

    #ifdef HL_LOCATION_H
        if (DataLocationExists(importPath))
    #else
        if (PathFileExists(importPath))
    #endif
        {
            importExists = true;
            success = loader(importPath.c_str(), value, errors, info ? info->mStringTable : 0);

            if (success)
            {
                if (info)
                    info->mImports.insert(importPath);

                success = AddImports(loader, PathLocation(importPath), value, errors, info);
            }

            if (!success && errors)
            {
                *errors += "  in ";
                *errors += importPath;
                *errors += '\n';
            }
        }

        // support loading variant files (path/file[_<variant>])
        if (info && !info->mVariant.empty())
        {
            String variantImportPath = PathWithSuffix(importPath, ("_" + info->mVariant).c_str());

            if (PathFileExists(variantImportPath))
            {
                importExists = true;
                Value variantValue;

                success = loader(variantImportPath.c_str(), &variantValue, errors, info ? info->mStringTable : 0);

                if (success)
                {
                    if (info)
                        info->mImports.insert(variantImportPath);

                    success = AddImports(loader, PathLocation(variantImportPath), &variantValue, errors, info);
                }

                if (!success && errors)
                {
                    *errors += "  in ";
                    *errors += variantImportPath;
                    *errors += '\n';
                }

                if (success)
                {
                    if (value->IsNull())
                        value->Swap(variantValue);
                    else
                        value->Merge(variantValue);
                }
            }
        }

        if (!importExists && errors)
        {
            *errors += "Couldn't find ";
            *errors += importPath;
            *errors += '\n';
        }

        return success;
    }

    bool AddImports(FileLoader loader, const char* basePath, Value* value, String* errors, ConfigInfo* info)
    {
        bool success = true;

        ArrayValue* av = value->AsArrayPtr();

        if (av)
        {
            for (Value& childValue : *av)
                if (!AddImports(loader, basePath, &childValue, errors, info))
                    success = false;

            return success;
        }

        ObjectValue* ov = value->AsObjectPtr();

        if (!ov)
            return success;

        for (NameValue member : *ov)
            if (!AddImports(loader, basePath, &member.value, errors, info))
                success = false;

        const Value& importValues = value->Member("import");

        if (importValues.IsNull())
            return success;

        Value importValue;
        bool oneSuccess = false;

        if (importValues.IsArray())
        {
            success = true;

            for (const Value& importPathValue : importValues.AsArray())
            {
                Value loadedValue;

                if (LoadImport(importPathValue, loader, basePath, &loadedValue, errors, info))
                {
                    importValue.Merge(loadedValue);
                    oneSuccess = true;
                }
                else
                    // we still continue trying to load any other imports so we return a "best effort" result along with the failure code.
                    success = false;
            }
        }
        else
        {
            success = LoadImport(importValues, loader, basePath, &importValue, errors, info);
            oneSuccess = success;
        }

        if (oneSuccess)
        {
            value->RemoveMember("import");
            value->Swap(importValue);  // our result is the import, overridden with any local members
            value->Merge(importValue);
        }

        return success;
    }

    bool LoadConfigFileGeneral(const char* path, Value* value, String* errors, StringTable* st)
    {
        if (PathHasExtensions(path, kJsonExtensions))
        {
        #ifdef HL_LOCATION_H
            IReadRef stream = OpenDataLocation(path);
            if (stream)
                return LoadJsonFile(stream, value, errors, st);
        #endif

            return LoadJsonFile(path, value, errors, st);
        }

    #ifdef HL_VALUE_YAML_H
        if (PathHasExtensions(path, kYamlExtensions))
        {
        #ifdef HL_LOCATION_H
            IReadRef stream = OpenDataLocation(path);

            if (stream)
            {
                String text;
                if (!ReadText(stream, &text))
                {
                    if (errors) AppendFormat(errors, "Couldn't read from '%s'", path);
                    return false;
                }

                return LoadYamlText(text.c_str(), value, errors, st);
            }
        #endif

            return LoadYamlFile(path, value, errors);
        }
    #endif

        if (errors) *errors += Format("Unsupported file format: '%s'\n", path);
        return false;
    }

    bool LoadConfigInternal(FileLoader loader, const char* path, Value* config, String* errors, ConfigInfo* info)
    {
        bool success = loader(path, config, errors, info ? info->mStringTable : nullptr);

        if (success)
        {
            if (info)
            {
                info->mMain = PathNormalise(path);
                path = info->mMain.c_str();
                info->mImports.clear();
            }

            success = AddImports(loader, PathLocation(path), config, errors, info);
        }

        if (!ApplyTemplates(config, errors))
            success = false;

        if (!success && errors)
        {
            *errors += "  in ";
            *errors += path;
            *errors += '\n';
        }

        return success;
    }
}

bool HL::LoadConfig(const char* path, Value* config, String* errors, ConfigInfo* info)
{
    return ::LoadConfigInternal(LoadConfigFileGeneral, path, config, errors, info);
}

bool HL::LoadJsonConfig(const char* path, Value* config, String* errors, ConfigInfo* info)
{
    return ::LoadConfigInternal(LoadJsonFile, path, config, errors, info);
}

#ifdef HL_VALUE_YAML_H
bool HL::LoadYamlConfig(const char* path, Value* config, String* errors, ConfigInfo* info)
{
    return ::LoadConfigInternal(LoadYamlFile, path, config, errors, info);
}
#endif

namespace
{
    const JsonFormat kConfigJsonFormat =
    {
        4,        // indent
        false,    // quoteKeys
        74,       // arrayMargin
        6,        // maxPrecision
        true,     // trimZeroes
        kInfNanC  // InfNanType How to emit floating point specials
    };
}

bool HL::SaveConfig(const char* path, const Value& config, String* errors)
{
    if (PathHasExtensions(path, kJsonExtensions))
        return SaveAsJson(path, config, kConfigJsonFormat);
    if (PathHasExtensions(path, kYamlExtensions))
        return SaveAsYaml(path, config, kConfigJsonFormat.indent);

    if (errors)
        *errors += "Unrecognised config type\n";

    return false;
}

bool HL::SaveConfig(FILE* file, const Value& config, const char* type, String* errors)
{
    // TODO: use config info to extract shared string table?
    if (!type || EqualI(type, "json"))
        return SaveAsJson(file, config, kConfigJsonFormat);
    if (EqualI(type, "yaml"))
        return SaveAsYaml(file, config, kConfigJsonFormat.indent);

    if (errors)
        *errors += "Unrecognised config type\n";

    return false;
}

bool HL::ApplySettings(int numSettings, const char* const settings[], Value* config, String* errors)
{
    Value settingsValue;
    Strings members;

    for (int i = 0; i < numSettings; i++)
    {
        const char* assignChar = strchr(settings[i], '=');

        if (!assignChar)
            assignChar = strchr(settings[i], ':');

        const char* memberValueStr = 0;

        if (!assignChar)
            members = Split(settings[i], ".");
        else
        {
            memberValueStr = assignChar + 1;
            while (*memberValueStr == ' ')
                memberValueStr++;

            String name(settings[i], assignChar);
            members = Split(name, ".");
        }

        Value* v = config;

        for (const char* memberName : members)
            v = &v->UpdateMember(memberName);

        if (!memberValueStr)
        {
            *v = true;
            continue;
        }

        String quotedMemberValueStr;

        if ( strchr("[{-\"", memberValueStr[0]) == nullptr
                && !isdigit(memberValueStr[0])
                && !EqualI(memberValueStr, "null")
                && !EqualI(memberValueStr, "true")
                && !EqualI(memberValueStr, "false")
           )
        {
            quotedMemberValueStr += '"';
            quotedMemberValueStr += memberValueStr;
            quotedMemberValueStr += '"';
            memberValueStr = quotedMemberValueStr.c_str();
        }

        Value memberValue;
        if (*memberValueStr && !LoadJsonText(memberValueStr, &memberValue, errors))
            return false;

        *v = memberValue;
    }

    return true;
}
