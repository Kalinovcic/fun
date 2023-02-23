#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
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
    Token* start;
    Token* cursor;
    Token* end;
};

static void set_nonempty_token_stream(Token_Stream* stream, Compiler* ctx, Array<Token> tokens)
{
    assert(tokens.count > 0);
    stream->ctx    = ctx;
    stream->start  = tokens.address;
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
    Token* report_at = cursor - 1;
    if (report_at < stream->start)
        report_at = stream->start;
    return ReportError(stream->ctx, report_at, expected_what);
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

    Type type = INVALID_TYPE;
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
    case ATOM_F16:    type = TYPE_F16;    break;
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

    assert(type != INVALID_TYPE);
    *out_type = (Type)(type + (indirection_count << TYPE_POINTER_SHIFT));
    return true;
}



struct Block_Builder
{
    Block* block;
    Dynamic_Array<Parsed_Expression> expressions;
    Concatenator <Parsed_Statement>  statements;
    Concatenator <Child_Block>       children_blocks;
};

static void finish_building(Compiler* ctx, Block_Builder* builder)
{
    Block* block = builder->block;
    Region* memory = &ctx->parser_memory;

    block->parsed_expressions = const_array(allocate_array(memory, &builder->expressions));
    block->parsed_statements  = const_array(resolve_to_array_and_free(&builder->statements, memory));
    block->children_blocks    = const_array(resolve_to_array_and_free(&builder->children_blocks, memory));
    free_heap_array(&builder->expressions);
}

// IMPORTANT! Make sure to not use the returned pointer after calling add_expression() again!
static Parsed_Expression* add_expression(Block_Builder* builder, Expression_Kind kind, flags32 flags, Token* from, Token* to, Expression* out_id)
{
    *out_id = (Expression) builder->expressions.count;
    Parsed_Expression* result = reserve_item(&builder->expressions);
    result->kind  = kind;
    result->flags = flags;
    result->from  = *from;
    result->to    = *to;
    result->visibility_limit = (Statement) builder->statements.count;
    return result;
}

static Parsed_Expression* add_expression(Block_Builder* builder, Expression_Kind kind, flags32 flags, Token* from, Expression to, Expression* out_id)
{
    return add_expression(builder, kind, flags, from, &builder->expressions[to].to, out_id);
}

static Parsed_Expression* add_expression(Block_Builder* builder, Expression_Kind kind, flags32 flags, Expression from, Token* to, Expression* out_id)
{
    return add_expression(builder, kind, flags, &builder->expressions[from].from, to, out_id);
}

static Parsed_Expression* add_expression(Block_Builder* builder, Expression_Kind kind, flags32 flags, Expression from, Expression to, Expression* out_id)
{
    return add_expression(builder, kind, flags, &builder->expressions[from].from, &builder->expressions[to].to, out_id);
}

static Block* parse_statement_block(Token_Stream* stream, flags32 flags = 0);
static bool parse_statement_block(Token_Stream* stream, Block_Builder* builder, Block_Index* out_block, flags32 flags = 0);
static bool parse_expression(Token_Stream* stream, Block_Builder* builder, Expression* out_expression);

