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
    ATOM_NUMBER,                // any numeric literal
    ATOM_STRING,                // any string literal
    ATOM_ZERO,                  // zero
    ATOM_TRUE,                  // true
    ATOM_FALSE,                 // false

    // types
    ATOM_VOID,                  // void
    ATOM_U8,                    // u8
    ATOM_U16,                   // u16
    ATOM_U32,                   // u32
    ATOM_U64,                   // u64
    ATOM_S8,                    // s8
    ATOM_S16,                   // s16
    ATOM_S32,                   // s32
    ATOM_S64,                   // s64
    ATOM_F16,                   // f16
    ATOM_F32,                   // f32
    ATOM_F64,                   // f64
    ATOM_BOOL8,                 // bool8
    ATOM_BOOL16,                // bool16
    ATOM_BOOL32,                // bool32
    ATOM_BOOL64,                // bool64
    ATOM_STRUCT,                // struct

    // keywords
    ATOM_IMPORT,                // import
    ATOM_USING,                 // using
    ATOM_TYPE,                  // type
    ATOM_BLOCK,                 // block
    ATOM_CODE_BLOCK,            // code_block
    ATOM_GLOBAL,                // global
    ATOM_THREAD_LOCAL,          // thread_local
    ATOM_UNIT_LOCAL,            // unit_local
    ATOM_UNIT_DATA,             // unit_data
    ATOM_UNIT_CODE,             // unit_code
    ATOM_LABEL,                 // label
    ATOM_GOTO,                  // goto
    ATOM_DEBUG,                 // debug
    ATOM_IF,                    // if
    ATOM_ELSE,                  // else
    ATOM_WHILE,                 // while
    ATOM_DO,                    // do
    ATOM_RUN,                   // run
    ATOM_RETURN,                // return
    ATOM_YIELD,                 // yield
    ATOM_DEFER,                 // defer
    ATOM_CAST,                  // cast

    // symbols
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
    TYPE_S8,
    TYPE_S16,
    TYPE_S32,
    TYPE_S64,
    TYPE_F16,
    TYPE_F32,
    TYPE_F64,
    TYPE_SOFT_NUMBER,           // this is the type of compile-time evaluated floating-point expressions

    TYPE_BOOL8,
    TYPE_BOOL16,
    TYPE_BOOL32,
    TYPE_BOOL64,
    TYPE_SOFT_BOOL,             // this is the type of compile-time evaluated logic expressions

    TYPE_TYPE,
    TYPE_SOFT_TYPE,             // this is the type of compile-time evaluated type expressions

    TYPE_SOFT_BLOCK,            // refers to a constant parsed block, not yet materialized
                                // @Reconsider - materialized blocks do not have values at the moment,
                                //               but we might need that for unit instantiation?

    TYPE_ONE_PAST_LAST_PRIMITIVE_TYPE,

    TYPE_FIRST_USER_TYPE = TYPE_ONE_PAST_LAST_PRIMITIVE_TYPE,
};

inline Type get_base_type  (Type type)                  { return (Type)(type & TYPE_BASE_MASK); }
inline u32  get_indirection(Type type)                  { return type >> TYPE_POINTER_SHIFT; }
inline Type set_indirection(Type type, u32 indirection) { return (Type)((type & TYPE_BASE_MASK) | (indirection << TYPE_POINTER_SHIFT)); }

inline bool is_integer_type         (Type type) { return type >= TYPE_U8    && type <= TYPE_S64;                          }
inline bool is_unsigned_integer_type(Type type) { return type >= TYPE_U8    && type <= TYPE_U64;                          }
inline bool is_signed_integer_type  (Type type) { return type >= TYPE_S8    && type <= TYPE_S64;                          }
inline bool is_floating_point_type  (Type type) { return type >= TYPE_F16   && type <= TYPE_F64;                          }
inline bool is_numeric_type         (Type type) { return is_integer_type(type) || is_floating_point_type(type) || type == TYPE_SOFT_NUMBER; }
inline bool is_bool_type            (Type type) { return type >= TYPE_BOOL8 && type <= TYPE_SOFT_BOOL;                    }
inline bool is_primitive_type       (Type type) { return type >= TYPE_VOID  && type < TYPE_ONE_PAST_LAST_PRIMITIVE_TYPE;  }
inline bool is_user_defined_type    (Type type) { return type >= TYPE_FIRST_USER_TYPE;                                    }
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

