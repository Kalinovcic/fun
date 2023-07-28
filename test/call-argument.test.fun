//# integer-alias-correct
foo :: ($x: u32) {}
run unit { foo(0); foo(100); }

//# integer-alias-doesnt-fit
//# ERROR WITH *can't fit in u32*
foo :: ($x: u32) {}
run unit { foo(123.456); }

//# integer-alias-bad-type
//# ERROR WITH *is expected to be a compile-time number*
foo :: ($x: u32) {}
run unit { foo(false); }

//# float-alias-correct
foo :: ($x: f32) {}
run unit { foo(2.5); foo(123); }

//# float-alias-doesnt-fit
//# ERROR WITH *can't be represented exactly as a f32*
foo :: ($x: f32) {}
run unit { foo(7528745872345259714371752874587234525971437175287458723452597143717528745872345259714); }

//# float-alias-bad-type
//# ERROR WITH *is expected to be a compile-time number*
foo :: ($x: f32) {}
run unit { foo(false); }

//# bool-alias-correct
foo :: ($x: bool) {}
run unit { foo(true); foo(false); }

//# bool-alias-bad-type
//# ERROR WITH *is expected to be a compile-time boolean*
foo :: ($x: bool) {}
run unit { foo(123); }

//# type-alias-correct
foo :: ($x: type) {}
run unit { foo(bool); foo(s64); }

//# type-alias-bad-type
//# ERROR WITH *is expected to be a compile-time type*
foo :: ($x: type) {}
run unit { foo(123); }

//# block-alias-correct
foo :: ($x: block) {}
run unit { foo(() => debug "yolo"); }

//# block-alias-sugar
foo :: ($x: block) {}
run unit { foo() => debug "yolo"; }

//# block-alias-bad-type
//# ERROR WITH *is expected to be a block*
foo :: ($x: block) {}
run unit { foo(123); }

//# bool-simple
foo :: (x: bool) {}
run unit { foo(x: bool = true); }

//# bool-hardened
foo :: (x: bool) {}
run unit { foo(true); }

//# bool-dont-match
//# ERROR WITH *1st argument in this call to foo doesn't match the 1st parameter (x) type bool*
foo :: (x: bool) {}
run unit { foo(123); }

//# type-simple
foo :: (x: type) {}
run unit { foo(x: type = bool); }

//# type-hardened
foo :: (x: type) {}
run unit { foo(bool); }

//# type-dont-match
//# ERROR WITH *1st argument in this call to foo doesn't match the 1st parameter (x) type type*
foo :: (x: type) {}
run unit { foo(123); }

//# integer-simple
foo :: (x: s64) {}
run unit { foo(x: s64 = -123); }

//# integer-hardened
foo :: (x: s64) {}
run unit { foo(-123); }

//# integer-overflow
//# ERROR WITH *The number doesn't fit in s64.*
foo :: (x: s64) {}
run unit { foo(1e100); }

//# integer-underflow
//# ERROR WITH *The number doesn't fit in s64.*
foo :: (x: s64) {}
run unit { foo(-1e100); }

//# integer-dont-match
//# ERROR WITH *1st argument in this call to foo doesn't match the 1st parameter (x) type s64*
foo :: (x: s64) {}
run unit { foo(false); }
