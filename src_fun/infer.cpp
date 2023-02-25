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
    if (parent_scope && parent_scope_visibility_limit != NO_VISIBILITY)
        block->block_parameter = parent_scope->block_parameter;

    Pipeline_Task task = {};
    task.unit  = unit;
    task.block = block;
    add_item(&unit->ctx->pipeline, &task);
    return block;
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



static u64 get_type_size(Unit* unit, Type type)
{
    if (get_indirection(type))
        return unit->pointer_size;

    if (is_user_defined_type(type))
        NotImplemented;

    switch (type)
    {
    case TYPE_VOID:                return 0;
    case TYPE_SOFT_ZERO:           Unreachable;
    case TYPE_U8:                  return 1;
    case TYPE_U16:                 return 2;
    case TYPE_U32:                 return 4;
    case TYPE_U64:                 return 8;
    case TYPE_S8:                  return 1;
    case TYPE_S16:                 return 2;
    case TYPE_S32:                 return 4;
    case TYPE_S64:                 return 8;
    case TYPE_SOFT_INTEGER:        Unreachable;
    case TYPE_F16:                 return 2;
    case TYPE_F32:                 return 4;
    case TYPE_F64:                 return 8;
    case TYPE_SOFT_FLOATING_POINT: Unreachable;
    case TYPE_BOOL8:               return 1;
    case TYPE_BOOL16:              return 2;
    case TYPE_BOOL32:              return 4;
    case TYPE_BOOL64:              return 8;
    case TYPE_SOFT_BOOL:           Unreachable;
    case TYPE_TYPE:                return 4;
    case TYPE_SOFT_TYPE:           Unreachable;
    IllegalDefaultCase;
    }
}

static u64 get_type_alignment(Unit* unit, Type type)
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



enum Yield_Result
{
    YIELD_COMPLETED,
    YIELD_MADE_PROGRESS,
    YIELD_NO_PROGRESS,
    YIELD_ERROR,
};

