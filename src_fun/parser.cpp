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
    String imports_relative_to_path;
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
    block_expr->parsed_block = block;

    Expression call_expr_id;
    Parsed_Expression* call_expr = add_expression(builder, EXPRESSION_CALL, &block->from, &block->to, &call_expr_id);
    call_expr->flags |= EXPRESSION_ALLOW_PARENT_SCOPE_VISIBILITY;
    call_expr->call.lhs       = block_expr_id;
    call_expr->call.arguments = make_expression_list(&stream->ctx->parser_memory, 0);

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

static bool expression_can_be_followed_by_operators(Parsed_Expression const* expr)
{
    if (expr->flags & EXPRESSION_IS_IN_PARENTHESES) return true;
    if (expr->kind == EXPRESSION_BLOCK) return false;
    if (expr->kind == EXPRESSION_DECLARATION) return false;
    return true;
}

typedef flags32 Parse_Flags;
enum: Parse_Flags
{
    PARSE_ALLOW_BLOCKS              = 0x0001,
    PARSE_ALLOW_INFERRED_TYPE_ALIAS = 0x0002,
};

static bool parse_expression(Token_Stream* stream, Block_Builder* builder, Expression* out_expression, Parse_Flags parse_flags);

static bool parse_expression_leaf(Token_Stream* stream, Block_Builder* builder, Expression* out_expression, Parse_Flags parse_flags)
{
    *out_expression = NO_EXPRESSION;

    Parse_Flags original_parse_flags = parse_flags;
    parse_flags &= ~PARSE_ALLOW_BLOCKS;
    parse_flags &= ~PARSE_ALLOW_INFERRED_TYPE_ALIAS;
#define InheritFlags(to_inherit) (parse_flags | (original_parse_flags & (to_inherit)))

    Token* start = stream->cursor;
    auto make_unary = [&](Expression_Kind kind, u32 next_parse_flags) -> bool
    {
        Expression unary_operand;
        if (!parse_expression_leaf(stream, builder, &unary_operand, next_parse_flags))
            return false;
        auto* operand_expr = &builder->expressions[unary_operand];

        Parsed_Expression* expr = add_expression(builder, kind, start, unary_operand, out_expression);
        expr->flags |= (operand_expr->flags & EXPRESSION_HAS_TO_BE_EXTERNALLY_INFERRED);
        expr->unary_operand = unary_operand;
        return true;
    };

    auto make_type_literal = [&](Type type)
    {
        add_expression(builder, EXPRESSION_TYPE_LITERAL, start, start, out_expression)->parsed_type = type;
    };

    if (maybe_take_atom(stream, ATOM_LEFT_PARENTHESIS))
    {
        auto is_this_probably_block_syntax = [&]()
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
                if (atom == ATOM_EQUAL_GREATER || atom == ATOM_LEFT_BRACE || atom == ATOM_MINUS_GREATER)
                    return true;
            }
            return false;
        };

        if ((original_parse_flags & PARSE_ALLOW_BLOCKS) && is_this_probably_block_syntax())
        {
            Block* parameter_block;
            {
                enum {} builder;  // don't use builder in this scope

                parameter_block = PushValue(&stream->ctx->parser_memory, Block);
                parameter_block->flags = BLOCK_IS_PARAMETER_BLOCK;
                parameter_block->from = *start;

                Block_Builder parameter_builder = {};
                parameter_builder.block = parameter_block;
                Defer(finish_building(stream->ctx, &parameter_builder));

                // Parse parameter declarations and add them to the scope
                bool first_parameter = true;
                while (!maybe_take_atom(stream, ATOM_RIGHT_PARENTHESIS))
                {
                    if (first_parameter) first_parameter = false;
                    else if (!take_atom(stream, ATOM_COMMA, "Expected ',' between parameters."_s))
                        return false;

                    bool is_baked = maybe_take_atom(stream, ATOM_DOLLAR);

                    Token* name = stream->cursor;
                    if (!take_atom(stream, ATOM_FIRST_IDENTIFIER, "Expected a parameter name."_s))
                        return false;
                    if (!take_atom(stream, ATOM_COLON, "Expected ':' between the parameter name and type."_s))
                        return false;

                    Expression type;
                    if (maybe_take_atom(stream, ATOM_BLOCK))
                    {
                        Token* block_token = stream->cursor - 1;
                        if (!is_baked)
                            return ReportError(stream->ctx, block_token, "The 'block' parameter must be baked. Put '$' in front of the parameter name."_s);
                        add_expression(&parameter_builder, EXPRESSION_TYPE_LITERAL, block_token, block_token, &type)->parsed_type = TYPE_SOFT_BLOCK;
                    }
                    else
                    {
                        if (!parse_expression_leaf(stream, &parameter_builder, &type, PARSE_ALLOW_INFERRED_TYPE_ALIAS))
                            return false;
                    }

                    Expression decl_expr_id;
                    Parsed_Expression* decl_expr = add_expression(&parameter_builder, EXPRESSION_DECLARATION, name, stream->cursor - 1, &decl_expr_id);
                    decl_expr->flags |= EXPRESSION_DECLARATION_IS_PARAMETER;
                    if (parameter_builder.expressions[type].flags & EXPRESSION_HAS_TO_BE_EXTERNALLY_INFERRED)
                    {
                        if (is_baked)
                            return ReportError(stream->ctx, decl_expr, "A parameter can't have an inferred type if it's baked, because baked values don't have assigned runtime types."_s);
                        decl_expr->flags |= EXPRESSION_HAS_TO_BE_EXTERNALLY_INFERRED;
                    }
                    if (is_baked)
                        decl_expr->flags |= EXPRESSION_DECLARATION_IS_ALIAS | EXPRESSION_HAS_TO_BE_EXTERNALLY_INFERRED;
                    decl_expr->declaration.name  = *name;
                    decl_expr->declaration.type  = type;
                    decl_expr->declaration.value = NO_EXPRESSION;
                    add_item(&parameter_builder.imperative_order, &decl_expr_id);
                }

                // Parse the return type
                Token*     return_from = stream->cursor;
                Block*     return_block = NULL;
                if (maybe_take_atom(stream, ATOM_MINUS_GREATER) && !maybe_take_atom(stream, ATOM_VOID))
                {
                    if (!take_atom(stream, ATOM_LEFT_PARENTHESIS, "Expected 'void' or '(' after '->' to begin the return type."_s))
                        return false;

                    return_block = PushValue(&stream->ctx->parser_memory, Block);
                    return_block->flags = BLOCK_IS_UNIT | BLOCK_HAS_STRUCTURE_PLACEMENT;
                    return_block->from = *return_from;

                    enum {} parameter_builder;  // don't use parameter_builder in this scope

                    Block_Builder return_builder = {};
                    return_builder.block = return_block;
                    Defer(finish_building(stream->ctx, &return_builder));

                    bool first_member = true;
                    while (!maybe_take_atom(stream, ATOM_RIGHT_PARENTHESIS))
                    {
                        if (first_member) first_member = false;
                        else if (!take_atom(stream, ATOM_COMMA, "Expected ',' between declarations."_s))
                            return false;

                        Token* name = stream->cursor;
                        if (!take_atom(stream, ATOM_FIRST_IDENTIFIER, "Expected a name."_s))
                            return false;
                        if (!take_atom(stream, ATOM_COLON, "Expected ':' between the name and type."_s))
                            return false;

                        Expression type;
                        if (!parse_expression_leaf(stream, &return_builder, &type, 0))
                            return false;

                        Expression decl_expr_id;
                        Parsed_Expression* decl_expr = add_expression(&return_builder, EXPRESSION_DECLARATION, name, stream->cursor - 1, &decl_expr_id);
                        decl_expr->declaration.name  = *name;
                        decl_expr->declaration.type  = type;
                        decl_expr->declaration.value = NO_EXPRESSION;
                        add_item(&return_builder.imperative_order, &decl_expr_id);
                    }

                    return_block->to = *(stream->cursor - 1);
                }
                Token* return_to = (return_from == stream->cursor) ? stream->cursor : stream->cursor - 1;

                Expression return_type;
                if (return_block)
                    add_expression(&parameter_builder, EXPRESSION_UNIT, return_from, return_to, &return_type)
                        ->parsed_block = return_block;
                else
                    add_expression(&parameter_builder, EXPRESSION_TYPE_LITERAL, return_from, return_to, &return_type)
                        ->parsed_type = TYPE_VOID;

                // And the return declaration to the scope
                {
                    Expression decl_expr_id;
                    Parsed_Expression* decl_expr = add_expression(&parameter_builder, EXPRESSION_DECLARATION, return_from, return_to, &decl_expr_id);
                    decl_expr->flags |= EXPRESSION_DECLARATION_IS_RETURN | EXPRESSION_DECLARATION_IS_USING;
                    decl_expr->declaration.name  = {};  // return declarations don't have a name
                    decl_expr->declaration.type  = return_type;
                    decl_expr->declaration.value = NO_EXPRESSION;
                    add_item(&parameter_builder.imperative_order, &decl_expr_id);
                }

                parameter_block->to = *(stream->cursor - 1);

                // Add either a call expression or an intrinsic expression
                if (lookahead_atom(stream, ATOM_LEFT_BRACE,  0) &&
                    lookahead_atom(stream, ATOM_RIGHT_BRACE, 1) &&
                    lookahead_atom(stream, ATOM_INTRINSIC,   2))
                {
                    stream->cursor += 3;
                    Token* name = stream->cursor;
                    if (!take_atom(stream, ATOM_STRING_LITERAL, "Expected the intrinsic name after 'intrinsic'."_s))
                        return false;

                    Expression intrinsic_expr_id;
                    Parsed_Expression* intrinsic_expr = add_expression(&parameter_builder, EXPRESSION_INTRINSIC, name - 1, name, &intrinsic_expr_id);
                    intrinsic_expr->intrinsic_name = *name;
                    add_item(&parameter_builder.imperative_order, &intrinsic_expr_id);
                }
                else
                {
                    Expression call;
                    if (!parse_child_block(stream, &parameter_builder, &call))
                        return false;
                    add_item(&parameter_builder.imperative_order, &call);
                }
            }

            Parsed_Expression* expr = add_expression(builder, EXPRESSION_BLOCK, start, stream->cursor - 1, out_expression);
            expr->parsed_block = parameter_block;
        }
        else
        {
            if (lookahead_atom(stream, ATOM_RIGHT_PARENTHESIS, 0) && is_this_probably_block_syntax())
                return ReportError(stream->ctx, next_token_or_eof(stream),
                        "Expected an expression.\n"
                        "It looks like you're trying to use block syntax, however it's not enabled in the current parsing context.\n"
                        "Place parentheses around the block to allow this."_s);

            if (!parse_expression(stream, builder, out_expression, InheritFlags(PARSE_ALLOW_INFERRED_TYPE_ALIAS) | PARSE_ALLOW_BLOCKS))
                return false;

            if (lookahead_atom(stream, ATOM_COMMA, 0) && is_this_probably_block_syntax())
                return ReportError(stream->ctx, next_token_or_eof(stream),
                        "Expected a closing ')' parenthesis.\n"
                        "It looks like you're trying to use block syntax, however it's not enabled in the current parsing context.\n"
                        "Place parentheses around the block to allow this."_s);

            if (!take_atom(stream, ATOM_RIGHT_PARENTHESIS, "Expected a closing ')' parenthesis."_s))
                return false;
            builder->expressions[*out_expression].flags |= EXPRESSION_IS_IN_PARENTHESES;
        }
    }
    else if (maybe_take_atom(stream, ATOM_IMPORT) || maybe_take_atom(stream, ATOM_RUN) || maybe_take_atom(stream, ATOM_UNIT) || maybe_take_atom(stream, ATOM_STRUCT))
    {
        bool is_struct = (start->atom == ATOM_STRUCT);
        bool is_import = (start->atom == ATOM_IMPORT);
        bool is_run    = (start->atom == ATOM_RUN);

        Block* block;
        if (is_import)
        {
            Token* name = stream->cursor;
            if (!take_atom(stream, ATOM_STRING_LITERAL, "Expected a string literal as the import path after 'import'."_s))
                return false;

            Token_Info_String* info = (Token_Info_String*) get_token_info(stream->ctx, name);
            // @Reconsider - check if path seems malicious
            String path = concatenate_path(temp, stream->imports_relative_to_path, info->value);

            block = parse_top_level_from_file(stream->ctx, path);
        }
        else
        {
            block = parse_block(stream, BLOCK_IS_UNIT | (is_struct ? BLOCK_HAS_STRUCTURE_PLACEMENT : 0));
        }
        if (!block)
            return false;

        Parsed_Expression* expr = add_expression(builder, is_run ? EXPRESSION_RUN : EXPRESSION_UNIT, start, stream->cursor - 1, out_expression);
        if (is_import) expr->flags |= EXPRESSION_UNIT_IS_IMPORT;
        expr->parsed_block = block;
    }
    else if (maybe_take_atom(stream, ATOM_MINUS))       { if (!make_unary(EXPRESSION_NEGATE,      parse_flags))                                   return false; }
    else if (maybe_take_atom(stream, ATOM_AMPERSAND))   { if (!make_unary(EXPRESSION_ADDRESS,     InheritFlags(PARSE_ALLOW_INFERRED_TYPE_ALIAS))) return false; }
    else if (maybe_take_atom(stream, ATOM_STAR))        { if (!make_unary(EXPRESSION_DEREFERENCE, InheritFlags(PARSE_ALLOW_INFERRED_TYPE_ALIAS))) return false; }
    else if (maybe_take_atom(stream, ATOM_SIZEOF))      { if (!make_unary(EXPRESSION_SIZEOF,      parse_flags))                                   return false; }
    else if (maybe_take_atom(stream, ATOM_ALIGNOF))     { if (!make_unary(EXPRESSION_ALIGNOF,     parse_flags))                                   return false; }
    else if (maybe_take_atom(stream, ATOM_CODEOF))      { if (!make_unary(EXPRESSION_CODEOF,      parse_flags))                                   return false; }
    else if (maybe_take_atom(stream, ATOM_DEBUG))       { if (!make_unary(EXPRESSION_DEBUG,       parse_flags))                                   return false; }
    else if (maybe_take_atom(stream, ATOM_DEBUG_ALLOC)) { if (!make_unary(EXPRESSION_DEBUG_ALLOC, parse_flags))                                   return false; }
    else if (maybe_take_atom(stream, ATOM_DEBUG_FREE))  { if (!make_unary(EXPRESSION_DEBUG_FREE,  parse_flags))                                   return false; }
    else if (maybe_take_atom(stream, ATOM_ZERO))  add_expression(builder, EXPRESSION_ZERO,  start, start, out_expression);
    else if (maybe_take_atom(stream, ATOM_TRUE))  add_expression(builder, EXPRESSION_TRUE,  start, start, out_expression);
    else if (maybe_take_atom(stream, ATOM_FALSE)) add_expression(builder, EXPRESSION_FALSE, start, start, out_expression);
    else if (maybe_take_atom(stream, ATOM_NUMBER_LITERAL))
    {
        Parsed_Expression* expr = add_expression(builder, EXPRESSION_NUMERIC_LITERAL, start, start, out_expression);
        expr->literal = *start;
    }
    else if (maybe_take_atom(stream, ATOM_STRING_LITERAL))
    {
        Parsed_Expression* expr = add_expression(builder, EXPRESSION_STRING_LITERAL, start, start, out_expression);
        expr->literal = *start;
    }
    else if (maybe_take_atom(stream, ATOM_VOID))   make_type_literal(TYPE_VOID);
    else if (maybe_take_atom(stream, ATOM_U8))     make_type_literal(TYPE_U8);
    else if (maybe_take_atom(stream, ATOM_U16))    make_type_literal(TYPE_U16);
    else if (maybe_take_atom(stream, ATOM_U32))    make_type_literal(TYPE_U32);
    else if (maybe_take_atom(stream, ATOM_U64))    make_type_literal(TYPE_U64);
    else if (maybe_take_atom(stream, ATOM_UMM))    make_type_literal(TYPE_UMM);
    else if (maybe_take_atom(stream, ATOM_S8))     make_type_literal(TYPE_S8);
    else if (maybe_take_atom(stream, ATOM_S16))    make_type_literal(TYPE_S16);
    else if (maybe_take_atom(stream, ATOM_S32))    make_type_literal(TYPE_S32);
    else if (maybe_take_atom(stream, ATOM_S64))    make_type_literal(TYPE_S64);
    else if (maybe_take_atom(stream, ATOM_SMM))    make_type_literal(TYPE_SMM);
    else if (maybe_take_atom(stream, ATOM_F16))    make_type_literal(TYPE_F16);
    else if (maybe_take_atom(stream, ATOM_F32))    make_type_literal(TYPE_F32);
    else if (maybe_take_atom(stream, ATOM_F64))    make_type_literal(TYPE_F64);
    else if (maybe_take_atom(stream, ATOM_BOOL))   make_type_literal(TYPE_BOOL);
    else if (maybe_take_atom(stream, ATOM_TYPE))   make_type_literal(TYPE_TYPE);
    else if (maybe_take_atom(stream, ATOM_STRING)) make_type_literal(TYPE_STRING);
    else if (maybe_take_atom(stream, ATOM_BLOCK))  return ReportError(stream->ctx, start, "'block' can only be used as a type for parameters."_s);
    else if (maybe_take_atom(stream, ATOM_USING))
    {
        if (!take_atom(stream, ATOM_FIRST_IDENTIFIER, "Expected an identifier after 'using'."_s))
            return false;
        if (!take_atom(stream, ATOM_COLON, "Expected ':' after the used name."_s))
            return false;
        goto parse_using;
    }
    else if (maybe_take_atom(stream, ATOM_FIRST_IDENTIFIER))
    {
        if (maybe_take_atom(stream, ATOM_COLON))
        {
            bool is_using;
            if (false) parse_using:
                is_using = true;
            else
                is_using = false;

            Token* name = start + (is_using ? 1 : 0);

            For (builder->expressions)
            {
                if (it->kind != EXPRESSION_DECLARATION) continue;
                Token* old_name = &it->declaration.name;
                if (old_name->atom != name->atom) continue;

                String identifier = get_identifier(stream->ctx, name);

                String old_source_token = get_source_token(stream->ctx, old_name);
                String new_source_token = get_source_token(stream->ctx, name);

                return Report(stream->ctx)
                    .part(name, Format(temp, "Duplicate declaration of '%'.", identifier))
                    .part(old_name, (old_source_token != new_source_token)
                        ? "Previously declared here. Keep in mind that multiple '_' characters are collapsed into one."_s
                        : "Previously declared here."_s)
                    .done();
            }

            flags32    flags = EXPRESSION_DECLARATION_IS_ORDERED;
            bool       alias = false;
            Expression type  = NO_EXPRESSION;
            Expression value = NO_EXPRESSION;

            if (is_using)
                flags |= EXPRESSION_DECLARATION_IS_USING;

            if (maybe_take_atom(stream, ATOM_COLON))
            {
                flags &= ~EXPRESSION_DECLARATION_IS_ORDERED;
                flags |=  EXPRESSION_DECLARATION_IS_ALIAS;

                if (maybe_take_atom(stream, ATOM_UNDERSCORE))
                    return ReportError(stream->ctx, stream->cursor - 1, "Can't have an uninitialized alias declaration."_s);

                if (!parse_expression(stream, builder, &value, InheritFlags(PARSE_ALLOW_BLOCKS | PARSE_ALLOW_INFERRED_TYPE_ALIAS)))
                    return false;
                if (builder->expressions[value].flags & EXPRESSION_HAS_TO_BE_EXTERNALLY_INFERRED)
                    flags |= EXPRESSION_HAS_TO_BE_EXTERNALLY_INFERRED;
            }
            else if (maybe_take_atom(stream, ATOM_EQUAL))
            {
                if (maybe_take_atom(stream, ATOM_UNDERSCORE))
                    return ReportError(stream->ctx, stream->cursor - 1, "Can't have an uninitialized variable without a type."_s);

                if (!parse_expression(stream, builder, &value, parse_flags))
                    return false;
            }
            else
            {
                if (!parse_expression_leaf(stream, builder, &type, parse_flags | PARSE_ALLOW_INFERRED_TYPE_ALIAS))
                    return false;

                if (maybe_take_atom(stream, ATOM_EQUAL))
                {
                    if (maybe_take_atom(stream, ATOM_UNDERSCORE))
                    {
                        flags |= EXPRESSION_DECLARATION_IS_UNINITIALIZED;
                    }
                    else
                    {
                        if (!parse_expression(stream, builder, &value, parse_flags))
                            return false;
                    }
                }
            }

            Parsed_Expression* expr = add_expression(builder, EXPRESSION_DECLARATION, start, stream->cursor - 1, out_expression);
            expr->flags |= flags;
            expr->declaration.name  = *name;
            expr->declaration.type  = type;
            expr->declaration.value = value;
        }
        else
        {
            Parsed_Expression* expr = add_expression(builder, EXPRESSION_NAME, start, start, out_expression);
            expr->name.token = *start;
        }
    }
    else if (maybe_take_atom(stream, ATOM_DOLLAR))
    {
        if (maybe_take_atom(stream, ATOM_IF))
            goto parse_baked_if;

        if (!(original_parse_flags & PARSE_ALLOW_INFERRED_TYPE_ALIAS))
            return ReportError(stream->ctx, stream->cursor - 1, "Inferred type alias is not allowed here, as there would be nowhere to infer the type from."_s);

        Token* name = stream->cursor;
        if (!take_atom(stream, ATOM_FIRST_IDENTIFIER, "Expected the type alias name after '$'."_s))
            return false;

        Parsed_Expression* expr = add_expression(builder, EXPRESSION_DECLARATION, start, stream->cursor - 1, out_expression);
        expr->flags |= EXPRESSION_HAS_TO_BE_EXTERNALLY_INFERRED | EXPRESSION_DECLARATION_IS_ALIAS | EXPRESSION_DECLARATION_IS_INFERRED_ALIAS;
        expr->declaration.name  = *name;
        expr->declaration.type  = NO_EXPRESSION;
        expr->declaration.value = NO_EXPRESSION;
    }
    else if (maybe_take_atom(stream, ATOM_CAST))
    {
        if (!take_atom(stream, ATOM_LEFT_PARENTHESIS, "Expected '(' after the 'cast' keyword."_s))
            return false;
        Expression lhs;
        if (!parse_expression(stream, builder, &lhs, parse_flags))
            return false;
        if (!take_atom(stream, ATOM_COMMA, "Expected ',' between the operands to 'cast'."_s))
            return false;
        Expression rhs;
        if (!parse_expression(stream, builder, &rhs, parse_flags))
            return false;
        if (!take_atom(stream, ATOM_RIGHT_PARENTHESIS, "Expected ')' after the second operand to 'cast'."_s))
            return false;

        Parsed_Expression* expr = add_expression(builder, EXPRESSION_CAST, start, stream->cursor - 1, out_expression);
        expr->binary.lhs = lhs;
        expr->binary.rhs = rhs;
    }
    else if (maybe_take_atom(stream, ATOM_IF))
    {
        bool is_baked;
        if (false) parse_baked_if:
            is_baked = true;
        else
            is_baked = false;

        Expression expression;
        if (!parse_expression(stream, builder, &expression, parse_flags))
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
        if (is_baked)
        {
            expr->flags |= EXPRESSION_BRANCH_IS_BAKED;
            if (if_true  != NO_EXPRESSION) builder->expressions[if_true ].flags |= EXPRESSION_HAS_CONDITIONAL_INFERENCE;
            if (if_false != NO_EXPRESSION) builder->expressions[if_false].flags |= EXPRESSION_HAS_CONDITIONAL_INFERENCE;
        }
        expr->branch.condition  = expression;
        expr->branch.on_success = if_true;
        expr->branch.on_failure = if_false;
    }
    else if (maybe_take_atom(stream, ATOM_WHILE))
    {
        Expression expression;
        if (!parse_expression(stream, builder, &expression, parse_flags))
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
    else if (maybe_take_atom(stream, ATOM_GOTO))
    {
        if (!take_atom(stream, ATOM_LEFT_PARENTHESIS, "Expected '(' after the 'goto' keyword."_s))
            return false;
        Expression lhs;
        if (!parse_expression(stream, builder, &lhs, parse_flags))
            return false;
        if (!take_atom(stream, ATOM_COMMA, "Expected ',' between the operands to 'goto'."_s))
            return false;
        Expression rhs;
        if (!parse_expression(stream, builder, &rhs, parse_flags))
            return false;
        if (!take_atom(stream, ATOM_RIGHT_PARENTHESIS, "Expected ')' after the second operand to 'goto'."_s))
            return false;

        Parsed_Expression* expr = add_expression(builder, EXPRESSION_GOTO_UNIT, start, stream->cursor - 1, out_expression);
        expr->binary.lhs = lhs;
        expr->binary.rhs = rhs;
    }
    else if (maybe_take_atom(stream, ATOM_YIELD))
    {
        Dynamic_Array<Expression> exprs = {};
        Defer(free_heap_array(&exprs));
        if (maybe_take_atom(stream, ATOM_LEFT_PARENTHESIS))
        {
            while (!maybe_take_atom(stream, ATOM_RIGHT_PARENTHESIS))
            {
                if (exprs.count && !take_atom(stream, ATOM_COMMA, "Expected ',' between assignments."_s))
                    return false;

                if (!parse_expression(stream, builder, reserve_item(&exprs), parse_flags))
                    return false;
                auto* assignment = &builder->expressions[exprs[exprs.count - 1]];
                if (assignment->kind != EXPRESSION_ASSIGNMENT)
                    return ReportError(stream->ctx, assignment, "Expected an assignment as part of 'yield' syntax."_s);
            }
        }

        Expression_List* list = make_expression_list(&stream->ctx->parser_memory, exprs.count);
        for (umm i = 0; i < exprs.count; i++)
            list->expressions[i] = exprs[i];

        Parsed_Expression* expr = add_expression(builder, EXPRESSION_YIELD, start, start, out_expression);
        expr->yield_assignments = list;
    }
    else
    {
        return ReportError(stream->ctx, next_token_or_eof(stream), "Expected an expression."_s);
    }

    assert(*out_expression != NO_EXPRESSION);
    if (!expression_can_be_followed_by_operators(&builder->expressions[*out_expression]))
        return true;

    while (true)
    {
        if (maybe_take_atom(stream, ATOM_LEFT_PARENTHESIS))
        {
            Dynamic_Array<Expression> args = {};
            Defer(free_heap_array(&args));

            bool first_argument = true;
            while (!maybe_take_atom(stream, ATOM_RIGHT_PARENTHESIS))
            {
                if (first_argument) first_argument = false;
                else if (!take_atom(stream, ATOM_COMMA, "Expected ',' between arguments."_s))
                    return false;

                Expression value;
                if (!parse_expression(stream, builder, reserve_item(&args), parse_flags | PARSE_ALLOW_BLOCKS))
                    return false;
            }

            if (lookahead_atom(stream, ATOM_EQUAL_GREATER, 0) || lookahead_atom(stream, ATOM_LEFT_BRACE, 0))
            {
                Block* child = parse_block(stream, 0);
                if (!child)
                    return false;
                add_expression(builder, EXPRESSION_BLOCK, &child->from, &child->to, reserve_item(&args))->parsed_block = child;
            }

            Expression_List* list = make_expression_list(&stream->ctx->parser_memory, args.count);
            for (umm i = 0; i < args.count; i++)
                list->expressions[i] = args[i];

            Expression lhs = *out_expression;
            Parsed_Expression* expr = add_expression(builder, EXPRESSION_CALL, start, stream->cursor - 1, out_expression);
            expr->call.lhs       = lhs;
            expr->call.arguments = list;
        }
        else if (maybe_take_atom(stream, ATOM_DOT))
        {
            Token* name = stream->cursor;
            if (!take_atom(stream, ATOM_FIRST_IDENTIFIER, "Expected a member name after the '.' operator."_s))
                return false;

            Expression lhs = *out_expression;
            Parsed_Expression* expr = add_expression(builder, EXPRESSION_MEMBER, start, stream->cursor - 1, out_expression);
            expr->member.lhs  = lhs;
            expr->member.name = *name;
        }
        else break;
    }

#undef InheritFlags
    return true;
}

