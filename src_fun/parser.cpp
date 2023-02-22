#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "api.h"
#include <stdio.h>

EnterApplicationNamespace


#define ReportError(...) (report_error(__VA_ARGS__), NULL)
#define ReportErrorEOF(ctx, stream, ...) (report_error((ctx), (stream)->end - 1, __VA_ARGS__), NULL)


// A Token_Stream doesn't start empty!
// This means end-1 is always a valid pointer, and always points to the last token.
// If something has no tokens, don't make a Token_Stream.
struct Token_Stream
{
    Compiler* ctx;
    Token* cursor;
    Token* end;
};

static void set_nonempty_token_stream(Token_Stream* stream, Compiler* ctx, Array<Token> tokens)
{
    assert(tokens.count > 0);
    stream->ctx    = ctx;
    stream->cursor = tokens.address;
    stream->end    = tokens.address + tokens.count;
}

static Token* next_token_or_eof(Token_Stream* stream)
{
    Token* cursor = stream->cursor;
    if (cursor >= stream->end)
        return stream->end - 1;
    return cursor;
}

static Token* maybe_take_atom(Token_Stream* stream, Atom atom)
{
    Token* cursor = stream->cursor;
    if (cursor >= stream->end)
        return NULL;
    if (atom == ATOM_FIRST_IDENTIFIER)
    {
        if (!is_identifier(cursor->atom))
            return NULL;
    }
    else if (atom != cursor->atom)
        return NULL;
    return stream->cursor++;
}

static Token* take_next_token(Token_Stream* stream, String expected_what)
{
    Token* cursor = stream->cursor++;
    if (cursor >= stream->end)
        return (Token*) ReportErrorEOF(stream->ctx, stream, Format(temp, "Unexpected end of file.\n%", expected_what));
    return cursor;
}

static bool take_atom(Token_Stream* stream, Atom atom, String expected_what)
{
    Token* cursor = stream->cursor++;
    if (cursor >= stream->end)
        return ReportErrorEOF(stream->ctx, stream, Format(temp, "Unexpected end of file.\n%", expected_what));
    if (atom == ATOM_FIRST_IDENTIFIER)
    {
        if (is_identifier(cursor->atom))
            return true;
    }
    else if (atom == cursor->atom)
        return true;
    return ReportError(stream->ctx, cursor, expected_what);
}

static bool lookahead_atom(Token_Stream* stream, Atom atom, umm lookahead)
{
    Token* token = stream->cursor + lookahead;
    if (token < stream->cursor || token >= stream->end)
        return false;
    if (atom == ATOM_FIRST_IDENTIFIER)
    {
        if (is_identifier(token->atom))
            return true;
    }
    return atom == token->atom;
}


static bool parse_type(Token_Stream* stream, Type* out_type)
{
    u32 indirection_count = 0;
    while (Token* star = maybe_take_atom(stream, ATOM_STAR))
    {
        indirection_count++;
        if (indirection_count > TYPE_MAX_INDIRECTION)
            return ReportError(stream->ctx, star, Format(temp, "Too many indirections! Maximum is %", TYPE_MAX_INDIRECTION));
    }

    Token* name = take_next_token(stream, "Expected a type name."_s);
    if (!name)
        return false;

    Type type = TYPE_INVALID;
    switch (name->atom)
    {
    case ATOM_VOID:   type = TYPE_VOID;   break;
    case ATOM_U8:     type = TYPE_U8;     break;
    case ATOM_U16:    type = TYPE_U16;    break;
    case ATOM_U32:    type = TYPE_U32;    break;
    case ATOM_U64:    type = TYPE_U64;    break;
    case ATOM_S8:     type = TYPE_S8;     break;
    case ATOM_S16:    type = TYPE_S16;    break;
    case ATOM_S32:    type = TYPE_S32;    break;
    case ATOM_S64:    type = TYPE_S64;    break;
    case ATOM_F32:    type = TYPE_F32;    break;
    case ATOM_F64:    type = TYPE_F64;    break;
    case ATOM_BOOL8:  type = TYPE_BOOL8;  break;
    case ATOM_BOOL16: type = TYPE_BOOL16; break;
    case ATOM_BOOL32: type = TYPE_BOOL32; break;
    case ATOM_BOOL64: type = TYPE_BOOL64; break;
    default:
    {
        return ReportError(stream->ctx, name, "Expected a type name."_s);
    } break;
    }

    *out_type = (Type)(type + (indirection_count << TYPE_POINTER_SHIFT));
    return true;
}



