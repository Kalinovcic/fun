#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
#include "api.h"
#include <stdio.h>


EnterApplicationNamespace



static Block* materialize_block(Unit* unit, Block* materialize_from,
                                Block* parent_scope, Visibility parent_scope_visibility_limit)
{
    Block* block = PushValue(&unit->memory, Block);
    *block = *materialize_from;
    assert(!(materialize_from->flags & BLOCK_IS_MATERIALIZED));

    block->flags |= BLOCK_IS_MATERIALIZED;
    block->materialized_from = materialize_from;

    block->inferred_expressions = allocate_array<Inferred_Expression>(&unit->memory, block->parsed_expressions.count);
    For (block->inferred_expressions)
    {
        it->constant_index = INVALID_CONSTANT_INDEX;
        it->type           = INVALID_TYPE;
        it->size           = INVALID_STORAGE_SIZE;
        it->offset         = INVALID_STORAGE_OFFSET;
    }

    block->parent_scope                  = parent_scope;
    block->parent_scope_visibility_limit = parent_scope_visibility_limit;

    unit->materialized_block_count++;
    unit->most_recent_materialized_block = block;

    Pipeline_Task task = {};
    task.unit  = unit;
    task.block = block;
    add_item(&unit->ctx->pipeline, &task);
    return block;
}



String vague_type_description(Unit* unit, Type type, bool point_out_soft_types)
{
    if (is_pointer_type(type))
        return "a pointer value"_s;

    if (point_out_soft_types)
    {
        switch (type)
        {
        case TYPE_SOFT_NUMBER: return "a compile-time number"_s;
        case TYPE_SOFT_BOOL:   return "a compile-time bool"_s;
        case TYPE_SOFT_TYPE:   return "a compile-time type"_s;
        }
    }

    switch (type)
    {
    // run-time values
    case TYPE_VOID:        return "a void value"_s;
    case TYPE_U8: case TYPE_U16: case TYPE_U32: case TYPE_U64:
    case TYPE_S8: case TYPE_S16: case TYPE_S32: case TYPE_S64:
    case TYPE_F16: case TYPE_F32: case TYPE_F64:
    case TYPE_SOFT_NUMBER: return "a numeric value"_s;
    case TYPE_BOOL8: case TYPE_BOOL16: case TYPE_BOOL32: case TYPE_BOOL64:
    case TYPE_SOFT_BOOL:   return "a bool value"_s;
    case TYPE_TYPE:
    case TYPE_SOFT_TYPE:   return "a type value"_s;
    case TYPE_SOFT_ZERO:   return "a zero"_s;
    case TYPE_SOFT_BLOCK:  return "a block"_s;
    }

    assert(is_user_defined_type(type));
    // @ErrorReporting more detail, what type of custom type
    return "a custom type value"_s;
}

String vague_type_description_in_compile_time_context(Unit* unit, Type type)
{
    return vague_type_description(unit, type, /* point_out_soft_types */ true);
}

String exact_type_description(Unit* unit, Type type)
{
    if (is_pointer_type(type))
    {
        umm indirection = get_indirection(type);
        String stars = {};
        while (indirection--)
            FormatAppend(&stars, temp, "&");
        return concatenate(temp, stars, exact_type_description(unit, get_base_type(type)));
    }

    switch (type)
    {
    // run-time values
    case TYPE_VOID:        return "void"_s;
    case TYPE_U8:          return "u8"_s;
    case TYPE_U16:         return "u16"_s;
    case TYPE_U32:         return "u32"_s;
    case TYPE_U64:         return "u64"_s;
    case TYPE_S8:          return "s8"_s;
    case TYPE_S16:         return "s16"_s;
    case TYPE_S32:         return "s32"_s;
    case TYPE_S64:         return "s64"_s;
    case TYPE_F16:         return "f16"_s;
    case TYPE_F32:         return "f32"_s;
    case TYPE_F64:         return "f64"_s;
    case TYPE_SOFT_NUMBER: return "compile-time number"_s;
    case TYPE_BOOL8:       return "bool8"_s;
    case TYPE_BOOL16:      return "bool16"_s;
    case TYPE_BOOL32:      return "bool32"_s;
    case TYPE_BOOL64:      return "bool64"_s;
    case TYPE_SOFT_BOOL:   return "compile-time bool"_s;
    case TYPE_TYPE:        return "type"_s;
    case TYPE_SOFT_TYPE:   return "compile-time type"_s;
    case TYPE_SOFT_ZERO:   return "zero"_s;
    case TYPE_SOFT_BLOCK:  return "block"_s;
    }

    assert(is_user_defined_type(type));
    return "<user defined type>"_s;
}



bool find_declaration(Unit* unit, Token const* name,
                      Block* scope, Visibility visibility_limit,
                      Block** out_decl_scope, Expression* out_decl_expr)
{
    while (scope)
    {
        for (Expression id = (Expression) 0; id < scope->parsed_expressions.count; id = (Expression)(id + 1))
        {
            auto* expr = &scope->parsed_expressions[id];
            if (expr->kind != EXPRESSION_DECLARATION) continue;

            if (expr->declaration.name.atom != name->atom) continue;
            if ((expr->flags & EXPRESSION_DECLARATION_IS_ORDERED) &&
                (expr->visibility_limit >= visibility_limit || visibility_limit == NO_VISIBILITY))
                break;

            *out_decl_scope = scope;
            *out_decl_expr = id;
            return true;
        }

        visibility_limit = scope->parent_scope_visibility_limit;
        scope            = scope->parent_scope;
    }
    return false;
}



u64 get_type_size(Unit* unit, Type type)
{
    if (get_indirection(type))
        return unit->pointer_size;

    if (is_user_defined_type(type))
        NotImplemented;

    switch (type)
    {
    case TYPE_VOID:        return 0;
    case TYPE_SOFT_ZERO:   Unreachable;
    case TYPE_U8:          return 1;
    case TYPE_U16:         return 2;
    case TYPE_U32:         return 4;
    case TYPE_U64:         return 8;
    case TYPE_S8:          return 1;
    case TYPE_S16:         return 2;
    case TYPE_S32:         return 4;
    case TYPE_S64:         return 8;
    case TYPE_F16:         return 2;
    case TYPE_F32:         return 4;
    case TYPE_F64:         return 8;
    case TYPE_SOFT_NUMBER: Unreachable;
    case TYPE_BOOL8:       return 1;
    case TYPE_BOOL16:      return 2;
    case TYPE_BOOL32:      return 4;
    case TYPE_BOOL64:      return 8;
    case TYPE_SOFT_BOOL:   Unreachable;
    case TYPE_TYPE:        return 4;
    case TYPE_SOFT_TYPE:   Unreachable;
    IllegalDefaultCase;
    }
}

