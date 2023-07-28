#include <stdio.h>

#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
#include "testing.h"
#include "api.h"

#if defined(OS_LINUX)
#include <sys/ioctl.h>
#include <unistd.h>
#endif


EnterApplicationNamespace


#define Print(fmt, ...) do {                       \
    String txt = Format(temp, fmt, ##__VA_ARGS__); \
    printf("%.*s", StringArgs(txt));               \
} while (0)


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

static String qualified_test_name(Test_Case* test)
{
    String filename = get_file_name(test->path);
    return Format(temp, "%/%", consume_until(&filename, ".test.fun"_s), test->id);
}

bool parse_test_file(Testing_Context* context, String relative_path, Array<String>* tests_to_run_wildcards)
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
        test.path = allocate_string(&context->memory, full_path);
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

        tests_parsed++;

        String full_name = qualified_test_name(&test);
        For (*tests_to_run_wildcards)
        {
            if (match_wildcard_string(*it, full_name))
            {
                add_item(&context->tests, &test);
                break;
            }
        }
    }

    if (tests_parsed == 0)
        Error("No tests in file!");

    return true;

#undef Error
}

struct Columns
{
    umm columns;
    umm key_width;
    umm value_width;
    umm column_width;

    Columns(umm key_width, umm value_width, umm columns = 0)
    : key_width(key_width),
      value_width(value_width),
      column_width(2 + key_width + 1 + value_width)
    {
#if defined(OS_LINUX)
        if (!columns)
        {
            winsize w = {};
            ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
            columns = w.ws_col / column_width;
            if (columns > 8)
                columns = 8;
        }
#endif
        this->columns = columns ? columns : 4;
    }

    umm serial = 0;

    void title(String title)
    {
        done(false);
        title = Format(temp, " % ", title);
        Print("\n\x1b[93;1m%\x1b[m\n",
            string_format(title, columns * column_width, String_Format::CENTER, '-'));
    }

    void add(String key, auto value)
    {
        String value_string = Format(temp, " %", value);
        if (!value) Print("\x1b[90;1m");
        Print("  % %", string_format(key, key_width),
                       string_format(value_string, value_width, String_Format::RIGHT, '.'));
        if (!value) Print("\x1b[m");
        if (++serial % columns == 0) Print("\n");
    }

    void done(bool final = true)
    {
        if (serial % columns != 0)
            Print("\n");
        serial = 0;
        if (final)
            Print("\n");
    }
};

bool run_code_of_test(Test_Case* test)
{
    String_Concatenator code_cat = {};
    for (umm i = 1; i < test->first_line_of_code; i++)
        add(&code_cat, "\n"_s);
    add(&code_cat, test->code);

    String code = resolve_to_string_and_free(&code_cat, temp);

    Compiler compiler = {};
    add_default_import_path_patterns(&compiler);
    Environment* env = make_environment(&compiler, NULL);
    assert(pump_pipeline(&compiler));  // force preload to complete

    // @Incomplete - add location information

    String assert_program = R"XXX(
        using System :: import "system";

        test_assert :: (condition: bool) {
            if !condition {
                puts(FD_STDERR, "Assertion failed!\n");
                raise(SIGKILL);
            }
        }

        assert_eq :: (lhs: $T, rhs: T) {
            if lhs != rhs {
                debug lhs;
                debug rhs;
                puts(FD_STDERR, "Equality assertion failed!\n");
                raise(SIGKILL);
            }
        }

        assert_neq :: (lhs: $T, rhs: T) {
            if lhs == rhs {
                debug lhs;
                debug rhs;
                puts(FD_STDERR, "Inequality assertion failed!\n");
                raise(SIGKILL);
            }
        }
    )XXX"_s;

    String imports_relative_to_directory = get_parent_directory_path(test->path);

    Unit* assert_having_unit = materialize_unit(
        env,
        parse_top_level_from_memory(&compiler, imports_relative_to_directory, "<test preload>"_s, assert_program)
    );

    assert(pump_pipeline(&compiler));  // force assert preload to complete

    String name = Format(temp, "<%>", qualified_test_name(test));
    Block* main = parse_top_level_from_memory(&compiler, imports_relative_to_directory, name, code);
    if (!main) return false;

    main->flags &= ~BLOCK_IS_TOP_LEVEL; // assert block is top level
    materialize_unit(env, main, assert_having_unit->entry_block);

    bool ok = pump_pipeline(&compiler);

    if (get_command_line_bool("stat"_s) ||
        get_command_line_bool("stats"_s) ||
        get_command_line_bool("statistics"_s))
    {
        auto expression_stats = [&](Columns* cols, auto& array)
        {
            for (umm kind = 1; kind < COUNT_EXPRESSIONS; kind++)
            {
                String name = Format(temp, "%", expression_kind_name[kind]);
                name = make_lowercase_copy(temp, name);
                For (name) if (*it == '_') *it = ' ';
                cols->add(name, array[kind]);
            }
        };

        Columns col(20, 12);

        col.title("Statistics"_s);
        col.add("files"_s,         compiler.top_level_blocks.count);
        col.add("lexer"_s,         to_string(size_format(compiler.lexer_memory.total_size)));
        col.add("parser"_s,        to_string(size_format(compiler.parser_memory.total_size)));
        col.add("pipeline"_s,      to_string(size_format(compiler.pipeline_memory.total_size)));
        col.add("environments"_s,  compiler.environments.count);

        col.title("Parsing counters"_s);
        col.add("identifier"_s,   compiler.identifiers.count);
        col.add("number token"_s, compiler.token_info_number.count);
        col.add("string token"_s, compiler.token_info_string.count);
        col.add("atom"_s,         compiler.atom_table.count);
        col.add("block"_s,        compiler.count_parsed_blocks);
        col.add("expressions"_s,  compiler.count_parsed_blocks);
        expression_stats(&col,    compiler.count_parsed_expressions_by_kind);

        col.title("Inference counters"_s);
        col.add("unit"_s,         compiler.count_inferred_units);
        col.add("block"_s,        compiler.count_inferred_blocks);
        col.add("constant"_s,     compiler.count_inferred_constants);
        col.add("expressions"_s,  compiler.count_inferred_expressions);
        expression_stats(&col,    compiler.count_inferred_expressions_by_kind);

        col.done();
    }

    return ok;
}