struct Block_Builder
{
    Block* block;
    Concatenator<Parsed_Expression> expressions;
    Concatenator<Parsed_Statement>  statements;
    Concatenator<Child_Block>       children_blocks;
};

static bool parse_expression(Token_Stream* stream, Block_Builder* builder, Expression* out_expression)
{
#define NextExpression() ((Expression) builder->expressions.count)

    auto add_expression = [builder](Expression_Kind kind, flags32 flags, Token* from, Token* to) -> Parsed_Expression*
    {
        Parsed_Expression* result = reserve_item(&builder->expressions);
        result->kind  = kind;
        result->flags = flags;
        result->visibility_limit = (Statement) builder->statements.count;
        result->from  = *from;
        result->to    = *to;
        return result;
    };

    Token* start = stream->cursor;
    if (maybe_take_atom(stream, ATOM_INTEGER))
    {
        *out_expression = NextExpression();
        Parsed_Expression* expr = add_expression(EXPRESSION_INTEGER_LITERAL, 0, start, start);
        expr->literal = *start;
    }
    else if (maybe_take_atom(stream, ATOM_FIRST_IDENTIFIER))
    {
        if (Token* colon = maybe_take_atom(stream, ATOM_COLON))
        {
            Type type;
            if (!parse_type(stream, &type))
                return false;

            *out_expression = NextExpression();
            Parsed_Expression* expr = add_expression(EXPRESSION_DECLARATION, 0, start, start);
            expr->name = *start;
            expr->parsed_type = type;
        }
        else
        {
            *out_expression = NextExpression();
            Parsed_Expression* expr = add_expression(EXPRESSION_NAME, 0, start, start);
            expr->name = *start;
        }
    }
    else if (maybe_take_atom(stream, ATOM_CAST))
    {
        if (!take_atom(stream, ATOM_LEFT_PARENTHESIS, "Expected '(' after the 'cast' keyword."_s))
            return false;
        Type type;
        if (!parse_type(stream, &type))
            return false;
        if (!take_atom(stream, ATOM_COMMA, "Expected ',' between the type and value operands to 'cast'."_s))
            return false;
        Expression operand;
        if (!parse_expression(stream, builder, &operand))
            return false;
        if (!take_atom(stream, ATOM_RIGHT_PARENTHESIS, "Expected ')' after the value operand to 'cast'."_s))
            return false;

        *out_expression = NextExpression();
        Parsed_Expression* expr = add_expression(EXPRESSION_CAST, 0, start, stream->cursor - 1);
        expr->parsed_type = type;
        expr->unary_operand = operand;
    }
    else
    {
        return ReportError(stream->ctx, next_token_or_eof(stream), "Expected an expression."_s);
    }

    if (maybe_take_atom(stream, ATOM_EQUAL))
    {
        Expression lhs = *out_expression;
        Expression rhs;
        if (!parse_expression(stream, builder, &rhs))
            return false;

        *out_expression = NextExpression();
        Parsed_Expression* expr = add_expression(EXPRESSION_ASSIGNMENT, 0, start, stream->cursor - 1);
        expr->binary_lhs = lhs;
        expr->binary_rhs = rhs;
    }

    return true;
#undef NextExpression
}

static Block* parse_statement_block(Token_Stream* stream);

static bool parse_statement_block(Token_Stream* stream, Block_Builder* builder, Block_Index* out_block)
{
    Block* block = parse_statement_block(stream);
    if (!block)
        return false;

    *out_block = (Block_Index) builder->children_blocks.count;

    Child_Block child = {};
    child.child = block;
    child.visibility_limit = (Statement) builder->statements.count;
    add_item(&builder->children_blocks, &child);
    return true;
}

