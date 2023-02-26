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
    if (argc != 2 && argc != 3)
    {
        fprintf(stderr, "Usage: %s file.fun [argument_list]\n", argv[0]);
        return 1;
    }

    Compiler compiler = {};
    Compiler* ctx = &compiler;
    Defer(free_compiler(ctx));

    Block* main;
    if (argc == 3)
        main = parse_top_level_from_memory(ctx, "<string>"_s, concatenate(temp, "debug "_s, make_string(argv[2]), ";"_s));
    else
        main = parse_top_level_from_file(ctx, make_string(argv[1]));
    if (!main) return 1;
    Unit* unit = materialize_unit(ctx, main);
    if (!unit) return 1;
    if (!pump_pipeline(ctx)) return 1;
    run_unit(unit);
    printf("\ndone\n");
    return 0;
}


ExitApplicationNamespace
