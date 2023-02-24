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



struct Block_Builder
{
    Block* block;
    Dynamic_Array<Parsed_Expression> expressions;
    Dynamic_Array<Expression> imperative_order;
};

static void finish_building(Compiler* ctx, Block_Builder* builder)
{
    Block* block = builder->block;
    Region* memory = &ctx->parser_memory;

    block->parsed_expressions = const_array(allocate_array(memory, &builder->expressions));
    block->imperative_order   = const_array(allocate_array(memory, &builder->imperative_order));
    free_heap_array(&builder->expressions);
    free_heap_array(&builder->imperative_order);
}

// IMPORTANT! Make sure to not use the returned pointer after calling add_expression() again!
static Parsed_Expression* add_expression(Block_Builder* builder, Expression_Kind kind, Token* from, Token* to, Expression* out_id)
{
    *out_id = (Expression) builder->expressions.count;
    Parsed_Expression* result = reserve_item(&builder->expressions);
    result->kind  = kind;
    result->flags = 0;
    result->from  = *from;
    result->to    = *to;
    result->visibility_limit = (Visibility) builder->imperative_order.count;
    return result;
}

static Parsed_Expression* add_expression(Block_Builder* builder, Expression_Kind kind, Token* from, Expression to, Expression* out_id)
{
    return add_expression(builder, kind, from, &builder->expressions[to].to, out_id);
}

static Parsed_Expression* add_expression(Block_Builder* builder, Expression_Kind kind, Expression from, Token* to, Expression* out_id)
{
    return add_expression(builder, kind, &builder->expressions[from].from, to, out_id);
}

static Parsed_Expression* add_expression(Block_Builder* builder, Expression_Kind kind, Expression from, Expression to, Expression* out_id)
{
    return add_expression(builder, kind, &builder->expressions[from].from, &builder->expressions[to].to, out_id);
}

static Block* parse_block(Token_Stream* stream, flags32 flags = 0);

static Expression_List* make_expression_list(Region* memory, umm count)
{
    CompileTimeAssert(sizeof(Expression_List) == sizeof(u32));
    CompileTimeAssert(sizeof(Expression) == sizeof(u32));
    Expression_List* args = (Expression_List*) alloc<u32>(memory, 1 + count);
    args->count = count;
    return args;
}

// Returns a call expression calling a block that's subscoped under the caller.
static bool parse_child_block(Token_Stream* stream, Block_Builder* builder, Expression* out_expression, flags32 flags = 0)
{
    Block* block = parse_block(stream, flags);
    if (!block)
        return false;

    Expression block_expr_id;
    Parsed_Expression* block_expr = add_expression(builder, EXPRESSION_BLOCK, &block->from, &block->to, &block_expr_id);
    block_expr->flags |= EXPRESSION_ALLOW_PARENT_SCOPE_VISIBILITY;
    block_expr->parsed_block = block;

    Expression call_expr_id;
    Parsed_Expression* call_expr = add_expression(builder, EXPRESSION_CALL, &block->from, &block->to, &call_expr_id);
    call_expr->call.lhs       = block_expr_id;
    call_expr->call.arguments = make_expression_list(&stream->ctx->parser_memory, 0);
    call_expr->call.block     = NO_EXPRESSION;

    *out_expression = call_expr_id;
    return true;
}

static bool semicolon_after_statement(Token_Stream* stream)
{
    if (stream->cursor - 1 >= stream->start)
    {
        Token* previous = stream->cursor - 1;
        if (previous->atom == ATOM_SEMICOLON)   return true;
        if (previous->atom == ATOM_RIGHT_BRACE) return true;
    }
    Token* semicolon = stream->cursor;
    if (!take_atom(stream, ATOM_SEMICOLON, "Expected ';' after the statement."_s))
        return false;
    return true;
}

static bool parse_expression(Token_Stream* stream, Block_Builder* builder, Expression* out_expression);