static bool parse_statement(Token_Stream* stream, Block_Builder* builder)
{
    Token*  statement_start = stream->cursor;
    flags32 statement_flags = 0;
    if (maybe_take_atom(stream, ATOM_DEFER))
        statement_flags |= STATEMENT_IS_DEFERRED;

    if (maybe_take_atom(stream, ATOM_IF))
    {
        Expression expression;
        if (!parse_expression(stream, builder, &expression))
            return false;

        Block_Index true_block;
        if (!parse_statement_block(stream, builder, &true_block))
            return false;

        Block_Index false_block = NO_BLOCK;
        if (maybe_take_atom(stream, ATOM_ELSE))
            if (!parse_statement_block(stream, builder, &false_block))
                return false;

        Parsed_Statement* stmt = reserve_item(&builder->statements);
        stmt->kind        = STATEMENT_IF;
        stmt->flags       = statement_flags;
        stmt->from        = *statement_start;
        stmt->to          = *(stream->cursor - 1);
        stmt->expression  = expression;
        stmt->true_block  = true_block;
        stmt->false_block = false_block;
    }
    else if (maybe_take_atom(stream, ATOM_WHILE))
    {
        Expression expression;
        if (!parse_expression(stream, builder, &expression))
            return false;

        Block_Index true_block;
        if (!parse_statement_block(stream, builder, &true_block))
            return false;

        Parsed_Statement* stmt = reserve_item(&builder->statements);
        stmt->kind        = STATEMENT_WHILE;
        stmt->flags       = statement_flags;
        stmt->from        = *statement_start;
        stmt->to          = *(stream->cursor - 1);
        stmt->expression  = expression;
        stmt->true_block  = true_block;
    }
    else if (maybe_take_atom(stream, ATOM_DEBUG))
    {
        Expression expression;
        if (!parse_expression(stream, builder, &expression))
            return false;

        Token* semicolon = stream->cursor;
        if (!take_atom(stream, ATOM_SEMICOLON, "Expected ';' after the expression."_s))
            return false;

        Parsed_Statement* stmt = reserve_item(&builder->statements);
        stmt->kind       = STATEMENT_DEBUG_OUTPUT;
        stmt->flags      = statement_flags;
        stmt->from       = *statement_start;
        stmt->to         = *semicolon;
        stmt->expression = expression;
    }
    else
    {
        Expression expression;
        if (!parse_expression(stream, builder, &expression))
            return false;

        Token* semicolon = stream->cursor;
        if (!take_atom(stream, ATOM_SEMICOLON, "Expected ';' after the expression."_s))
            return false;

        Parsed_Statement* stmt = reserve_item(&builder->statements);
        stmt->kind       = STATEMENT_EXPRESSION;
        stmt->flags      = statement_flags;
        stmt->from       = *statement_start;
        stmt->to         = *semicolon;
        stmt->expression = expression;
    }

    return true;
}