static Yield_Result check_expression(Pipeline_Task* task, Expression id)
{
    Unit*  unit  = task->unit;
    Block* block = task->block;

    bool override_made_progress = false;

    auto add_constant_integer = [](Block* block, Integer* integer) -> u32
    {
        u32 index = block->constants.count;
        add_item(&block->constants, integer);
        return index;
    };

    auto* expr  = &block->parsed_expressions[id];
    auto* infer = &block->inferred_expressions[id];
    assert(infer->type == INVALID_TYPE);


    #define Infer(t)                                                            \
    {                                                                           \
        assert(t != INVALID_TYPE);                                              \
        if (t == TYPE_SOFT_ZERO)                                                \
            assert(expr->kind == EXPRESSION_ZERO);                              \
        else if (is_soft_type(t))                                               \
            assert(infer->constant_index != INVALID_CONSTANT_INDEX);            \
        if (is_soft_type(t))                                                    \
        {                                                                       \
            infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;    \
            assert(infer->size   == INVALID_STORAGE_SIZE);                      \
            assert(infer->offset == INVALID_STORAGE_OFFSET);                    \
        }                                                                       \
        infer->type = t;                                                        \
        return YIELD_COMPLETED;                                                 \
    }

    #define Wait() return override_made_progress ? YIELD_MADE_PROGRESS : YIELD_NO_PROGRESS;

    #define Error(...) return (report_error(unit->ctx, expr, Format(temp, ##__VA_ARGS__)), YIELD_ERROR)

    switch (expr->kind)
    {

    case EXPRESSION_ZERO:                   Infer(TYPE_SOFT_ZERO);
    case EXPRESSION_TRUE:                   infer->constant_bool = true;  Infer(TYPE_SOFT_BOOL);
    case EXPRESSION_FALSE:                  infer->constant_bool = false; Infer(TYPE_SOFT_BOOL);
    case EXPRESSION_INTEGER_LITERAL:
    {
        Token_Info_Integer* token_info = (Token_Info_Integer*) get_token_info(unit->ctx, &expr->literal);

        Integer integer = int_clone(&token_info->value);
        infer->constant_index = add_constant_integer(block, &integer);
        Infer(TYPE_SOFT_INTEGER);
    } break;

    case EXPRESSION_FLOATING_POINT_LITERAL:
        NotImplemented;

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

                        report_error(unit->ctx, name, Format(temp, "Can't find name '%'.", identifier), decl_name, hint);
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
                report_error(unit->ctx, name, Format(temp, "Can't find name '%'.", identifier),
                             best_alternative, Format(temp, "Maybe you meant '%'?", get_identifier(unit->ctx, best_alternative)));
                return YIELD_ERROR;
            }

            Error("Can't find name '%'.", identifier);
        }

        Inferred_Expression* decl_infer = &decl_scope->inferred_expressions[decl_id];
        if (decl_infer->type == INVALID_TYPE) Wait();

        if (decl_infer->type == TYPE_SOFT_INTEGER)
        {
            Integer copy = int_clone(&decl_scope->constants[decl_infer->constant_index]);
            infer->constant_index = add_constant_integer(block, &copy);
        }
        else if (decl_infer->type == TYPE_SOFT_FLOATING_POINT)
        {
            NotImplemented;
        }
        else if (decl_infer->type == TYPE_SOFT_BOOL)
        {
            infer->constant_bool = decl_infer->constant_bool;
        }
        else if (decl_infer->type == TYPE_SOFT_TYPE)
        {
            infer->constant_type = decl_infer->constant_type;
        }
        else if (decl_infer->type == TYPE_SOFT_BLOCK)
        {
            infer->constant_block = decl_infer->constant_block;
        }
        else
        {
            assert(!is_soft_type(decl_infer->type));
            assert(decl_infer->size   != INVALID_STORAGE_SIZE);
            assert(decl_infer->offset != INVALID_STORAGE_OFFSET);
            infer->size   = decl_infer->size;
            infer->offset = decl_infer->offset;
        }

        Infer(decl_infer->type);
    } break;

    case EXPRESSION_NEGATE:
    {
        Inferred_Expression* op_infer = &block->inferred_expressions[expr->unary_operand];
        if (op_infer->type == INVALID_TYPE) Wait();

        if (!is_numeric_type(op_infer->type))
            Error("Unary operator '-' expects a numeric argument.");

        if (op_infer->type == TYPE_SOFT_INTEGER)
        {
            Integer integer = int_clone(&block->constants[op_infer->constant_index]);
            int_negate(&integer);
            infer->constant_index = add_constant_integer(block, &integer);
        }
        else if (op_infer->type == TYPE_SOFT_FLOATING_POINT)
        {
            NotImplemented;
        }
        else assert(!is_soft_type(op_infer->type));

        Infer(op_infer->type);
    } break;

    case EXPRESSION_ADDRESS:
    {
        Inferred_Expression* op_infer = &block->inferred_expressions[expr->unary_operand];
        if (op_infer->type == INVALID_TYPE) Wait();

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
            Error("Can't take an address of a compile-time value.");
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
        if (op_infer->type == INVALID_TYPE) Wait();

        if (is_type_type(op_infer->type))
        {
            if (op_infer->type != TYPE_SOFT_TYPE)
                Error("The operand to '*' is a type not known at compile-time.");

            Type type = op_infer->constant_type;
            u32 indirection = get_indirection(type);
            if (!indirection)
                Error("The operand to '*' is not a pointer!");
            infer->constant_type = set_indirection(type, indirection - 1);
            Infer(TYPE_SOFT_TYPE);
        }
        else if (is_soft_type(op_infer->type))
        {
            Error("Can't dereference a compile-time value.");
        }
        else
        {
            Type type = op_infer->type;
            u32 indirection = get_indirection(type);
            if (!indirection)
                Error("The operand to '*' is not a pointer!");
            Infer(set_indirection(type, indirection - 1));
        }
    } break;

    case EXPRESSION_ASSIGNMENT:
    {
        if (block->parsed_expressions[expr->binary.lhs].kind != EXPRESSION_NAME)
            Error("Expected a variable name on the left side of the assignment.");

        Type lhs_type = block->inferred_expressions[expr->binary.lhs].type;
        Type rhs_type = block->inferred_expressions[expr->binary.rhs].type;
        if (lhs_type == INVALID_TYPE || rhs_type == INVALID_TYPE) Wait();
        if (lhs_type != rhs_type)
            Error("LHS and RHS types don't match.");
        Infer(lhs_type);
    } break;

    case EXPRESSION_ADD:
    case EXPRESSION_SUBTRACT:
    case EXPRESSION_MULTIPLY:
    case EXPRESSION_DIVIDE:
    {
        Type lhs_type = block->inferred_expressions[expr->binary.lhs].type;
        Type rhs_type = block->inferred_expressions[expr->binary.rhs].type;
        if (lhs_type == INVALID_TYPE || rhs_type == INVALID_TYPE) Wait();
        if (lhs_type != rhs_type)
            Error("LHS and RHS types don't match.");
        if (!is_numeric_type(lhs_type))
            Error("Expected a numeric operand.");

        if (lhs_type == TYPE_SOFT_INTEGER)
        {
            Integer const* lhs_int = &block->constants[block->inferred_expressions[expr->binary.lhs].constant_index];
            Integer const* rhs_int = &block->constants[block->inferred_expressions[expr->binary.rhs].constant_index];
            Integer result = int_clone(lhs_int);
                 if (expr->kind == EXPRESSION_ADD)      int_add(&result, rhs_int);
            else if (expr->kind == EXPRESSION_SUBTRACT) int_sub(&result, rhs_int);
            else if (expr->kind == EXPRESSION_MULTIPLY) int_mul(&result, rhs_int);
            else if (expr->kind == EXPRESSION_DIVIDE)
            {
                Integer mod = {};
                Defer(int_free(&mod));
                if (!int_div(&result, rhs_int, &mod))
                    Error("Division by zero!");
            }
            infer->constant_index = add_constant_integer(block, &result);
        }
        else if (lhs_type == TYPE_SOFT_FLOATING_POINT)
        {
            NotImplemented;
        }
        else assert(!is_soft_type(lhs_type));

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
        if (lhs_infer->type == INVALID_TYPE || rhs_infer->type == INVALID_TYPE) Wait();

        if (!is_type_type(lhs_infer->type))
            Error("Expected a type as the first operand to 'cast'.");
        if (lhs_infer->type != TYPE_SOFT_TYPE)
            Error("The first operand to 'cast' is not known at compile-time.");

        if (!is_numeric_type(rhs_infer->type))  // @Incomplete
            Error("Expected a numeric value as the second operand to 'cast'.");

        Type cast_type = lhs_infer->constant_type;
        if (!is_numeric_type(cast_type))
            Error("Expected a numeric type as the first operand to 'cast'.");

        if (rhs_infer->type == TYPE_SOFT_INTEGER)
        {
            if (!is_integer_type(cast_type))
                NotImplemented;

            u64 target_size = get_type_size(unit, cast_type);

            Integer const* integer = &block->constants[rhs_infer->constant_index];
            if (integer->negative)
            {
                if (is_unsigned_integer_type(cast_type))
                    Error("The cast value operand is a negative constant, it can't be cast to an unsigned type.");

                Integer min = {};
                int_set_zero(&min);
                int_set_bit(&min, target_size * 8 - 1);
                if (int_compare_abs(integer, &min) > 0)
                    Error("The cast value operand doesn't fit in the specified type.");
            }
            else
            {
                u64 max_log2 = (target_size * 8) - (is_signed_integer_type(cast_type) ? 1 : 0);
                if (int_log2_abs(integer) >= max_log2)
                    Error("The cast value operand doesn't fit in the specified type.");
            }

            Integer copy = int_clone(integer);
            infer->constant_index = add_constant_integer(block, &copy);
        }
        else if (rhs_infer->type == TYPE_SOFT_FLOATING_POINT)
        {
            NotImplemented;
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
            if (type == INVALID_TYPE) Wait();
            if (!is_integer_type(type) && !is_bool_type(type))
                Error("Expected an integer or boolean value as the condition.");
            if (is_soft_type(type))
                Error("@Incomplete - condition may not be a compile-time value");
        }

        Type success_type = INVALID_TYPE;
        Type failure_type = INVALID_TYPE;
        if (expr->branch.on_success != NO_EXPRESSION)
        {
            success_type = block->inferred_expressions[expr->branch.on_success].type;
            if (success_type == INVALID_TYPE) Wait();
        }
        if (expr->branch.on_failure != NO_EXPRESSION)
        {
            failure_type = block->inferred_expressions[expr->branch.on_failure].type;
            if (failure_type == INVALID_TYPE) Wait();
        }

        if (success_type != INVALID_TYPE && failure_type != INVALID_TYPE)
            if (success_type != failure_type)
                Error("The expression yields a different type depending on the condition.");
        Infer(success_type);
    } break;

    case EXPRESSION_CALL:
    {
        // Make sure everything on our side is inferred.
        auto* lhs_infer = &block->inferred_expressions[expr->call.lhs];
        if (lhs_infer->type == INVALID_TYPE) Wait();
        if (lhs_infer->type != TYPE_SOFT_BLOCK)
            Error("Expected a block on the left-hand side of the call expression.");

        Expression_List const* args = expr->call.arguments;
        for (umm arg = 0; arg < args->count; arg++)
            if (block->inferred_expressions[args->expressions[arg]].type == INVALID_TYPE)
                Wait();

        if (expr->call.block != NO_EXPRESSION)
        {
            auto* block_infer = &block->inferred_expressions[expr->call.block];
            if (block_infer->type == INVALID_TYPE) Wait();
            if (block_infer->type != TYPE_SOFT_BLOCK)
                Error("Internal error: Expected a block as the baked block parameter. But this should always be the case?");
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
            if (regular_parameter_count != args->count + (have_block ? 1 : 0))
                Error("Callee expects % %, but you provided %.",
                    regular_parameter_count, plural(regular_parameter_count, "parameter"_s, "parameters"_s),
                    args->count);

            // Now materialize the callee
            Visibility visibility = (expr->flags & EXPRESSION_ALLOW_PARENT_SCOPE_VISIBILITY)
                                  ? expr->visibility_limit
                                  : NO_VISIBILITY;
            Block* callee = materialize_block(unit, lhs_block, lhs_parent, visibility);
            infer->called_block = callee;

            override_made_progress = true;  // We made progress by materializing the callee.
        }

        bool waiting_on_a_parameter = false;

        // Second stage: checking the parameter types
        Block* callee = infer->called_block;
        umm parameter_index = 0;
        For (callee->imperative_order)
        {
            auto* param_expr  = &callee->parsed_expressions  [*it];
            auto* param_infer = &callee->inferred_expressions[*it];
            if (!(param_expr->flags & EXPRESSION_DECLARATION_IS_PARAMETER)) continue;
            Defer(parameter_index++);
            assert(param_expr->declaration.type != NO_EXPRESSION);
            assert(param_expr->declaration.value == NO_EXPRESSION);

            auto* type_infer = &callee->inferred_expressions[param_expr->declaration.type];
            if (type_infer->type == INVALID_TYPE)
            {
                // We don't immediately Wait(), to enable out of order parameter inference.
                waiting_on_a_parameter = true;
                continue;
            }
            if (type_infer->type != TYPE_SOFT_TYPE) Wait();  // let the callee report this error
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
                param_infer->type = TYPE_SOFT_BLOCK;
                override_made_progress = true;  // We made progress by inferring the callee's param type.
            }
            else
            {
                assert(args->count > parameter_index);
                auto* arg_infer = &block->inferred_expressions[args->expressions[parameter_index]];
                if (param_expr->flags & EXPRESSION_DECLARATION_IS_ALIAS)
                {
                    if (param_infer->type != INVALID_TYPE) continue;  // already inferred

                    if (is_integer_type(param_type))
                    {
                        if (arg_infer->type != TYPE_SOFT_INTEGER)
                            Error("Argument #% is expected to be a compile-time integer.", parameter_index + 1);
                        // @Incomplete - check if the value fits in the actual type
                        Integer copy = int_clone(&block->constants[arg_infer->constant_index]);
                        param_infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;
                        param_infer->constant_index = add_constant_integer(callee, &copy);
                        param_infer->type = TYPE_SOFT_INTEGER;
                        override_made_progress = true;  // We made progress by inferring the callee's param type.
                    }
                    else if (is_floating_point_type(param_type))
                    {
                        NotImplemented;
                    }
                    else if (is_bool_type(param_type))
                    {
                        if (arg_infer->type != TYPE_SOFT_BOOL)
                            Error("Argument #% is expected to be a compile-time boolean.", parameter_index + 1);
                        param_infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;
                        param_infer->constant_bool = arg_infer->constant_bool;
                        param_infer->type = TYPE_SOFT_BOOL;
                        override_made_progress = true;  // We made progress by inferring the callee's param type.
                    }
                    else if (param_type == TYPE_TYPE)
                    {
                        if (arg_infer->type != TYPE_SOFT_TYPE)
                            Error("Argument #% is expected to be a compile-time type.", parameter_index + 1);
                        param_infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;
                        param_infer->constant_type = arg_infer->constant_type;
                        param_infer->type = TYPE_SOFT_TYPE;
                        override_made_progress = true;  // We made progress by inferring the callee's param type.
                    }
                    else Unreachable;
                }
                else
                {
                    if (param_type != arg_infer->type)
                        Error("Argument #% doesn't match the parameter type.", parameter_index + 1);
                }
            }
        }

        if (waiting_on_a_parameter)
            Wait();

        // @Incomplete - yield type
        Infer(TYPE_VOID);
    } break;

    case EXPRESSION_DECLARATION:
    {
        if (expr->flags & EXPRESSION_DECLARATION_IS_PARAMETER)
            infer->flags |= INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME;

        if (expr->flags & EXPRESSION_DECLARATION_IS_ALIAS)
        {
            if (expr->declaration.value == NO_EXPRESSION)
            {
                assert(expr->flags & EXPRESSION_DECLARATION_IS_PARAMETER);
                Wait();  // we can't infer this ourself, we're waiting for the callee to infer it for us
            }

            auto* value_infer = &block->inferred_expressions[expr->declaration.value];
            if (value_infer->type == INVALID_TYPE) Wait();
            if (value_infer->type == TYPE_SOFT_INTEGER)
            {
                Integer copy = int_clone(&block->constants[value_infer->constant_index]);
                infer->constant_index = add_constant_integer(block, &copy);
            }
            else if (value_infer->type == TYPE_SOFT_FLOATING_POINT)
            {
                NotImplemented;
            }
            else if (value_infer->type == TYPE_SOFT_BOOL)
            {
                infer->constant_bool = value_infer->constant_bool;
            }
            else if (value_infer->type == TYPE_SOFT_TYPE)
            {
                infer->constant_type = value_infer->constant_type;
            }
            else if (value_infer->type == TYPE_SOFT_BLOCK)
            {
                infer->constant_block = value_infer->constant_block;
            }
            else if (!is_soft_type(value_infer->type))
                Error("RHS is not a constant.");
            else Unreachable;

            Infer(value_infer->type);
        }
        else
        {
            Type value_type = INVALID_TYPE;
            if (expr->declaration.value != NO_EXPRESSION)
            {
                value_type = block->inferred_expressions[expr->declaration.value].type;
                if (value_type == INVALID_TYPE) Wait();
                if (value_type == TYPE_SOFT_BLOCK)
                    Error("Blocks can't be assigned to runtime values.");
            }

            Type type = INVALID_TYPE;
            if (expr->declaration.type != NO_EXPRESSION)
            {
                auto* type_infer = &block->inferred_expressions[expr->declaration.type];
                if (type_infer->type == INVALID_TYPE) Wait();

                if (!is_type_type(type_infer->type))
                    Error("Expected a type after ':' in declaration.");
                if (type_infer->type != TYPE_SOFT_TYPE)
                    Error("The type in declaration is not known at compile-time.");
                type = type_infer->constant_type;
            }
            else
            {
                if (is_soft_type(value_type))
                    Error("An explicit type is required in this context.");
                type = value_type;
            }

            if (expr->declaration.value != NO_EXPRESSION && type != value_type)
                Error("LHS and RHS types don't match.");

            allocate_unit_storage(unit, type, &infer->size, &infer->offset);
            Infer(type);
        }
    } break;

    case EXPRESSION_RUN: NotImplemented;

    case EXPRESSION_DEBUG:
    {
        Inferred_Expression* op_infer = &block->inferred_expressions[expr->unary_operand];
        if (op_infer->type == INVALID_TYPE) Wait();
        Infer(TYPE_VOID);
    } break;

    IllegalDefaultCase;
    }
#undef Infer
#undef Wait
#undef Error

    Unreachable;
}

static Yield_Result check_block(Pipeline_Task* task)
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

        Yield_Result result = check_expression(task, id);
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


Unit* materialize_unit(Compiler* ctx, Run* initiator)
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
    unit->initiator_run  = initiator;

    unit->pointer_size      = sizeof(void*);
    unit->pointer_alignment = alignof(void*);

    unit->entry_block    = materialize_block(unit, initiator->entry_block, NULL, NO_VISIBILITY);
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

            Yield_Result result = check_block(&ctx->pipeline[it_index]);
            switch (result)
            {
            case YIELD_COMPLETED:       ctx->pipeline[it_index].block = NULL;  // fallthrough
            case YIELD_MADE_PROGRESS:   made_progress = true;                  // fallthrough
            case YIELD_NO_PROGRESS:     break;
            case YIELD_ERROR:           return false;
            IllegalDefaultCase;
            }
        }

        if (!tried_to_make_progress)
            break;

        if (!made_progress)
            return report_error_locationless(ctx, "Can't progress typechecking."_s);
    }

    return true;
}



ExitApplicationNamespace