static bool parse_expression_leaf(Token_Stream* stream, Block_Builder* builder, Expression* out_expression)
{
    Token* start = stream->cursor;
    if (maybe_take_atom(stream, ATOM_LEFT_PARENTHESIS))
    {
        if (!parse_expression(stream, builder, out_expression))
            return false;
        if (!take_atom(stream, ATOM_RIGHT_PARENTHESIS, "Expected a closing ')' parenthesis."_s))
            return false;
        builder->expressions[*out_expression].flags |= EXPRESSION_IS_IN_PARENTHESES;
    }
    else if (maybe_take_atom(stream, ATOM_MINUS))
    {
        Expression unary_operand;
        if (!parse_expression_leaf(stream, builder, &unary_operand))
            return false;
        Parsed_Expression* expr = add_expression(builder, EXPRESSION_NEGATE, 0, start, unary_operand, out_expression);
        expr->unary_operand = unary_operand;
    }
    else if (maybe_take_atom(stream, ATOM_ZERO))  add_expression(builder, EXPRESSION_ZERO,  0, start, start, out_expression);
    else if (maybe_take_atom(stream, ATOM_TRUE))  add_expression(builder, EXPRESSION_TRUE,  0, start, start, out_expression);
    else if (maybe_take_atom(stream, ATOM_FALSE)) add_expression(builder, EXPRESSION_FALSE, 0, start, start, out_expression);
    else if (maybe_take_atom(stream, ATOM_INTEGER))
    {
        Parsed_Expression* expr = add_expression(builder, EXPRESSION_INTEGER_LITERAL, 0, start, start, out_expression);
        expr->literal = *start;
    }
    else if (maybe_take_atom(stream, ATOM_FIRST_IDENTIFIER))
    {
        if (maybe_take_atom(stream, ATOM_COLON))
        {
            For (builder->expressions)
            {
                Token* old_name = NULL;
                     if (it->kind == EXPRESSION_VARIABLE_DECLARATION) old_name = &it->variable_declaration.name;
                else if (it->kind == EXPRESSION_ALIAS_DECLARATION   ) old_name = &it->alias_declaration   .name;
                else if (it->kind == EXPRESSION_BLOCK_DECLARATION   ) old_name = &it->block_declaration   .name;
                if (old_name && old_name->atom == start->atom)
                {
                    String identifier = get_identifier(stream->ctx, start);

                    String old_source_token = get_source_token(stream->ctx, old_name);
                    String new_source_token = get_source_token(stream->ctx, start);

                    return ReportError(stream->ctx,
                        start, Format(temp, "Duplicate declaration of '%'.", identifier),
                        old_name, (old_source_token != new_source_token)
                            ? "Previously declared here. Keep in mind that multiple '_' characters are collapsed into one."_s
                            : "Previously declared here."_s);
                }
            }

            if (maybe_take_atom(stream, ATOM_LEFT_PARENTHESIS))
            {
                bool accepts_code_block = maybe_take_atom(stream, ATOM_CODE_BLOCK);
                if (!take_atom(stream, ATOM_RIGHT_PARENTHESIS, "Expected ')' after the block parameter list."_s))
                    return false;

                flags32 block_flags = 0;
                if (accepts_code_block)
                    block_flags |= BLOCK_HAS_CODE_BLOCK_PARAMETER;

                Block* block = parse_statement_block(stream, block_flags);
                if (!block)
                    return false;

                Parsed_Expression* expr = add_expression(builder, EXPRESSION_BLOCK_DECLARATION, 0, start, stream->cursor - 1, out_expression);
                expr->block_declaration.name = *start;
                expr->block_declaration.parsed_block = block;
            }
            else if (maybe_take_atom(stream, ATOM_COLON))
            {
                Expression value;
                if (!parse_expression(stream, builder, &value))
                    return false;

                Parsed_Expression* expr = add_expression(builder, EXPRESSION_ALIAS_DECLARATION, 0, start, value, out_expression);
                expr->alias_declaration.name = *start;
                expr->alias_declaration.value = value;
            }
            else if (maybe_take_atom(stream, ATOM_EQUAL))
            {
                Expression value;
                if (!parse_expression(stream, builder, &value))
                    return false;

                Parsed_Expression* expr = add_expression(builder, EXPRESSION_VARIABLE_DECLARATION, 0, start, value, out_expression);
                expr->variable_declaration.name = *start;
                expr->variable_declaration.parsed_type = INVALID_TYPE;
                expr->variable_declaration.value = value;
            }
            else
            {
                Type type;
                if (!parse_type(stream, &type))
                    return false;

                if (maybe_take_atom(stream, ATOM_EQUAL))
                {
                    Expression value;
                    if (!parse_expression(stream, builder, &value))
                        return false;

                    Parsed_Expression* expr = add_expression(builder, EXPRESSION_VARIABLE_DECLARATION, 0, start, value, out_expression);
                    expr->variable_declaration.name = *start;
                    expr->variable_declaration.parsed_type = type;
                    expr->variable_declaration.value = value;
                }
                else
                {
                    Parsed_Expression* expr = add_expression(builder, EXPRESSION_VARIABLE_DECLARATION, 0, start, stream->cursor - 1, out_expression);
                    expr->variable_declaration.name = *start;
                    expr->variable_declaration.parsed_type = type;
                    expr->variable_declaration.value = NO_EXPRESSION;
                }
            }
        }
        else
        {
            Parsed_Expression* expr = add_expression(builder, EXPRESSION_NAME, 0, start, start, out_expression);
            expr->name.token = *start;
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

        Parsed_Expression* expr = add_expression(builder, EXPRESSION_CAST, 0, start, stream->cursor - 1, out_expression);
        expr->cast.parsed_type = type;
        expr->cast.value = operand;
    }
    else
    {
        return ReportError(stream->ctx, next_token_or_eof(stream), "Expected an expression."_s);
    }

    while (true)
    {
        if (maybe_take_atom(stream, ATOM_LEFT_PARENTHESIS))
        {
            if (!take_atom(stream, ATOM_RIGHT_PARENTHESIS, "Expected ')' after the argument list."_s))
                return false;

            Block_Index block = NO_BLOCK;
            if (lookahead_atom(stream, ATOM_DO, 0) || lookahead_atom(stream, ATOM_LEFT_BRACE, 0))
                if (!parse_statement_block(stream, builder, &block))
                    return false;

            CompileTimeAssert(sizeof(Expression_List) == sizeof(u32));
            CompileTimeAssert(sizeof(Expression) == sizeof(u32));
            umm argument_count = 0;
            Expression_List* args = (Expression_List*) alloc<u32>(&stream->ctx->parser_memory, 1 + argument_count);
            args->count = argument_count;

            Expression lhs = *out_expression;
            Parsed_Expression* expr = add_expression(builder, EXPRESSION_CALL, 0, start, stream->cursor - 1, out_expression);
            expr->call.lhs       = lhs;
            expr->call.arguments = args;
            expr->call.block     = block;
        }
        else break;
    }

    return true;
}

static bool parse_expression(Token_Stream* stream, Block_Builder* builder, Expression* out_expression)
{
    Expression lhs;
    if (!parse_expression_leaf(stream, builder, &lhs))
        return false;

    Dynamic_Array<Expression_Kind> ops   = {};
    Dynamic_Array<Expression>      exprs = {};
    Defer(free_heap_array(&ops));
    Defer(free_heap_array(&exprs));

    auto should_pop = [](Expression_Kind lhs, Expression_Kind rhs)
    {
        auto priority = [](Expression_Kind kind) -> u32
        {
            switch (kind)
            {
            case EXPRESSION_ASSIGNMENT:       return 0;
            case EXPRESSION_EQUAL:            return 1;
            case EXPRESSION_NOT_EQUAL:        return 1;
            case EXPRESSION_GREATER_THAN:     return 2;
            case EXPRESSION_GREATER_OR_EQUAL: return 2;
            case EXPRESSION_LESS_THAN:        return 2;
            case EXPRESSION_LESS_OR_EQUAL:    return 2;
            case EXPRESSION_ADD:              return 3;
            case EXPRESSION_SUBTRACT:         return 3;
            case EXPRESSION_MULTIPLY:         return 4;
            case EXPRESSION_DIVIDE:           return 4;
            default:                          return U32_MAX;
            }
        };

        if (priority(lhs) > priority(rhs)) return true;
        if (priority(lhs) < priority(rhs)) return false;
        if (lhs == EXPRESSION_ASSIGNMENT && rhs == EXPRESSION_ASSIGNMENT)
            return false;
        return true;
    };

    auto pop = [&]()
    {
        Expression      binop_lhs  = exprs.address[exprs.count - 2];
        Expression      binop_rhs  = exprs.address[exprs.count - 1];
        Expression_Kind binop_kind = ops  .address[ops  .count - 1];
        exprs.count--;
        ops  .count--;

        Parsed_Expression* expr = add_expression(builder, binop_kind, 0, binop_lhs, binop_rhs, &exprs.address[exprs.count - 1]);
        expr->binary.lhs = binop_lhs;
        expr->binary.rhs = binop_rhs;
    };

    add_item(&exprs, &lhs);

    while (true)
    {
        Expression_Kind op;
             if (maybe_take_atom(stream, ATOM_EQUAL))         op = EXPRESSION_ASSIGNMENT;
        else if (maybe_take_atom(stream, ATOM_PLUS))          op = EXPRESSION_ADD;
        else if (maybe_take_atom(stream, ATOM_MINUS))         op = EXPRESSION_SUBTRACT;
        else if (maybe_take_atom(stream, ATOM_STAR))          op = EXPRESSION_MULTIPLY;
        else if (maybe_take_atom(stream, ATOM_SLASH))         op = EXPRESSION_DIVIDE;
        else if (maybe_take_atom(stream, ATOM_EQUAL))         op = EXPRESSION_EQUAL;
        else if (maybe_take_atom(stream, ATOM_BANG_EQUAL))    op = EXPRESSION_NOT_EQUAL;
        else if (maybe_take_atom(stream, ATOM_GREATER))       op = EXPRESSION_GREATER_THAN;
        else if (maybe_take_atom(stream, ATOM_GREATER_EQUAL)) op = EXPRESSION_GREATER_OR_EQUAL;
        else if (maybe_take_atom(stream, ATOM_LESS))          op = EXPRESSION_LESS_THAN;
        else if (maybe_take_atom(stream, ATOM_LESS_EQUAL))    op = EXPRESSION_LESS_OR_EQUAL;
        else break;

        Expression rhs;
        if (!parse_expression_leaf(stream, builder, &rhs)) return false;
        while (ops.count && should_pop(ops[ops.count - 1], op)) pop();
        add_item(&ops, &op);
        add_item(&exprs, &rhs);
    }

    while (ops.count) pop();
    assert(exprs.count == 1);
    *out_expression = exprs[0];
    return true;
}

static bool parse_statement_block(Token_Stream* stream, Block_Builder* builder, Block_Index* out_block, flags32 flags)
{
    Block* block = parse_statement_block(stream, flags);
    if (!block)
        return false;

    *out_block = (Block_Index) builder->children_blocks.count;

    Child_Block child = {};
    child.child = block;
    child.visibility_limit = (Statement) builder->statements.count;
    add_item(&builder->children_blocks, &child);
    return true;
}

static Token* semicolon_after_expression(Token_Stream* stream, Block_Builder* builder, Expression id)
{
    Parsed_Expression* expr = &builder->expressions[id];
    if (!(expr->flags & EXPRESSION_IS_IN_PARENTHESES))
    {
        if (expr->kind == EXPRESSION_BLOCK_DECLARATION)
            return stream->cursor - 1;

        if (expr->kind == EXPRESSION_CALL && expr->call.block != NO_BLOCK)
            return stream->cursor - 1;
    }
    Token* semicolon = stream->cursor;
    if (!take_atom(stream, ATOM_SEMICOLON, "Expected ';' after the expression."_s))
        return NULL;
    return semicolon;
}

static bool parse_statement(Token_Stream* stream, Block_Builder* builder)
{
    Token*  statement_start = stream->cursor;
    flags32 statement_flags = 0;
    // if (maybe_take_atom(stream, ATOM_DEFER))
    //     statement_flags |= STATEMENT_IS_DEFERRED;

    if (maybe_take_atom(stream, ATOM_IF))
    {
        if (!take_atom(stream, ATOM_LEFT_PARENTHESIS, "Expected a '(' before the condition."_s))
            return false;

        Expression expression;
        if (!parse_expression(stream, builder, &expression))
            return false;

        if (!take_atom(stream, ATOM_RIGHT_PARENTHESIS, "Expected a ')' after the condition."_s))
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
        if (!take_atom(stream, ATOM_LEFT_PARENTHESIS, "Expected a '(' before the condition."_s))
            return false;

        Expression expression;
        if (!parse_expression(stream, builder, &expression))
            return false;

        if (!take_atom(stream, ATOM_RIGHT_PARENTHESIS, "Expected a ')' after the condition."_s))
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

        Token* semicolon = semicolon_after_expression(stream, builder, expression);
        if (!semicolon) return false;

        Parsed_Statement* stmt = reserve_item(&builder->statements);
        stmt->kind       = STATEMENT_DEBUG_OUTPUT;
        stmt->flags      = statement_flags;
        stmt->from       = *statement_start;
        stmt->to         = *semicolon;
        stmt->expression = expression;
    }
    else if (maybe_take_atom(stream, ATOM_CODE_BLOCK))
    {
        Token* semicolon = stream->cursor;
        if (!take_atom(stream, ATOM_SEMICOLON, "Expected ';' after the 'code_block' keyword."_s))
            return false;

        Parsed_Statement* stmt = reserve_item(&builder->statements);
        stmt->kind  = STATEMENT_CODE_BLOCK_EXPANSION;
        stmt->flags = statement_flags;
        stmt->from  = *statement_start;
        stmt->to    = *semicolon;
    }
    else
    {
        Expression expression;
        if (!parse_expression(stream, builder, &expression))
            return false;

        Token* semicolon = semicolon_after_expression(stream, builder, expression);
        if (!semicolon) return false;

        Parsed_Statement* stmt = reserve_item(&builder->statements);
        stmt->kind       = STATEMENT_EXPRESSION;
        stmt->flags      = statement_flags;
        stmt->from       = *statement_start;
        stmt->to         = *semicolon;
        stmt->expression = expression;
    }

    return true;
}

static Block* parse_statement_block(Token_Stream* stream, flags32 flags)
{
    Compiler* ctx = stream->ctx;
    Token* block_start = stream->cursor;
    if (maybe_take_atom(stream, ATOM_LEFT_BRACE))
    {
        Block* block = PushValue(&ctx->parser_memory, Block);
        block->flags = flags;
        block->from = *block_start;

        Block_Builder builder = {};
        builder.block = block;
        Defer(finish_building(ctx, &builder));

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
        block->flags = flags;
        block->from = *block_start;

        Block_Builder builder = {};
        builder.block = block;
        Defer(finish_building(ctx, &builder));

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