static constexpr Expression NO_EXPRESSION = (Expression) 0xFFFFFFFF;
static constexpr Visibility NO_VISIBILITY = (Visibility) 0xFFFFFFFF;


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
    EXPRESSION_TYPE_LITERAL,
    EXPRESSION_BLOCK,

    EXPRESSION_NAME,

    // unary operators
    EXPRESSION_NEGATE,
    EXPRESSION_ADDRESS,
    EXPRESSION_DEREFERENCE,

    // binary operators
    EXPRESSION_ASSIGNMENT,
    EXPRESSION_ADD,
    EXPRESSION_SUBTRACT,
    EXPRESSION_MULTIPLY,
    EXPRESSION_DIVIDE_WHOLE,
    EXPRESSION_DIVIDE_FRACTIONAL,
    EXPRESSION_EQUAL,
    EXPRESSION_NOT_EQUAL,
    EXPRESSION_GREATER_THAN,
    EXPRESSION_GREATER_OR_EQUAL,
    EXPRESSION_LESS_THAN,
    EXPRESSION_LESS_OR_EQUAL,
    EXPRESSION_CAST,

    // branching expressions
    EXPRESSION_BRANCH,
    EXPRESSION_CALL,

    // other
    EXPRESSION_DECLARATION,
    EXPRESSION_RUN,
    EXPRESSION_DEBUG,
};

enum: flags16
{
    EXPRESSION_IS_IN_PARENTHESES             = 0x0001,
    EXPRESSION_DECLARATION_IS_PARAMETER      = 0x0002,
    EXPRESSION_DECLARATION_IS_ALIAS          = 0x0004,
    EXPRESSION_DECLARATION_IS_ORDERED        = 0x0008,
    EXPRESSION_ALLOW_PARENT_SCOPE_VISIBILITY = 0x0010,
    EXPRESSION_BRANCH_IS_LOOP                = 0x0020,
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
            Expression block;
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


static constexpr u64 INVALID_CONSTANT_INDEX = U64_MAX;
static constexpr u64 INVALID_STORAGE_SIZE   = U64_MAX;
static constexpr u64 INVALID_STORAGE_OFFSET = U64_MAX;

struct Soft_Block
{
    struct Block* materialized_parent;
    struct Block* parsed_child;
};

enum: flags32
{
    INFERRED_EXPRESSION_IS_NOT_EVALUATED_AT_RUNTIME = 0x0001,
    INFERRED_EXPRESSION_DOES_NOT_ALLOCATE_STORAGE   = 0x0002,
};

struct Inferred_Expression
{
    flags32       flags;
    Type          type;
    u64           size;
    u64           offset;
    struct Block* called_block;
    union
    {
        u64        constant_index;
        bool       constant_bool;
        Type       constant_type;
        Soft_Block constant_block;
    };
};

CompileTimeAssert(sizeof(Inferred_Expression) == 48);


enum Wait_Reason: u32
{
    WAITING_ON_OPERAND,              // most boring wait reason, skipped in chain reporting because it's obvious
    WAITING_ON_DECLARATION,          // a name expression can wait for the declaration to infer
    WAITING_ON_PARAMETER_INFERENCE,  // a call expression can wait for the callee to infer its parameter types
    WAITING_ON_BAKE_INFERENCE,       // an alias parameter declaration can wait for the caller to infer it
};

struct Wait_Info
{
    Wait_Reason why;
    Expression  on_expression;
    Block*      on_block;
};


enum: flags32
{
    BLOCK_IS_MATERIALIZED     = 0x0001,
    BLOCK_IS_PARAMETER_BLOCK  = 0x0002,
    BLOCK_HAS_BLOCK_PARAMETER = 0x0004,
    BLOCK_RUNTIME_ALLOCATED   = 0x0008,
    BLOCK_IS_TOP_LEVEL        = 0x0010,
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
    Block* materialized_from;
    Array<struct Inferred_Expression> inferred_expressions;  // parallel to parsed_expressions
    Dynamic_Array<Fraction> constants;

    Table(Expression, Wait_Info, hash_u32) waiting_expressions;

    Block*     parent_scope;
    Visibility parent_scope_visibility_limit;
};



/*struct Run
{
    Token  from;
    Token  to;
    Block* entry_block;
};*/


static constexpr umm MAX_BLOCKS_PER_UNIT = 10000;

struct Unit
{
    Region memory;
    Compiler* ctx;

    Token  initiator_from;
    Token  initiator_to;
    Block* initiator_block;