static Block* parse_statement_block(Token_Stream* stream)
{
    Compiler* ctx = stream->ctx;
    Token* block_start = stream->cursor;
    if (maybe_take_atom(stream, ATOM_LEFT_BRACE))
    {
        Block* block = PushValue(&ctx->parser_memory, Block);
        block->from = *block_start;

        Block_Builder builder = {};
        builder.block = block;
        Defer(block->parsed_expressions = const_array(resolve_to_array_and_free(&builder.expressions,     &ctx->parser_memory)));
        Defer(block->parsed_statements  = const_array(resolve_to_array_and_free(&builder.statements,      &ctx->parser_memory)));
        Defer(block->children_blocks    = const_array(resolve_to_array_and_free(&builder.children_blocks, &ctx->parser_memory)));

        while (stream->cursor < stream->end && stream->cursor->atom != ATOM_RIGHT_BRACE)
            if (!parse_statement(stream, &builder))
                return NULL;
        Token* block_end = stream->cursor;
        if (!take_atom(stream, ATOM_RIGHT_BRACE, "Body was not closed by '}'.\n"
                                                 "(Check for mismatched braces.)"_s))
            return NULL;
        block->to = *block_end;

        return block;
    }
    else if (maybe_take_atom(stream, ATOM_DO))
    {
        Block* block = PushValue(&ctx->parser_memory, Block);
        block->from = *block_start;

        Block_Builder builder = {};
        builder.block = block;
        Defer(block->parsed_expressions = const_array(resolve_to_array_and_free(&builder.expressions,     &ctx->parser_memory)));
        Defer(block->parsed_statements  = const_array(resolve_to_array_and_free(&builder.statements,      &ctx->parser_memory)));
        Defer(block->children_blocks    = const_array(resolve_to_array_and_free(&builder.children_blocks, &ctx->parser_memory)));

        if (!parse_statement(stream, &builder))
            return NULL;

        block->to = *(stream->cursor - 1);
        return block;
    }
    else
    {
        return (Block*) ReportError(stream->ctx, next_token_or_eof(stream), "Expected '{' or 'do' to start a block."_s);
    }
}

