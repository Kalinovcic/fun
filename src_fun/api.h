EnterApplicationNamespace



////////////////////////////////////////////////////////////////////////////////
// Fraction
////////////////////////////////////////////////////////////////////////////////


bool int_get_abs_u64(u64* out, Integer const* i);

String int_base10(Integer const* integer, Region* memory = temp, umm min_digits = 0);
String int_base16(Integer const* integer, Region* memory = temp, umm min_digits = 0);

// Always reduced, denominator always positive.
struct Fraction
{
    Integer num;
    Integer den;
};

Fraction fract_make_u64   (u64 integer);
Fraction fract_make       (Integer const* num, Integer const* den);
void     fract_free       (Fraction* f);
Fraction fract_clone      (Fraction const* from);
void     fract_reduce     (Fraction* f);
bool     fract_is_zero    (Fraction const* f);
bool     fract_is_negative(Fraction const* f);
bool     fract_is_integer (Fraction const* f);
bool     fract_equals     (Fraction const* a, Fraction const* b);
Fraction fract_neg        (Fraction const* a);
Fraction fract_add        (Fraction const* a, Fraction const* b);
Fraction fract_sub        (Fraction const* a, Fraction const* b);
Fraction fract_mul        (Fraction const* a, Fraction const* b);
bool     fract_div_fract  (Fraction* out, Fraction const* a, Fraction const* b);
bool     fract_div_whole  (Fraction* out, Fraction const* a, Fraction const* b);
String   fract_display    (Fraction const* f, Region* memory = temp);
String   fract_display_hex(Fraction const* f, Region* memory = temp);

// Returns true if the number fits losslessly.
bool fract_scientific_abs(Fraction const* f, umm count_decimals,
                          Integer* mantissa, smm* exponent, umm* mantissa_size, umm* msb);


////////////////////////////////////////////////////////////////////////////////
// Lexer
////////////////////////////////////////////////////////////////////////////////


enum Atom: u32
{
    ATOM_INVALID,

    // literals
    ATOM_NUMBER_LITERAL,        // any numeric literal
    ATOM_STRING_LITERAL,        // any string literal
    ATOM_COMMENT,               // any comment
    ATOM_ZERO,                  // zero
    ATOM_TRUE,                  // true
    ATOM_FALSE,                 // false

    // types
    ATOM_VOID,                  // void
    ATOM_U8,                    // u8
    ATOM_U16,                   // u16
    ATOM_U32,                   // u32
    ATOM_U64,                   // u64
    ATOM_UMM,                   // umm
    ATOM_S8,                    // s8
    ATOM_S16,                   // s16
    ATOM_S32,                   // s32
    ATOM_S64,                   // s64
    ATOM_SMM,                   // smm
    ATOM_F16,                   // f16
    ATOM_F32,                   // f32
    ATOM_F64,                   // f64
    ATOM_BOOL,                  // bool
    ATOM_STRUCT,                // struct
    ATOM_STRING,                // string

    // keywords
    ATOM_IMPORT,                // import
    ATOM_USING,                 // using
    ATOM_TYPE,                  // type
    ATOM_BLOCK,                 // block
    ATOM_CODE_BLOCK,            // code_block
    ATOM_GLOBAL,                // global
    ATOM_THREAD_LOCAL,          // thread_local
    ATOM_UNIT,                  // unit
    ATOM_UNIT_LOCAL,            // unit_local
    ATOM_UNIT_DATA,             // unit_data
    ATOM_UNIT_CODE,             // unit_code
    ATOM_LABEL,                 // label
    ATOM_GOTO,                  // goto
    ATOM_DEBUG,                 // debug
    ATOM_DEBUG_ALLOC,           // debug_alloc
    ATOM_DEBUG_FREE,            // debug_free
    ATOM_DELETE,                // delete
    ATOM_IF,                    // if
    ATOM_ELSE,                  // else
    ATOM_ELIF,                  // elif
    ATOM_WHILE,                 // while
    ATOM_DO,                    // do
    ATOM_RUN,                   // run
    ATOM_RETURN,                // return
    ATOM_YIELD,                 // yield
    ATOM_DEFER,                 // defer
    ATOM_CAST,                  // cast
    ATOM_SIZEOF,                // sizeof
    ATOM_ALIGNOF,               // alignof
    ATOM_CODEOF,                // codeof
    ATOM_INTRINSIC,             // intrinsic

    // symbols
    ATOM_DOT,                   // .
    ATOM_COMMA,                 // ,
    ATOM_SEMICOLON,             // ;
    ATOM_COLON,                 // :
    ATOM_EQUAL,                 // =
    ATOM_LEFT_PARENTHESIS,      // (
    ATOM_RIGHT_PARENTHESIS,     // )
    ATOM_LEFT_BRACKET,          // [
    ATOM_RIGHT_BRACKET,         // ]
    ATOM_LEFT_BRACE,            // {
    ATOM_RIGHT_BRACE,           // }
    ATOM_PLUS,                  // +
    ATOM_MINUS,                 // -
    ATOM_STAR,                  // *
    ATOM_SLASH,                 // /
    ATOM_BANG_SLASH,            // !/
    ATOM_PERCENT_SLASH,         // %/
    ATOM_PERCENT,               // %
    ATOM_PIPE,                  // |
    ATOM_AMPERSAND,             // &
    ATOM_AMPERSAND_PLUS,        // &+
    ATOM_AMPERSAND_MINUS,       // &-
    ATOM_BANG,                  // !
    ATOM_MINUS_GREATER,         // ->
    ATOM_EQUAL_GREATER,         // =>
    ATOM_EQUAL_EQUAL,           // ==
    ATOM_BANG_EQUAL,            // !=
    ATOM_LESS,                  // <
    ATOM_LESS_EQUAL,            // <=
    ATOM_GREATER,               // >
    ATOM_GREATER_EQUAL,         // >=
    ATOM_DOLLAR,                // $
    ATOM_UNDERSCORE,            // _

