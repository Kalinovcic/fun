EnterApplicationNamespace


struct Compiler;


////////////////////////////////////////////////////////////////////////////////
// Fraction
////////////////////////////////////////////////////////////////////////////////


bool int_get_abs_u64(u64* out, Integer const* i);

String int_base10(Integer const* integer, Region* memory = temp, umm min_digits = 0);

// Always reduced, denominator always positive.
struct Fraction
{
    Integer num;
    Integer den;
};

Fraction fract_make_u64  (u64 integer);
Fraction fract_make      (Integer const* num, Integer const* den);
void     fract_free      (Fraction* f);
Fraction fract_clone     (Fraction const* from);
void     fract_reduce    (Fraction* f);
bool     fract_is_zero   (Fraction const* f);
bool     fract_is_integer(Fraction const* f);
Fraction fract_neg       (Fraction const* a);
Fraction fract_add       (Fraction const* a, Fraction const* b);
Fraction fract_sub       (Fraction const* a, Fraction const* b);
Fraction fract_mul       (Fraction const* a, Fraction const* b);
bool     fract_div_fract (Fraction* out, Fraction const* a, Fraction const* b);
bool     fract_div_whole (Fraction* out, Fraction const* a, Fraction const* b);
String   fract_display   (Fraction const* f, Region* memory = temp);


////////////////////////////////////////////////////////////////////////////////
// Lexer
////////////////////////////////////////////////////////////////////////////////


enum Atom: u32
{
    ATOM_INVALID,

    // literals
    ATOM_NUMBER_LITERAL,        // any numeric literal
    ATOM_STRING_LITERAL,        // any string literal
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
    ATOM_IF,                    // if
    ATOM_ELSE,                  // else
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
    ATOM_AMPERSAND,             // &
    ATOM_AMPERSAND_PLUS,        // &+
    ATOM_AMPERSAND_MINUS,       // &-
    ATOM_BANG,                  // !
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
    TYPE_STRING = TYPE_FIRST_USER_TYPE,
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

enum Expression_Kind: u16
{
    EXPRESSION_INVALID,

    // literal expressions
    EXPRESSION_ZERO,
    EXPRESSION_TRUE,
    EXPRESSION_FALSE,
    EXPRESSION_NUMERIC_LITERAL,
    EXPRESSION_STRING_LITERAL,
    EXPRESSION_TYPE_LITERAL,
    EXPRESSION_BLOCK,
    EXPRESSION_UNIT,

    EXPRESSION_NAME,
    EXPRESSION_MEMBER,

    // unary operators
    EXPRESSION_NEGATE,
    EXPRESSION_ADDRESS,
    EXPRESSION_DEREFERENCE,
    EXPRESSION_SIZEOF,
    EXPRESSION_ALIGNOF,
    EXPRESSION_CODEOF,
    EXPRESSION_DEBUG,
    EXPRESSION_DEBUG_ALLOC,
    EXPRESSION_DEBUG_FREE,

    // binary operators
    EXPRESSION_ASSIGNMENT,
    EXPRESSION_ADD,
    EXPRESSION_SUBTRACT,
    EXPRESSION_MULTIPLY,
    EXPRESSION_DIVIDE_WHOLE,
    EXPRESSION_DIVIDE_FRACTIONAL,
    EXPRESSION_POINTER_ADD,
    EXPRESSION_POINTER_SUBTRACT,
    EXPRESSION_EQUAL,
    EXPRESSION_NOT_EQUAL,
    EXPRESSION_GREATER_THAN,
    EXPRESSION_GREATER_OR_EQUAL,
    EXPRESSION_LESS_THAN,
    EXPRESSION_LESS_OR_EQUAL,
    EXPRESSION_CAST,
    EXPRESSION_GOTO_UNIT,

    // branching expressions
    EXPRESSION_BRANCH,
    EXPRESSION_CALL,

