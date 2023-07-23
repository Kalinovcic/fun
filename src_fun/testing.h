EnterApplicationNamespace

struct Test_Case
{
    String id;
    String path;
    String description;

    String code;
    u64    first_line_of_code; // starts at 1.

    bool   must_error_to_succeed;
    String error_wildcard;

    u64     rng_seed; // U64_MAX means random seed

    GUID    serialized_file_guid;
    Process process;
    u32     pid;
    bool    passed;
};

bool run_code_of_test(Test_Case* test);

struct Testing_Context
{
    Region memory;
    String root_directory;    // contains test files

    Dynamic_Array<Test_Case> tests; // ordered by their filename alphabetically,
                                    // and then in the order they appear in the file.
};


bool initialize_test_suite(Testing_Context* context, String directory, Array<String>* tests_to_run_wildcards);
void free_test_suite      (Testing_Context* context);

// returns true if all tests pass
bool run_tests(Testing_Context* context, char* argv0, bool only_log_fails = false, bool show_explanations = true);

int test_runner_entry();


ExitApplicationNamespace