int test_runner_entry()
{
    assert(get_command_line_integer("seed"_s) != 0);

    String bin_path = get_command_line_string("test_process"_s);
    assert(bin_path);

    String bin_test = {};
    assert(read_entire_file(bin_path, &bin_test, temp));

    Test_Case test = deserialize_test(bin_test);
    bool ok = run_code_of_test(&test);

    return ok ? 0 : 1;
}


static String get_testing_temp_dir()
{
    return concatenate_path(temp, get_executable_directory(), "test_env_temp"_s);
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


bool initialize_test_suite(Testing_Context* context, String directory, Array<String>* tests_to_run_wildcards)
{
    context->root_directory = allocate_string(&context->memory, directory);

    String temp_dir = get_testing_temp_dir();

    if (check_if_directory_exists(temp_dir))
        assert(delete_directory_with_contents(temp_dir));

    assert(create_directory(temp_dir));

    // @Temporary - copy the modules directory to not mess up relative import paths.
    String original_modules_dir = concatenate_path(temp, get_executable_directory(), "../modules"_s);
    String copied_modules_dir   = concatenate_path(temp, temp_dir, "modules"_s);
    assert(create_directory_recursive(copied_modules_dir));

    For (list_files(original_modules_dir))
        assert(copy_file(
            concatenate_path(temp, original_modules_dir, *it),
            concatenate_path(temp, copied_modules_dir,   *it),
            true /* error on overwrite - dir is fresh */
        ));

    For (list_files(context->root_directory, "test.fun"_s))
        if (!parse_test_file(context, *it, tests_to_run_wildcards)) return false;

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

bool run_tests(Testing_Context* context, char* argv0, bool only_log_fails, bool show_explanations)
{
#define PassedLiteral "\x1b[32;1mPASSED\x1b[m"
#define FailedLiteral "\x1b[31;1mFAILED\x1b[m"


    assert(delete_directory_conditional(
        get_testing_temp_dir(), /* delete_directory */ false,
        [](String parent, String name, bool is_file, void* userdata)
        {
            return (is_file && get_file_extension(name) != "bin-test"_s) ? DELETE_FILE_OR_DIRECTORY : DO_NOT_DELETE;
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
            *reserve_item(&args) = Format(temp, "-test_process:%", get_test_bin_file_path(it));
            *reserve_item(&args) = Format(temp, "-seed:%",         it->rng_seed);
            it->process.arguments = allocate_array(temp, &args);

            assert(run_process(&it->process));
        }

        For (batch)
        {
            auto get_stderr = [it]()
            {
                String stderr = {};
                assert(read_entire_file(it->process.file_stderr, &stderr, temp));
                return stderr;
            };

            test_index++;
            if (!only_log_fails)
            {
                String name = qualified_test_name(it);
                Print("Running test (% / %) %",
                      u64_format(test_index, digits),
                      u64_format(context->tests.count, digits),
                      name);

                s64 pad = 48 - name.length;
                for (s64 i = 0; i < pad; i++) Print(".");
            }

            u32 exit_code = 12345;
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
                    if (!it->passed)
                        fail_explanation = get_stderr();
                }
                else
                {
                    if (exit_code == 0)
                    {
                        it->passed       = false;
                        fail_explanation = Format(temp, "    The test must error to succeed, but it didn't.");
                    }
                    else
                    {
                        String stderr = {};
                        assert(read_entire_file(it->process.file_stderr, &stderr, temp));

                        it->passed = match_wildcard_string(Format(temp, "*%*", it->error_wildcard), stderr);
                        if (!it->passed)
                            fail_explanation = Format(temp,
                                "    Errored (as it should), but the error does not match the wildcard '%':\n\n%",
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
                if (fail_explanation && show_explanations)
                    Print("%\n", fail_explanation);
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

        Print("cmd to run failed test '%':\n", it->id);
        Print("    % -test % -inline -seed:%\n",
              argv0, qualified_test_name(it), it->rng_seed);
    }

#undef PassedLiteral
#undef FailedLiteral

    return (passed_count == context->tests.count);
}

ExitApplicationNamespace