    ATOM_ONE_PAST_LAST_FIXED_ATOM,

    // all atoms from here on out are unique identifier atoms
    ATOM_FIRST_IDENTIFIER = 1024
};

CompileTimeAssert(ATOM_FIRST_IDENTIFIER >= ATOM_ONE_PAST_LAST_FIXED_ATOM);

inline bool is_identifier(Atom atom)
{
    return atom >= ATOM_FIRST_IDENTIFIER;
}


struct Source_Info
{
    String path; // optional, for error reporting
    String name;
    String code;
    Array<u32> line_offsets;
};

struct Token_Info
{
    u16 source_index;
    u16 length;
    u32 offset;
};

CompileTimeAssert(sizeof(Token_Info) == 8);

struct Token_Info_Number: Token_Info
{
    Fraction value;
};

CompileTimeAssert(sizeof(Token_Info_Number) == 72);

struct Token_Info_String: Token_Info
{
    String value;
};

CompileTimeAssert(sizeof(Token_Info_String) == 24);

struct Token
{
    Atom atom;
    u32  info_index;
};

CompileTimeAssert(sizeof(Token) == 8);



////////////////////////////////////////////////////////////////////////////////
// Types
////////////////////////////////////////////////////////////////////////////////


// top 4 bits in a Type represent the indirection count (how many * in the pointer type)
constexpr u32 TYPE_POINTER_SHIFT   = 28;
constexpr u32 TYPE_MAX_INDIRECTION = (1ul << (32 - TYPE_POINTER_SHIFT)) - 1;
constexpr u32 TYPE_POINTER_MASK    = TYPE_MAX_INDIRECTION << TYPE_POINTER_SHIFT;
constexpr u32 TYPE_BASE_MASK       = ~TYPE_POINTER_MASK;

enum Type: u32
{
    INVALID_TYPE,

    TYPE_VOID,
    TYPE_SOFT_ZERO,

    TYPE_U8,
    TYPE_U16,
    TYPE_U32,
    TYPE_U64,
    TYPE_UMM,
    TYPE_S8,
    TYPE_S16,
    TYPE_S32,
    TYPE_S64,
    TYPE_SMM,
    TYPE_F16,
    TYPE_F32,
    TYPE_F64,
    TYPE_SOFT_NUMBER,           // this is the type of compile-time evaluated floating-point expressions

    TYPE_BOOL,
    TYPE_SOFT_BOOL,             // this is the type of compile-time evaluated logic expressions

    TYPE_TYPE,
    TYPE_SOFT_TYPE,             // this is the type of compile-time evaluated type expressions

    TYPE_SOFT_BLOCK,            // refers to a constant parsed block, not yet materialized
                                // @Reconsider - materialized blocks do not have values at the moment,
                                //               but we might need that for unit instantiation?

    TYPE_ONE_PAST_LAST_PRIMITIVE_TYPE,

    TYPE_FIRST_USER_TYPE = TYPE_ONE_PAST_LAST_PRIMITIVE_TYPE,
    TYPE_STRING,

    TYPE_VOID_POINTER = (1 << TYPE_POINTER_SHIFT) | TYPE_VOID,
};

inline Type get_base_type  (Type type)                  { return (Type)(type & TYPE_BASE_MASK); }
inline u32  get_indirection(Type type)                  { return type >> TYPE_POINTER_SHIFT; }
inline Type set_indirection(Type type, u32 indirection) { return (Type)((type & TYPE_BASE_MASK) | (indirection << TYPE_POINTER_SHIFT)); }

inline bool is_integer_type         (Type type) { return type >= TYPE_U8    && type <= TYPE_SMM;                          }
inline bool is_unsigned_integer_type(Type type) { return type >= TYPE_U8    && type <= TYPE_UMM;                          }
inline bool is_signed_integer_type  (Type type) { return type >= TYPE_S8    && type <= TYPE_SMM;                          }
inline bool is_pointer_integer_type (Type type) { return type == TYPE_UMM || type == TYPE_SMM;                            }
inline bool is_floating_point_type  (Type type) { return type >= TYPE_F16   && type <= TYPE_F64;                          }
inline bool is_numeric_type         (Type type) { return is_integer_type(type) || is_floating_point_type(type) || type == TYPE_SOFT_NUMBER; }
inline bool is_bool_type            (Type type) { return type == TYPE_BOOL || type == TYPE_SOFT_BOOL;                     }
inline bool is_primitive_type       (Type type) { return type >= TYPE_VOID  && type < TYPE_ONE_PAST_LAST_PRIMITIVE_TYPE;  }
inline bool is_user_defined_type    (Type type) { return get_indirection(type) == 0 && type >= TYPE_FIRST_USER_TYPE;      }
inline bool is_type_type            (Type type) { return type == TYPE_TYPE  || type == TYPE_SOFT_TYPE;                    }
inline bool is_block_type           (Type type) { return type == TYPE_SOFT_BLOCK;                                         }
inline bool is_soft_type            (Type type) { return type == TYPE_SOFT_ZERO || type == TYPE_SOFT_NUMBER || type == TYPE_SOFT_BOOL || type == TYPE_SOFT_TYPE || type == TYPE_SOFT_BLOCK; }
inline bool is_pointer_type         (Type type) { return get_indirection(type) > 0; }
inline Type get_element_type        (Type type) { assert(is_pointer_type(type)); return set_indirection(type, get_indirection(type) - 1); }



