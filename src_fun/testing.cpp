#include <stdio.h>

#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
#include "testing.h"
#include "api.h"


EnterApplicationNamespace


static const String subsystem = "testing"_s;

static Random the_rng;
GlobalBlock
{
    seed(&the_rng);
};


static void debug_print_test_case(Test_Case* test)
{
    String_Concatenator padded_code = {};
    for (u64 loc = 1; loc < test->first_line_of_code; loc++)
        add(&padded_code, "\n"_s);
    add(&padded_code, test->code);
    String c = resolve_to_string_and_free(&padded_code, temp);

    Debug(R"(
id:                    %
path:                  %
must_error_to_succeed: %
error_wildcard:        %
description:
%
first_loc: %
code (unpadded):
%
code (padded):
)", test->id, test->path, test->must_error_to_succeed, test->error_wildcard, test->description, test->first_line_of_code, test->code);
}

static String serialize_test(Region* memory, Test_Case* test)
{
    String_Concatenator cat = {};
    add_string(&cat, test->id);
    add_string(&cat, test->path);
    add_string(&cat, test->description);
    add_string(&cat, test->code);
    add_u64   (&cat, test->first_line_of_code);
    add_u64   (&cat, test->must_error_to_succeed != 0);
    add_string(&cat, test->error_wildcard);
    add_u64   (&cat, test->rng_seed);
    add_string(&cat, print_guid(test->serialized_file_guid));
    return resolve_to_string_and_free(&cat, memory);
}

static Test_Case deserialize_test(String contents)
{
    Test_Case t = {};
    t.id                    = read_string(&contents);
    t.path                  = read_string(&contents);
    t.description           = read_string(&contents);
    t.code                  = read_string(&contents);
    t.first_line_of_code    = read_u64   (&contents);
    t.must_error_to_succeed = read_u64   (&contents) != 0;
    t.error_wildcard        = read_string(&contents);
    t.rng_seed              = read_u64   (&contents);
    t.serialized_file_guid  = parse_guid(read_string(&contents));
    return t;
}

bool parse_test_file(Testing_Context* context, String relative_path)
{
    String full_path = concatenate_path(temp, context->root_directory, relative_path);
    String contents  = {};
    if (!read_entire_file(full_path, &contents, temp)) return false;

    String line = {};
    String line_preserved_whitespace = {};
    u64    current_line = 0;

    auto consume_test_line = [&]()
    {
        line_preserved_whitespace = consume_line_preserve_whitespace(&contents);
        line = trim(line_preserved_whitespace);
        current_line++;

        if (prefix_equals(line, "//#"_s))
        {
            line = trim(consume(line, 3));
            return true;
        }

        return false;
    };

    auto peek_test_line = [&]()
    {
        String peek_contents = contents;
        String peek_line = consume_line(&peek_contents);
        return prefix_equals(peek_line, "//#"_s);
    };

#define Error(fmt, ...) do {                                    \
    LogError(subsystem, "Bad test in '%', line %: "   \
             fmt, relative_path, current_line, ##__VA_ARGS__);  \
    return false;                                               \
} while (0)

    umm tests_parsed = 0;
    while (contents)
    {
        if (!consume_test_line()) continue;

        Test_Case test = {};
        test.path = allocate_string(&context->memory, relative_path);
        test.id   = allocate_string(&context->memory, line);
        test.rng_seed = U64_MAX;
        if (!test.id) Error("Expected the test's ID.");
        for (umm i = 0; i < test.id.length; i++)
        {
            char c = test.id[i];
            if (!IsAlphaNumeric(c) && c != '-' && c != '_' && c != '.' && c != '!' && c != '?' && c != '$')
                Error("Bad test ID '%'; can only contain alphanumeric characters and -_.!?$", test.id);
        }

        bool condition_defined           = false;
        bool seed_defined                = false;
        bool started_parsing_description = false;

        String_Concatenator desc_cat = {}; // meow
        while (consume_test_line())
        {
            if (!started_parsing_description)
            {
                if (!line) continue;

                if (!condition_defined)
                {
                    Defer(condition_defined = true);

                    if (prefix_equals(line, "ERROR WITH"_s))
                    {
                        consume_until(&line, "ERROR WITH"_s);
                        test.must_error_to_succeed = true;
                        test.error_wildcard        = allocate_string(&context->memory, trim(line));
                        if (!test.error_wildcard)
                            Error("Expected a wildcard pattern after 'ERROR WITH'. "
                                  "If you don't want a pattern, use just 'ERROR' instead");
                        continue;
                    }
                    else if (prefix_equals(line, "ERROR"_s))
                    {
                        if (line != "ERROR"_s)
                            Error("Badly defined test condition, must be either 'SUCCESS', 'ERROR', or 'ERROR WITH <pattern>.'");

                        test.must_error_to_succeed = true;
                        test.error_wildcard        = "*"_s;
                        continue;
                    }
                    else if (prefix_equals(line, "SUCCESS"_s))
                    {
                        if (line != "SUCCESS"_s)
                            Error("Badly defined test condition, must be either 'SUCCESS', 'ERROR', or 'ERROR WITH <pattern>.'");

                        test.must_error_to_succeed = false;
                        continue;
                    }
                }

                if (!seed_defined)
                {
                    Defer(seed_defined = true);

                    if (prefix_equals(line, "SEED"_s))
                    {
                        consume_until(&line, "SEED"_s);
                        test.rng_seed = consume_u64(&line);
                        if (line)                     Error("Badly defined test seed, seed is defined as SEED <integer>.");
                        if (test.rng_seed == U64_MAX) Error("Badly defined test seed, seed must be < U64_MAX.");

                        continue;
                    }
                    else test.rng_seed = U64_MAX;
                }
            }

            started_parsing_description = true;
            add(&desc_cat, line);
            add(&desc_cat, "\n"_s);
        }

        test.description        = resolve_to_string_and_free(&desc_cat, &context->memory);
        test.first_line_of_code = current_line;

        // parse source until we get to a //# line
        String_Concatenator code_cat = {};
        while (true)
        {
            add(&code_cat, line_preserved_whitespace);
            add(&code_cat, "\n"_s);
            if (!contents || peek_test_line()) break;
            consume_test_line();
        }

        test.code = resolve_to_string_and_free(&code_cat, &context->memory);

        if (trim(test.code).length == 0)
            Error("No code for test ID '%',", test.id);

        add_item(&context->tests, &test);
        tests_parsed++;
    }

    if (tests_parsed == 0)
        Error("No tests in file!");

    return true;

#undef Error
}