    // other
    EXPRESSION_DECLARATION,
};

enum: flags16
{
    EXPRESSION_IS_IN_PARENTHESES             = 0x0001,
    EXPRESSION_DECLARATION_IS_PARAMETER      = 0x0002,
    EXPRESSION_DECLARATION_IS_ALIAS          = 0x0004,
    EXPRESSION_DECLARATION_IS_ORDERED        = 0x0008,
    EXPRESSION_ALLOW_PARENT_SCOPE_VISIBILITY = 0x0010,
    EXPRESSION_BRANCH_IS_LOOP                = 0x0020,
    EXPRESSION_BLOCK_IS_IMPORTED             = 0x0040,
    EXPRESSION_DECLARATION_IS_INFERRED_ALIAS = 0x0080,
    EXPRESSION_HAS_TO_BE_EXTERNALLY_INFERRED = 0x0100,
    EXPRESSION_DECLARATION_IS_UNINITIALIZED  = 0x0200,
    EXPRESSION_BRANCH_IS_BAKED               = 0x0400,
    EXPRESSION_HAS_CONDITIONAL_INFERENCE     = 0x0800,
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
        Expression    unary_operand;
        Type          parsed_type;
        struct Block* parsed_block;

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
    INFERRED_EXPRESSION_MEMBER_IS_INDIRECT          = 0x0020,
};

struct Inferred_Expression
{
    flags32       flags;
    Type          type;
    struct Block* called_block;
    u64           constant;
};

CompileTimeAssert(sizeof(Inferred_Expression) == 24);


struct Soft_Block
{
    struct Block* materialized_parent;
    struct Block* parsed_child;

    bool          has_alias;
    Token         alias;
};

union Constant
{
    Fraction   number;
    bool       boolean;
    Type       type;
    Soft_Block block;
};

struct Resolved_Name
{
    Block*     scope;
    Expression declaration;
};


enum Wait_Reason: u32
{
    WAITING_ON_OPERAND,              // most boring wait reason, skipped in chain reporting because it's obvious
    WAITING_ON_DECLARATION,          // a name expression can wait for the declaration to infer
    WAITING_ON_PARAMETER_INFERENCE,  // a call expression can wait for the callee to infer its parameter types
    WAITING_ON_EXTERNAL_INFERENCE,   // an alias parameter declaration can wait for the caller to infer it,
                                     // or an inferred type alias can wait for its surrounding expression to infer it
    WAITING_ON_CONDITION_INFERENCE,  // a call expression of a baked branch can wait for the condition to be inferred
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
    BLOCK_IS_TOP_LEVEL            = 0x0008,
    BLOCK_IS_UNIT                 = 0x0010,
    BLOCK_HAS_STRUCTURE_PLACEMENT = 0x0020,
};

struct Block
{
    // Filled out in parsing:
    flags32 flags;
    Token   from;
    Token   to;

    Array<struct Parsed_Expression const> parsed_expressions;
    Array<Expression const> imperative_order;

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



static constexpr umm MAX_BLOCKS_PER_UNIT = 10000;

enum: flags32
{
    UNIT_IS_STRUCT    = 0x0001,
    UNIT_IS_PLACED    = 0x0002,
    UNIT_IS_COMPLETED = 0x0004,
};

struct Unit
{
    Region memory;
    Compiler* ctx;
    flags32 flags;

    Token  initiator_from;
    Token  initiator_to;
    Block* initiator_block;

    Block* entry_block;

    umm pointer_size;
    umm pointer_alignment;

    umm    materialized_block_count;
    Block* most_recent_materialized_block;

    u64    next_storage_offset;

    umm    blocks_not_completed;
    umm    blocks_not_ready_for_placement;
    u64    storage_alignment;
    u64    storage_size;

    bool   compiled_bytecode;
    Array<struct Bytecode> bytecode;
};



enum Bytecode_Operation: u64
{
    INVALID_OP,