////////////////////////////////////////////////////////////////////////////////
// AST
////////////////////////////////////////////////////////////////////////////////


// enums so we can't just assign any number in assignment
enum Expression: u32 {};
enum Visibility: u32 {};

static constexpr Expression NO_EXPRESSION  = (Expression) 0xFFFFFFFF;
static constexpr Visibility NO_VISIBILITY  = (Visibility) 0xFFFFFFFF;
static constexpr Visibility ALL_VISIBILITY = (Visibility) 0xFFFFFFFE;


struct Expression_List
{
    u32        count;
    Expression expressions[0];
};

CompileTimeAssert(sizeof(Expression_List) == 4);

#define EXPRESSION_LIST             \
                                    \
    X(INVALID)                      \
                                    \
    /* literal expressions */       \
    X(ZERO)                         \
    X(TRUE)                         \
    X(FALSE)                        \
    X(NUMERIC_LITERAL)              \
    X(STRING_LITERAL)               \
    X(TYPE_LITERAL)                 \
    X(COMMENT)                      \
    X(BLOCK)                        \
    X(UNIT)                         \
                                    \
    X(NAME)                         \
    X(MEMBER)                       \
                                    \
    /* unary operators */           \
    X(NOT)                          \
    X(NEGATE)                       \
    X(ADDRESS)                      \
    X(DEREFERENCE)                  \
    X(SIZEOF)                       \
    X(ALIGNOF)                      \
    X(CODEOF)                       \
    X(DEBUG)                        \
    X(DEBUG_ALLOC)                  \
    X(DEBUG_FREE)                   \
                                    \
    /* binary operators */          \
    X(ASSIGNMENT)                   \
    X(ADD)                          \
    X(SUBTRACT)                     \
    X(MULTIPLY)                     \
    X(DIVIDE_WHOLE)                 \
    X(DIVIDE_FRACTIONAL)            \
    X(POINTER_ADD)                  \
    X(POINTER_SUBTRACT)             \
    X(EQUAL)                        \
    X(NOT_EQUAL)                    \
    X(GREATER_THAN)                 \
    X(GREATER_OR_EQUAL)             \
    X(LESS_THAN)                    \
    X(LESS_OR_EQUAL)                \
    X(AND)                          \
    X(OR)                           \
    X(CAST)                         \
    X(GOTO_UNIT)                    \
                                    \
    /* branching expressions */     \
    X(BRANCH)                       \
    X(CALL)                         \
    X(INTRINSIC)                    \
    X(YIELD)                        \
                                    \
    /* other */                     \
    X(DECLARATION)                  \
    X(RUN)                          \
    X(DELETE)

enum Expression_Kind: u16
{
#define X(name) EXPRESSION_##name,
    EXPRESSION_LIST
#undef X
    COUNT_EXPRESSIONS
};

static inline constexpr char const* const expression_kind_name[COUNT_EXPRESSIONS] =
{
#define X(name) #name,
    EXPRESSION_LIST
#undef X
};

enum: flags16
{
    EXPRESSION_IS_IN_PARENTHESES             = 0x0001,
    EXPRESSION_DECLARATION_IS_PARAMETER      = 0x0002,
    EXPRESSION_DECLARATION_IS_RETURN         = 0x0004,
    EXPRESSION_DECLARATION_IS_ALIAS          = 0x0008,
    EXPRESSION_DECLARATION_IS_ORDERED        = 0x0010,
    EXPRESSION_DECLARATION_IS_UNINITIALIZED  = 0x0020,
    EXPRESSION_DECLARATION_IS_INFERRED_ALIAS = 0x0040,
    EXPRESSION_DECLARATION_IS_USING          = 0x0080,
    EXPRESSION_ALLOW_PARENT_SCOPE_VISIBILITY = 0x0100,
    EXPRESSION_UNIT_IS_IMPORT                = 0x0200,
    EXPRESSION_BRANCH_IS_LOOP                = 0x0400,
    EXPRESSION_BRANCH_IS_BAKED               = 0x0800,
    EXPRESSION_HAS_CONDITIONAL_INFERENCE     = 0x1000,
    EXPRESSION_HAS_TO_BE_EXTERNALLY_INFERRED = 0x2000,
};

enum Comment_Relation
{
    // The relations are defined based on separation from the nearest non-comment
    // expressions on either side. If there's at least one blank row between
    // an expression and a comment, then this comment and expression are separated.

