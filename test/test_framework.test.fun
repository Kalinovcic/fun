//# implicit-success
//#
//# This file mainly exists to verify that the testing framework works as expected.
//# This is a test with an implicit SUCCESS condition,
//# the test must compile, run, and exit cleanly to succeed.

run unit {
    debug (1 + 1);
}


//# explicit-success
//#
//# SUCCESS
//#
//# The condition can also be set explicitly.

run unit {
    debug (1 + 1);
}

//# error
//#
//# ERROR
//#
//# Error condition means that the test must raise a compile error, or an assertion must be failed.

undefined_symbol;


//# match-err-msg
//#
//# ERROR WITH Can't find *
//#
//# Same as 'ERROR', but the error message must match a wildcard string.
//# The pattern syntax only includes '*', which means match anything.
//# You can't escape '*', and can't match the start or end of the error string.

undefined_symbol;


//# seed
//#
//# SEED 12345
//#
//# You can set the compiler rng seed. The test will always run with this seed.
//# If you don't set the seed it's always randomized.

foo :: 1;


//# assert
//#
//# You can use the intrinsic function
//#
//# test_assert(condition: bool) {} intrinsic;
//#
//# To fail the test if the condition doesn't hold.
//# You should not use an 'ERROR' test combined with a test_assert() failure,
//# It's better to invert the condition and make a 'SUCCESS'.
//# 'ERROR' tests are meant to check that specific compiler errors are generated.

run unit {
    test_assert(1 + 1 == 2);
}

//# assert-bad
//#
//# ERROR
//#
//# ...You can still do it tho, but don't.

run unit {
    test_assert(false);
}
