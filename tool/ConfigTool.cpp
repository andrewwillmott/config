//
// ConfigTool.cpp
//
// Command-line utility for working with json config files
//
// Andrew Willmott
//

#define _CRT_SECURE_NO_WARNINGS

#include "ArgSpec.h"

#include "Config.hpp"
#include "Value.hpp"

// For writing...
#include "ValueJson.hpp"
#include "ValueYaml.hpp"

#ifndef HL_LOG_H
    #define HL_LOG_I(CONST_CHANNEL, FMT, ...) fprintf(stdout, FMT "\n", __VA_ARGS__)
    #define HL_LOG_E(CONST_CHANNEL, FMT, ...) fprintf(stderr, FMT "\n", __VA_ARGS__)
#endif

using namespace HL;
using namespace AS;

namespace
{
    bool ReadText(const char* path, String* text)
    {
        FILE* file = fopen(path, "r");

        if (!file)
            return false;

        size_t fileSize = 0;

        if (fseek(file, 0, SEEK_END) == 0)
            fileSize = ftell(file);

        if (fseek(file, 0, SEEK_SET) != 0)
            fileSize = 0;

        text->resize(fileSize);

        if (fread((char*) text->data(), 1, fileSize, file) != fileSize)
            text->clear();

        fclose(file);
        return !text->empty();
    }

    enum ResultCodes : int
    {
        kResultOK               =  0,
        kResultError            =  1,  // General error
        kResultArgError         = 64,  // EX_USAGE
        kResultIOError          = 74,  // EX_IOERR
        kResultConfigError      = 78,  // EX_CONFIG
    };

    bool DumpConfig(const Value& config, const char* query, bool membersOnly, bool yaml, const JsonFormat& format)
    {
        const Value* v = &config;

        if (query)
        {
            v = &MemberPath(config, query);

            if (v->IsNull())
            {
                fprintf(stderr, "%s not found\n", query);
                return false;
            }
        }

        if (membersOnly && v->IsObject())
            for (ConstNameValue nv : v->AsObject())
                printf("%s\n", nv.name);
        else
        {
            if (yaml)
                SaveAsYaml(stdout, *v, format.indent);
            else
                SaveAsJson(stdout, *v, format);
            fprintf(stdout, "\n");
        }

        return true;
    }
}

//
// Main
//

int main(int argc, const char* argv[])
{
    // InstallCrashHandler();

    enum Flags
    {
        kFlagVerbose,
        kFlagDebug,
        kFlagQuiet,
        kFlagMembersOnly,
        kFlagYaml,
        kFlagJsonStrict,
        kFlagShowDeps,
    };

    ConfigInfo configInfo;
    std::vector<const char*> inputPaths;
    const char* query = nullptr;
    std::vector<const char*> settings;
    JsonFormat format;

    cArgSpec spec;
    spec.ConstructSpec
    (
        "Tool for working with config files",
            "[<path:cstring> ...]", &inputPaths,
            "Read given config file(s) and dump corresponding data",
        "-query <object_path:cstring>", &query,
            "Show the value at the given path, e.g., people.bob.name",
        "-set <cstring> ...", &settings,
            "Additional settings to apply to the config after reading",
        "-names^", kFlagMembersOnly,
            "For an object, show only member names",
        "-indent <int>", &format.indent,
            "Set indent, default=2",
        "-margin <int>", &format.arrayMargin,
            "Set right margin for array wrapping purposes, or 0 to disable wrapping (each element on its own line)",
        "-precision <int>", &format.maxPrecision,
            "Set max precision for number output",
        "-quote_keys <bool>", &format.quoteKeys,
            "Set whether to quote keys",
        "-trim_zeroes <bool>", &format.trimZeroes,
            "Set whether to trim trailing zeroes from real numbers",
        "-strict^", kFlagJsonStrict,
            "Select output options for a strict json parser",
        "-deps^", kFlagShowDeps,
            "List input file dependencies",
        "-yaml^", kFlagYaml,
            "Output result as yaml rather than json",
    #ifdef HL_LOG_H
        "-v^", kFlagVerbose,
            "Verbose output",
        "-d^", kFlagDebug,
            "Debug output",
        "-q^", kFlagQuiet,
            "Quiet: only show warnings/errors",
    #endif
        nullptr
    );

    tArgError err = spec.Parse(argc, argv);

    if (err != kArgNoError)
    {
        printf("%s\n", spec.ErrorString());
        return kResultArgError;
    }

    if (argc == 1)
    {
        printf("%s\n", spec.HelpString(argv[0]));
        return kResultArgError;
    }

#ifdef HL_LOG_H
    if (spec.Flag(kFlagDebug))
        SetGlobalLogLevel(kLogDebug);
    else if (spec.Flag(kFlagVerbose))
        SetGlobalLogLevel(kLogVerbose);
    else if (spec.Flag(kFlagQuiet))
        SetGlobalLogLevel(kLogWarning);
    else
        SetGlobalLogLevel(kLogInfo);
#endif

    if (spec.Flag(kFlagJsonStrict))
        format = kJsonFormatStrict;

    String errors;
    int result = kResultOK;

    for (const char* inputPath : inputPaths)
    {
        Value config;

        if (size_i(inputPaths) > 1)
            HL_LOG_I(Console, "%s:\n", inputPath);

        String text;
        ReadText(inputPath, &text);

        if (LoadConfig(inputPath, &config, &errors, &configInfo))
        {
            if (spec.Flag(kFlagShowDeps))
            {
                HL_LOG_I(Console, "%s:", configInfo.mMain.c_str());

                for (const String& import : configInfo.mImports)
                    HL_LOG_I(Console, "     %s", import.c_str());
            }
            else
            {
                if (!ApplySettings(size_i(settings), settings.data(), &config, &errors))
                {
                    HL_LOG_E(Console, "Parse error in value: %s", errors.c_str());
                    result = kResultConfigError;
                }

                if (!DumpConfig(config, query, spec.Flag(kFlagMembersOnly), spec.Flag(kFlagYaml), format))
                    result = kResultIOError;
            }
        }
        else
            result = kResultArgError;
    }

    if (!errors.empty())
    {
        HL_LOG_E(Console, "%s", errors.c_str());
        errors.clear();
    }

    return result;
}