    Block* entry_block;

    umm pointer_size;
    umm pointer_alignment;

    umm    materialized_block_count;
    Block* most_recent_materialized_block;

    u64 next_storage_offset;
};


////////////////////////////////////////////////////////////////////////////////
// API
////////////////////////////////////////////////////////////////////////////////


struct Pipeline_Task
{
    Unit*  unit;
    Block* block;
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
};

void free_compiler(Compiler* compiler);


////////////////////////////////////////////////////////////////////////////////
// Lexer

// Your responsibility that code remains allocated as long as necessary!
bool lex_from_memory(Compiler* ctx, String name, String code, Array<Token>* out_tokens);
bool lex_file(Compiler* ctx, String path, Array<Token>* out_tokens);

inline Token_Info* get_token_info(Compiler* ctx, Token const* token)
{
    if (token->atom == ATOM_NUMBER)
        return &ctx->token_info_number[token->info_index];
    if (token->atom == ATOM_STRING)
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
Block* parse_top_level_from_memory(Compiler* ctx, String name, String code);


////////////////////////////////////////////////////////////////////////////////
// Inference

Unit* materialize_unit(Compiler* ctx, Block* initiator);

String vague_type_description(Unit* unit, Type type, bool point_out_soft_types = false);
String vague_type_description_in_compile_time_context(Unit* unit, Type type);
String exact_type_description(Unit* unit, Type type);

bool find_declaration(Unit* unit, Token const* name,
                      Block* scope, Visibility visibility_limit,
                      Block** out_decl_scope, Expression* out_decl_expr);

u64  get_type_size        (Unit* unit, Type type);
u64  get_type_alignment   (Unit* unit, Type type);
void allocate_unit_storage(Unit* unit, Type type, u64* out_size, u64* out_offset);

bool pump_pipeline(Compiler* ctx);


////////////////////////////////////////////////////////////////////////////////
// Runtime

void run_unit(Unit* unit);



////////////////////////////////////////////////////////////////////////////////
// Developer output


void html_add_graph(String title, String dot_code);
void html_dump(String path);



////////////////////////////////////////////////////////////////////////////////
// Reporting

Token_Info dummy_token_info_for_expression(Compiler* ctx, Parsed_Expression const* expr);

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
    Report& intro(Severity severity);
    Report& continuation();
    Report& message(String message);

    inline Report& intro       (Severity severity, auto at)   { return internal_intro       (severity, convert(at)); }
    inline Report& continuation(auto at, bool skinny = false) { return internal_continuation(convert(at), skinny); }
    inline Report& snippet     (auto at, bool skinny = false) { return internal_snippet     (convert(at), skinny); }
    inline Report& suggestion(String left, auto at, String right, bool skinny = false) { return internal_suggestion(left, convert(at), right, skinny); }

    inline Report& part(auto at, String msg, Severity severity = SEVERITY_ERROR)
    {
        first_part ? intro(severity, at) : continuation(at);
        message(msg);
        snippet(at);
        first_part = false;
        return *this;
    }

    bool done();

private:
    Report& internal_intro(Severity severity, Token_Info info);
    Report& internal_continuation(Token_Info info, bool skinny);
    Report& internal_snippet(Token_Info info, bool skinny);
    Report& internal_suggestion(String left, Token_Info info, String right, bool skinny);

    inline Token_Info convert(Token_Info info)               { return  info;                                      }
    inline Token_Info convert(Token_Info const* info)        { return *info;                                      }
    inline Token_Info convert(Token const* t)                { return *get_token_info(ctx, t);                    }
    inline Token_Info convert(Parsed_Expression const* expr) { return dummy_token_info_for_expression(ctx, expr); }
};


void get_line(Compiler* ctx, Token_Info const* info,
              u32* out_line, u32* out_column = NULL, String* out_source_name = NULL);

void get_source_code_slice(Compiler* ctx, Token_Info const* info,
                           umm extra_lines_before, umm extra_lines_after,
                           String* out_source, u32* out_source_offset, u32* out_source_line);

String highlighted_snippet(Compiler* ctx, Token_Info const* info, umm lines_before = 2, umm lines_behind = 0, bool skinny = false);

template <typename T>
inline bool report_error(Compiler* ctx, T at, String message, Severity severity = SEVERITY_ERROR)
{
    return Report(ctx).part(at, message, severity).done();
}

bool supports_colored_output();



ExitApplicationNamespace
