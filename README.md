# Config Library

This directory contains code I often use to build configuration systems. The
basic idea is that you set up a hierarchy of configuration files, which are read
on app startup, and the corresponding settings are then used to control the app,
gameplay entity setup, rendering, materials, effects, platform scaling options,
and so on. Some variant of it has shipped in almost every game or app I've ever
worked on, though it's evolved a fair bit over the years.

You can also use it as a quick way to read/modify/write json files, or even just
for the core `Value` class.

Its main plus is that, unlike some similar libraries, it has a relatively small
header footprint (1000 lines for the core), and the source is simple and easy to
customise.

The library requires C++17 to build if StringTable is used, due to the internal
external/ankerl::unordered_dense dependency (*), but you can build against its
headers with C++11. To keep dependencies to a minimum it does use `<string>` and
`<vector>`, but the former can be replaced via a custom String definition, and
the latter (other than for external interoperability for ease of use) is limited
to a couple of internal locations if you need to replace it.


## Quick Start

Just run 'make', and then

    ./config_tool                             # see usage
    ./config_tool examples/renderer.json      # emit unified config from renderer.json and included/templated files
    ./config_tool examples/models.json -yaml  # convert models.json to yaml

    ./config_tool examples/renderer.json -query materials.default.albedo  # query specific value

To install the prebuilt library and config_tool on Mac/Unix systems:

    PREFIX=/usr/local make install

Or, add the subset of files you need directly to your Visual Studio or Xcode
project. You may want to replace the definitions and calls in String.hpp and
Path.hpp with your own equivalents.


## Overview

The code comprises:

- The Value class, defined in [Value.hpp](Value.hpp), which supports the classic
  null/bool/int/float/string/array/object variant type. ('Object' is sometimes
  referred to as a 'map' or a 'dict' in other systems.)

