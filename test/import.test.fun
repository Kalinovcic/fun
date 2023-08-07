//# import-file

// You can import files (as unit types), relative from the current file.
// You must prefix the path with ./
Unit        :: import "./import_test_files/example.fun";
Unit_Deeper :: import "./import_test_files/deeper_folder/example.fun";
Unit1 :: import "./import_test_files/../import_test_files/example.fun";

// You can also specify an absolute path, this won't be tested because it breaks on different PCs.
// Unit2 :: import "/c/home/.fun/some_module.fun"

// Paths are just resolved using the filesystem.
// This works on Windows but not on Linux.
// Unit3 :: import "./import_test_files/EXAmPLe.FUN";

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
    Second_Import_Of_Same_Unit :: import "./import_test_files/example.fun";
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


//# exported-member
run unit {
    A :: import "./import_test_files/example.fun";
    test_assert(A.example_constant == 1);

    B :: A;
    test_assert(B.example_constant == 1);

    C :: B;
    test_assert(C.example_constant == 1);

    fn :: (discard: $T) {
        test_assert(T.example_constant == 1);
    }

    c: C;
    fn(c);

    using a: A;
    test_assert(example_constant == 1);
}

//# import-indirect-1
//# ERROR WITH alias
run unit {
    A :: import "./import_test_files/file_which_imports.fun";
    test_assert(A.exported.example_constant == 1);
}

//# import-indirect-2
//# ERROR WITH alias
run unit {
    A :: import "./import_test_files/file_which_imports.fun";
    test_assert(A.exported.example_constant == 1);
}

//# import-indirect-3
run unit {
    using A :: import "./import_test_files/file_which_imports.fun";
    test_assert(Exported.example_constant == 1);
}

//# import-indirect-4
run unit {
    A :: import "./import_test_files/file_which_imports_and_uses.fun";
    test_assert(A.example_constant == 1);
}

//# import-indirect-5
//# @Reconsider when implementing private
run unit {
    using A :: import "./import_test_files/file_which_imports_and_uses.fun";
    test_assert(example_constant == 1);
}

//# import-indirect-6
run unit {
    using A :: import "./import_test_files/file_which_imports_and_uses.fun";
    test_assert(Exported.example_constant == 1);
}



//# self-private
run unit {
    using A :: import "./import_test_files/self_import.fun";
    a: A;
    a.unit_member = 1;
    add_to_member(&a, 2);
    test_assert(a.unit_member == 3);
}