#if 0
Procedure* parse_procedure_or_macro(Compiler* ctx, Token_Stream* stream)
{
    bool is_macro = (stream->cursor->atom == ATOM_MACRO);
    bool is_proc  = !is_macro;
    Token* start = stream->cursor++;  // take PROC/MACRO

    Token* name = stream->cursor;
    if (!take_atom(stream, ATOM_FIRST_IDENTIFIER,
                   is_macro ? "Expected the procedure name after 'macro'."_s
                            : "Expected the procedure name after 'proc'."_s))
        return NULL;


    // Parse parameter list
    Token* header_start = stream->cursor;
    if (!take_atom(stream, ATOM_LEFT_PARENTHESIS, "Expected a parameter list starting with '('."_s))
        return NULL;

    Dynamic_Array<Parameter> parameters = {};
    Defer(free_heap_array(&parameters));

    bool allow_comma = false;
    while (stream->cursor < stream->end && stream->cursor->atom != ATOM_RIGHT_PARENTHESIS)
    {
        if (allow_comma)
        {
            Parameter* previous_parameter = &parameters[parameters.count - 1];
            if (is_proc)
            {
                if (!take_atom(stream, ATOM_COMMA, "Expected either a ',' or ')'."_s))
                    return NULL;
                previous_parameter->flags |= PARAMETER_FOLLOWED_BY_COMMA;
                if (stream->cursor >= stream->end)
                    return (Procedure*) ReportErrorEOF(ctx, stream,
                        "Unexpected ',' at the end of the file. Expected a parameter declaration.\n"_s);
            }
            else if (maybe_take_atom(stream, ATOM_COMMA))
                previous_parameter->flags |= PARAMETER_FOLLOWED_BY_COMMA;

        }

        Parameter* parameter = reserve_item(&parameters);
        parameter->kind = PARAMETER_LITERAL;

        Token* backtick = stream->cursor;
        bool is_token_parameter = maybe_take_atom(stream, ATOM_BACKTICK);
        if (is_token_parameter)
        {
            if (is_proc)
                return (Procedure*) ReportError(ctx, backtick, "Procedures can't accept token parameters."_s);
            parameter->kind = PARAMETER_TOKEN;
        }

        Token* name = stream->cursor;
        if (!take_atom(stream, ATOM_FIRST_IDENTIFIER, "Expected either an identifier or ')'."_s))
            return NULL;
        parameter->name = *name;

        if (maybe_take_atom(stream, ATOM_COLON))
        {
            if (is_token_parameter)
                return (Procedure*) ReportError(ctx, backtick, "Token parameters don't have types."_s);
            parameter->kind = PARAMETER_VARIABLE;

            if (!parse_type(ctx, stream, &parameter->parsed_type))
                return NULL;
        }
        else if (is_proc)
            return (Procedure*) ReportError(ctx, name,
                    "Expected a ':' followed by the parameter type.\n"
                    "(Procedures can't accept literal parameters, only macros.)"_s);

        allow_comma = true;
    }
    if (!take_atom(stream, ATOM_RIGHT_PARENTHESIS, "Parameter list was not closed by ')'."_s))
        return NULL;

    Type return_type = TYPE_VOID;
    bool accepts_code_block = false;
    if (maybe_take_atom(stream, ATOM_CODE_BLOCK))
    {
        if (!is_macro)
            return (Procedure*) ReportError(ctx, stream->cursor, "Only macros can accept a code_block! This is a procedure."_s);
        accepts_code_block = true;
    }
    else if (stream->cursor < stream->end && (stream->cursor->atom != ATOM_LEFT_BRACE && stream->cursor->atom != ATOM_DO))
    {
        if (!parse_type(ctx, stream, &return_type))
            return NULL;
    }


    // Create declaration
    Declaration declaration = (Declaration) ctx->declarations.count;
    if (declaration & DECLARATION_IS_GLOBAL)
        return (Procedure*) ReportError(ctx, name,
            Format(temp, "Too many declarations! Limit is %.", DECLARATION_IS_GLOBAL));
    declaration = (Declaration)(declaration | DECLARATION_IS_GLOBAL);

    Declaration previous_declaration;
    if (set(&ctx->declaration_table, &name->atom, &declaration, &previous_declaration))
    {
        assert(previous_declaration & DECLARATION_IS_GLOBAL);
        Declaration_Data* old_declaration_data = &ctx->declarations[previous_declaration & DECLARATION_INDEX_MASK];
        if (old_declaration_data->kind != DECLARATION_PROCEDURE_OR_MACRO)
            return (Procedure*) ReportError(ctx,
                name, Format(temp, "Duplicate global declaration with name '%'.", get_identifier(ctx, name)),
                &old_declaration_data->name, "Also declared here."_s);
    }
    else
    {
        previous_declaration = DECLARATION_INVALID;
    }

    Declaration_Data* declaration_data = reserve_item(&ctx->declarations);
    declaration_data->kind = DECLARATION_PROCEDURE_OR_MACRO;
    declaration_data->name = *name;
    declaration_data->next_overload = previous_declaration;

    // Create procedure
    Procedure* procedure = PushValue(&ctx->parser_memory, Procedure);
    procedure->from = *start;
    procedure->name = *name;
    if (is_macro)
        procedure->flags |= PROCEDURE_IS_MACRO;
    if (accepts_code_block)
        procedure->flags |= PROCEDURE_EXPECTS_CODE_BLOCK;

    procedure->parameters = parameters;
    parameters = {};  // clear so it doesn't get freed by a Defer above
    procedure->return_type = return_type;
    declaration_data->procedure_or_macro = procedure;

    // Parse body
    Block* block = parse_statement_block(stream);
    if (!block)
        return NULL;
    procedure->entry_block = block;
    procedure->to = *(stream->cursor - 1);

    return procedure;
}
#endif

Run* parse_run(Compiler* ctx, Token_Stream* stream)
{
    Token* start = stream->cursor++;  // take RUN

    Block* block = parse_statement_block(stream);
    if (!block)
        return NULL;

    Run* run = PushValue(&ctx->parser_memory, Run);
    run->from = *start;
    run->to = *(stream->cursor - 1);
    run->entry_block = block;
    return run;
}


bool parse_top_level(Compiler* ctx, Array<Token> tokens)
{
    if (!tokens.count)
        return true;

    Token_Stream stream;
    assert(tokens.count > 0);  // checked above
    set_nonempty_token_stream(&stream, ctx, tokens);

    while (stream.cursor < stream.end)
    {
        if (stream.cursor->atom == ATOM_RUN)
        {
            Run* run = parse_run(ctx, &stream);
            if (!run)
                return false;
            add_item(&ctx->runs, &run);
        }
        else
        {
            return ReportError(ctx, stream.cursor, "Unexpected token in top-level scope."_s);
        }
    }
    return true;
}



ExitApplicationNamespace
