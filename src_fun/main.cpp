#include <stdio.h>
#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
#include "api.h"

EnterApplicationNamespace


void free_compiler(Compiler* ctx)
{
    lk_region_free(&ctx->lexer_memory);
    free_heap_array(&ctx->sources);
    free_heap_array(&ctx->token_info_other);
    free_heap_array(&ctx->token_info_number);
    free_heap_array(&ctx->token_info_string);
    free_heap_array(&ctx->identifiers);
    free_table(&ctx->atom_table);
    lk_region_free(&ctx->parser_memory);

    ZeroStruct(ctx);
}

extern "C" int main(int argc, char** argv)
{
    /*add_log_handler([](String severity, String subsystem, String msg)
    {
        printf("%.*s\n", StringArgs(msg));
    });*/

    Dynamic_Array<String> non_flag_args = {};
    for (umm i = 1; i < argc; i++)
        if (argv[i][0] != '-')
            *reserve_item(&non_flag_args) = wrap_string(argv[i]);

    if (non_flag_args.count != 1 && non_flag_args.count != 2)
    {
        fprintf(stderr, "Usage: %s file.fun [argument_list]\n", argv[0]);
        return 1;
    }

    Compiler compiler = {};
    Compiler* ctx = &compiler;
    Defer(free_compiler(ctx));

    Unit* preload_unit = materialize_unit(ctx, parse_top_level_from_memory(ctx, "<preload>"_s, R"XXX(
        `string`@ :: struct
        {
            length: umm;
            base: &u8;
        }
    )XXX"_s));
    assert(preload_unit);
    assert(pump_pipeline(ctx));
    assert(get_identifier(ctx, &get_user_type_data(ctx, TYPE_STRING)->alias) == "string"_s);

    Block* main;
    if (non_flag_args.count == 2)
        main = parse_top_level_from_memory(ctx, "<string>"_s, concatenate(temp, "debug "_s, non_flag_args[1], ";"_s));
    else
        main = parse_top_level_from_file(ctx, non_flag_args[0]);
    if (!main) return 1;
    Unit* unit = materialize_unit(ctx, main);
    if (!unit) return 1;
    if (!pump_pipeline(ctx)) return 1;
    return 0;
}


ExitApplicationNamespace