static bool run_code_of_test(Test_Case* test)
{
    String_Concatenator code_cat = {};
    add(&code_cat, "test_assert :: (condition: bool) {} intrinsic \"test_assert\";");

    for (umm i = 1; i < test->first_line_of_code; i++)
        add(&code_cat, "\n"_s);
    add(&code_cat, test->code);

    String code = resolve_to_string_and_free(&code_cat, temp);

    Compiler compiler = {};
    Environment* env = make_environment(&compiler, NULL);
    assert(pump_pipeline(&compiler));  // force preload to complete

    Block* main = parse_top_level_from_memory(&compiler, "<string>"_s, code);
    if (!main) return false;
    materialize_unit(env, main);

    return pump_pipeline(&compiler);
}


int test_runner_entry()
{
    assert(get_command_line_bool("test_runner"_s));
    assert(get_command_line_integer("seed"_s) != 0);

    String bin_path = get_command_line_string("test_path"_s);
    assert(bin_path);

    String bin_test = {};
    assert(read_entire_file(bin_path, &bin_test, temp));

    Test_Case test = deserialize_test(bin_test);
    bool ok = run_code_of_test(&test);

    return ok ? 0 : 1;
}


static String get_testing_temp_dir()
{
    return concatenate_path(temp, get_executable_directory(), "test_runner_temp"_s);
}

static String get_test_bin_file_path(Test_Case* test)
{
    return concatenate_path(temp,
        get_testing_temp_dir(),
        Format(temp, "%.bin-test", print_guid(test->serialized_file_guid)));
}

static String get_test_stdout_path(Test_Case* test)
{
    return concatenate_path(temp,
        get_testing_temp_dir(),
        Format(temp, "%.stdout", print_guid(test->serialized_file_guid)));
}

static String get_test_stderr_path(Test_Case* test)
{
    return concatenate_path(temp,
        get_testing_temp_dir(),
        Format(temp, "%.stderr", print_guid(test->serialized_file_guid)));
}


bool initialize_test_suite(Testing_Context* context, String directory)
{
    context->root_directory = allocate_string(&context->memory, directory);

    String temp_dir = get_testing_temp_dir();

    if (check_if_directory_exists(temp_dir))
        assert(delete_directory_with_contents(temp_dir));

    assert(create_directory(temp_dir));

    For (list_files(context->root_directory, "test.fun"_s))
        if (!parse_test_file(context, *it)) return false;

    For (context->tests)
    {
        it->serialized_file_guid = generate_guid();
        assert(write_entire_file(get_test_bin_file_path(it), serialize_test(temp, it)));
    }

    return true;
}

void free_test_suite(Testing_Context* context)
{
    free_heap_array(&context->tests);
    lk_region_free(&context->memory);
}


