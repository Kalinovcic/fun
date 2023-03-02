#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
#include "api.h"
#include <stdio.h>


EnterApplicationNamespace



#if 0
#define TRACE 0

union Memory
{
    byte  base[0];
    byte* as_address;
    u8    as_u8;
    u16   as_u16;
    u32   as_u32;
    u64   as_u64;
    u64   as_umm;
    s8    as_s8;
    s16   as_s16;
    s32   as_s32;
    s64   as_s64;
    smm   as_smm;
    f32   as_f32;
    f64   as_f64;
    struct
    {
        u8 value;
        inline operator bool()        { return value; }
        inline void operator=(bool v) { value = v ? 1 : 0; }
    } as_bool;
};

void vm(Runtime_Code_Address at)
{
next_instruction:
    if (!at.unit) return;

    Unit* unit = at.unit;
    Expression_Reference ref = unit->execution_order[at.instruction];
    if (!ref.block)
    {
        auto* expr = &unit->runtime_expressions[ref.id];
        switch (expr->kind)
        {
        case RUNTIME_EXPRESSION_YIELD:
        {
            assert(expr->storage_size == sizeof(Runtime_Code_Address));
            at = *(Runtime_Code_Address*)(at.storage + expr->storage_offset);
            goto next_instruction;
        } break;
        case RUNTIME_EXPRESSION_GOTO:
        {
            at.instruction = expr->instruction;
            goto next_instruction;
        } break;
        case RUNTIME_EXPRESSION_BRANCH_IF_FALSE:
        {
            u64 condition = 0;
            assert(expr->storage_size <= sizeof(condition));
            memcpy(&condition, at.storage + expr->storage_offset, expr->storage_size);
            if (condition) break;
            at.instruction = expr->instruction;
            goto next_instruction;
        } break;
        IllegalDefaultCase;
        }
    }
    else
    {
        Block*  block   = ref.block;
        auto*   expr    = &block->parsed_expressions  [ref.id];
        auto*   infer   = &block->inferred_expressions[ref.id];
        Memory* address = (Memory*)(at.storage + infer->offset);

        auto get_operand = [&](Expression op)
        {
            return (Memory*)(at.storage + block->inferred_expressions[op].offset);
        };

        auto specialize = [](Type type, Memory* memory, auto&& lambda)
        {
            switch (type)
            {
            case TYPE_U8:     lambda(&memory->as_u8);     break;
            case TYPE_U16:    lambda(&memory->as_u16);    break;
            case TYPE_U32:    lambda(&memory->as_u32);    break;
            case TYPE_U64:    lambda(&memory->as_u64);    break;
            case TYPE_UMM:    lambda(&memory->as_umm);    break;
            case TYPE_S8:     lambda(&memory->as_s8);     break;
            case TYPE_S16:    lambda(&memory->as_s16);    break;
            case TYPE_S32:    lambda(&memory->as_s32);    break;
            case TYPE_S64:    lambda(&memory->as_s64);    break;
            case TYPE_SMM:    lambda(&memory->as_smm);    break;
            case TYPE_F32:    lambda(&memory->as_f32);    break;
            case TYPE_F64:    lambda(&memory->as_f64);    break;
            case TYPE_BOOL:   lambda(&memory->as_bool);   break;
            IllegalDefaultCase;
            }
        };

        auto specialize_numeric_binary = [&](auto&& lambda)
        {
            assert(block->inferred_expressions[expr->binary.lhs].type == infer->type);
            assert(block->inferred_expressions[expr->binary.rhs].type == infer->type);
            Memory* lhs = get_operand(expr->binary.lhs);
            Memory* rhs = get_operand(expr->binary.rhs);
            specialize(infer->type, address, [&](auto* v) { lambda(v, (decltype(v)) lhs, (decltype(v)) rhs); });
        };

        auto specialize_logic_binary = [&](auto&& lambda)
        {
            assert(infer->type == TYPE_BOOL);
            assert(block->inferred_expressions[expr->binary.lhs].type == block->inferred_expressions[expr->binary.rhs].type);
            Memory* lhs = get_operand(expr->binary.lhs);
            Memory* rhs = get_operand(expr->binary.rhs);
            specialize(infer->type, lhs, [&](auto* lhs) { lambda(&address->as_bool, lhs, (decltype(lhs)) rhs); });
        };

        switch (expr->kind)
        {

        case EXPRESSION_STRING_LITERAL:
        {
            assert(expr->literal.atom == ATOM_STRING_LITERAL);
            Token_Info_String* token = (Token_Info_String*) get_token_info(unit->ctx, &expr->literal);
            assert(infer->size == sizeof(String));
            *(String*) address = token->value;
        } break;

        case EXPRESSION_NEGATE:
        {
            assert(block->inferred_expressions[expr->unary_operand].type == infer->type);
            Memory* op = get_operand(expr->unary_operand);
            specialize(infer->type, address, [&](auto* v) { *v = -*(decltype(v)) op; });
        } break;

        case EXPRESSION_ADDRESS:
        {
            address->as_address = (byte*) get_operand(expr->unary_operand);
        } break;

        case EXPRESSION_DEREFERENCE:
        {
            address = (Memory*) get_operand(expr->unary_operand)->as_address;
        } break;

        case EXPRESSION_CODEOF:
        {
            auto* op_infer = &block->inferred_expressions[expr->unary_operand];
            Type unit_type;
            if (is_user_defined_type(op_infer->type))
                unit_type = op_infer->type;
            else
                unit_type = *get_constant_type(block, expr->unary_operand);
            Unit* result = get_user_type_data(unit->ctx, unit_type)->unit;
            assert(result);
            address->as_address = (byte*) result;
        } break;

        case EXPRESSION_DEBUG:
        {
            auto* op_infer = &block->inferred_expressions[expr->unary_operand];
            if (is_soft_type(op_infer->type))
            {
                switch (op_infer->type)
                {
                case TYPE_SOFT_ZERO: printf("zero\n"); break;
                case TYPE_SOFT_NUMBER:
                {
                    String str = fract_display(get_constant_number(block, expr->unary_operand));
                    printf("%.*s\n", StringArgs(str));
                } break;
                case TYPE_SOFT_BOOL: printf("%s\n", *get_constant_bool(block, expr->unary_operand) ? "true" : "false"); break;
                case TYPE_SOFT_TYPE:
                {
                    String type = exact_type_description(unit, *get_constant_type(block, expr->unary_operand));
                    printf("%.*s\n", StringArgs(type));
                } break;
                case TYPE_SOFT_BLOCK:
                {
                    Token_Info info;
                    {
                        Soft_Block constant_block = *get_constant_block(block, expr->unary_operand);
                        Token_Info* from_info = get_token_info(unit->ctx, &constant_block.parsed_child->from);
                        Token_Info* to_info   = get_token_info(unit->ctx, &constant_block.parsed_child->to);
                        info = *from_info;
                        info.length = to_info->offset + to_info->length - from_info->offset;
                    }
                    String location = Report(unit->ctx).snippet(info, false, 0, 0).return_without_reporting();
                    printf("<soft block>\n%.*s\n", StringArgs(location));
                } break;
                IllegalDefaultCase;
                }
            }
            else
            {
                Memory* value = get_operand(expr->unary_operand);
                if (is_numeric_type(op_infer->type) || is_bool_type(op_infer->type))
                {
                    specialize(op_infer->type, value, [](auto* v)
                    {
                        String text = Format(temp, "%", *v);
                        printf("%.*s\n", StringArgs(text));
                    });
                }
                else if (is_pointer_type(op_infer->type))
                    printf("%p\n", value->as_address);
                else if (op_infer->type == TYPE_TYPE)
                    printf("type id %u\n", value->as_u32);
                else if (op_infer->type == TYPE_STRING)
                    printf("%.*s\n", StringArgs(*(String*) value));
                else if (op_infer->type == TYPE_VOID)
                    printf("void\n");
                else Unreachable;
            }
        } break;

        case EXPRESSION_DEBUG_ALLOC:
        {
            Type type = infer->type;
            assert(is_pointer_type(type));
            Type to_alloc = set_indirection(type, get_indirection(type) - 1);
            umm size = get_type_size(unit, to_alloc);
            // @Reconsider - what about alignment?
            address->as_address = alloc<byte>(NULL, size);
        } break;

        case EXPRESSION_DEBUG_FREE:
        {
            free(get_operand(expr->unary_operand)->as_address);
        } break;

        case EXPRESSION_ASSIGNMENT:
        {
            u64 size = infer->size;
            assert(size == block->inferred_expressions[expr->binary.lhs].size);
            assert(size == block->inferred_expressions[expr->binary.rhs].size);

            Memory* rhs = get_operand(expr->binary.rhs);
            Memory* lhs = get_operand(expr->binary.lhs);
            memcpy(lhs, rhs, size);
            memcpy(address, lhs, size);
        } break;

        case EXPRESSION_ADD:                specialize_numeric_binary([](auto* v, auto* l, auto* r) { *v = *l + *r; }); break;
        case EXPRESSION_SUBTRACT:           specialize_numeric_binary([](auto* v, auto* l, auto* r) { *v = *l - *r; }); break;
        case EXPRESSION_MULTIPLY:           specialize_numeric_binary([](auto* v, auto* l, auto* r) { *v = *l * *r; }); break;

        // @Reconsider
        case EXPRESSION_DIVIDE_WHOLE:       specialize_numeric_binary([](auto* v, auto* l, auto* r) { *v = *l / *r; }); break;
        case EXPRESSION_DIVIDE_FRACTIONAL:  specialize_numeric_binary([](auto* v, auto* l, auto* r) { *v = *l / *r; }); break;

        case EXPRESSION_POINTER_ADD:
        {
            auto* lhs_infer = &block->inferred_expressions[expr->binary.lhs];
            auto* rhs_infer = &block->inferred_expressions[expr->binary.rhs];
            assert(infer    ->size == sizeof(umm));
            assert(lhs_infer->size == sizeof(umm));
            assert(rhs_infer->size == sizeof(umm));

            Memory* lhs = get_operand(expr->binary.lhs);
            Memory* rhs = get_operand(expr->binary.rhs);

            if (is_pointer_type(lhs_infer->type))
            {
                Type pointer_type = lhs_infer->type;
                Type element_type = set_indirection(pointer_type, get_indirection(pointer_type) - 1);
                umm  element_size = get_type_size(unit, element_type);
                address->as_umm = lhs->as_umm + rhs->as_umm * element_size;
            }
            else
            {
                Type pointer_type = rhs_infer->type;
                Type element_type = set_indirection(pointer_type, get_indirection(pointer_type) - 1);
                umm  element_size = get_type_size(unit, element_type);
                address->as_umm = rhs->as_umm + lhs->as_umm * element_size;
            }
        } break;

        case EXPRESSION_POINTER_SUBTRACT: NotImplemented;

        case EXPRESSION_EQUAL:              specialize_logic_binary([](auto* v, auto* l, auto* r) { *v = *l == *r; }); break;
        case EXPRESSION_NOT_EQUAL:          specialize_logic_binary([](auto* v, auto* l, auto* r) { *v = *l != *r; }); break;
        case EXPRESSION_GREATER_THAN:       specialize_logic_binary([](auto* v, auto* l, auto* r) { *v = *l >  *r; }); break;
        case EXPRESSION_GREATER_OR_EQUAL:   specialize_logic_binary([](auto* v, auto* l, auto* r) { *v = *l >= *r; }); break;
        case EXPRESSION_LESS_THAN:          specialize_logic_binary([](auto* v, auto* l, auto* r) { *v = *l <  *r; }); break;
        case EXPRESSION_LESS_OR_EQUAL:      specialize_logic_binary([](auto* v, auto* l, auto* r) { *v = *l <= *r; }); break;

        case EXPRESSION_CAST:
        {
            if (is_soft_type(block->inferred_expressions[expr->binary.rhs].type))
            {
                if (is_integer_type(infer->type))
                {
                    Fraction const* fract = get_constant_number(block, expr->binary.rhs);
                    assert(fract);
                    assert(fract_is_integer(fract));

                    u64 abs = 0;
                    bool ok = int_get_abs_u64(&abs, &fract->num);
                    assert(ok);
                    if (fract->num.negative)
                        abs = -abs;
                    memcpy(address->base, &abs, infer->size);
                }
                else if (infer->type == TYPE_TYPE)
                {
                    assert(infer->size == 4);
                    address->as_u32 = *get_constant_type(block, expr->binary.rhs);
                }
                else NotImplemented;
            }
            else
            {
                Memory* op = get_operand(expr->binary.rhs);
                specialize(infer->type, address, [&](auto* result)
                {
                    specialize(block->inferred_expressions[expr->binary.rhs].type, op, [&](auto* value)
                    {
                        *result = *value;
                    });
                });
            }
        } break;

        case EXPRESSION_GOTO_UNIT:
        {
            assert(block->inferred_expressions[expr->binary.lhs].size == sizeof(void*));
            assert(block->inferred_expressions[expr->binary.rhs].size == sizeof(void*));
            Memory* rhs = get_operand(expr->binary.rhs);
            Memory* lhs = get_operand(expr->binary.lhs);

            Runtime_Code_Address return_address = at;
            return_address.instruction++;

            at.unit    = (Unit*) lhs->as_address;
            at.storage =         rhs->as_address;

            assert(at.unit->entry_block->first_instruction_in_executable_order != UMM_MAX);
            at.instruction = at.unit->entry_block->first_instruction_in_executable_order;

            assert(at.unit->entry_block->return_address_offset_in_storage != INVALID_STORAGE_OFFSET);
            *(Runtime_Code_Address*)(at.storage + at.unit->entry_block->return_address_offset_in_storage) = return_address;
            goto next_instruction;
        } break;

        case EXPRESSION_CALL:
        {
            Block* callee = infer->called_block;
            assert(callee);

            Expression_List const* args = expr->call.arguments;
            assert(args);

            umm parameter_index = 0;
            For (callee->imperative_order)
            {
                auto* param_expr  = &callee->parsed_expressions  [*it];
                auto* param_infer = &callee->inferred_expressions[*it];
                if (!(param_expr->flags & EXPRESSION_DECLARATION_IS_PARAMETER)) continue;
                Defer(parameter_index++);

                if (param_expr->flags & EXPRESSION_DECLARATION_IS_ALIAS) continue;

                assert(parameter_index < args->count);
                Expression arg_expression = args->expressions[parameter_index];
                umm size = block->inferred_expressions[arg_expression].size;
                assert(size == param_infer->size);
                assert(param_infer->offset != INVALID_STORAGE_OFFSET);

                Memory* arg = get_operand(arg_expression);
                Memory* param = (Memory*)(at.storage + param_infer->offset);
                memcpy(param, arg, size);
            }


            assert(callee->return_address_offset_in_storage != INVALID_STORAGE_OFFSET);
            auto* return_address = (Runtime_Code_Address*)(at.storage + callee->return_address_offset_in_storage);
            *return_address = at;
            return_address->instruction++;

            assert(callee->first_instruction_in_executable_order != UMM_MAX);
            at.instruction = callee->first_instruction_in_executable_order;
            goto next_instruction;
        } break;

        case EXPRESSION_DECLARATION:
        {
            u64 size = infer->size;
            if (expr->declaration.value == NO_EXPRESSION)
            {
                memset(address, 0, size);
            }
            else
            {
                assert(size == block->inferred_expressions[expr->declaration.value].size);
                Memory* value = get_operand(expr->declaration.value);
                memcpy(address, value, size);
            }
        } break;

        IllegalDefaultCase;
        }
    }

    at.instruction++;
    goto next_instruction;
}