static bool parse_expression(Token_Stream* stream, Block_Builder* builder, Expression* out_expression, Parse_Flags parse_flags)
{
    Expression lhs;
    if (!parse_expression_leaf(stream, builder, &lhs, parse_flags))
        return false;

    assert(lhs != NO_EXPRESSION);
    if (!expression_can_be_followed_by_operators(&builder->expressions[lhs]))
    {
        *out_expression = lhs;
        return true;
    }

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
            case EXPRESSION_ASSIGNMENT:        return 0;
            case EXPRESSION_EQUAL:             return 1;
            case EXPRESSION_NOT_EQUAL:         return 1;
            case EXPRESSION_GREATER_THAN:      return 2;
            case EXPRESSION_GREATER_OR_EQUAL:  return 2;
            case EXPRESSION_LESS_THAN:         return 2;
            case EXPRESSION_LESS_OR_EQUAL:     return 2;
            case EXPRESSION_ADD:               return 3;
            case EXPRESSION_SUBTRACT:          return 3;
            case EXPRESSION_POINTER_ADD:       return 3;
            case EXPRESSION_POINTER_SUBTRACT:  return 3;
            case EXPRESSION_MULTIPLY:          return 4;
            case EXPRESSION_DIVIDE_WHOLE:      return 4;
            case EXPRESSION_DIVIDE_FRACTIONAL: return 4;
            default:                           return U32_MAX;
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
             if (maybe_take_atom(stream, ATOM_EQUAL))           op = EXPRESSION_ASSIGNMENT;
        else if (maybe_take_atom(stream, ATOM_PLUS))            op = EXPRESSION_ADD;
        else if (maybe_take_atom(stream, ATOM_MINUS))           op = EXPRESSION_SUBTRACT;
        else if (maybe_take_atom(stream, ATOM_STAR))            op = EXPRESSION_MULTIPLY;
        else if (maybe_take_atom(stream, ATOM_BANG_SLASH))      op = EXPRESSION_DIVIDE_WHOLE;
        else if (maybe_take_atom(stream, ATOM_PERCENT_SLASH))   op = EXPRESSION_DIVIDE_FRACTIONAL;
        else if (maybe_take_atom(stream, ATOM_AMPERSAND_PLUS))  op = EXPRESSION_POINTER_ADD;
        else if (maybe_take_atom(stream, ATOM_AMPERSAND_MINUS)) op = EXPRESSION_POINTER_SUBTRACT;
        else if (maybe_take_atom(stream, ATOM_EQUAL_EQUAL))     op = EXPRESSION_EQUAL;
        else if (maybe_take_atom(stream, ATOM_BANG_EQUAL))      op = EXPRESSION_NOT_EQUAL;
        else if (maybe_take_atom(stream, ATOM_GREATER))         op = EXPRESSION_GREATER_THAN;
        else if (maybe_take_atom(stream, ATOM_GREATER_EQUAL))   op = EXPRESSION_GREATER_OR_EQUAL;
        else if (maybe_take_atom(stream, ATOM_LESS))            op = EXPRESSION_LESS_THAN;
        else if (maybe_take_atom(stream, ATOM_LESS_EQUAL))      op = EXPRESSION_LESS_OR_EQUAL;
        else if (maybe_take_atom(stream, ATOM_SLASH))
        {
            return ReportError(stream->ctx, stream->cursor - 1,
                "For your own safety, and the safety of others, you can't use '/' as a division operator.\n"
                "Use '!/' if you want whole number division, or '%/' for fractional division."_s);
        }
        else break;

        Expression rhs;
        if (!parse_expression_leaf(stream, builder, &rhs, parse_flags)) return false;
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
    if (!parse_expression(stream, builder, &expression, PARSE_ALLOW_BLOCKS))
        return false;

    assert(expression != NO_EXPRESSION);
    auto* expr = &builder->expressions[expression];
    if (expr->kind == EXPRESSION_BLOCK)
    {
        if (lookahead_atom(stream, ATOM_LEFT_PARENTHESIS, 0))
            Report(stream->ctx)
                .intro(SEVERITY_WARNING, expr)
                .message("Block expression has no effect.\n"
                         "It looks like you're trying to call it, but in that case you have to place the block inside parentheses."_s)
                .suggestion("("_s, expr, ")"_s)
                .done();
        else
            report_error(stream->ctx, expr, "Block expression has no effect."_s, SEVERITY_WARNING);
    }

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

Block* parse_top_level(Compiler* ctx, String canonical_name, String imports_relative_to_path, Array<Token> tokens)
{
    Block* block = get(&ctx->top_level_blocks, &canonical_name);
    if (block) return block;

    block = PushValue(&ctx->parser_memory, Block);
    block->flags |= BLOCK_IS_TOP_LEVEL | BLOCK_IS_UNIT | BLOCK_HAS_STRUCTURE_PLACEMENT;

    canonical_name = allocate_string(&ctx->parser_memory, canonical_name);
    set(&ctx->top_level_blocks, &canonical_name, &block);

    Block_Builder builder = {};
    builder.block = block;
    Defer(finish_building(ctx, &builder));

    if (tokens.count)
    {
        Token_Stream stream;
        assert(tokens.count > 0);  // checked above
        set_nonempty_token_stream(&stream, ctx, tokens);
        stream.imports_relative_to_path = imports_relative_to_path;

        block->from = *next_token_or_eof(&stream);
        while (stream.cursor < stream.end)
        {
            if (!parse_statement(&stream, &builder)) return NULL;
            if (!semicolon_after_statement(&stream)) return NULL;
        }
        block->to = *next_token_or_eof(&stream);
    }

    return block;
}

Block* parse_top_level_from_file(Compiler* ctx, String path)
{
    Array<Token> tokens = {};
    bool ok = lex_file(ctx, path, &tokens);
    if (!ok)
        return NULL;

    // @Incomplete - normalize path and all that stuff
    String import_path = get_parent_directory_path(path);
    return parse_top_level(ctx, path, import_path, tokens);
}

Block* parse_top_level_from_memory(Compiler* ctx, String name, String code)
{
    Array<Token> tokens = {};
    bool ok = lex_from_memory(ctx, name, code, &tokens);
    if (!ok) return NULL;
    return parse_top_level(ctx, name, "."_s, tokens);
}



ExitApplicationNamespace