    COMMENT_IS_INSIDE,  // within an expression
    COMMENT_IS_ALONE,   // separated on both sides
    COMMENT_IS_AFTER,   // separated from below, or on the same line as the previous expr
    COMMENT_IS_BEFORE,  // default case
};

struct Parsed_Expression
{
    Expression_Kind kind;
    flags16         flags;
    Visibility      visibility_limit;

    Token from;
    Token to;

    union
    {
        Token         literal;
        Token         deleted_name;
        Token         intrinsic_name;
        Expression    unary_operand;
        Type          parsed_type;
        struct Block* parsed_block;

        Expression_List const* yield_assignments;

        struct
        {
            Token            token;
            Comment_Relation relation;
            Expression       relative_to;  // may be NO_EXPRESSION if relation == 'COMMENT_IS_ALONE'
        } comment;

        struct
        {
            Token token;
        } name;

        struct
        {
            Expression lhs;
            Token      name;
        } member;

        struct
        {
            Token      name;
            Expression type;   // may be NO_EXPRESSION if 'name := value;' declaration
            Expression value;  // may be NO_EXPRESSION if 'name: Type;' declaration
        } declaration;

        struct
        {
            Expression lhs;
            Expression rhs;
        } binary;

        struct
        {
            Expression lhs;
            Expression_List const* arguments;
        } call;

        struct
        {
            Expression condition;
            Expression on_success;
            Expression on_failure;
        } branch;
    };
};

CompileTimeAssert(sizeof(Parsed_Expression) == 40);


static constexpr u64 INVALID_CONSTANT       = U64_MAX;
static constexpr u64 INVALID_STORAGE_SIZE   = U64_MAX;
static constexpr u64 INVALID_STORAGE_OFFSET = U64_MAX;

enum: flags32
{
    INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME = 0x0001,
    INFERRED_EXPRESSION_DOES_NOT_ALLOCATE_STORAGE   = 0x0002,
    INFERRED_EXPRESSION_COMPLETED_INFERENCE         = 0x0004,
    INFERRED_EXPRESSION_CONDITION_DISABLED          = 0x0008,
    INFERRED_EXPRESSION_CONDITION_ENABLED           = 0x0010,
    INFERRED_EXPRESSION_IS_HARDENED_CONSTANT        = 0x0020,
};

struct Inferred_Expression
{
    flags32       flags;
    Type          type;
    Type          hardened_type;
    struct Block* called_block;
    u64           constant;
};

CompileTimeAssert(sizeof(Inferred_Expression) == 32);


struct Soft_Block
{
    struct Block* materialized_parent;
    struct Block* parsed_child;

    bool          has_alias;
    Token         alias;

    bool operator==(Soft_Block const& other) const
    {
        return materialized_parent == other.materialized_parent
            && parsed_child        == other.parsed_child;
    }
};

union Constant
{
    Fraction   number;
    bool       boolean;
    Type       type;
    Soft_Block block;
};


struct Argument_Key
{
    Type     type;
    Constant constant;  // zero where N/A, so this case can be ignored

    inline bool operator==(Argument_Key const& other) const
    {
        if (type != other.type) return false;
        if (!is_soft_type(type)) return true;

        switch (type)
        {
        case TYPE_SOFT_NUMBER: return fract_equals(&constant.number, &other.constant.number);
        case TYPE_SOFT_BOOL:   return constant.boolean == other.constant.boolean;
        case TYPE_SOFT_TYPE:   return constant.type    == other.constant.type;
        case TYPE_SOFT_BLOCK:  return constant.block   == other.constant.block;
        IllegalDefaultCase;
        }
    }

    static inline u64 hash(Argument_Key const& k)
    {
        // @Optimization - better hash function
        u64 hash = hash_u64((u64) k.type);
        if (!is_soft_type(k.type))
            return hash;

        Constant const& c = k.constant;
        switch (k.type)
        {
        case TYPE_SOFT_NUMBER:


            hash ^= hash_u64(c.number.num.negative);
            hash ^= hash_u64(c.number.den.negative);
            hash ^= hash64(c.number.num.digit, c.number.num.size * sizeof(*c.number.num.digit));
            hash ^= hash64(c.number.den.digit, c.number.den.size * sizeof(*c.number.den.digit));
            break;
        case TYPE_SOFT_BOOL:   hash ^= hash_u64(c.boolean);    break;
        case TYPE_SOFT_TYPE:   hash ^= hash_u64((u64) c.type); break;
        case TYPE_SOFT_BLOCK:  hash ^= hash_pointer(c.block.materialized_parent);
                               hash ^= hash_pointer(c.block.parsed_child);
                               break;
        IllegalDefaultCase;
        }

        return hash;
    }
};

// What has to be equal for the calls to be equivalent:
//  - the unit it occurs inside
//  - the parsed block which is being called
//    (this information is implicit since the table is inside the block)
//  - the parent materialized block of the callee, but only if the callee
//    can have multiple parents
//  - set of argument types, in order of the parameter list, including implicit ones
//  - set of soft constants which resolve alias arguments, in order same as above
struct Call_Key
{
    struct Unit* unit;
    struct Block* materialized_parent;
    Array<Argument_Key> arguments;

    inline bool operator==(Call_Key const& other) const
    {
        if (unit != other.unit) return false;
        if (materialized_parent != other.materialized_parent) return false;
        if (arguments.count != other.arguments.count) return false;
        for (umm i = 0; i < arguments.count; i++)
            if (arguments[i] != other.arguments[i])
                return false;
        return true;
    }

