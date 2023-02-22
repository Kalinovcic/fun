
macro entry()
{
    label continue_label;
    if (1) do debug 10;
    else do debug 20;
    debug 30;
    goto continue_label;
}


run
{
    label continue_label;
    if (1) do debug 10;
    else do debug 20;
    debug 30;
    goto continue_label;
}


proc entry(argc: u32, argv: **u8) s32  // comment
{
    /*
    result_____variable: u32 = 0;
    for (x in 0, 10) do
        result_variable = result_variable + x;

    if (result_variable > 100)
    {
        result_variable = result_variable * 10;
    }

    /*
    multi
    line
    /* and nested */
    comment
    */
    */
}

macro for(`it in start: u32, end: u32) code_block
{
    // `it := do
    // {
    //     yield it: start;
    // };

    label continue_label;
    // if (it >= to) do
    //     return;
    code_block;
    goto continue_label;
}

