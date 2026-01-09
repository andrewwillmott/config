// Tiny program to test minimal subset of Value.*, ValueJson.*, String.hpp: load JSON and dump it back out.

#include "Value.hpp"
#include "ValueJson.hpp"

int main(int argc, char** argv)
{
    HL::Value v;
    HL::String errors;

    if (argc <= 1)
    {
        fprintf(stderr, "Usage: %s <json-file>\n", argv[0]);
        return 1;
    }

    if (LoadJsonFile(argv[1], &v, &errors))
    {
        SaveAsJson(stdout, v);

        return 0;
    }
    else
    {
        fprintf(stderr, "Errors loading JSON:\n%s\n", errors.c_str());
        return 1;
    }
}
