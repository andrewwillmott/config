//
// Config.hpp
//
// Utilities for managing Value-based configurations, e.g., preferences,
// game system tuning, model definitions...
//
// Andrew Willmott
//

#ifndef HL_CONFIG_H
#define HL_CONFIG_H

#include "Value.hpp"
#include "vector_set.hpp"

namespace HL
{
    typedef Value Config;
    struct StringTable;

    // Loading

    struct ConfigInfo
    {
        String mVariant;              // if non-empty, additionally look for variant files with given suffix to import

        String mMain;                 // filled in with path to root config file
        vector_set<String> mImports;  // all other imported config files

        StringTable* mStringTable = 0;  // Optional shared string table used during loading
    };

    bool LoadConfig(const char* path, Value* config, String* errors = nullptr, ConfigInfo* info = nullptr);
    // loads the given file as a config, with support for the 'import' directive. Returns false on error, and optionally
    // fills in 'errors' and 'deps'. Currently supports .json or .yaml files.

    bool LoadJsonConfig(const char* path, Value* config, String* errors = nullptr, ConfigInfo* info = nullptr);
    // json-specific version of LoadConfig()

    bool LoadYamlConfig(const char* path, Value* config, String* errors = nullptr, ConfigInfo* info = nullptr);
    // yaml-specific version of LoadConfig()

    bool SaveConfig(const char* path, const Value& config, String* errors = nullptr);  // Save config according to extension
    bool SaveConfig(FILE*       file, const Value& config, const char* type = 0, String* errors = nullptr);  // Save config with optional type "json"/"yaml"

    // Utilities
    bool ApplySettings(int numSettings, const char* const settings[], Value* config, String* errors = 0);
    // Applies the given settings to the given config. Each setting is of the form "<member>=<value>", where member can
    // be a simple key, or a full path expression (e.g., "a.b.c[2].d"). The <value> is parsed as json, and can be
    // omitted, in which case the member's value is set to 'true'.
    // Returns false if there was an error parsing one of the values, with 'errors' set correspondingly.

    template<class C, class U = typename C::value_type>
    bool ApplySettings(const C& c, Value* config, String* errors = 0);  // variant for a container of const char*


    //
    // Inlines
    //

    template<class C, class U> bool ApplySettings(const C& c, Value* config, String* errors)
    {
        return ApplySettings(size_i(c), c.data(), config, errors);
    }
}

#endif