    u64 computed_hash;  // we store the computed the hash since it's fairly expensive

    inline void recompute_hash()
    {
        computed_hash  = hash_pointer(unit);
        computed_hash ^= hash_pointer(materialized_parent);
        For (arguments)
            computed_hash ^= Argument_Key::hash(*it);
    }

    static inline u64 hash(Call_Key const& k)
    {
        return k.computed_hash;
    }
};

// The value identifies the specific call which started the inference.
// This is so the call knows it's free to resume typechecking instead of
// waiting on another call.
struct Call_Value
{
    Block*     caller_block;
    Expression call_expression;

    inline operator bool()
    {
        return caller_block != NULL;
    }
};


struct Resolved_Name
{
    Block*     scope;
    Expression declaration;

    struct Use
    {
        Block*     scope;
        Expression declaration;
    };
    Dynamic_Array<Use> use_chain;
};


enum Wait_Reason: u32
{
    WAITING_ON_OPERAND,              // most boring wait reason, skipped in chain reporting because it's obvious
    WAITING_ON_DECLARATION,          // a name expression can wait for the declaration to infer
    WAITING_ON_PARAMETER_INFERENCE,  // a call expression can wait for the callee to infer its parameter types
    WAITING_ON_RETURN_TYPE_INFERENCE,
    WAITING_ON_EXTERNAL_INFERENCE,   // an alias parameter declaration can wait for the caller to infer it,
                                     // or an inferred type alias can wait for its surrounding expression to infer it
    WAITING_ON_CONDITION_INFERENCE,  // a call expression of a baked branch can wait for the condition to be inferred
    WAITING_ON_USING_TYPE,
    WAITING_ON_ANOTHER_CALL,
};

struct Wait_Info
{
    Wait_Reason why;
    Expression  on_expression;
    Block*      on_block;
};


enum: flags32
{
    BLOCK_IS_MATERIALIZED         = 0x0001,
    BLOCK_IS_PARAMETER_BLOCK      = 0x0002,
    BLOCK_READY_FOR_PLACEMENT     = 0x0004,  // means types and sizes are inferred, but constants maybe not
    BLOCK_IS_UNIT                 = 0x0008,
    BLOCK_HAS_STRUCTURE_PLACEMENT = 0x0010,
    BLOCK_IS_TOP_LEVEL            = 0x0020,
    BLOCK_HAS_BEEN_PLACED         = 0x0040,
    BLOCK_HAS_BEEN_GENERATED      = 0x0080,
};

struct Block
{
    // Filled out in parsing:
    flags32 flags;
    Token   from;
    Token   to;

    Array<struct Parsed_Expression const> parsed_expressions;
    Array<Expression const> imperative_order;

    // Filled out in inference, but stored on parsed block:
    Table(Call_Key, Call_Value, Call_Key::hash) calls;

    // Filled out in inference:
    struct Unit* materialized_by_unit;
    Block*       materialized_from;
    Array<struct Inferred_Expression> inferred_expressions;  // parallel to parsed_expressions
    Dynamic_Array<Constant> constants;

    Table(Expression, Resolved_Name, hash_u32) resolved_names;
    Table(Expression, Wait_Info,     hash_u32) waiting_expressions;

    Block*     parent_scope;
    Visibility parent_scope_visibility_limit;

    // Filled out in bytecode generation:
    Table(Expression, u64, hash_u32) declaration_placement;
    umm first_instruction;
    u64 return_address_offset;
};



static constexpr umm MAX_BLOCKS_PER_UNIT = 1000;

enum: flags32
{
    UNIT_IS_STRUCT  = 0x0001,
    UNIT_IS_PLACED  = 0x0002,
    UNIT_IS_PATCHED = 0x0004,
};

struct Unit
{
    flags32 flags;
    Type    type_id;

    Token   initiator_from;
    Token   initiator_to;
    Block*  initiator_block;

    Block*  entry_block;

    u64     storage_size;
    u64     storage_alignment;

    /////////////////////////////////////////////////////////////
    // up to this point, the members are shared with Fun users //
    /////////////////////////////////////////////////////////////

    Region              memory;
    struct Environment* env;

    umm    materialized_block_count;
    Block* most_recent_materialized_block;

    u64    next_storage_offset;

    umm    blocks_not_completed;
    umm    blocks_not_ready_for_placement;

    bool   compiled_bytecode;
    Array<struct Bytecode       const> bytecode;
    Array<struct Bytecode_Patch const> bytecode_patches;
};



enum: flags32
{
    OP_COMPARE_EQUAL   = 0x00000001,
    OP_COMPARE_GREATER = 0x00000002,
    OP_COMPARE_LESS    = 0x00000004,
};

enum Bytecode_Operation: u32
{
    INVALID_OP,

    OP_ZERO,                    // r =  destination                s = size
    OP_ZERO_INDIRECT,           // r = &destination                s = size
    OP_LITERAL,                 // r =  destination  a =  content  s = size
    OP_COPY,                    // r =  destination  a =  source   s = size
    OP_COPY_FROM_INDIRECT,      // r =  destination  a = &source   s = size
    OP_COPY_TO_INDIRECT,        // r = &destination  a =  source   s = size
    OP_COPY_BETWEEN_INDIRECT,   // r = &destination  a = &source   s = size
    OP_ADDRESS,                 // r =  destination  a =  offset

