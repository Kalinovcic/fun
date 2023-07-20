#include <stdio.h>
#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
#include "api.h"
#include "testing.h"


EnterApplicationNamespace


extern "C" int main(int argc, char** argv)
{
    /*add_log_handler([](String severity, String subsystem, String msg)
    {
        printf("%.*s\n", StringArgs(msg));
    });*/

    String first_flag_arg = {};
    if (argc > 1 && argv[1][0] == '-')
        first_flag_arg = consume(wrap_string(argv[1]), 1);

    if (first_flag_arg == "test_runner"_s)
        return test_runner_entry();

    if (first_flag_arg == "test"_s)
    {
        add_log_handler([](String severity, String subsystem, String msg)
        {
            printf("%.*s\n", StringArgs(msg));
        });

        Testing_Context context = {};
        Defer(free_test_suite(&context));
        if (!initialize_test_suite(&context, "."_s)) return 1;
        run_tests(&context, argv[0]);

        return 0;
    }

    if (first_flag_arg == "stress_test"_s)
    {
        Testing_Context context = {};
        Defer(free_test_suite(&context));
        if (!initialize_test_suite(&context, "."_s)) return 1;

        umm iteration = 0;
        while (true)
        {
            if (!run_tests(&context, argv[0], /* only log fails */ true))
            {
                printf("\n\nSome tests started failing after %lu iterations.\n", iteration);
                break;
            }

            iteration++;
            if (iteration % 1000 == 0)
                printf("Ran all tests %lu times.\n", iteration);
        }

        return 0;
    }

    if (first_flag_arg == "run"_s)
    {
        String_Concatenator code_cat = {};
        add(&code_cat, "run unit { debug\n"_s);
        for (umm i = 2; i < argc; i++)
        {
            add(&code_cat, " "_s);
            add(&code_cat, wrap_string(argv[i]));
        }
        add(&code_cat, ";\n}"_s);
        String code = resolve_to_string_and_free(&code_cat, temp);

        Compiler compiler = {};
        Environment* env = make_environment(&compiler, NULL);

        assert(pump_pipeline(&compiler));  // force preload to complete

        Block* main = parse_top_level_from_memory(&compiler, "<string>"_s, code);
        if (!main) return 1;

        materialize_unit(env, main);

        if (!pump_pipeline(&compiler)) return 1;
        return 0;
    }

    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s file.fun [argument_list]\n", argv[0]);
        return 1;
    }

    Compiler compiler = {};
    Environment* env = make_environment(&compiler, NULL);

    assert(pump_pipeline(&compiler));  // force preload to complete

    Block* main = parse_top_level_from_file(&compiler, wrap_string(argv[1]));
    if (!main) return 1;

    materialize_unit(env, main);

    if (!pump_pipeline(&compiler)) return 1;
    return 0;
}


ExitApplicationNamespace