    OP_ZERO,                    // r =  destination                s = size
    OP_LITERAL,                 // r =  destination  a =  content  s = size
    OP_LITERAL_INDIRECT,        // r =  destination  a = &content  s = size
    OP_COPY,                    // r =  destination  a =  source   s = size
    OP_COPY_FROM_INDIRECT,      // r =  destination  a = &source   s = size
    OP_COPY_TO_INDIRECT,        // r = &destination  a =  source   s = szie
    OP_ADDRESS,                 // r =  destination  a =  offset

    OP_NEGATE,                  // r = result  a = operand
    OP_ADD,                     // r = result  a = lhs  b = rhs  s = type
    OP_SUBTRACT,                // r = result  a = lhs  b = rhs  s = type
    OP_MULTIPLY,                // r = result  a = lhs  b = rhs  s = type
    OP_DIVIDE_WHOLE,            // r = result  a = lhs  b = rhs  s = type
    OP_DIVIDE_FRACTIONAL,       // r = result  a = lhs  b = rhs  s = type

    OP_MOVE_POINTER_FORWARD,    // r = result  a = pointer  b = integer  s = multiplier
    OP_MOVE_POINTER_BACKWARD,   // r = result  a = pointer  b = integer  s = multiplier
    OP_POINTER_DISTANCE,        // r = result  a = pointer  b = pointer  s = divisor

    OP_CAST,                    // r = result  a = value  b = value type  s = result tpye

    OP_GOTO,                    // r = instruction index
    OP_GOTO_IF_FALSE,           // r = instruction index  a = condition
    OP_GOTO_INDIRECT,           // r = stored code address
    OP_STORE_CODE_ADDRESS,      // r = instruction index  a = destination for code address
    OP_SWITCH_UNIT,             // r = code pointer       a = storage pointer
    OP_FINISH_UNIT,             //

    OP_DEBUG_PRINT,             // r = operand                        s = type
    OP_DEBUG_ALLOC,             // r = destination  a = size operand
    OP_DEBUG_FREE,              // r = operand
};

struct Bytecode
{
    Bytecode_Operation op;
    u64 r;
    u64 a;
    u64 b;
    u64 s;
};



////////////////////////////////////////////////////////////////////////////////
// API
////////////////////////////////////////////////////////////////////////////////


enum Pipeline_Task_Kind
{
    INVALID_PIPELINE_TASK,
    PIPELINE_TASK_INFER_BLOCK,
};

struct Pipeline_Task
{
    Pipeline_Task_Kind kind;
    Unit*  unit;
    Block* block;
};

struct User_Type
{
    Block*     created_by_block;
    Expression created_by_expression;
    Unit*      unit;

    bool       has_alias;
    Token      alias;
};

struct Compiler
{
    // Lexer
    bool lexer_initialized;
    Region lexer_memory;

    Dynamic_Array<Source_Info,       false> sources;
    Dynamic_Array<Token_Info,        false> token_info_other;
    Dynamic_Array<Token_Info_Number, false> token_info_number;
    Dynamic_Array<Token_Info_String, false> token_info_string;
    Dynamic_Array<String,            false> identifiers;

    Atom next_identifier_atom;
    Table(String, Atom, hash_string) atom_table;

    // Parser
    Region parser_memory;