    OP_NOT,                     // r = result  a = operand
    OP_NEGATE,                  // r = result  a = operand       s = type  (type options: s,f)
    OP_ADD,                     // r = result  a = lhs  b = rhs  s = type  (type options: u,f)
    OP_SUBTRACT,                // r = result  a = lhs  b = rhs  s = type  (type options: u,f)
    OP_MULTIPLY,                // r = result  a = lhs  b = rhs  s = type  (type options: u,f)
    OP_DIVIDE_WHOLE,            // r = result  a = lhs  b = rhs  s = type  (type options: u,s)
    OP_DIVIDE_FRACTIONAL,       // r = result  a = lhs  b = rhs  s = type  (type options: f)
    OP_COMPARE,                 // r = result  a = lhs  b = rhs  s = type  (type options: u,s,b,f)

    OP_MOVE_POINTER_CONSTANT,   // r = result  a = pointer               s = amount
    OP_MOVE_POINTER_FORWARD,    // r = result  a = pointer  b = integer  s = multiplier
    OP_MOVE_POINTER_BACKWARD,   // r = result  a = pointer  b = integer  s = multiplier
    OP_POINTER_DISTANCE,        // r = result  a = pointer  b = pointer  s = divisor

    OP_CAST,                    // r = result  a = value  b = value type  s = result type  (type options: u,s,f,b)

    OP_GOTO,                    // r =  instruction
    OP_GOTO_IF_FALSE,           // r =  instruction  a = condition
    OP_GOTO_INDIRECT,           // r = &instruction
    OP_CALL,                    // r =  instruction  a = return address
    OP_SWITCH_UNIT,             // r = &code         a = &storage
    OP_FINISH_UNIT,             //

    OP_INTRINSIC,               // a = &block  b = name.data  s = name.length

    OP_DEBUG_PRINT,             // r = operand                        s = type  (type options: any)
    OP_DEBUG_ALLOC,             // r = destination  a = size operand
    OP_DEBUG_FREE,              // r = operand

    COUNT_OPS,
};

struct Bytecode
{
    Block*             generated_from_block;
    Expression         generated_from_expression;
    Bytecode_Operation op;
    flags32            flags;
    u64                r;
    u64                a;
    u64                b;
    u64                s;
};

struct Bytecode_Patch
{
    Block*     block;
    Expression expression;
    umm        label;
};



////////////////////////////////////////////////////////////////////////////////
// API
////////////////////////////////////////////////////////////////////////////////


struct User_Type
{
    Unit*      unit;
    bool       has_alias;
    Token      alias;
};

struct Bytecode_Continuation
{
    Unit* unit;
    umm   instruction;
    byte* storage;
};

enum Pipeline_Task_Kind
{
    INVALID_PIPELINE_TASK,
    PIPELINE_TASK_INFER_BLOCK,
    PIPELINE_TASK_PLACE,
    PIPELINE_TASK_PATCH,
    PIPELINE_TASK_AWAIT_RUN,
    PIPELINE_TASK_RUN,

    COUNT_PIPELINE_TASKS,
};

struct Pipeline_Task
{
    Pipeline_Task_Kind    kind;
    Unit*                 unit;
    Block*                block;

    Environment*          run_environment;
    Bytecode_Continuation run_from;
};

static constexpr umm MAX_UNITS_PER_ENVIRONMENT = 1000;

struct Environment
{
    struct Compiler* ctx;
    struct User* user;

    bool silence_errors;
    u64  pointer_size;
    u64  pointer_alignment;

    Dynamic_Array<User_Type>    user_types;
    Table(u64, Unit*, hash_u64) top_level_units;
    umm                         materialized_unit_count;
    Unit*                       most_recent_materialized_unit;

    Dynamic_Array<Pipeline_Task> pipeline;

    Environment*          puppeteer;
    Bytecode_Continuation puppeteer_continuation;
    Pipeline_Task         puppeteer_event;
    bool                  puppeteer_event_is_actionable;
    bool                  puppeteer_is_waiting;
    bool                  puppeteer_has_custom_backend;
};

struct Compiler
{
    // Lexer
    bool lexer_initialized;
    Region lexer_memory;

    Array<String> import_path_patterns;

    Dynamic_Array<Source_Info,       false> sources;
    Dynamic_Array<Token_Info,        false> token_info_other;
    Dynamic_Array<Token_Info_Number, false> token_info_number;
    Dynamic_Array<Token_Info_String, false> token_info_string;
    Dynamic_Array<String,            false> identifiers;

    Atom next_identifier_atom;
    Table(String, Atom, hash_string) atom_table;

    // Parser
    Region parser_memory;
    Table(String, Block*, hash_string) top_level_blocks;

    umm count_parsed_blocks;
    umm count_parsed_expressions;
    umm count_parsed_expressions_by_kind[COUNT_EXPRESSIONS];

    // Inference
    umm count_inferred_units;
    umm count_inferred_blocks;
    umm count_inferred_constants;
    umm count_inferred_expressions;
    umm count_inferred_expressions_by_kind[COUNT_EXPRESSIONS];

