#include <stdio.h>
#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
#include "api.h"

EnterApplicationNamespace


extern "C" int main(int argc, char** argv)
{
    /*add_log_handler([](String severity, String subsystem, String msg)
    {
        printf("%.*s\n", StringArgs(msg));
    });*/

    Dynamic_Array<String> non_flag_args = {};
    for (umm i = 1; i < argc; i++)
        if (argv[i][0] != '-')
            *reserve_item(&non_flag_args) = wrap_string(argv[i]);

    if (non_flag_args.count != 1 && non_flag_args.count != 2)
    {
        fprintf(stderr, "Usage: %s file.fun [argument_list]\n", argv[0]);
        return 1;
    }

    Compiler compiler = {};
    Compiler* ctx = &compiler;


    Environment* env = make_environment(ctx, NULL);

    assert(pump_pipeline(ctx));  // force preload to complete

    Block* main;
    if (non_flag_args.count == 2)
        main = parse_top_level_from_memory(ctx, "<string>"_s, concatenate(temp, "debug "_s, non_flag_args[1], ";"_s));
    else
        main = parse_top_level_from_file(ctx, non_flag_args[0]);
    if (!main) return 1;

    materialize_unit(env, main);

    if (!pump_pipeline(ctx)) return 1;
    return 0;
}


ExitApplicationNamespace
