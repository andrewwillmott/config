//
// Path.cpp
//
// Mini path library
//

#include "Path.hpp"

#if HL_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    #undef min
    #undef max
#else
    #include <fcntl.h>
    #include <dirent.h>
    #include <unistd.h>
    #include <sys/stat.h>
#endif

using namespace HL;

namespace
{
#if HL_WINDOWS
    const char kDirectorySeparator = '\\';

    inline bool IsSeparator(char c)
    {
        return c == '\\' || c == '/';
    }
    inline bool IsSeparatorOrEnd(char c)
    {
        return c == 0 || c == '\\' || c == '/';
    }
#else
    const char kDirectorySeparator = '/';

    inline bool IsSeparator(char c)
    {
        return c == '/';
    }
    inline bool IsSeparatorOrEnd(char c)
    {
        return c == 0 || c == '/';
    }
#endif
}

bool HL::PathFileExists(const char* path)
{
#if HL_WINDOWS
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0);
#else
    struct stat sb;
    if (stat(path, &sb))
        return false;
    return S_ISREG(sb.st_mode);
#endif
}

String HL::PathLocation(const char* path)
{
    const char* pos = strrchr(path, kDirectorySeparator);

    if (pos)
        return String(path, pos - path);
    else
        return String();
}


String HL::PathWithSuffix(const char* path, const char* suffix)
{
    const char* lastDot = strrchr(path, '.');

    if (lastDot)
    {
        String result(path, lastDot - path);
        result += suffix;
        result += lastDot;
        return result;
    }

    String result(path);
    result += suffix;
    return result;
}

bool HL::PathHasExtension(const char* path, const char* extension)
{
    const char* lastDot = strrchr(path, '.');
    if (lastDot)
        return HL::EqualI(lastDot, extension);

    return false;
}

bool HL::PathHasExtensions(const char* path, const char* const extensions[])
{
    int pl = strlen(path);
    bool hasExtension;

    while (extensions[0])
    {
        const char* extension = extensions[0];

        int el = strlen(extension);

        if (extension[0] == '.')
            hasExtension = ((pl >= el) && EqualI(path + pl - el, extension));
        else
            hasExtension = (pl > el) && path[pl - el - 1] == '.' && EqualI(path + pl - el, extension);

        if (hasExtension)
            return true;


        extensions++;
    }

    return false;
}

String HL::PathFull(const char* path, const char* basePath)
{
    if (!basePath || basePath[0] == 0 || (basePath[0] == '.' && basePath[1] == 0))
        // no base, early out
        return path;

    if (PathIsAbsolute(path))
        return path;

    // removed some code here to handle taking just the volume from the base path

    String result;
    result = basePath;
    result += kDirectorySeparator;
    result += path;

    return PathNormalise(result);
}

namespace
{
    // Examples:
    // ""
    // C:
    // file://
    // http://server/
    // \\server\mount  <- mount must be present
    // /mnt/remote/
    // /Volumes/Disk/

    int VolumeLength(const char* path)
    {
        size_t pos = strcspn(path, "/\\:");

        if (path[pos] == 0)
            return 0;

        if (pos > 0 && path[pos] == ':')    // drive or scheme
        {
            pos++;

            if (path[pos] == '/' && path[pos + 1] == '/')    // scheme://[authority]/
            {
                pos += 2;
                const char* nextSep = strchr(path + pos, '/');

                if (nextSep)
                    return int(nextSep - path);
            }

            return (int) pos;
        }

        if (pos != 0)
            return 0;

        // \\server\  ...
        if (path[0] == '\\' && path[1] == '\\')
        {
            const char* nextSep = strchr(path + 2, '\\');

            if (nextSep)
                return int(nextSep - path);
        }
        else if (strncmp(path, "/Volumes/", sizeof("/Volumes/") - 1) == 0)
        {
            pos = sizeof("/Volumes/") - 1;
            const char* nextSep = strchr(path + pos, '/');

            if (nextSep)
                return int(nextSep - path);
        }
        else if (strncmp(path, "/mnt/", sizeof("/mnt/") - 1) == 0)
        {
            pos = sizeof("/mnt/") - 1;
            const char* nextSep = strchr(path + pos, '/');

            if (nextSep)
                return int(nextSep - path);
        }

        return (int) pos;
    }
}

bool HL::PathIsAbsolute(const char* path)
{
    path += VolumeLength(path);
    return IsSeparator(path[0]);
}

String HL::PathNormalise(const char* path)
{
    if (path[0] == 0)
        return ".";

    int prefixLength = VolumeLength(path);
    String result(path, prefixLength);
    path += prefixLength;

    bool absolute = IsSeparator(path[0]);
    if (absolute)
    {
        result += kDirectorySeparator;
        path++;
        prefixLength++;
    }

    int lastStart = prefixLength;

    while (path[0])
    {
        if (IsSeparator(path[0]))
        {
            path++;
            continue;
        }

        if (path[0] == '.')
        {
            if (IsSeparatorOrEnd(path[1]))
            {
                path++;
                continue;
            }

            if (path[1] == '.' && IsSeparatorOrEnd(path[2]))
            {
                path += 2;

                if (!absolute && lastStart == size_i(result))    // Nothing to pop, add ..
                {
                    if (prefixLength < size_i(result))             // do we have anything already?
                    {
                        result += kDirectorySeparator;
                    }
                    result += "..";
                    lastStart = size_i(result);  // can't go back beyond this
                }
                else
                {
                    // pop off last directory including separator
                    while (lastStart < size_i(result))
                    {
                        bool separator = IsSeparator(result.back());
                        result.pop_back();

                        if (separator)
                        {
                            break;
                        }
                    }
                }
                continue;
            }
        }

        if (prefixLength < size_i(result))    // do we have anything already?
            result += kDirectorySeparator;

        while (!IsSeparatorOrEnd(*path))    // Copy to next separator
            result += *path++;
    }

    if (result.empty())
        result += '.';

    return result;
}
