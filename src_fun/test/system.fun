

ARCH_X64 :: true;  // @Reconsider - how do we set this?

arch_info :: () -> (name: string, page_size: umm) {
    $if ARCH_X64 {
        yield (name = "x86_64",
               page_size = 4096);
    }
    else {
        x: u8 = "architecture not implemented yet";
    }
}


FD_STDIN  :: 0;
FD_STDOUT :: 1;
FD_STDERR :: 2;

SYS_READ   :: 0;
SYS_WRITE  :: 1;
SYS_GETPID :: 39;
SYS_KILL   :: 62;


syscall :: (sys: umm, rdi: umm, rsi: umm, rdx: umm, r10: umm, r8: umm, r9: umm) -> (rax: umm) {} intrinsic "syscall";

syscall0 :: (sys: umm)                                                           -> (rax: umm) { rax = syscall(sys, zero, zero, zero, zero, zero, zero).rax; }
syscall1 :: (sys: umm, rdi: umm)                                                 -> (rax: umm) { rax = syscall(sys, rdi,  zero, zero, zero, zero, zero).rax; }
syscall2 :: (sys: umm, rdi: umm, rsi: umm)                                       -> (rax: umm) { rax = syscall(sys, rdi,  rsi,  zero, zero, zero, zero).rax; }
syscall3 :: (sys: umm, rdi: umm, rsi: umm, rdx: umm)                             -> (rax: umm) { rax = syscall(sys, rdi,  rsi,  rdx,  zero, zero, zero).rax; }
syscall4 :: (sys: umm, rdi: umm, rsi: umm, rdx: umm, r10: umm)                   -> (rax: umm) { rax = syscall(sys, rdi,  rsi,  rdx,  r10,  zero, zero).rax; }
syscall5 :: (sys: umm, rdi: umm, rsi: umm, rdx: umm, r10: umm, r8: umm)          -> (rax: umm) { rax = syscall(sys, rdi,  rsi,  rdx,  r10,  r8,   zero).rax; }
syscall6 :: (sys: umm, rdi: umm, rsi: umm, rdx: umm, r10: umm, r8: umm, r9: umm) -> (rax: umm) { rax = syscall(sys, rdi,  rsi,  rdx,  r10,  r8,   r9  ).rax; }


SIGABRT :: 6;
SIGKILL :: 9;

raise :: (sig: umm) {
    pid := syscall0(SYS_GETPID).rax;
    syscall2(SYS_KILL, pid, SIGKILL);
}

illegal_argument :: (where: string, what: string, why: string) {
    puts(FD_STDERR, "Illegal parameter in ");
    puts(FD_STDERR, where);
    puts(FD_STDERR, ": '");
    puts(FD_STDERR, what);
    puts(FD_STDERR, "' ");
    puts(FD_STDERR, why);
    puts(FD_STDERR, "\n");
    raise(SIGKILL);
}

not_implemented :: (what: string) {
    puts(FD_STDERR, "NOT IMPLEMENTED: ");
    puts(FD_STDERR, what);
    puts(FD_STDERR, "\n");
    raise(SIGKILL);
}





divisible :: (x: $T, mod: T) -> (lower: T, upper: T) {
    if mod <= 0
     => illegal_argument("divisible", "x", "must be greater than 0");

    if x < 0 {
        upper = (x !/ mod) * mod;
        if x == upper
         => lower = upper;
        else
         => lower = upper - mod;
    }
    else {
        lower = (x !/ mod) * mod;
        if x == lower
         => upper = lower;
        else
         => upper = lower + mod;
    }
}



Region :: struct {
    page_size:      umm;    // allocation size override
    page_end:      &u8;
    cursor:        &u8;
}

allocate :: (region: &Region, size: umm, alignment: umm, debug_type: type) -> (base: &void) {

    system_page_size := arch_info().page_size;
    page_size := region.page_size;
    if page_size == 0
     => page_size = system_page_size;
    else
     => page_size = divisible(page_size, system_page_size).upper;

    debug page_size;
}

push :: (region: &Region, $T: type) -> (base: &T) {
    base = cast(&T, allocate(region, sizeof T, alignof T, T).base);
}

push_array :: (region: &Region, $T: type, n: umm) -> (base: &T) {
    base = cast(&T, allocate(region, sizeof T * n, alignof T, T).base);
}





consume :: (str: &string, n: umm) -> (lhs: string) {
    lhs.length = n;
    lhs.base   = str.base;
    str.length = str.length - n;
    str.base   = str.base  &+ n;
}

puts :: (fd: umm, what: string) -> (amount_written: umm, error: umm) {
    while what.length > 0 {
        amount := syscall3(SYS_WRITE, fd, cast(umm, what.base), what.length).rax;
        if amount <= what.length
         => amount_written = amount_written + amount;
        else
         => yield(error = -amount);
         
        consume(&what, amount);
    }
}