u64 get_type_alignment(Unit* unit, Type type)
{
    if (get_indirection(type))
        return unit->pointer_alignment;

    if (is_user_defined_type(type))
        NotImplemented;

    // For primitives, the alignment is the same as their size.
    u64 size = get_type_size(unit, type);
    if (size == 0)
        size = 1;
    return size;
}

void allocate_unit_storage(Unit* unit, Type type, u64* out_size, u64* out_offset)
{
    u64 size      = get_type_size     (unit, type);
    u64 alignment = get_type_alignment(unit, type);

    while (unit->next_storage_offset % alignment)
        unit->next_storage_offset++;

    *out_size   = size;
    *out_offset = unit->next_storage_offset;
    unit->next_storage_offset += size;
}



static umm edit_distance(String a, String b)
{
    if (!a) return b.length;
    if (!b) return a.length;

    if (a[0] == b[0])
        return edit_distance(consume(a, 1), consume(b, 1));

    umm option1 = edit_distance(consume(a, 1), b);
    umm option2 = edit_distance(a, consume(b, 1));
    umm option3 = edit_distance(consume(a, 1), consume(b, 1));

    umm best = option1;
    if (best > option2) best = option2;
    if (best > option3) best = option3;
    return 1 + best;
}

static bool check_constant_fits_in_runtime_type(Unit* unit, Parsed_Expression const* expr, Fraction const* fraction, Type type)
{
    assert(!is_soft_type(type));
    assert(is_numeric_type(type));
    String name = exact_type_description(unit, type);

    if (!is_integer_type(type))
        NotImplemented;

#define Error(...) return (report_error(unit->ctx, expr, Format(temp, ##__VA_ARGS__)), false)
    if (!fract_is_integer(fraction))
        Error("The number is fractional, it can't fit in %.\n"
              "    value: %",
              exact_type_description(unit, type), fract_display(fraction));

    u64 target_size = get_type_size(unit, type);

    Integer const* integer = &fraction->num;
    if (integer->negative)
    {
        if (is_unsigned_integer_type(type))
            Error("The number doesn't fit in %.\n"
                  "    value: %\n"
                  "  minimum: 0 (lowest number that fits in %)",
                  name, fract_display(fraction), name);

        Integer min = {};
        int_set_zero(&min);
        int_set_bit(&min, target_size * 8 - 1);
        int_negate(&min);
        if (int_compare(integer, &min) < 0)
            Error("The number doesn't fit in %.\n"
                  "    value: %\n"
                  "  minimum: % (lowest number that fits in %)",
                  name, fract_display(fraction), int_base10(&min), name);
    }
    else
    {
        u64 max_log2 = (target_size * 8) - (is_signed_integer_type(type) ? 1 : 0);
        u64 max_value = (((1ull << (max_log2 - 1)) - 1) << 1) | 1;  // clown behavior because << 64 doesn't work
        if (int_log2_abs(integer) >= max_log2)
            Error("The number doesn't fit in %.\n"
                  "    value: %\n"
                  "  maximum: % (greatest number that fits in %)",
                  name, fract_display(fraction), max_value, name);
    }
#undef Error

    return true;
}

static void set_inferred_type(Block* block, Expression id, Type type)
{
    auto* expr  = &block->parsed_expressions[id];
    auto* infer = &block->inferred_expressions[id];

    assert(type != INVALID_TYPE);
    if (type == TYPE_SOFT_ZERO)
        assert(expr->kind == EXPRESSION_ZERO);
    else if (is_soft_type(type))
        assert(infer->constant_index != INVALID_CONSTANT_INDEX);
    if (is_soft_type(type))
    {
        infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;
        infer->flags |= INFERRED_EXPRESSION_DOES_NOT_ALLOCATE_STORAGE;
        assert(infer->size   == INVALID_STORAGE_SIZE);
        assert(infer->offset == INVALID_STORAGE_OFFSET);
    }
    infer->type = type;
    remove(&block->waiting_expressions, &id);
}

static bool pattern_matching_inference(Unit* unit, Block* block, Expression id, Type type,
                                       Parsed_Expression const* inferred_from, Type full_inferred_type)
{
    assert(type != INVALID_TYPE);
    assert(!is_soft_type(type));

    auto* expr  = &block->parsed_expressions[id];
    auto* infer = &block->inferred_expressions[id];
    assert(expr->flags & EXPRESSION_HAS_TO_BE_EXTERNALLY_INFERRED);
    assert(infer->type == INVALID_TYPE);

    switch (expr->kind)
    {

    case EXPRESSION_DECLARATION:
    {
        if (expr->flags & EXPRESSION_DECLARATION_IS_INFERRED_ALIAS)
        {
            assert(expr->declaration.type  == NO_EXPRESSION);
            assert(expr->declaration.value == NO_EXPRESSION);
            infer->constant_type = type;
            set_inferred_type(block, id, TYPE_SOFT_TYPE);
        }
        else if (expr->flags & EXPRESSION_DECLARATION_IS_ALIAS)
        {
            assert(expr->declaration.type  == NO_EXPRESSION);
            assert(expr->declaration.value != NO_EXPRESSION);
            if (!pattern_matching_inference(unit, block, expr->declaration.value, type, inferred_from, full_inferred_type))
                return false;
            infer->constant_type = type;
            set_inferred_type(block, id, TYPE_SOFT_TYPE);
        }
        else Unreachable;
    } break;

    case EXPRESSION_ADDRESS:
    {
        if (!is_pointer_type(type))
        {
            String full_type = exact_type_description(unit, full_inferred_type);
            Report(unit->ctx).part(expr, "Can't match the type to the expected pattern."_s)
                             .part(inferred_from, Format(temp, "The type is %, inferred from here:", full_type))
                             .done();
            return false;
        }

        Type reduced = set_indirection(type, get_indirection(type) - 1);
        if (!pattern_matching_inference(unit, block, expr->unary_operand, reduced, inferred_from, full_inferred_type))
            return false;
        infer->constant_type = type;
        set_inferred_type(block, id, TYPE_SOFT_TYPE);
    } break;

    case EXPRESSION_DEREFERENCE:
    {
        u32 indirection = get_indirection(type) + 1;
        if (indirection > TYPE_MAX_INDIRECTION)
        {
            String full_type = exact_type_description(unit, full_inferred_type);
            Report(unit->ctx).part(expr, Format(temp, "Can't match the type to the expected pattern, because it would exceed the maximum indirection %.", TYPE_MAX_INDIRECTION))
                             .part(inferred_from, Format(temp, "The type is %, inferred from here:", full_type))
                             .done();
        }

        Type indirected = set_indirection(type, indirection);
        if (!pattern_matching_inference(unit, block, expr->unary_operand, indirected, inferred_from, full_inferred_type))
            return false;
        infer->constant_type = type;
        set_inferred_type(block, id, TYPE_SOFT_TYPE);
    } break;

    IllegalDefaultCase;

    }

    return true;
}