static bool parse_expression_leaf(Token_Stream* stream, Block_Builder* builder, Expression* out_expression)
{
    Token* start = stream->cursor;
    auto make_unary = [&](Expression_Kind kind) -> bool
    {
        Expression unary_operand;
        if (!parse_expression_leaf(stream, builder, &unary_operand))
            return false;
        Parsed_Expression* expr = add_expression(builder, kind, start, unary_operand, out_expression);
        expr->unary_operand = unary_operand;
        return true;
    };

    auto make_type_literal = [&](Type type)
    {
        add_expression(builder, EXPRESSION_TYPE_LITERAL, start, start, out_expression)->parsed_type = type;
    };

    if (maybe_take_atom(stream, ATOM_LEFT_PARENTHESIS))
    {
        // Significant lookahead ahead:
        // Whenever we encounter a '(' token, we scan forward to see if we can find '=>' or '{''
        // If we can, then parse this as a block expression.
        // Otherwise, it's just a normal parenthesized expression
        bool parse_as_a_block = false;
        {
            umm open_parents_counter = 1;
            umm lookahead = 0;
            while (open_parents_counter && (stream->cursor + lookahead < stream->end))
            {
                Atom atom = stream->cursor[lookahead++].atom;
                     if (atom == ATOM_LEFT_PARENTHESIS)  open_parents_counter++;
                else if (atom == ATOM_RIGHT_PARENTHESIS) open_parents_counter--;
            }
            if (!open_parents_counter && (stream->cursor + lookahead < stream->end))
            {
                Atom atom = stream->cursor[lookahead].atom;
                if (atom == ATOM_EQUAL_GREATER || atom == ATOM_LEFT_BRACE)
                    parse_as_a_block = true;
            }
        }

        if (parse_as_a_block)
        {
            Block* parameter_block;
            {
                parameter_block = PushValue(&stream->ctx->parser_memory, Block);
                parameter_block->flags = BLOCK_IS_PARAMETER_BLOCK;
                parameter_block->from = *start;

                Block_Builder parameter_builder = {};
                parameter_builder.block = parameter_block;
                Defer(finish_building(stream->ctx, &parameter_builder));

                bool first_parameter = true;
                while (!maybe_take_atom(stream, ATOM_RIGHT_PARENTHESIS))
                {
                    if (first_parameter) first_parameter = false;
                    else if (!take_atom(stream, ATOM_COMMA, "Expected ',' between parameters."_s))
                        return false;

                    Token* name = stream->cursor;
                    if (!take_atom(stream, ATOM_FIRST_IDENTIFIER, "Expected a parameter name."_s))
                        return false;
                    if (!take_atom(stream, ATOM_COLON, "Expected ':' between the parameter name and type."_s))
                        return false;

                    Expression type;
                    if (!parse_expression(stream, &parameter_builder, &type))
                        return false;

                    Expression decl_expr_id;
                    Parsed_Expression* decl_expr = add_expression(&parameter_builder, EXPRESSION_VARIABLE_DECLARATION, name, stream->cursor - 1, &decl_expr_id);
                    decl_expr->flags |= EXPRESSION_IS_PARAMETER;
                    decl_expr->variable_declaration.name  = *name;
                    decl_expr->variable_declaration.type  = type;
                    decl_expr->variable_declaration.value = NO_EXPRESSION;
                    add_item(&builder->imperative_order, &decl_expr_id);
                }
                parameter_block->to = *(stream->cursor - 1);

                // bool accepts_block = maybe_take_atom(stream, ATOM_BLOCK);
                // if (!take_atom(stream, ATOM_RIGHT_PARENTHESIS, "Expected ')' after the block parameter list."_s))
                //     return false;

                // if (accepts_block)
                //     block_flags |= BLOCK_HAS_BLOCK_PARAMETER;

                Expression call;
                if (!parse_child_block(stream, &parameter_builder, &call))
                    return false;
                add_item(&builder->imperative_order, &call);
            }

            Parsed_Expression* expr = add_expression(builder, EXPRESSION_BLOCK, start, stream->cursor - 1, out_expression);
            expr->parsed_block = parameter_block;
        }
        else
        {
            if (!parse_expression(stream, builder, out_expression))
                return false;
            if (!take_atom(stream, ATOM_RIGHT_PARENTHESIS, "Expected a closing ')' parenthesis."_s))
                return false;
            builder->expressions[*out_expression].flags |= EXPRESSION_IS_IN_PARENTHESES;
        }
    }
    else if (maybe_take_atom(stream, ATOM_MINUS))     { if (!make_unary(EXPRESSION_NEGATE))      return false; }
    else if (maybe_take_atom(stream, ATOM_AMPERSAND)) { if (!make_unary(EXPRESSION_ADDRESS))     return false; }
    else if (maybe_take_atom(stream, ATOM_STAR))      { if (!make_unary(EXPRESSION_DEREFERENCE)) return false; }
    else if (maybe_take_atom(stream, ATOM_ZERO))  add_expression(builder, EXPRESSION_ZERO,  start, start, out_expression);
    else if (maybe_take_atom(stream, ATOM_TRUE))  add_expression(builder, EXPRESSION_TRUE,  start, start, out_expression);
    else if (maybe_take_atom(stream, ATOM_FALSE)) add_expression(builder, EXPRESSION_FALSE, start, start, out_expression);
    else if (maybe_take_atom(stream, ATOM_INTEGER))
    {
        Parsed_Expression* expr = add_expression(builder, EXPRESSION_INTEGER_LITERAL, start, start, out_expression);
        expr->literal = *start;
    }
    else if (maybe_take_atom(stream, ATOM_VOID))   make_type_literal(TYPE_VOID);
    else if (maybe_take_atom(stream, ATOM_U8))     make_type_literal(TYPE_U8);
    else if (maybe_take_atom(stream, ATOM_U16))    make_type_literal(TYPE_U16);
    else if (maybe_take_atom(stream, ATOM_U32))    make_type_literal(TYPE_U32);
    else if (maybe_take_atom(stream, ATOM_U64))    make_type_literal(TYPE_U64);
    else if (maybe_take_atom(stream, ATOM_S8))     make_type_literal(TYPE_S8);
    else if (maybe_take_atom(stream, ATOM_S16))    make_type_literal(TYPE_S16);
    else if (maybe_take_atom(stream, ATOM_S32))    make_type_literal(TYPE_S32);
    else if (maybe_take_atom(stream, ATOM_S64))    make_type_literal(TYPE_S64);
    else if (maybe_take_atom(stream, ATOM_F16))    make_type_literal(TYPE_F16);
    else if (maybe_take_atom(stream, ATOM_F32))    make_type_literal(TYPE_F32);
    else if (maybe_take_atom(stream, ATOM_F64))    make_type_literal(TYPE_F64);
    else if (maybe_take_atom(stream, ATOM_BOOL8))  make_type_literal(TYPE_BOOL8);
    else if (maybe_take_atom(stream, ATOM_BOOL16)) make_type_literal(TYPE_BOOL16);
    else if (maybe_take_atom(stream, ATOM_BOOL32)) make_type_literal(TYPE_BOOL32);
    else if (maybe_take_atom(stream, ATOM_BOOL64)) make_type_literal(TYPE_BOOL64);
    else if (maybe_take_atom(stream, ATOM_TYPE))   make_type_literal(TYPE_TYPE);
    else if (maybe_take_atom(stream, ATOM_FIRST_IDENTIFIER))
    {
        if (maybe_take_atom(stream, ATOM_COLON))
        {
            For (builder->expressions)
            {
                Token* old_name = NULL;
                     if (it->kind == EXPRESSION_VARIABLE_DECLARATION) old_name = &it->variable_declaration.name;
                else if (it->kind == EXPRESSION_ALIAS_DECLARATION   ) old_name = &it->alias_declaration   .name;
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

            if (maybe_take_atom(stream, ATOM_COLON))
            {
                Expression value;
                if (!parse_expression(stream, builder, &value))
                    return false;

                Parsed_Expression* expr = add_expression(builder, EXPRESSION_ALIAS_DECLARATION, start, value, out_expression);
                expr->alias_declaration.name = *start;
                expr->alias_declaration.value = value;
            }
            else if (maybe_take_atom(stream, ATOM_EQUAL))
            {
                Expression value;
                if (!parse_expression(stream, builder, &value))
                    return false;

                Parsed_Expression* expr = add_expression(builder, EXPRESSION_VARIABLE_DECLARATION, start, value, out_expression);
                expr->variable_declaration.name = *start;
                expr->variable_declaration.type = NO_EXPRESSION;
                expr->variable_declaration.value = value;
            }
            else
            {
                Expression type;
                if (!parse_expression_leaf(stream, builder, &type))
                    return false;

                if (maybe_take_atom(stream, ATOM_EQUAL))
                {
                    Expression value;
                    if (!parse_expression(stream, builder, &value))
                        return false;

                    Parsed_Expression* expr = add_expression(builder, EXPRESSION_VARIABLE_DECLARATION, start, value, out_expression);
                    expr->variable_declaration.name = *start;
                    expr->variable_declaration.type = type;
                    expr->variable_declaration.value = value;
                }
                else
                {
                    Parsed_Expression* expr = add_expression(builder, EXPRESSION_VARIABLE_DECLARATION, start, stream->cursor - 1, out_expression);
                    expr->variable_declaration.name = *start;
                    expr->variable_declaration.type = type;
                    expr->variable_declaration.value = NO_EXPRESSION;
                }
            }
        }
        else
        {
            Parsed_Expression* expr = add_expression(builder, EXPRESSION_NAME, start, start, out_expression);
            expr->name.token = *start;
        }
    }
    else if (maybe_take_atom(stream, ATOM_CAST))
    {
        if (!take_atom(stream, ATOM_LEFT_PARENTHESIS, "Expected '(' after the 'cast' keyword."_s))
            return false;
        Expression lhs;
        if (!parse_expression(stream, builder, &lhs))
            return false;
        if (!take_atom(stream, ATOM_COMMA, "Expected ',' between the operands to 'cast'."_s))
            return false;
        Expression rhs;
        if (!parse_expression(stream, builder, &rhs))
            return false;
        if (!take_atom(stream, ATOM_RIGHT_PARENTHESIS, "Expected ')' after the second operand to 'cast'."_s))
            return false;

        Parsed_Expression* expr = add_expression(builder, EXPRESSION_CAST, start, stream->cursor - 1, out_expression);
        expr->binary.lhs = lhs;
        expr->binary.rhs = rhs;
    }
    else if (maybe_take_atom(stream, ATOM_IF))
    {
        if (!take_atom(stream, ATOM_LEFT_PARENTHESIS, "Expected a '(' before the condition."_s))
            return false;

        Expression expression;
        if (!parse_expression(stream, builder, &expression))
            return false;

        if (!take_atom(stream, ATOM_RIGHT_PARENTHESIS, "Expected a ')' after the condition."_s))
            return false;

        Expression if_true;
        if (!parse_child_block(stream, builder, &if_true))
            return false;
        if (!semicolon_after_statement(stream))
            return false;

        Expression if_false = NO_EXPRESSION;
        if (maybe_take_atom(stream, ATOM_ELSE))
            if (!parse_child_block(stream, builder, &if_false))
                return false;

        Parsed_Expression* expr = add_expression(builder, EXPRESSION_BRANCH, start, stream->cursor - 1, out_expression);
        expr->branch.condition  = expression;
        expr->branch.on_success = if_true;
        expr->branch.on_failure = if_false;
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

        Expression if_true;
        if (!parse_child_block(stream, builder, &if_true))
            return false;

        Parsed_Expression* expr = add_expression(builder, EXPRESSION_BRANCH, start, stream->cursor - 1, out_expression);
        expr->flags |= EXPRESSION_BRANCH_IS_LOOP;
        expr->branch.condition  = expression;
        expr->branch.on_success = if_true;
        expr->branch.on_failure = NO_EXPRESSION;
    }
    else if (maybe_take_atom(stream, ATOM_DEBUG))
    {
        Expression expression;
        if (!parse_expression(stream, builder, &expression))
            return false;

        Parsed_Expression* expr = add_expression(builder, EXPRESSION_DEBUG, start, stream->cursor - 1, out_expression);
        expr->unary_operand = expression;
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

            Expression block = NO_EXPRESSION;
            if (lookahead_atom(stream, ATOM_EQUAL_GREATER, 0) || lookahead_atom(stream, ATOM_LEFT_BRACE, 0))
                if (!parse_child_block(stream, builder, &block))
                    return false;

            Expression_List* args = make_expression_list(&stream->ctx->parser_memory, 0);

            Expression lhs = *out_expression;
            Parsed_Expression* expr = add_expression(builder, EXPRESSION_CALL, start, stream->cursor - 1, out_expression);
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

        Parsed_Expression* expr = add_expression(builder, binop_kind, binop_lhs, binop_rhs, &exprs.address[exprs.count - 1]);
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

static bool parse_statement(Token_Stream* stream, Block_Builder* builder)
{
    Expression expression;
    if (!parse_expression(stream, builder, &expression))
        return false;
    add_item(&builder->imperative_order, &expression);
    return true;
}

static Block* parse_block(Token_Stream* stream, flags32 flags)
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
        {
            if (!parse_statement(stream, &builder)) return NULL;
            if (!semicolon_after_statement(stream)) return NULL;
        }
        Token* block_end = stream->cursor;
        if (!take_atom(stream, ATOM_RIGHT_BRACE, "Body was not closed by '}'.\n"
                                                 "(Check for mismatched braces.)"_s))
            return NULL;
        block->to = *block_end;

        return block;
    }
    else if (maybe_take_atom(stream, ATOM_EQUAL_GREATER))
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

    Block* block = parse_block(stream);
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
