//
// Path.hpp
//
// Mini path library
//

#include "String.hpp"

namespace HL
{
    String PathLocation(const char* path);
    String PathWithSuffix(const char* path, const char* suffix);
    bool   PathHasExtension(const char* path, const char* extension);
    bool   PathHasExtensions(const char* path, const char* const extensions[]);

    bool   PathIsAbsolute(const char* path);
    String PathFull(const char* path, const char* basePath);
    String PathNormalise(const char* path);

    bool PathFileExists(const char* path);  // Returns true if path exists and is a file
}