    // Inference
    Dynamic_Array<Pipeline_Task> pipeline;
    Dynamic_Array<User_Type> user_types;
};

void free_compiler(Compiler* compiler);


////////////////////////////////////////////////////////////////////////////////
// Lexer

// Your responsibility that code remains allocated as long as necessary!
bool lex_from_memory(Compiler* ctx, String name, String code, Array<Token>* out_tokens);
bool lex_file(Compiler* ctx, String path, Array<Token>* out_tokens);

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

Block* parse_top_level(Compiler* ctx, String imports_relative_to_path, Array<Token> tokens);
Block* parse_top_level_from_file(Compiler* ctx, String path);
// Your responsibility that code remains allocated as long as necessary!
Block* parse_top_level_from_memory(Compiler* ctx, String name, String code);


////////////////////////////////////////////////////////////////////////////////
// Inference

Unit* materialize_unit(Compiler* ctx, Block* initiator, Block* materialized_parent = NULL);

String vague_type_description(Unit* unit, Type type, bool point_out_soft_types = false);
String vague_type_description_in_compile_time_context(Unit* unit, Type type);
String exact_type_description(Unit* unit, Type type);

bool find_declaration(Token const* name,
                      Block* scope, Visibility visibility_limit,
                      Block** out_decl_scope, Expression* out_decl_expr,
                      bool allow_parent_traversal = true);

u64 get_type_size     (Unit* unit, Type type);
u64 get_type_alignment(Unit* unit, Type type);

Constant* get_constant(Block* block, Expression expr, Type type_assertion);
void set_constant(Block* block, Expression expr, Type type_assertion, Constant* value);

inline void set_constant_number(Block* block, Expression expr, Fraction   value) { Constant c = {}; c.number  = value; set_constant(block, expr, TYPE_SOFT_NUMBER, &c); }
inline void set_constant_bool  (Block* block, Expression expr, bool       value) { Constant c = {}; c.boolean = value; set_constant(block, expr, TYPE_SOFT_BOOL,   &c); }
inline void set_constant_type  (Block* block, Expression expr, Type       value) { Constant c = {}; c.type    = value; set_constant(block, expr, TYPE_SOFT_TYPE,   &c); }
inline void set_constant_block (Block* block, Expression expr, Soft_Block value) { Constant c = {}; c.block   = value; set_constant(block, expr, TYPE_SOFT_BLOCK,  &c); }

inline Fraction   const* get_constant_number(Block* block, Expression expr) { Constant* c = get_constant(block, expr, TYPE_SOFT_NUMBER); return c ? &c->number  : NULL; }
inline bool       const* get_constant_bool  (Block* block, Expression expr) { Constant* c = get_constant(block, expr, TYPE_SOFT_BOOL);   return c ? &c->boolean : NULL; }
inline Type       const* get_constant_type  (Block* block, Expression expr) { Constant* c = get_constant(block, expr, TYPE_SOFT_TYPE);   return c ? &c->type    : NULL; }
inline Soft_Block const* get_constant_block (Block* block, Expression expr) { Constant* c = get_constant(block, expr, TYPE_SOFT_BLOCK);  return c ? &c->block   : NULL; }

User_Type* get_user_type_data(Compiler* ctx, Type type);

bool pump_pipeline(Compiler* ctx);


////////////////////////////////////////////////////////////////////////////////
// Bytecode

void generate_bytecode_for_unit_placement(Unit* unit);
void generate_bytecode_for_unit_completion(Unit* unit);


////////////////////////////////////////////////////////////////////////////////
// Runtime

void run_unit(Unit* unit);



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
    inline Report& suggestion(String left, auto at, String right, bool skinny = false, umm before = 2, umm after = 1) { return internal_suggestion(left, convert(at), right, skinny, before, after); }

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
    Report& internal_suggestion(String left, Token_Info info, String right, bool skinny, umm before, umm after);

    inline Token_Info convert(Token_Info info)        const { return  info;                   }
    inline Token_Info convert(Token_Info const* info) const { return *info;                   }
    inline Token_Info convert(Token const* t)         const { return *get_token_info(ctx, t); }
    inline Token_Info convert(Parsed_Expression const* expr) const
    {
        Token_Info* from_info = get_token_info(ctx, &expr->from);
        Token_Info* to_info   = get_token_info(ctx, &expr->to);

        Token_Info result = *from_info;
        u32 length = to_info->offset + to_info->length - from_info->offset;
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

void get_line(Compiler* ctx, Token_Info const* info, u32* out_line, u32* out_column = NULL, String* out_source_name = NULL);

bool supports_colored_output();



ExitApplicationNamespace
