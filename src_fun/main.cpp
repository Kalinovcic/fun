#include <stdio.h>
#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "api.h"

EnterApplicationNamespace


void free_compiler(Compiler* ctx)
{
    lk_region_free(&ctx->lexer_memory);
    free_heap_array(&ctx->sources);
    free_heap_array(&ctx->token_info_other);
    free_heap_array(&ctx->token_info_integer);
    free_heap_array(&ctx->identifiers);
    free_table(&ctx->atom_table);

    lk_region_free(&ctx->parser_memory);
    // free_heap_array(&ctx->declarations);
    // free_table(&ctx->declaration_table);

    ZeroStruct(ctx);
}

extern "C" int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s file.fun\n", argv[0]);
        return 1;
    }

    Compiler compiler = {};

    Compiler* ctx = &compiler;
    Defer(free_compiler(ctx));

    Array<Token> tokens = {};
    bool ok = lex_file(ctx, make_string(argv[1]), &tokens);
    if (!ok)
        return 1;

    if (!parse_top_level(ctx, tokens))
        return 1;

    For (ctx->runs)
    {
        Unit* unit = materialize_unit(ctx, *it);
        if (!unit)
            return 1;
        printf("got unit %p\n", unit);

        if (!check_unit(unit))
            return 1;
        printf("typechecked\n");

        run_unit(unit);
    }

    printf("ok!\n");

    return 0;
}


ExitApplicationNamespace
