//# hello-world
run unit {
    debug "hello world!";
}


//# alias
A :: 1;
X :: 1;
B :: A;
C :: B;

run unit {
    test_assert(A == 1);
    test_assert(A == A);
    test_assert(A == X);

    test_assert(A == B);
    test_assert(B == C);
    test_assert(A == C);

    test_assert(X == B);
    test_assert(X == C);
}

//# num-literals
run unit {
    test_assert(1 == 1.0);

    test_assert(0b10 == 2);
    test_assert(0x10 == 16);
    test_assert(0o10 == 8);

    test_assert(2e0 == 2);
    test_assert(2e1 == 20);
    test_assert(2e3 == 2000);
    test_assert(2e-1 == 0.2);
    test_assert(2e-3 == 0.002);
    test_assert(0e9 == 0);

    test_assert(2E0 == 2);
    test_assert(2E1 == 20);
    test_assert(2E3 == 2000);
    test_assert(2E-1 == 0.2);
    test_assert(2E-3 == 0.002);
    test_assert(0E9 == 0);


    test_assert(0x2p0 == 2);
    test_assert(0x2p1 == 32);
    test_assert(0x2p3 == 2 * 16 * 16 * 16);
    test_assert(0x2p-1 == 2 %/ 16);
    test_assert(0x2p-3 == 2 %/ 16 %/ 16 %/ 16);
    test_assert(0x0p9 == 0);

    test_assert(0x2P0 == 2);
    test_assert(0x2P1 == 32);
    test_assert(0x2P3 == 2 * 16 * 16 * 16);
    test_assert(0x2P-1 == 2 %/ 16);
    test_assert(0x2P-3 == 2 %/ 16 %/ 16 %/ 16);
    test_assert(0x0P9 == 0);

    // NOT IMPLEMENTED
    //test_assert(1 << 3 == 16);
    //test_assert(1 << 0 == 1);
    //test_assert(1 << 1 == 2);
    //test_assert(1 << 3 == 16);
    //test_assert(16 >> 3 == 1);
    //test_assert(16 >> 4 == 0);

    //test_assert(cast(1,  u64) << 3 == 16);
    //test_assert(cast(1,  u64) << 0 == 1);
    //test_assert(cast(1,  u64) << 1 == 2);
    //test_assert(cast(1,  u64) << 3 == 16);
    //test_assert(cast(16, u64) >> 3 == 1);
    //test_assert(cast(16, u64) >> 4 == 0);

    //test_assert(cast(1,  s64) << 3 == 16);
    //test_assert(cast(1,  s64) << 0 == 1);
    //test_assert(cast(1,  s64) << 1 == 2);
    //test_assert(cast(1,  s64) << 3 == 16);
    //test_assert(cast(16, s64) >> 3 == 1);
    //test_assert(cast(16, s64) >> 4 == 0);
}

//# math-basic
run unit {
    // Addition
    test_assert(1 + 1 == 2);
    test_assert(1 + 0 == 1);
    test_assert(0 + 0 == 0);

    // Subtraction
    test_assert(2 - 1 == 1);
    test_assert(1 - 1 == 0);
    test_assert(1 - 0 == 1);

    // Negative numbers, negation
    test_assert(-1 == 0 - 1);
    test_assert(-1 <   0);
    test_assert(-1 <=  0);
    test_assert(-1 <= -1);
    test_assert(-1 == -1);
    test_assert(-(-1) == 1);

    // Multiple operands
    test_assert(1 + 1 + 1 + 1 + 1 == 5);
    test_assert(1 + -1 + 1 + -1 == -1 + -(-1));

    // Testing multiplication
    test_assert(2 * 2 == 4);
    test_assert(1 * 1 == 1);
    test_assert(0 * 1 == 0);
    test_assert(0 * 0 == 0);

    // Testing associativity of addition and subtraction
    test_assert((1 + 2) + 3 == 1 + (2 + 3));
    test_assert((2 - 1) - 1 == 2 - (1 + 1));
    test_assert((3 * 2) * 2 == 3 * (2 * 2));

    // Testing distributivity of multiplication over addition
    test_assert(3 * (2 + 1) == (3 * 2) + (3 * 1));

    // Test operator precedence
    test_assert(2 + 3 * 4 == 2 + (3 * 4));
    test_assert(2 + -3 * 4 == 2 + (-3 * 4));
    test_assert(2 + 3 * 4 !/ 5 == 2 + (3 * 4) !/ 5);
    test_assert(2 + 3 * 4 !/ 5 == 2 + ((3 * 4) !/ 5));
    test_assert(2 + 3 * 4 %/ 5 == 2 + (3 * 4) %/ 5);
    test_assert(2 + 3 * 4 %/ 5 == 2 + ((3 * 4) %/ 5));

    // Testing whole-number division operator
    test_assert(10 !/ 5 == 2);
    test_assert(10 !/ 3 == 3); // Rounded up
    test_assert(0 !/ 1 == 0);

    // Infinite precision soft literals
    // NOT IMPLEMENTED
    // test_assert(0.1 != cast(f64, 0.1));

    // Testing floating-point division
    EPS :: 1e-10;
    test_assert(10 %/ 2 == 5.0);
    test_assert(10 %/ 3 - 3.33333333333333333333333333 < EPS);
    test_assert(0 %/ 1 == 0.0);
    test_assert(0 %/ 1 == 0);

    // Decimal and non decimal literals are the same!
    test_assert(0.0 == 0);
    test_assert(1.0 == 1);
    test_assert(1.5 - 1.0 == 0.5);

    test_assert(0.1 + 0.2 == 0.3); // Unlike for f64...
    // NOT YET IMPLEMENTED
    //test_assert(cast(f64, 0.1) + cast(f64, 0.2) != cast(f64, 0.3));

    // Testing high numbers
    test_assert(10000000000000000000000000 * 10000000000000000000000000 == 100000000000000000000000000000000000000000000000000);

    //// Testing low numbers
    test_assert(1e-123 + 1e-123 == 2e-123);
    weird_tiny_number :: 1242437423647237463782462378467236472342364823684623874238462e-123;
    test_assert(weird_tiny_number %/ weird_tiny_number == 1);
}

