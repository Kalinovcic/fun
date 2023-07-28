//# complete-parameter

complete_block :: () -> (ptr: &u32) => yield(ptr = &x: u32);

run unit {
    x1 := complete_block().ptr;
    y1 := complete_block().ptr;   assert_eq( x1, y1);
    *x1 = 1;                      assert_eq(*y1, 1);
    *x1 = 2;                      assert_eq(*y1, 2);
}

//# incomplete-type-parameter

incomplete_type :: (p:  $T) -> (ptr: &u32) => yield(ptr = &x: u32);

run unit {
    t := true;
    x2 := incomplete_type(t).ptr;
    y2 := incomplete_type(t).ptr;   assert_eq( x2, y2);
    *x2 = 3;                        assert_eq(*y2, 3);
    *x2 = 4;                        assert_eq(*y2, 4);

    f := false;
    x3 := incomplete_type(f).ptr;
    y3 := incomplete_type(f).ptr;   assert_eq( x3, y3);
    *x3 = 5;                        assert_eq(*y3, 5);
    *x3 = 6;                        assert_eq(*y3, 6);

    assert_eq( x2,  x3);
    assert_eq(*x2, *x3);
    assert_eq( y2,  y3);
    assert_eq(*y2, *y3);

    i: s32 = 420;
    x4 := incomplete_type(i).ptr;
    y4 := incomplete_type(i).ptr;   assert_eq( x4, y4);
    *x4 = 7;                        assert_eq(*y4, 7);
    *x4 = 8;                        assert_eq(*y4, 8);

    assert_neq( x2,  x4);
    assert_neq(*x2, *x4);
    assert_neq( y2,  y4);
    assert_neq(*y2, *y4);

    j: s64 = 1337;
    x5 := incomplete_type(j).ptr;
    y5 := incomplete_type(j).ptr;   assert_eq( x5, y5);
    *x5 = 9;                        assert_eq(*y5, 9);
    *x5 = 10;                       assert_eq(*y5, 10);

    assert_neq( x2,  x5);
    assert_neq(*x2, *x5);
    assert_neq( y2,  y5);
    assert_neq(*y2, *y5);

    assert_neq( x4,  x5);
    assert_neq(*x4, *x5);
    assert_neq( y4,  y5);
    assert_neq(*y4, *y5);
}

//# incomplete-alias-parameter

incomplete_alias :: ($p: u8) -> (ptr: &u32) => yield(ptr = &x: u32);

run unit {
    x1 := incomplete_alias(0).ptr;
    y1 := incomplete_alias(0).ptr;   assert_eq( x1, y1);
    *x1 = 1;                         assert_eq(*y1, 1);
    *x1 = 2;                         assert_eq(*y1, 2);

    x2 := incomplete_alias(0).ptr;
    y2 := incomplete_alias(0).ptr;   assert_eq( x2, y2);
    *x2 = 3;                         assert_eq(*y2, 3);
    *x2 = 4;                         assert_eq(*y2, 4);

    assert_eq( x1,  x2);
    assert_eq(*x1, *x2);
    assert_eq( y1,  y2);
    assert_eq(*y1, *y2);

    x3 := incomplete_alias(1).ptr;
    y3 := incomplete_alias(1).ptr;   assert_eq( x3, y3);
    *x3 = 5;                         assert_eq(*y3, 5);
    *x3 = 6;                         assert_eq(*y3, 6);

    x4 := incomplete_alias(1).ptr;
    y4 := incomplete_alias(1).ptr;   assert_eq( x4, y4);
    *x4 = 7;                         assert_eq(*y4, 7);
    *x4 = 8;                         assert_eq(*y4, 8);

    assert_eq( x3,  x4);
    assert_eq(*x3, *x4);
    assert_eq( y3,  y4);
    assert_eq(*y3, *y4);

    assert_neq( x1,  x3);
    assert_neq(*x1, *x3);
    assert_neq( y1,  y3);
    assert_neq(*y1, *y3);
}