template<typename T, typename L>
static void batch_for(Array<T> array, umm batch_size, L&& it)
{
    for (umm batch_start = 0; batch_start < array.count; batch_start += batch_size)
    {
        umm one_after_last = batch_start + batch_size;
        if (one_after_last > array.count)
            one_after_last = array.count;
        Array<T> batch = { one_after_last - batch_start, array.address + batch_start };
        it(batch);
    }
}

bool run_tests(Testing_Context* context, char* argv0, bool only_log_fails)
{

#define Print(fmt, ...) do {                       \
    String txt = Format(temp, fmt, ##__VA_ARGS__); \
    printf("%.*s", StringArgs(txt));               \
} while (0)

#define PassedLiteral "\x1b[32;1mPASSED\x1b[m"
#define FailedLiteral "\x1b[31;1mFAILED\x1b[m"


    assert(delete_directory_conditional(
        get_testing_temp_dir(), /* delete_directory */ false,
        [](String parent, String name, bool is_file, void* userdata)
        {
            return (!is_file || get_file_extension(name) != "bin-test"_s) ? DELETE_FILE_OR_DIRECTORY : DO_NOT_DELETE;
        }, NULL));


    if (!only_log_fails)
        Print("Running tests...\n");

    umm digits = digits_base10_u64(context->tests.count);
    umm passed_count = 0;
    umm test_index = 0;

    batch_for(context->tests, get_hardware_parallelism(), [&](Array<Test_Case> batch)
    {
        For (batch)
        {
            it->process.path              = get_executable_path();
            it->process.file_stdout       = get_test_stdout_path(it);
            it->process.file_stderr       = get_test_stderr_path(it);
            it->process.current_directory = get_testing_temp_dir();
            it->process.detached                = false;
            it->process.prohibit_console_window = true;

            if (it->rng_seed == U64_MAX)
                it->rng_seed = next_u64(&the_rng, 1, U64_MAX);

            Dynamic_Array<String> args = {};
            Defer(free_heap_array(&args));
            *reserve_item(&args) = "-test_runner"_s;
            *reserve_item(&args) = Format(temp, "-test_path:%", get_test_bin_file_path(it));
            *reserve_item(&args) = Format(temp, "-seed:%",      it->rng_seed);
            it->process.arguments = allocate_array(temp, &args);

            assert(run_process(&it->process));
        }

        For (batch)
        {
            test_index++;
            if (!only_log_fails)
            {
                String path_copy = it->path;
                String test_name = Format(temp, "%/%", consume_until(&path_copy, ".test.fun"_s), it->id);

                Print("Running test (% / %) %",
                      u64_format(test_index, digits),
                      u64_format(context->tests.count, digits),
                      test_name);

                s64 pad = 48 - test_name.length;
                for (s64 i = 0; i < pad; i++) Print(".");
            }

            u32 exit_code = 1;
            bool timed_out = !wait_for_process(&it->process, 10.0, &exit_code); // wait for up to 10s

            String fail_explanation = {};
            if (timed_out)
            {
                terminate_process_without_waiting(&it->process);
                fail_explanation = "Test timed out after 10s."_s;
            }
            else
            {
                if (!it->must_error_to_succeed)
                {
                    it->passed = (exit_code == 0);
                }
                else
                {
                    if (exit_code == 0)
                    {
                        it->passed       = false;
                        fail_explanation = Format(temp, "The test must error to succeed, but it didn't.");
                    }
                    else
                    {
                        String stderr = {};
                        assert(read_entire_file(it->process.file_stderr, &stderr, temp));

                        it->passed = match_wildcard_string(Format(temp, "*%*", it->error_wildcard), stderr);
                        if (!it->passed)
                            fail_explanation = Format(temp,
                                "Errored (as it should), but the error does not match the wildcard '%':\n\n%",
                                it->error_wildcard, stderr
                            );
                    }
                }
            }

            if (it->passed)
            {
                if (!only_log_fails)
                    Print(PassedLiteral "\n");
                passed_count++;
            }
            else
            {
                Print(FailedLiteral "\n");
                if (fail_explanation)
                    Print("    %\n", fail_explanation);
            }
        }
    });

    if (!only_log_fails)
        Print("\nDone! % / % tests passed.\n",
              u64_format(passed_count, digits), u64_format(context->tests.count, digits));

    bool first_rerun = true;
    For (context->tests)
    {
        if (it->passed) continue;

        if (first_rerun)
        {
            first_rerun = false;
            Print("\n");
        }

        Print("cmd to run '%':\n", it->id);
        Print("    % -test_runner -test_path:% -seed:%\n",
              argv0, get_test_bin_file_path(it), it->rng_seed);
    }

#undef Print
#undef PassedLiteral
#undef FailedLiteral

    return (passed_count == context->tests.count);
}

ExitApplicationNamespace