enum Yield_Result
{
    YIELD_COMPLETED,
    YIELD_MADE_PROGRESS,
    YIELD_NO_PROGRESS,
    YIELD_ERROR,
};

static Yield_Result infer_expression(Pipeline_Task* task, Expression id)
{
    Unit*  unit  = task->unit;
    Block* block = task->block;

    bool override_made_progress = false;

    auto add_constant = [](Block* block, Fraction fraction) -> u32
    {
        u32 index = block->constants.count;
        add_item(&block->constants, &fraction);
        return index;
    };

    auto* expr  = &block->parsed_expressions[id];
    auto* infer = &block->inferred_expressions[id];
    assert(infer->type == INVALID_TYPE);


    #define Infer(type) return (set_inferred_type(block, id, type), YIELD_COMPLETED);

    #define Wait(why, on_expression, on_block)                                      \
    {                                                                               \
        Wait_Info info = { why, on_expression, on_block };                          \
        set(&block->waiting_expressions, &id, &info);                               \
        return override_made_progress ? YIELD_MADE_PROGRESS : YIELD_NO_PROGRESS;    \
    }

    #define WaitOperand(on_expression) Wait(WAITING_ON_OPERAND, on_expression, block)

    #define Error(...) return (report_error(unit->ctx, expr, Format(temp, ##__VA_ARGS__)), YIELD_ERROR)

    if (expr->flags & EXPRESSION_HAS_TO_BE_EXTERNALLY_INFERRED)
        Wait(WAITING_ON_EXTERNAL_INFERENCE, NO_EXPRESSION, NULL);

    switch (expr->kind)
    {

    case EXPRESSION_ZERO:                   Infer(TYPE_SOFT_ZERO);
    case EXPRESSION_TRUE:                   infer->constant_bool = true;  Infer(TYPE_SOFT_BOOL);
    case EXPRESSION_FALSE:                  infer->constant_bool = false; Infer(TYPE_SOFT_BOOL);
    case EXPRESSION_NUMERIC_LITERAL:
    {
        Token_Info_Number* token_info = (Token_Info_Number*) get_token_info(unit->ctx, &expr->literal);
        infer->constant_index = add_constant(block, fract_clone(&token_info->value));
        Infer(TYPE_SOFT_NUMBER);
    } break;

    case EXPRESSION_TYPE_LITERAL:
    {
        // @Future @Incomplete - do typechecking inside the type here, once we get to type polymorphism
        assert(is_primitive_type(expr->parsed_type));
        infer->constant_type = expr->parsed_type;
        Infer(TYPE_SOFT_TYPE);
    } break;

    case EXPRESSION_BLOCK:
    {
        infer->constant_block.materialized_parent = block;
        infer->constant_block.parsed_child = expr->parsed_block;
        Infer(TYPE_SOFT_BLOCK);
    } break;

    case EXPRESSION_NAME:
    {
        Token const* name = &expr->name.token;

        Block*     decl_scope;
        Expression decl_id;
        if (!find_declaration(unit, name, block, expr->visibility_limit, &decl_scope, &decl_id))
        {
            Token const* best_alternative = NULL;
            umm          best_distance    = UMM_MAX;

            String     identifier       = get_identifier(unit->ctx, name);
            Block*     scope            = block;
            Visibility visibility_limit = expr->visibility_limit;
            while (scope)
            {
                for (Expression id = (Expression) 0; id < scope->parsed_expressions.count; id = (Expression)(id + 1))
                {
                    auto* expr = &scope->parsed_expressions[id];
                    if (expr->kind != EXPRESSION_DECLARATION) continue;

                    Token const* decl_name = &expr->declaration.name;
                    if (decl_name->atom == name->atom)
                    {
                        String hint = "Maybe you are referring to this, but you don't have visibility because of imperative order."_s;
                        if (visibility_limit == NO_VISIBILITY)
                            hint = "Maybe you are referring to this, but you don't have visibility because it's in an outer scope."_s;
                        Report(unit->ctx).part(name, Format(temp, "Can't find name '%'.", identifier)).part(decl_name, hint).done();
                        return YIELD_ERROR;
                    }

                    String other_identifier = get_identifier(unit->ctx, decl_name);
                    umm distance = edit_distance(identifier, other_identifier);
                    if (best_distance > distance && distance < identifier.length / 3)
                    {
                        best_distance = distance;
                        best_alternative = decl_name;
                    }
                }

                visibility_limit = scope->parent_scope_visibility_limit;
                scope            = scope->parent_scope;
            }

            if (best_alternative)
            {
                Report(unit->ctx).part(name, Format(temp, "Can't find name '%'.", identifier))
                                 .part(best_alternative, Format(temp, "Maybe you meant '%'?", get_identifier(unit->ctx, best_alternative)))
                                 .done();
                return YIELD_ERROR;
            }

            Error("Can't find name '%'.", identifier);
        }

        Inferred_Expression* decl_infer = &decl_scope->inferred_expressions[decl_id];
        if (decl_infer->type == INVALID_TYPE)
            Wait(WAITING_ON_DECLARATION, decl_id, decl_scope);

        if (decl_infer->type == TYPE_SOFT_NUMBER)
            infer->constant_index = add_constant(block, fract_clone(&decl_scope->constants[decl_infer->constant_index]));
        else if (decl_infer->type == TYPE_SOFT_BOOL)
            infer->constant_bool = decl_infer->constant_bool;
        else if (decl_infer->type == TYPE_SOFT_TYPE)
            infer->constant_type = decl_infer->constant_type;
        else if (decl_infer->type == TYPE_SOFT_BLOCK)
            infer->constant_block = decl_infer->constant_block;
        else
        {
            assert(!is_soft_type(decl_infer->type));
            assert(decl_infer->size   != INVALID_STORAGE_SIZE);
            assert(decl_infer->offset != INVALID_STORAGE_OFFSET);
            infer->flags |= INFERRED_EXPRESSION_DOES_NOT_ALLOCATE_STORAGE;
            infer->size   = decl_infer->size;
            infer->offset = decl_infer->offset;
        }

        Infer(decl_infer->type);
    } break;

    case EXPRESSION_NEGATE:
    {
        Inferred_Expression* op_infer = &block->inferred_expressions[expr->unary_operand];
        if (op_infer->type == INVALID_TYPE)
            WaitOperand(expr->unary_operand);

        if (!is_numeric_type(op_infer->type))
            Error("Unary operator '-' expects a numeric argument, but got %.", vague_type_description(unit, op_infer->type));
        if (op_infer->type == TYPE_SOFT_NUMBER)
            infer->constant_index = add_constant(block, fract_neg(&block->constants[op_infer->constant_index]));

        Infer(op_infer->type);
    } break;

    case EXPRESSION_ADDRESS:
    {
        Inferred_Expression* op_infer = &block->inferred_expressions[expr->unary_operand];
        if (op_infer->type == INVALID_TYPE)
            WaitOperand(expr->unary_operand);

        if (is_type_type(op_infer->type))
        {
            if (op_infer->type != TYPE_SOFT_TYPE)
                Error("The operand to '&' is a type not known at compile-time.");

            Type type = op_infer->constant_type;
            u32 indirection = get_indirection(type) + 1;
            if (indirection > TYPE_MAX_INDIRECTION)
                Error("The operand to '&' is already at maximum indirection %!", TYPE_MAX_INDIRECTION);
            infer->constant_type = set_indirection(type, indirection);
            Infer(TYPE_SOFT_TYPE);
        }
        else if (is_soft_type(op_infer->type))
        {
            Error("Can't take an address of %.", vague_type_description_in_compile_time_context(unit, op_infer->type));
        }
        else
        {
            Type type = op_infer->type;
            u32 indirection = get_indirection(type) + 1;
            if (indirection > TYPE_MAX_INDIRECTION)
                Error("The operand to '&' is already at maximum indirection %!", TYPE_MAX_INDIRECTION);
            Infer(set_indirection(type, indirection));
        }
    } break;

    case EXPRESSION_DEREFERENCE:
    {
        Inferred_Expression* op_infer = &block->inferred_expressions[expr->unary_operand];
        if (op_infer->type == INVALID_TYPE)
            WaitOperand(expr->unary_operand);

        if (is_type_type(op_infer->type))
        {
            if (op_infer->type != TYPE_SOFT_TYPE)
                Error("The operand to '*' is a type not known at compile-time.");

            Type type = op_infer->constant_type;
            u32 indirection = get_indirection(type);
            if (!indirection)
                Error("Expected a pointer as operand to '*', but got %.", exact_type_description(unit, type));
            infer->constant_type = set_indirection(type, indirection - 1);
            Infer(TYPE_SOFT_TYPE);
        }
        else if (is_soft_type(op_infer->type))
        {
            Error("Can't dereference %.", vague_type_description_in_compile_time_context(unit, op_infer->type));
        }
        else
        {
            Type type = op_infer->type;
            u32 indirection = get_indirection(type);
            if (!indirection)
                Error("Expected a pointer as operand to '*', but got %.", exact_type_description(unit, type));

            infer->flags |= INFERRED_EXPRESSION_DOES_NOT_ALLOCATE_STORAGE;
            Infer(set_indirection(type, indirection - 1));
        }
    } break;

    case EXPRESSION_ASSIGNMENT:
    {
        if (block->parsed_expressions[expr->binary.lhs].kind != EXPRESSION_NAME &&
            block->parsed_expressions[expr->binary.lhs].kind != EXPRESSION_DEREFERENCE)
            Error("Expected a variable name or dereference expression on the left side of the assignment.");

        Type lhs_type = block->inferred_expressions[expr->binary.lhs].type;
        Type rhs_type = block->inferred_expressions[expr->binary.rhs].type;
        if (lhs_type == INVALID_TYPE) WaitOperand(expr->binary.lhs);
        if (rhs_type == INVALID_TYPE) WaitOperand(expr->binary.rhs);
        if (is_soft_type(lhs_type))
            Error("Can't assign to a constant expression.");
        if (lhs_type != rhs_type)
            Error("Types don't match.\n"
                  "    lhs: %\n"
                  "    rhs: %",
                  exact_type_description(unit, lhs_type),
                  exact_type_description(unit, rhs_type));
        Infer(lhs_type);
    } break;

    case EXPRESSION_ADD:
    case EXPRESSION_SUBTRACT:
    case EXPRESSION_MULTIPLY:
    case EXPRESSION_DIVIDE_WHOLE:
    case EXPRESSION_DIVIDE_FRACTIONAL:
    {
        Type lhs_type = block->inferred_expressions[expr->binary.lhs].type;
        Type rhs_type = block->inferred_expressions[expr->binary.rhs].type;
        if (lhs_type == INVALID_TYPE) WaitOperand(expr->binary.lhs);
        if (rhs_type == INVALID_TYPE) WaitOperand(expr->binary.rhs);
        if (lhs_type != rhs_type)
            Error("Types don't match.\n"
                  "    lhs: %\n"
                  "    rhs: %",
                  exact_type_description(unit, lhs_type),
                  exact_type_description(unit, rhs_type));
        if (!is_numeric_type(lhs_type))
            Error("Expected a numeric operand, but got %.", vague_type_description(unit, lhs_type));

        if (lhs_type == TYPE_SOFT_NUMBER)
        {
            Fraction const* lhs_fract = &block->constants[block->inferred_expressions[expr->binary.lhs].constant_index];
            Fraction const* rhs_fract = &block->constants[block->inferred_expressions[expr->binary.rhs].constant_index];
            Fraction result = {};
                 if (expr->kind == EXPRESSION_ADD)      result = fract_add(lhs_fract, rhs_fract);
            else if (expr->kind == EXPRESSION_SUBTRACT) result = fract_sub(lhs_fract, rhs_fract);
            else if (expr->kind == EXPRESSION_MULTIPLY) result = fract_mul(lhs_fract, rhs_fract);
            else if (expr->kind == EXPRESSION_DIVIDE_WHOLE)
            {
                if (!fract_div_whole(&result, lhs_fract, rhs_fract))
                    Error("Division by zero!");
            }
            else if (expr->kind == EXPRESSION_DIVIDE_FRACTIONAL)
            {
                if (!fract_div_fract(&result, lhs_fract, rhs_fract))
                    Error("Division by zero!");
            }
            infer->constant_index = add_constant(block, result);
        }

        Infer(lhs_type);
    } break;

    case EXPRESSION_EQUAL:            NotImplemented;
    case EXPRESSION_NOT_EQUAL:        NotImplemented;
    case EXPRESSION_GREATER_THAN:     NotImplemented;
    case EXPRESSION_GREATER_OR_EQUAL: NotImplemented;
    case EXPRESSION_LESS_THAN:        NotImplemented;
    case EXPRESSION_LESS_OR_EQUAL:    NotImplemented;

    case EXPRESSION_CAST:
    {
        auto* lhs_infer = &block->inferred_expressions[expr->binary.lhs];
        auto* rhs_infer = &block->inferred_expressions[expr->binary.rhs];
        if (lhs_infer->type == INVALID_TYPE) WaitOperand(expr->binary.lhs);
        if (rhs_infer->type == INVALID_TYPE) WaitOperand(expr->binary.rhs);

        if (!is_type_type(lhs_infer->type))
            Error("Expected a type as the first operand to 'cast', but got %.", vague_type_description(unit, lhs_infer->type));
        if (lhs_infer->type != TYPE_SOFT_TYPE)
            Error("The first operand to 'cast' is not known at compile-time.");

        if (!is_numeric_type(rhs_infer->type))  // @Incomplete
            Error("Expected a numeric value as the second operand to 'cast', but got %.", vague_type_description(unit, rhs_infer->type));

        Type cast_type = lhs_infer->constant_type;
        if (!is_numeric_type(cast_type))
            Error("Expected a numeric type as the first operand to 'cast', but got %.", exact_type_description(unit, cast_type));

        if (rhs_infer->type == TYPE_SOFT_NUMBER)
        {
            Fraction const* fraction = &block->constants[rhs_infer->constant_index];
            if (!check_constant_fits_in_runtime_type(unit, &block->parsed_expressions[expr->binary.rhs], fraction, cast_type))
                return YIELD_ERROR;
            infer->constant_index = add_constant(block, fract_clone(fraction));
        }
        else
        {
            assert(!is_soft_type(rhs_infer->type));
            // @Incomplete - check if cast is possible
        }

        Infer(cast_type);
    } break;

    case EXPRESSION_BRANCH:
    {
        if (expr->branch.condition != NO_EXPRESSION)
        {
            Type type = block->inferred_expressions[expr->branch.condition].type;
            if (type == INVALID_TYPE) WaitOperand(expr->branch.condition);
            if (!is_integer_type(type) && !is_bool_type(type))
                Error("Expected an integer or boolean value as the condition, but got %.", vague_type_description(unit, type));
            if (is_soft_type(type))
                Error("@Incomplete - condition may not be a compile-time value");
        }

        Type success_type = INVALID_TYPE;
        Type failure_type = INVALID_TYPE;
        if (expr->branch.on_success != NO_EXPRESSION)
        {
            success_type = block->inferred_expressions[expr->branch.on_success].type;
            if (success_type == INVALID_TYPE) WaitOperand(expr->branch.on_success);
        }
        if (expr->branch.on_failure != NO_EXPRESSION)
        {
            failure_type = block->inferred_expressions[expr->branch.on_failure].type;
            if (failure_type == INVALID_TYPE) WaitOperand(expr->branch.on_failure);
        }

        if (success_type != INVALID_TYPE && failure_type != INVALID_TYPE)
            if (success_type != failure_type)
                Error("The expression yields a different type depending on the condition.\n"
                      "On success, it yields %.\n"
                      "On failure, it yields %.",
                      exact_type_description(unit, success_type),
                      exact_type_description(unit, failure_type));
        Infer(success_type);
    } break;

    case EXPRESSION_CALL:
    {
        // Make sure everything on our side is inferred.
        auto* lhs_infer = &block->inferred_expressions[expr->call.lhs];
        if (lhs_infer->type == INVALID_TYPE) WaitOperand(expr->call.lhs);
        if (lhs_infer->type != TYPE_SOFT_BLOCK)
            Error("Expected a block on the left-hand side of the call expression, but got %.", vague_type_description(unit, lhs_infer->type));

        Expression_List const* args = expr->call.arguments;
        for (umm arg = 0; arg < args->count; arg++)
            if (block->inferred_expressions[args->expressions[arg]].type == INVALID_TYPE)
                WaitOperand(args->expressions[arg]);

        if (expr->call.block != NO_EXPRESSION)
        {
            auto* block_infer = &block->inferred_expressions[expr->call.block];
            if (block_infer->type == INVALID_TYPE) WaitOperand(expr->call.block);
            if (block_infer->type != TYPE_SOFT_BLOCK)
                Error("Internal error: Expected a block as the baked block parameter. But this should always be the case? It's %.", exact_type_description(unit, block_infer->type));
        }

        // First stage: materializing the callee
        if (!infer->called_block)
        {
            // First check against the parsed block that the argument count is correct.
            Block* lhs_parent = lhs_infer->constant_block.materialized_parent;
            Block* lhs_block  = lhs_infer->constant_block.parsed_child;
            assert(lhs_block);

            bool expects_block = lhs_block->flags & BLOCK_HAS_BLOCK_PARAMETER;
            bool have_block    = expr->call.block != NO_EXPRESSION;
            if (expects_block && !have_block) Error("Callee expects a block parameter.");
            if (!expects_block && have_block) Error("Callee doesn't expect a block parameter.");

            umm regular_parameter_count = 0;
            For (lhs_block->imperative_order)
                if (lhs_block->parsed_expressions[*it].flags & EXPRESSION_DECLARATION_IS_PARAMETER)
                    regular_parameter_count++;
            if (expects_block)
                regular_parameter_count--;
            if (regular_parameter_count != args->count)
                Error("Callee expects % %, but you provided %.",
                    regular_parameter_count, plural(regular_parameter_count, "argument"_s, "arguments"_s),
                    args->count);

            // Now materialize the callee
            Visibility visibility = (expr->flags & EXPRESSION_ALLOW_PARENT_SCOPE_VISIBILITY)
                                  ? expr->visibility_limit
                                  : NO_VISIBILITY;
            Block* callee = materialize_block(unit, lhs_block, lhs_parent, visibility);
            infer->called_block = callee;

            override_made_progress = true;  // We made progress by materializing the callee.
        }

        Expression waiting_on_a_parameter = NO_EXPRESSION;

        // Second stage: checking the parameter types
        Block* callee = infer->called_block;
        umm parameter_index = 0;
        For (callee->imperative_order)
        {
            Expression param_id = *it;
            auto* param_expr  = &callee->parsed_expressions  [param_id];
            auto* param_infer = &callee->inferred_expressions[param_id];
            if (!(param_expr->flags & EXPRESSION_DECLARATION_IS_PARAMETER)) continue;
            Defer(parameter_index++);
            assert(param_expr->declaration.type != NO_EXPRESSION);
            assert(param_expr->declaration.value == NO_EXPRESSION);

            String param_name = get_identifier(unit->ctx, &param_expr->declaration.name);

            auto* param_type_expr = &callee->parsed_expressions[param_expr->declaration.type];
            if ((param_type_expr->flags & EXPRESSION_HAS_TO_BE_EXTERNALLY_INFERRED) &&
                param_infer->type == INVALID_TYPE)
            {
                assert(!(param_expr->flags & EXPRESSION_DECLARATION_IS_ALIAS));
                assert(parameter_index < args->count);

                auto* arg_expr = &block->parsed_expressions  [args->expressions[parameter_index]];
                Type  arg_type =  block->inferred_expressions[args->expressions[parameter_index]].type;
                assert(arg_type != INVALID_TYPE);

                if (is_soft_type(arg_type))
                {
                    Report(unit->ctx).part(arg_expr, Format(temp, "A runtime value is required for parameter #% ('%'), but the argument is %.",
                                                            parameter_index + 1, param_name,
                                                            vague_type_description_in_compile_time_context(unit, arg_type)))
                                     .part(param_type_expr, "This is required because the parameter infers its type from the caller."_s)
                                     .done();
                    return YIELD_ERROR;
                }

                if (!pattern_matching_inference(unit, callee, param_expr->declaration.type, arg_type, arg_expr, arg_type))
                    return YIELD_ERROR;

                param_infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;
                allocate_unit_storage(unit, arg_type, &param_infer->size, &param_infer->offset);
                set_inferred_type(callee, param_id, arg_type);
                override_made_progress = true;  // We made progress by inferring the callee's param type.
            }

            auto* type_infer = &callee->inferred_expressions[param_expr->declaration.type];
            if (type_infer->type == INVALID_TYPE || type_infer->type != TYPE_SOFT_TYPE)
            {
                // We don't immediately Wait(), to enable out of order parameter inference.
                waiting_on_a_parameter = param_expr->declaration.type;
                continue;
            }

            Type param_type = type_infer->constant_type;

            if (args->count == parameter_index)
            {
                if (param_infer->type != INVALID_TYPE) continue;  // already inferred

                // The last parameter, which isn't in our expression list but is in the callee's
                // parameter scope, is the block parameter.
                assert(param_type == TYPE_SOFT_BLOCK);
                assert(param_expr->flags & EXPRESSION_DECLARATION_IS_ALIAS);
                assert(expr->call.block != NO_EXPRESSION);
                assert(block->inferred_expressions[expr->call.block].type == TYPE_SOFT_BLOCK);

                Soft_Block soft = block->inferred_expressions[expr->call.block].constant_block;
                type_infer->constant_block = soft;

                param_infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;
                param_infer->constant_block = soft;
                set_inferred_type(callee, param_id, TYPE_SOFT_BLOCK);
                override_made_progress = true;  // We made progress by inferring the callee's param type.
            }
            else
            {
                assert(args->count > parameter_index);
                auto* arg_expr  = &block->parsed_expressions  [args->expressions[parameter_index]];
                auto* arg_infer = &block->inferred_expressions[args->expressions[parameter_index]];
                Type  arg_type  = arg_infer->type;
                if (param_expr->flags & EXPRESSION_DECLARATION_IS_ALIAS)
                {
                    if (param_infer->type != INVALID_TYPE) continue;  // already inferred

                    if (is_numeric_type(param_type))
                    {
                        if (is_floating_point_type(param_type))
                            NotImplemented;

                        if (arg_type != TYPE_SOFT_NUMBER)
                            Error("Argument #% ('%') is expected to be a compile-time integer, but is %.",
                                parameter_index + 1, param_name,
                                vague_type_description_in_compile_time_context(unit, arg_type));

                        Fraction const* fraction = &block->constants[arg_infer->constant_index];
                        if (!fract_is_integer(fraction))
                            Error("Argument #% ('%') is expected to be a compile-time integer, but is fractional.\n"
                                  "Value is %.", parameter_index + 1, param_name, fract_display(fraction));

                        if (!check_constant_fits_in_runtime_type(unit, arg_expr, fraction, param_type))
                            return YIELD_ERROR;

                        param_infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;
                        param_infer->constant_index = add_constant(callee, fract_clone(fraction));
                        set_inferred_type(callee, param_id, TYPE_SOFT_NUMBER);
                        override_made_progress = true;  // We made progress by inferring the callee's param type.
                    }
                    else if (is_bool_type(param_type))
                    {
                        if (arg_type != TYPE_SOFT_BOOL)
                            Error("Argument #% ('%') is expected to be a compile-time boolean, but is %.",
                                parameter_index + 1, param_name,
                                vague_type_description_in_compile_time_context(unit, arg_type));
                        param_infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;
                        param_infer->constant_bool = arg_infer->constant_bool;
                        set_inferred_type(callee, param_id, TYPE_SOFT_BOOL);
                        override_made_progress = true;  // We made progress by inferring the callee's param type.
                    }
                    else if (param_type == TYPE_TYPE)
                    {
                        if (arg_type != TYPE_SOFT_TYPE)
                            Error("Argument #% ('%') is expected to be a compile-time type, but is %.",
                                parameter_index + 1, param_name,
                                vague_type_description_in_compile_time_context(unit, arg_type));
                        param_infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;
                        param_infer->constant_type = arg_infer->constant_type;
                        set_inferred_type(callee, param_id, TYPE_SOFT_TYPE);
                        override_made_progress = true;  // We made progress by inferring the callee's param type.
                    }
                    else Unreachable;
                }
                else
                {
                    if (param_type != arg_type)
                        Error("Argument #% ('%') doesn't match the parameter type.\n"
                              "    expected: %\n"
                              "    received: %",
                              parameter_index + 1, param_name,
                              exact_type_description(unit, param_type),
                              exact_type_description(unit, arg_type));
                }
            }
        }

        if (waiting_on_a_parameter != NO_EXPRESSION)
            Wait(WAITING_ON_PARAMETER_INFERENCE, waiting_on_a_parameter, callee);

        // @Incomplete - yield type
        Infer(TYPE_VOID);
    } break;

    case EXPRESSION_DECLARATION:
    {
        if (expr->flags & EXPRESSION_DECLARATION_IS_PARAMETER)
            infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;

        Type type = INVALID_TYPE;
        if (expr->declaration.type != NO_EXPRESSION)
        {
            auto* type_expr = &block->parsed_expressions[expr->declaration.type];
            if (type_expr->flags & EXPRESSION_HAS_TO_BE_EXTERNALLY_INFERRED)
            {
                assert(expr->declaration.value != NO_EXPRESSION);
                auto* value_expr = &block->parsed_expressions[expr->declaration.value];
                Type value_type = block->inferred_expressions[expr->declaration.value].type;
                if (value_type == INVALID_TYPE) WaitOperand(expr->declaration.value);
                if (is_soft_type(value_type))
                {
                    Report(unit->ctx).part(value_expr, Format(temp, "A runtime value is required here, but this is %.\n",
                                                              vague_type_description_in_compile_time_context(unit, value_type)))
                                     .part(type_expr, "This is because the declaration type is inferred from the value."_s)
                                     .done();
                    return YIELD_ERROR;
                }
                if (!pattern_matching_inference(unit, block, expr->declaration.type, value_type, value_expr, value_type))
                    return YIELD_ERROR;
            }

            auto* type_infer = &block->inferred_expressions[expr->declaration.type];
            if (type_infer->type == INVALID_TYPE) WaitOperand(expr->declaration.type);

            if (!is_type_type(type_infer->type))
                Error("Expected a type after ':' in declaration, but got %.", vague_type_description_in_compile_time_context(unit, type_infer->type));
            if (type_infer->type != TYPE_SOFT_TYPE)
                Error("The type in declaration is not known at compile-time.");
            type = type_infer->constant_type;
        }

        Type value_type = INVALID_TYPE;
        if (expr->declaration.value != NO_EXPRESSION)
        {
            value_type = block->inferred_expressions[expr->declaration.value].type;
            if (value_type == INVALID_TYPE) WaitOperand(expr->declaration.value);
        }

        assert(!(expr->flags & EXPRESSION_DECLARATION_IS_INFERRED_ALIAS));
        if (expr->flags & EXPRESSION_DECLARATION_IS_ALIAS)
        {
            assert(expr->declaration.value != NO_EXPRESSION);
            auto* value_expr  = &block->parsed_expressions  [expr->declaration.value];
            auto* value_infer = &block->inferred_expressions[expr->declaration.value];
            if (value_infer->type == INVALID_TYPE) WaitOperand(expr->declaration.value);
            if (value_infer->type == TYPE_SOFT_NUMBER)
                infer->constant_index = add_constant(block, fract_clone(&block->constants[value_infer->constant_index]));
            else if (value_infer->type == TYPE_SOFT_BOOL)
                infer->constant_bool = value_infer->constant_bool;
            else if (value_infer->type == TYPE_SOFT_TYPE)
                infer->constant_type = value_infer->constant_type;
            else if (value_infer->type == TYPE_SOFT_BLOCK)
                infer->constant_block = value_infer->constant_block;
            else
            {
                Report(unit->ctx).part(value_expr, Format(temp, "A compile-time value is required here, but this is %.",
                                                          vague_type_description_in_compile_time_context(unit, value_infer->type)))
                                 .done();
                return YIELD_ERROR;
            }

            Infer(value_infer->type);
        }
        else
        {
            assert(type != INVALID_TYPE || value_type != INVALID_TYPE);
            if (value_type == TYPE_SOFT_BLOCK)
                Error("Blocks can't be assigned to runtime values.");
            if (expr->declaration.type == NO_EXPRESSION)
            {
                if (is_soft_type(value_type))
                {
                    assert(expr->declaration.value != NO_EXPRESSION);
                    auto* value_expr = &block->parsed_expressions[expr->declaration.value];
                    Report(unit->ctx).part(value_expr, Format(temp, "A runtime value is required here, but this is %.\n",
                                                              vague_type_description_in_compile_time_context(unit, value_type)))
                                     .done();
                    return YIELD_ERROR;
                }
                type = value_type;
            }
            assert(type != INVALID_TYPE);

            if (expr->declaration.value != NO_EXPRESSION && type != value_type)
                Error("Types don't match.\n"
                      "    lhs: %\n"
                      "    rhs: %",
                      exact_type_description(unit, type),
                      exact_type_description(unit, value_type));

            allocate_unit_storage(unit, type, &infer->size, &infer->offset);
            Infer(type);
        }
    } break;

    case EXPRESSION_RUN: NotImplemented;

    case EXPRESSION_DEBUG:
    {
        Inferred_Expression* op_infer = &block->inferred_expressions[expr->unary_operand];
        if (op_infer->type == INVALID_TYPE) WaitOperand(expr->unary_operand);
        Infer(TYPE_VOID);
    } break;

    IllegalDefaultCase;
    }

#undef Infer
#undef Wait
#undef WaitOperand
#undef Error

    Unreachable;
}