    // Pipeline
    Region pipeline_memory;
    Dynamic_Array<Environment*> environments;
};

void add_default_import_path_patterns(Compiler* ctx);

Environment* make_environment(Compiler* ctx, Environment* puppeteer);

bool pump_pipeline(Compiler* ctx);


////////////////////////////////////////////////////////////////////////////////
// Lexer

// Your responsibility that code remains allocated as long as necessary!
// As comments should be considered whitespace, they are not included in the resulting
// tokens array, however we do keep track of their locations and return a
// separate comments array.
bool lex_from_memory(Compiler* ctx, String name, String code, Array<Token>* out_tokens, Array<Token>* out_comments, Source_Info** out_source_info = NULL);
bool lex_file(Compiler* ctx, String path, Array<Token>* out_tokens, Array<Token>* out_comments);

inline Token_Info* get_token_info(Compiler* ctx, Token const* token)
{
    if (token->atom == ATOM_NUMBER_LITERAL)
        return &ctx->token_info_number[token->info_index];
    if (token->atom == ATOM_STRING_LITERAL)
        return &ctx->token_info_string[token->info_index];
    return &ctx->token_info_other[token->info_index];
}

inline String get_source_token(Compiler* ctx, Token const* token)
{
    Token_Info* info = get_token_info(ctx, token);
    Source_Info* source = &ctx->sources[info->source_index];
    return { info->length, source->code.data + info->offset };
}

inline String get_identifier(Compiler* ctx, Token const* token)
{
    umm atom = token->atom - ATOM_FIRST_IDENTIFIER;
    if (atom >= ctx->identifiers.count)
        return "<invalid identifier atom>"_s;
    return ctx->identifiers[atom];
}



////////////////////////////////////////////////////////////////////////////////
// Parser

Block* parse_top_level(Compiler* ctx, String canonical_name, String imports_relative_to_path, Array<Token> tokens);
// `path` must be an absolute path.
Block* parse_top_level_from_file(Compiler* ctx, String path);
// Your responsibility that code (and all other strings that are passed as arguments) remains allocated as long as necessary!
Block* parse_top_level_from_memory(Compiler* ctx, String imports_relative_to_directory, String name, String code);


////////////////////////////////////////////////////////////////////////////////
// Inference

Unit* materialize_unit(Environment* env, Block* initiator, Block* materialized_parent = NULL);

String vague_type_description(Unit* unit, Type type, bool point_out_soft_types = false);
String vague_type_description_in_compile_time_context(Unit* unit, Type type);
String exact_type_description(Unit* unit, Type type);

enum Find_Result
{
    FIND_SUCCESS,
    FIND_FAILURE,
    FIND_DELETED,
    FIND_WAIT,
};

Find_Result find_declaration(Environment* env, Token const* name,
                             Block* scope, Visibility visibility_limit,
                             Block** out_decl_scope, Expression* out_decl_expr,
                             Dynamic_Array<Resolved_Name::Use>* out_use_chain,
                             bool allow_parent_traversal = true);

u64 get_type_size     (Unit* unit, Type type);
u64 get_type_alignment(Unit* unit, Type type);

struct Numeric_Description
{
    bool is_signed;
    bool is_integer;
    bool is_floating_point;

    umm bits;
    umm radix;

    // floating-point only
    bool supports_subnormal;
    bool supports_infinity;
    bool supports_nan;

    umm mantissa_bits;
    umm significand_bits;

    umm exponent_bits;
    umm exponent_bias;
    smm min_exponent;
    smm min_exponent_subnormal;
    smm max_exponent;
};

bool get_numeric_description(Unit* unit, Numeric_Description* desc, Type type);

Constant* get_constant(Block* block, Expression expr, Type type_assertion);
void set_constant(Compiler* ctx, Block* block, Expression expr, Type type_assertion, Constant* value);

inline void set_constant_number(Compiler* ctx, Block* block, Expression expr, Fraction   value) { Constant c = {}; c.number  = value; set_constant(ctx, block, expr, TYPE_SOFT_NUMBER, &c); }
inline void set_constant_bool  (Compiler* ctx, Block* block, Expression expr, bool       value) { Constant c = {}; c.boolean = value; set_constant(ctx, block, expr, TYPE_SOFT_BOOL,   &c); }
inline void set_constant_type  (Compiler* ctx, Block* block, Expression expr, Type       value) { Constant c = {}; c.type    = value; set_constant(ctx, block, expr, TYPE_SOFT_TYPE,   &c); }
inline void set_constant_block (Compiler* ctx, Block* block, Expression expr, Soft_Block value) { Constant c = {}; c.block   = value; set_constant(ctx, block, expr, TYPE_SOFT_BLOCK,  &c); }

inline Fraction   const* get_constant_number(Block* block, Expression expr) { Constant* c = get_constant(block, expr, TYPE_SOFT_NUMBER); return c ? &c->number  : NULL; }
inline bool       const* get_constant_bool  (Block* block, Expression expr) { Constant* c = get_constant(block, expr, TYPE_SOFT_BOOL);   return c ? &c->boolean : NULL; }
inline Type       const* get_constant_type  (Block* block, Expression expr) { Constant* c = get_constant(block, expr, TYPE_SOFT_TYPE);   return c ? &c->type    : NULL; }
inline Soft_Block const* get_constant_block (Block* block, Expression expr) { Constant* c = get_constant(block, expr, TYPE_SOFT_BLOCK);  return c ? &c->block   : NULL; }