- Support for reading and writing [json](https://www.json.org) to/from a `Value`
  via [ValueJson.hpp](ValueJson.hpp). This supports both strict and relaxed,
  json5-style dialects. Namely, allowing comments (important in this use case),
  trailing commas, unquoted keys, and so on.

- Support for reading and writing yaml to/from `Value` via
  [ValueYaml.hpp](ValueYaml.hpp). This uses
  [libyaml](https://github.com/yaml/libyaml) for parsing (via the single-file
  cut-down version in `external/`), so that it can support anchors, aliases, and
  merging. This is because in the situations where I've needed yaml, it's been
  for interoperation with other systems, where you can't rely on people sticking
  to the small sane subset of yaml. (A simpler parser is possible that supports
  just that vanilla syntax, but the spec is such a mess that in my opinion
  you're better off using toml or a json variant at that point.)

- An optional `StringTable` class, which can be used to ensure string keys and
  optionally string values are shared. For large configurations with a lot of
  repeated keys, this can save significant memory. If this is not needed, e.g.,
  because any such string sharing is not worth the small hit on read, or you
  want to reduce code complexity, you can just comment out the corresponding
  includes.

- A config system built on top of all this in [Config.hpp](Config.hpp), which
  handles freely mixing json and yaml files, an `include` directive to bring in
  other files, and a `template` directive, which handles object-level merging. A
  common pattern is to have a base configuration file that is then included by a
  more specific file that overrides just the settings it needs, or to define a
  base config, and use 'template' to reference it and then apply further
  settings on top of it.

- The command-line tool `config_tool`, which can be used to read, query, and
  write configuration files. This can also be used to translate json to yaml
  or vice-versa, or to reformat files.

The files are are provided loose so you can include just what you need. For
example, a minimal subset that just supports json would be

    Value.* ValueJson.* String.hpp vector*.hpp

and the full system supporting both json and yaml and file hierarchies comprises

    Value* Config* String* Path* vector*.hpp external/*


## Value class

One of the design goals of this class is to avoid a lot of error checking when
querying it, hence it takes a 'fail gracefully' approach. For instance, if a key
is not present in an object Value, a null value is returned, and null values
when used with any of the `AsXXX()` methods return a default value (which can be
specified in the call). You can still check explicitly for types if you wish to
detect specification errors in the input, but you can rely on an ill-formed
config file or other Value setup not to cause an exception or crash.

Examples:

    value.AsBool();     // Returns true/false, supports bool, numeric (!= 0), string ("true"/"false")

    value.AsInt();      // Returns integer value if interpretable as an integer, otherwise 0
    value.AsInt(606);   // Returns integer value if interpretable as an integer, otherwise default value 606

    value.AsFloat(-1);  // Returns float value, supports bool (0/1) and all integer/float types.

    value.AsString();            // Returns string or "" if not a string
    value.AsString("default");   // Returns string or "default" if not present/not a string
    value.AsCString();           // Returns string or nullptr if not a string

    const Value& v = value["key"];  // Corresponding value or null (kNullValue) if not present
    const Value& v = value["key1"]["key2"]["key3"];  // null if any of the keys can't be found.

    Value& v = value("key");        // () Returns a writable reference, bumping the modification count.
    value("key") = 88;              // Round brackets are used to avoid accidental key insertion with square brackets.
    value.SetMember("key", 88);     // Explicit variant.

    for (const Value& av : value.AsArray())  // idiomatic way to iterate over array, does nothing if not an array
        Process(av);

    for (ConstNameValue nv : value.AsObject())  // idiomatic way to iterate over object, does nothing if not an object
        SetIntProp(nv.name, nv.value.AsInt());

### Notes:

- Value supports a modification count (ModCount()), which can be useful for
  change detection in hot-load scenarios.

- Some effort is made to support STL-style access, i.e., operator [] will
  do the right thing for arrays and objects, and size()/empty() work as usual.
  Arrays and objects also support range-based for. However, once difference
  is that with objects, [] can only be used for read access. Use () instead
  to get a non-const reference to the underlying value. This is to avoid the
  common bug where [] is intended just to read from an object value, but
  is used in a non-const context, and a query on a key that doesn't exist
  winds up auto-inserting a null value under that key.

- There are explicit versions of the STL-alike operators and methods for
  clarity, e.g., `Member()/UpdateMember()/Elt()/NumElts()`. Because '0' can be
  interpreted either as a string key or an array index, it can be better to
  explicitly use `Elt()` or `Member()` in this case.

- Although HasMember() is provided, it's better to use []/Member() and then
  check IsNull() if you're going to access the value, as it avoids a double
  lookup.

- Strings and arrays can be shared, and are by default when copying values. They
  are assumed to be read-only once created, and hence any update requires
  replacing the previous array value or string rather then modifying in place.

- Objects _can_ be shared, but are copied by default, as they are mutable.

- Numeric conversions are clamped, e.g., a float outside the range of a 32-bit
  signed or unsigned value will be clamped to [U]INT_MIN/MAX. Ditto 64-bit
  integer values.

- Integer to float conversions involve potential precision loss. You should
  use AsDouble() if you want 32-bit integers to be perfectly represented,
  or otherwise specifically query Type() and handle integers directly.

- Object merging is supported via `Merge()`, which copies the contents of the
  source argument into 'this', overwriting any keys that already exist. It
  merges recursively so object hierarchies are supported.

- The system is capable of additionally storing comments if `HL_VALUE_COMMENTS`
  is defined, though currently this is only used by the json reader/writer. This
  can be useful if you want to preserve comments when reading and writing files,
  or potentially to support annotation directives.


## Config System

The main API is in [Config.hpp](Config.hpp), and the main entry point is:

    bool LoadConfig(const char* path, Value* config, String* errors = nullptr, ConfigInfo* info = nullptr);

If an error occurs, `LoadConfig` will return false, and if supplied `errors`
will have the error and its location appended. Otherwise `config` will hold
the resulting unified configuration, and can be accessed as above, e.g.,

    const char* appName = config["app"]["name"].AsCString();
    bool wireframe = config["renderer"]["wireframe"].AsBool(false);
    float hue = config["ui"]["hue"].AsFloat(0.0f);
    // ...

If a `ConfigInfo` argument is
supplied, it will be filled with all the files referenced, which can be useful
for change detection while an app is running. You can also use `ConfigInfo` to
pass a master `StringTable`, for instance, to share strings across multiple
config files and/or loads. (Without this, by default strings will be shared only
within files.)

### Import and Template

There is an example config setup for a simple renderer in `examples` which
contains both `import` and `template` directives. Importing is simple enough,
just add to the top level of your file:

    import: "file1"

or

    import: ["file1", "file2", ...]

The referenced files will be imported in order, with each successive file (and
then the main file itself) overriding any common settings from the previous one.

The 'template' directive on the other hand can be used in any object, and
imports the settings of the referenced object from the same level, before any
additional settings are applied over the top. A simple example is:

    models: {
        bunny: {
            mesh: "models/test/bunny.gltf",
            coord_sys: "LUF",
            // ...
        },

        red_bunny: {
            template: "bunny",
            colour: "red"
        },

        // ...
    }

The corresponding expanded result seen on the code side is:

    models: {
        bunny: {
            coord_sys: "LUF",
            mesh: "models/test/bunny.gltf"
        },
        red_bunny: {
            colour: "red",
            coord_sys: "LUF",
            mesh: "models/test/bunny.gltf"
        }
    }

### Command-line Settings

It's common to want to allow command-line customisation of settings, and for
this purpose there is the `ApplySettings` helper:

    bool ApplySettings(int numSettings, const char* const settings[], Value* config, String* errors);

If you pass this an array of string settings, say from argc/argv, it will
interpret them as either `member_path` (which will be set to 'true') or
`member_path=<value>`, where `value` can either be a simple string or number, or
even a full json specification, though that can require careful quoting. The
resulting settings are then applied to `config`.

The config_tool has an example of this via its `-set` option. For instance:

    config_tool my_config.json -set ui.hue=320 renderer.wireframe camera.position=[1,2,3]



## Footnotes
* At some point I will remove the C++17 dependency, as it's mostly for type
  trait stuff that isn't needed in this specific use case.