void run_unit(Unit* unit)
{
    byte* storage = alloc<byte>(NULL, unit->next_storage_offset);
    Defer(free(storage));

    memset(storage, 0xCD, unit->next_storage_offset);

    assert(unit->entry_block->return_address_offset_in_storage != INVALID_STORAGE_OFFSET);
    memset(storage + unit->entry_block->return_address_offset_in_storage, 0, sizeof(Runtime_Code_Address));

    Runtime_Code_Address entry = {};
    entry.unit        = unit;
    entry.storage     = storage;
    entry.instruction = unit->entry_block->first_instruction_in_executable_order;
    vm(entry);
}
#endif

void run_bytecode(Unit* unit, umm instruction, byte* storage)
{
run:
    if (!unit) return;

    printf("at %p:%d, on %p\n", unit, (int) instruction, storage);
    Bytecode* bc = &unit->bytecode[instruction];
    u64 r = bc->r;
    u64 a = bc->a;
    u64 b = bc->b;
    u64 s = bc->s;
#define M(type, offset) (*(type*)(storage + (offset)))

    switch (bc->op)
    {
    IllegalDefaultCase;

    case OP_ZERO:                   memset(storage + r, 0, s);              break;
    case OP_LITERAL:                memcpy(storage + r, &a, s);             break;
    case OP_LITERAL_INDIRECT:       memcpy(storage + r, (void*)(umm) a, s); break;
    case OP_COPY:                   memcpy(storage + r, storage + a, s);    break;
    case OP_COPY_FROM_INDIRECT:     memcpy(storage + r, M(void*, a), s);    break;
    case OP_COPY_TO_INDIRECT:       memcpy(M(void*, r), storage + a, s);    break;
    case OP_COPY_BETWEEN_INDIRECT:  memcpy(M(void*, r), M(void*, a), s);    break;
    case OP_ADDRESS:                M(byte*, r) = storage + a;              break;
    case OP_NEGATE:                 NotImplemented;
    case OP_ADD:                    NotImplemented;
    case OP_SUBTRACT:               NotImplemented;
    case OP_MULTIPLY:               NotImplemented;
    case OP_DIVIDE_WHOLE:           NotImplemented;
    case OP_DIVIDE_FRACTIONAL:      NotImplemented;
    case OP_MOVE_POINTER_FORWARD:   NotImplemented;
    case OP_MOVE_POINTER_BACKWARD:  NotImplemented;
    case OP_POINTER_DISTANCE:       NotImplemented;
    case OP_CAST:                   NotImplemented;
    case OP_GOTO:                   instruction = r;                              goto run;
    case OP_GOTO_IF_FALSE:          instruction = M(u8, a) ? instruction + 1 : r; goto run;
    case OP_GOTO_INDIRECT:          instruction = M(umm, r);                      goto run;
    case OP_CALL:                   M(umm, a) = instruction + 1; instruction = r; goto run;
    case OP_SWITCH_UNIT:
    {
        void** destination = M(void**, a);
        destination[0] = (void*)(unit);
        destination[1] = (void*)(umm)(instruction + 1);
        destination[2] = (void*)(storage);

        unit    = M(Unit*, r);
        storage = M(byte*, a);
        instruction = unit->entry_block->first_instruction;
        goto run;
    } break;
    case OP_FINISH_UNIT:
    {
        unit        = *(Unit**)(storage + 0 * sizeof(void*));
        instruction = *(umm  *)(storage + 1 * sizeof(void*));
        storage     = *(byte**)(storage + 2 * sizeof(void*));
        goto run;
    } break;
    case OP_DEBUG_PRINT:
    {
        String text = {};
        switch (s)
        {
        case TYPE_VOID:   text = "void"_s;                             break;
        case TYPE_U8:     text = Format(temp, "%",      M(u8,     r)); break;
        case TYPE_U16:    text = Format(temp, "%",      M(u16,    r)); break;
        case TYPE_U32:    text = Format(temp, "%",      M(u32,    r)); break;
        case TYPE_U64:    text = Format(temp, "%",      M(u64,    r)); break;
        case TYPE_UMM:    text = Format(temp, "%",      M(umm,    r)); break;
        case TYPE_S8:     text = Format(temp, "%",      M(s8,     r)); break;
        case TYPE_S16:    text = Format(temp, "%",      M(s16,    r)); break;
        case TYPE_S32:    text = Format(temp, "%",      M(s32,    r)); break;
        case TYPE_S64:    text = Format(temp, "%",      M(s64,    r)); break;
        case TYPE_SMM:    text = Format(temp, "%",      M(smm,    r)); break;
        case TYPE_F16:    NotImplemented;
        case TYPE_F32:    text = Format(temp, "%",      M(f32,    r)); break;
        case TYPE_F64:    text = Format(temp, "%",      M(f64,    r)); break;
        case TYPE_BOOL:   text = Format(temp, "%",      M(bool,   r)); break;
        case TYPE_TYPE:   text = Format(temp, "type %", M(Type,   r)); break;
        case TYPE_STRING: text =                        M(String, r);  break;
        default:
            if (is_pointer_type((Type) s))
            {
                text = Format(temp, "%", M(void*, r));
            }
            else
            {
                assert(is_user_defined_type((Type) s));
                text = "user defined type"_s;
            }
            break;
        }
        printf("%.*s\n", StringArgs(text));
    } break;
    case OP_DEBUG_ALLOC:            M(void*, r) = malloc(M(umm, a)); break;
    case OP_DEBUG_FREE:             free(M(void*, r));               break;
    }

    rough_sleep(0.1);
    instruction++;
    goto run;
#undef M
}

void run_unit(Unit* unit)
{
    assert(unit->compiled_bytecode);
    assert(!(unit->flags & UNIT_IS_STRUCT));

    byte* storage = alloc<byte>(NULL, unit->next_storage_offset);
    Defer(free(storage));
    memset(storage, 0xCD, unit->next_storage_offset);
    memset(storage, 0, 3 * sizeof(void*));

    run_bytecode(unit, unit->entry_block->first_instruction, storage);
}


ExitApplicationNamespace