User_Type* get_user_type_data(Environment* env, Type type);

void confirm_unit_placed (Unit* unit, u64 size, u64 alignment);
void confirm_unit_patched(Unit* unit);


////////////////////////////////////////////////////////////////////////////////
// Bytecode

void generate_bytecode_for_unit_placement(Unit* unit);
void generate_bytecode_for_unit_completion(Unit* unit);


////////////////////////////////////////////////////////////////////////////////
// Security

struct User* create_user();
void         delete_user(struct User* user);

byte* user_alloc(struct User* user, umm size, umm alignment);
void  user_free (struct User* user, void* base);

// In lockdown, the calling thread + user threads are the only running threads in the process,
// and all memory not allocated by the provided user is read-only.
void enter_lockdown(struct User* user);
void exit_lockdown(struct User* user);

void set_most_recent_execution_location(struct User* user, Unit* unit, Bytecode const* bc);


////////////////////////////////////////////////////////////////////////////////
// Runtime

void run_bytecode(User* user, Bytecode_Continuation continue_from);



////////////////////////////////////////////////////////////////////////////////
// Reporting

enum Severity
{
    SEVERITY_NONE,
    SEVERITY_ERROR,
    SEVERITY_WARNING,
};

struct Report
{
    String_Concatenator cat;
    Compiler*           ctx;
    bool                colored;
    String              indentation;
    bool                first_part;

    Report(Compiler* ctx);
    ~Report();
    Report& intro(Severity severity);
    Report& continuation();
    Report& message(String message);

    inline Report& intro(Severity severity, auto at) { return internal_intro(severity, convert(at)); }
    inline Report& continuation(auto at, bool skinny = false) { return internal_continuation(convert(at), skinny); }
    inline Report& snippet(auto at, bool skinny = false, umm before = 2, umm after = 1) { return internal_snippet(convert(at), skinny, before, after); }
    inline Report& suggestion_insert(String left, auto at, String right, bool skinny = false, umm before = 2, umm after = 1) { return internal_suggestion_insert(left, convert(at), right, skinny, before, after); }
    inline Report& suggestion_replace(auto at, auto replace_with, bool skinny = false, umm before = 2, umm after = 1) { return internal_suggestion_replace(convert(at), convert(replace_with), skinny, before, after); }
    inline Report& suggestion_replace(auto at, String replace_with, bool skinny = false, umm before = 2, umm after = 1) { return internal_suggestion_replace(convert(at), replace_with, skinny, before, after); }
    inline Report& suggestion_remove(auto at, bool skinny = false, umm before = 2, umm after = 1) { return internal_suggestion_remove(convert(at), skinny, before, after); }
    inline Report& part(auto at, String msg, Severity severity = SEVERITY_ERROR)
    {
        first_part ? intro(severity, at) : continuation(at);
        message(msg);
        snippet(at);
        first_part = false;
        return *this;
    }

    bool done();
    inline String return_without_reporting() { return resolve_to_string_and_free(&cat, temp); }

private:
    Report& internal_intro(Severity severity, Token_Info info);
    Report& internal_continuation(Token_Info info, bool skinny);
    Report& internal_snippet(Token_Info info, bool skinny, umm before, umm after);
    Report& internal_suggestion_insert(String left, Token_Info info, String right, bool skinny, umm before, umm after);
    Report& internal_suggestion_replace(Token_Info info, Token_Info replace_with, bool skinny, umm before, umm after);
    Report& internal_suggestion_replace(Token_Info info, String replace_with, bool skinny, umm before, umm after);
    Report& internal_suggestion_remove(Token_Info info, bool skinny, umm before, umm after);

    inline Token_Info convert(Token_Info        info) const { return  info;                   }
    inline Token_Info convert(Token_Info const* info) const { return *info;                   }
    inline Token_Info convert(Token        t)         const { return *get_token_info(ctx, &t); }
    inline Token_Info convert(Token const* t)         const { return *get_token_info(ctx,  t); }
    inline Token_Info convert(Parsed_Expression const* expr) const
    {
        Token_Info* from_info = get_token_info(ctx, &expr->from);
        Token_Info* to_info   = get_token_info(ctx, &expr->to);
        return merge_from_to(from_info, to_info);
    }
    inline Token_Info convert(Block const* block) const
    {
        Token_Info* from_info = get_token_info(ctx, &block->from);
        Token_Info* to_info   = get_token_info(ctx, &block->to);
        return merge_from_to(from_info, to_info);
    }

    inline Token_Info merge_from_to(Token_Info const* from, Token_Info const* to) const
    {
        Token_Info result = *from;
        u32 length = to->offset + to->length - from->offset;
        if (length > U16_MAX) length = U16_MAX;
        result.length = length;
        return result;
    }
};

template <typename T>
inline bool report_error(Compiler* ctx, T at, String message, Severity severity = SEVERITY_ERROR)
{
    return Report(ctx).part(at, message, severity).done();
}

u32 get_line(Compiler* ctx, Token* token);
void get_line(Compiler* ctx, Token_Info const* info, u32* out_line, u32* out_column = NULL, String* out_source_name = NULL);

bool supports_colored_output();



ExitApplicationNamespace
