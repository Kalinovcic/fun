

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


FD_STDIN        :: 0;
FD_STDOUT       :: 1;
FD_STDERR       :: 2;

SYS_READ        :: 0;
SYS_WRITE       :: 1;
SYS_MMAP        :: 9;
SYS_MPROTECT    :: 10;
SYS_MUNMAP      :: 11;
SYS_GETPID      :: 39;
SYS_KILL        :: 62;


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




allocate_virtual_memory :: (size: umm) -> (base: umm) {
    PROT_READ     :: 0x1;
    PROT_WRITE    :: 0x2;
    PROT_EXEC     :: 0x4;

    MAP_SHARED    :: 0x01;
    MAP_PRIVATE   :: 0x02;
    MAP_FILE      :: 0;
    MAP_ANONYMOUS :: 0x20;

    fd := cast(umm, cast(smm, -1));
    base = syscall6(SYS_MMAP, zero, size, PROT_READ + PROT_WRITE, MAP_PRIVATE + MAP_ANONYMOUS, fd, zero).rax;
}

release_virtual_memory :: (base: umm, size: umm) {
    syscall2(SYS_MUNMAP, base, size);
}

zero_memory :: (base: $T, size: umm) {
    cursor     := cast(&u8, base);
    end_cursor := cursor &+ size;
    while cursor < end_cursor {
        *cursor = 0;
        cursor = cursor &+ cast(umm, 1);
    }
}



Region :: struct {
    page_size:      umm;  // allocation size override
    page_end:       umm;
    cursor:         umm;
    last_page:      umm;

    Cursor :: struct {
        page_end:   umm;
        cursor:     umm;
        last_page:  umm;
    }

    Header :: struct {
        previous:   umm;
        size:       umm;
    }
}


allocate :: (region: &Region, size: umm, alignment: umm, debug_type: type) -> (base: umm) {
    cursor := divisible(region.cursor, alignment).upper;
    if cursor + size <= region.page_end {
        base = cursor;
        region.cursor = cursor + size;
        yield;
    }

    system_page_size := arch_info().page_size;
    page_size := region.page_size;
    if page_size == 0  =>  page_size = 16 * system_page_size;
    else               =>  page_size = divisible(page_size, system_page_size).upper;

    header_size     := divisible(cast(umm, sizeof Region.Header), alignment).upper;
    min_size        := header_size + size;
    big_allocation  := min_size > page_size !/ 4;
    allocation_size := page_size;
    if big_allocation
     => allocation_size = divisible(min_size, system_page_size).upper;

    page := allocate_virtual_memory(allocation_size).base;
    header := cast(&Region.Header, page);
    header.previous = region.last_page;
    header.size     = allocation_size;
    region.last_page = page;

    base = page + header_size;
    if !big_allocation {
        region.page_end = page + allocation_size;
        region.cursor   = page + min_size;
    }
}

push :: (region: &Region, $T: type) -> (base: &T) {
    base = cast(&T, allocate(region, sizeof T, alignof T, T).base);
}

push_array :: (region: &Region, $T: type, n: umm) -> (base: &T) {
    base = cast(&T, allocate(region, sizeof T * n, alignof T, T).base);
}


drop :: (region: &Region) {
    while region.last_page != 0 {
        header := cast(&Region.Header, region.last_page);
        region.last_page = header.previous;
        release_virtual_memory(cast(umm, header), header.size);
    }

    region.page_end = 0;
    region.cursor   = 0;
}


cursor :: (region: &Region, cursor: &Region.Cursor) {
    cursor.page_end  = region.page_end;
    cursor.cursor    = region.cursor;
    cursor.last_page = region.last_page;
}

rewind :: (region: &Region, cursor: &Region.Cursor) {
    while region.last_page != cursor.last_page {
        header := cast(&Region.Header, region.last_page);
        region.last_page = header.previous;
        release_virtual_memory(cast(umm, header), header.size);
    }

    amount_to_zero := cursor.page_end - cursor.cursor;
    if region.page_end == cursor.page_end
     => amount_to_zero = cursor.cursor - region.cursor;
    zero_memory(cast(&void, cursor.cursor), amount_to_zero);

    region.page_end = cursor.page_end;
    region.cursor   = cursor.cursor;
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
