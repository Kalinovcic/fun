//# import-file

// You can import files (as unit types), relative from the current file.
// You must prefix the path with ./
Unit        :: import "./files_for_import_tests/example.fun";
Unit_Deeper :: import "./files_for_import_tests/deeper_folder/example.fun";
Unit1 :: import "./files_for_import_tests/../files_for_import_tests/example.fun";

// You can also specify an absolute path, this won't be tested because it breaks on different PCs.
// Unit2 :: import "/c/home/.fun/some_module.fun"

// Paths are just resolved using the filesystem.
// This works on Windows but not on Linux.
// Unit3 :: import "./files_for_import_tests/EXAmPLe.FUN";

// Modules can be imported without specifying the full path.
// They are searched for in paths defined inside Compiler.import_path_patterns.
// Default import locations are:
//  .../modules/<string>.fun
//  .../modules/<string>/module.fun (to support multi-file modules)
System :: import "system";

run unit {
    test_assert(Unit.example_constant == 1);
    run Unit.Example_Unit;

    // The same module can be imported multiple times.
    Second_Import_Of_Same_Unit :: import "./files_for_import_tests/example.fun";
    run Second_Import_Of_Same_Unit.Example_Unit;

    test_assert(Second_Import_Of_Same_Unit.example_constant == Unit.example_constant);
}

//# import-file-not-found
//# ERROR
Unit :: import "./nonexisting_path/nope.fun"

//# import-file-not-found-abs
//# ERROR
Unit :: import "/c/nonexisting_path/nope.fun"

//# import-module-not-found
//# ERROR
Unit :: import "nonexisting module";
