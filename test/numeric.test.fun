//# soft-to-f32-near-limits-exact
run unit {
    assert_eq(*cast(&u32, &cast(f32,  0b1.1111111_11111111_11111111e127)),
               cast( u32,     0b01111111_01111111_11111111_11111111));
    assert_eq(*cast(&u32, &cast(f32, -0b1.1111111_11111111_11111111e127)),
               cast( u32,     0b11111111_01111111_11111111_11111111));
    assert_eq(*cast(&u32, &cast(f32,  0b1.1010101_10101010_01010101e127)),
               cast( u32,     0b01111111_01010101_10101010_01010101));
    assert_eq(*cast(&u32, &cast(f32, -0b1.1010101_10101010_01010101e127)),
               cast( u32,     0b11111111_01010101_10101010_01010101));
    assert_eq(*cast(&u32, &cast(f32,  0b0.1010101_10101010_01010101e127)),
               cast( u32,     0b01111110_10101011_01010100_10101010));
    assert_eq(*cast(&u32, &cast(f32, -0b0.1010101_10101010_01010101e127)),
               cast( u32,     0b11111110_10101011_01010100_10101010));

    assert_eq(*cast(&u32, &cast(f32, 0b1e-149)),
               cast( u32, 0b00000000_00000000_00000000_00000001));
}

//# soft-to-f32-no-negative-zero
run unit { assert_eq(*cast(&u32, &cast(f64, -0)), 0); }

//# soft-to-f32-near-max-overflow
//# ERROR WITH *greater: +infinity*
run unit { cast(f32, 0b1e128); }

//# soft-to-f32-near-min-overflow
//# ERROR WITH *less: -infinity*
run unit { cast(f32, -0b1e128); }

//# soft-to-f32-positive-underflow
//# ERROR WITH *(0x0)*
run unit { cast(f32, 0b1.1e-149); }

//# soft-to-f32-negative-underflow
//# ERROR WITH *(0x0)*
run unit { cast(f32, 0b1e-1000); }

//# soft-to-f32-negative-underflow
//# ERROR WITH *(0x0)*
run unit { cast(f32, -0b1.1e-149); }

//# soft-to-f32-positive-underflow
//# ERROR WITH *(0x0)*
run unit { cast(f32, -0b1e-1000); }

//# soft-to-f32-imprecise-one
//# ERROR WITH *0x1.000002*
run unit { cast(f32, 0b1.00000000000000000000000000000000000000000000000000001); }

//# soft-to-f32-near-max-inexact
//# ERROR WITH *greater: 3*
run unit { cast(f32, 0b1.1111111_11111111_111111101e127); }

//# soft-to-f32-near-min-inexact
//# ERROR WITH *less: -3*
run unit { cast(f32, -0b1.1111111_11111111_111111101e127); }

//# soft-to-f32-near-max-inexact-inf
//# ERROR WITH *greater: +infinity*
run unit { cast(f32, 0b1.1111111_11111111_1111111101e127); }

//# soft-to-f32-near-min-inexact-inf
//# ERROR WITH *less: -infinity*
run unit { cast(f32, -0b1.1111111_11111111_1111111101e127); }