//# math-int-div0
//# ERROR
run unit {
    1 !/ 0;
}

//# math-float-div0
//# ERROR
run unit {
    1 %/ 0;
}

//# math-int-div0-left-rt
//# ERROR
//# NOT IMPLEMENTED this should be a warning
run unit {
    a: u64 = 1;
    a !/ 0;
}


//# math-int-div0-rt
//# ERROR
run unit {
    cast(umm, 1) !/ cast(umm, 0);
}

// NOT IMPLEMENTED
////# math-float-div0-rt
////# ERROR
//run unit {
//    1 %/ 0;
//}

//# vars
run unit {
    a: u32;
    test_assert(a == 0);

    a = 1;
    test_assert(a == 1);

    a = cast(u32, cast(s32, -1));
    test_assert(a == 0xFFFFFFFF);
    a = a + 1;
    test_assert(a == 0);

    a = 1;
    b := cast(u32, 1);
    c := a + b;
    test_assert(c == 2);

    a = a;
    test_assert(a == 1);
}

//# signed-conversion-1
//# ERROR WITH doesn't fit
run unit {
    a: u32 = -1;
}

//# signed-conversion-2
//# ERROR WITH doesn't fit
run unit {
    a: u32 = cast(u32, -1);
}

//# signed-conversion-3
//# ERROR WITH doesn't fit
run unit {
    a: u32;
    a = cast(u32, -1);
}

//# var-redefinition
//# ERROR
run unit {
    a: u32;
    a: u64;
}

//# sizes
run unit {
    // NOT IMPLEMENTED
    // sizeof for variables
    x1:  void; //test_assert(sizeof(x1)   == 0);
    x2:  u8;   //test_assert(sizeof(x2)   == 8  !/ 8);
    x3:  u16;  //test_assert(sizeof(x3)   == 16 !/ 8);
    x4:  u32;  //test_assert(sizeof(x4)   == 32 !/ 8);
    x5:  u64;  //test_assert(sizeof(x5)   == 64 !/ 8);
    x6:  umm;  //test_assert(sizeof(x6)   == sizeof(&void));
    x7:  s8;   //test_assert(sizeof(x7)   == 8  !/ 8);
    x8:  s16;  //test_assert(sizeof(x8)   == 16 !/ 8);
    x9:  s32;  //test_assert(sizeof(x9)   == 32 !/ 8);
    x10: s64;  //test_assert(sizeof(x10)  == 64 !/ 8);
    x11: smm;  //test_assert(sizeof(x11)  == sizeof(&void));
    x12: f16;  //test_assert(sizeof(x12)  == 16 !/ 8);
    x13: f32;  //test_assert(sizeof(x13)  == 32 !/ 8);
    x14: f64;  //test_assert(sizeof(x14)  == 64 !/ 8);
    x15: bool; //test_assert(sizeof(x15)  == 1);

    test_assert(sizeof(void)   == 0);
    test_assert(sizeof(u8)     == 8  !/ 8);
    test_assert(sizeof(u16)    == 16 !/ 8);
    test_assert(sizeof(u32)    == 32 !/ 8);
    test_assert(sizeof(u64)    == 64 !/ 8);
    test_assert(sizeof(umm)    == sizeof(&void));
    test_assert(sizeof(s8)     == 8  !/ 8);
    test_assert(sizeof(s16)    == 16 !/ 8);
    test_assert(sizeof(s32)    == 32 !/ 8);
    test_assert(sizeof(s64)    == 64 !/ 8);
    test_assert(sizeof(smm)    == sizeof(&void));
    test_assert(sizeof(f16)    == 16 !/ 8);
    test_assert(sizeof(f32)    == 32 !/ 8);
    test_assert(sizeof(f64)    == 64 !/ 8);
    test_assert(sizeof(bool)   == 1);

    s: string = "hello";
    debug s;
}