static Yield_Result infer_block(Pipeline_Task* task)
{
    Unit*  unit  = task->unit;
    Block* block = task->block;

#define STRESS_TEST 1

#if STRESS_TEST
    static Random rng = {};
    OnlyOnce seed(&rng);

    Array<Expression> visit_order = allocate_array<Expression>(NULL, block->parsed_expressions.count);
    for (umm i = 0; i < visit_order.count; i++)
        visit_order[i] = (Expression) i;
    shuffle_array(&rng, visit_order);
#endif

    bool made_progress = false;
    bool waiting       = false;

#if STRESS_TEST
    for (Expression* id_ptr : visit_order)
    {
        Expression id = *id_ptr;
#else
    for (Expression id = (Expression) 0; id < block->parsed_expressions.count; id = (Expression)(id + 1))
    {
#endif

        if (block->inferred_expressions[id].type != INVALID_TYPE) continue;

        Yield_Result result = infer_expression(task, id);
        if (result == YIELD_COMPLETED || result == YIELD_MADE_PROGRESS)
            made_progress = true;
        else if (result == YIELD_NO_PROGRESS)
            waiting = true;
        else if (result == YIELD_ERROR)
            return YIELD_ERROR;
        else Unreachable;

#if STRESS_TEST
        if (made_progress)
            return YIELD_MADE_PROGRESS;
#endif
    }

    if (waiting)
        return made_progress ? YIELD_MADE_PROGRESS : YIELD_NO_PROGRESS;
    return YIELD_COMPLETED;
}


Unit* materialize_unit(Compiler* ctx, Block* initiator)
{
    Unit* unit;
    {
        Region memory = {};
        unit = PushValue(&memory, Unit);
        unit->memory = memory;
    }

    unit->ctx            = ctx;
    unit->initiator_from = initiator->from;
    unit->initiator_to   = initiator->to;

    unit->pointer_size      = sizeof(void*);
    unit->pointer_alignment = alignof(void*);

    unit->entry_block = materialize_block(unit, initiator, NULL, NO_VISIBILITY);
    return unit;
}


bool pump_pipeline(Compiler* ctx)
{
    while (true)
    {
        bool made_progress = false;
        bool tried_to_make_progress = false;
        for (umm it_index = 0; it_index < ctx->pipeline.count; it_index++)
        {
            if (!ctx->pipeline[it_index].block) continue;  // already completed
            tried_to_make_progress = true;

            Yield_Result result = infer_block(&ctx->pipeline[it_index]);
            switch (result)
            {
            case YIELD_COMPLETED:       ctx->pipeline[it_index].block = NULL;  // fallthrough
            case YIELD_MADE_PROGRESS:   made_progress = true;                  // fallthrough
            case YIELD_NO_PROGRESS:     break;
            case YIELD_ERROR:           return false;
            IllegalDefaultCase;
            }

            Unit* unit = ctx->pipeline[it_index].unit;
            if (unit->materialized_block_count >= MAX_BLOCKS_PER_UNIT)
            {
                Report(ctx).part(&unit->initiator_from, Format(temp, "Too many blocks instantiated in this unit. Maximum is %.", MAX_BLOCKS_PER_UNIT))
                           .part(&unit->most_recent_materialized_block->from, "The most recent instantiated block is here. It may or may not be part of the problem."_s)
                           .done();
                return false;
            }
        }

        if (!tried_to_make_progress)
            break;

        if (!made_progress)
        {
            Report report(ctx);
            report.intro(SEVERITY_ERROR);
            report.message("Can't make inference progress."_s);
            For (ctx->pipeline)
            {
                Block* block = it->block;
                if (!block) continue;
                For (block->waiting_expressions)
                {
                    auto* expr = &block->parsed_expressions[it->key];
                    Parsed_Expression const* waiting_on = NULL;
                    if (it->value.on_expression != NO_EXPRESSION)
                        waiting_on = &it->value.on_block->parsed_expressions[it->value.on_expression];

                    if (it->value.why == WAITING_ON_OPERAND)
                        continue;
                    else if (it->value.why == WAITING_ON_DECLARATION)
                    {
                        report.continuation();
                        report.message("This name is waiting for a declaration..."_s);
                        report.snippet(expr, /* skinny */ true);
                        if (waiting_on)
                        {
                            report.message("and this is the declaration it's waiting for."_s);
                            report.snippet(waiting_on, /* skinny */ true);
                        }
                    }
                    else if (it->value.why == WAITING_ON_PARAMETER_INFERENCE)
                    {
                        report.continuation();
                        report.message("This call is waiting for a parameter type."_s);
                        report.snippet(expr, /* skinny */ true);
                        if (waiting_on)
                        {
                            report.message("and this is the parameter it's waiting for."_s);
                            report.snippet(waiting_on, /* skinny */ true);
                        }
                    }
                    else if (it->value.why == WAITING_ON_EXTERNAL_INFERENCE)
                    {
                        report.continuation();
                        report.message("This alias is waiting to be inferred."_s);
                        report.snippet(expr, /* skinny */ true);
                    }
                    else Unreachable;
                }
            }
            report.done();
            return false;
        }
    }

    return true;
}



ExitApplicationNamespace
