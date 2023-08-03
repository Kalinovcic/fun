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

    String first_arg_if_is_flag = {};
    if (argc > 1 && argv[1][0] == '-')
    {
        first_arg_if_is_flag = consume(wrap_string(argv[1]), 1);
        first_arg_if_is_flag = consume_until(&first_arg_if_is_flag, ":"_s);
    }

    Dynamic_Array<String> non_flag_args = {};
    Defer(free_heap_array(&non_flag_args));
    for (umm i = 1; i < argc; i++)
        if (argv[i][0] != '-')
            *reserve_item(&non_flag_args) = wrap_string(argv[i]);


    if (first_arg_if_is_flag == "test_process"_s)
        return test_runner_entry();

    if (first_arg_if_is_flag == "test"_s)
    {
        add_log_handler([](String severity, String subsystem, String msg)
        {
            printf("%.*s\n", StringArgs(msg));
        });

        String directory = concatenate_path(temp, get_current_working_directory(), "test"_s);

        Dynamic_Array<String> tests_to_run_wildcards = {};
        Defer(free_heap_array(&tests_to_run_wildcards));

        if (non_flag_args.count == 1 && suffix_equals(non_flag_args[0], ".test.fun"_s))
        {
            if (!check_if_file_exists(non_flag_args[0]))
            {
                printf("Test file '%.*s' doesn't exist.", StringArgs(non_flag_args[0]));
                return 1;
            }

            directory = get_parent_directory_path(non_flag_args[0]);
            String fn = get_file_name(non_flag_args[0]);
            *reserve_item(&tests_to_run_wildcards) = Format(temp, "%*", consume_until(&fn, ".test.fun"_s));
        }
        else

        For (non_flag_args)
            *reserve_item(&tests_to_run_wildcards) = Format(temp, "*%*", *it);

        // If no wildcards have been provided match all tests.
        if (tests_to_run_wildcards.count == 0)
            *reserve_item(&tests_to_run_wildcards) = "*"_s;

        Testing_Context context = {};
        Defer(free_test_suite(&context));
        if (!initialize_test_suite(&context, directory, &tests_to_run_wildcards)) return 1;

        if (!get_command_line_bool("stress"_s))
        {
            if (!get_command_line_bool("inline"_s))
            {
                run_tests(&context, argv[0],
                    /* only log fails */    false,
                    /* show_explanations */ !get_command_line_bool("silent"_s)
                );
            }
            else
            {
                For (context.tests)
                {
                    bool ok = run_code_of_test(it);
                    if (!ok)
                        LogWarn("test"_s, "*** pump_environment returned false ***");
                }
            }
        }
        else
        {
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
        }

        return 0;
    }

    if (first_arg_if_is_flag == "run"_s)
    {
        String_Concatenator code_cat = {};
        add(&code_cat, "run unit { debug(\n"_s);
        for (umm i = 2; i < argc; i++)
        {
            if (i > 2) add(&code_cat, " "_s);
            add(&code_cat, wrap_string(argv[i]));
        }
        add(&code_cat, ");\n}"_s);
        String code = resolve_to_string_and_free(&code_cat, temp);

        Compiler compiler = {};
        add_default_import_path_patterns(&compiler);
        Environment* env = make_environment(&compiler, NULL);

        assert(pump_pipeline(&compiler));  // force preload to complete

        Block* main = parse_top_level_from_memory(&compiler, get_current_working_directory(), "<string>"_s, code);
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
    add_default_import_path_patterns(&compiler);
    Environment* env = make_environment(&compiler, NULL);

    assert(pump_pipeline(&compiler));  // force preload to complete

    String path_to_file = wrap_string(argv[1]);
    if (is_path_relative(path_to_file))
    {
        String cwd = get_current_working_directory();
        path_to_file = concatenate_path(temp, cwd, path_to_file);
    }

    if (!is_path_absolute(path_to_file))
    {
        fprintf(stderr, "Error: Can't resolve the path of '%s'\n", argv[1]);
        return 1;
    }

    Block* main = parse_top_level_from_file(&compiler, path_to_file);
    if (!main) return 1;

    materialize_unit(env, main);

    if (!pump_pipeline(&compiler)) return 1;
    return 0;
}


ExitApplicationNamespace
