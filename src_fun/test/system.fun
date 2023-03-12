
syscall :: (rax: umm, rdi: umm, rsi: umm, rdx: umm, r10: umm, r8: umm, r9: umm) -> (rax: umm) {} intrinsic "syscall";

consume :: (str: &string, n: umm) -> (lhs: string) {
    lhs.length = n;
    lhs.base   = str.base;
    str.length = str.length - n;
    str.base   = str.base  &+ n;
}

puts :: (what: string) -> (amount_written: umm, error: umm) {
    SYS_WRITE: umm = 1;
    fd: umm = 1;  // stdout
    while what.length > 0 {
        amount := syscall(SYS_WRITE, fd, cast(umm, what.base), what.length, zero, zero, zero).rax;
        if amount <= what.length
         => amount_written = amount_written + amount;
        else
         => yield(error = -amount);
         
        consume(&what, amount);
    }
}
