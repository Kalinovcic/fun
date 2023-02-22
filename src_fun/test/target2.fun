
proc write_file(file: umm, buffer: *void, bytes_to_write: u32, bytes_written: *u32, overlapped; *void) u32
    foreign "WriteFile", "kernel32", "stdcall";

proc exit_process(exit_code: u32)
    foreign "ExitProcess", "kernel32", "stdcall";

hello: u64 : 0x6f6c6c6568;  // hello

proc entry()
{
    write_file(-11, &hello, 5, zero, zero);
    exit_process(42069);
}
