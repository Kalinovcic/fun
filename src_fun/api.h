EnterApplicationNamespace


struct Compiler;


////////////////////////////////////////////////////////////////////////////////
// Lexer
////////////////////////////////////////////////////////////////////////////////


enum Atom: u32
{
    ATOM_INVALID,

    // literals
    ATOM_INTEGER,               // any unsigned integer literal
    ATOM_FLOAT,                 // any floating point literal
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
    ATOM_UNDERSCORE,            // _

    // symbols
    ATOM_COMMA,                 // ,
    ATOM_SEMICOLON,             // ;
    ATOM_COLON,                 // :
    ATOM_EQUAL,                 // =
    ATOM_BACKTICK,              // `
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

struct Token_Info_Integer: Token_Info
{
    Integer value;
};

CompileTimeAssert(sizeof(Token_Info_Integer) == 40);

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
    TYPE_SOFT_INTEGER,          // this is the type of compile-time evaluated integer expressions

    TYPE_F16,
    TYPE_F32,
    TYPE_F64,
    TYPE_SOFT_FLOATING_POINT,   // this is the type of compile-time evaluated floating-point expressions

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

inline u32  get_indirection(Type type)                  { return type >> TYPE_POINTER_SHIFT; }
inline Type set_indirection(Type type, u32 indirection) { return (Type)((type & TYPE_BASE_MASK) | (indirection << TYPE_POINTER_SHIFT)); }

inline bool is_integer_type         (Type type) { return type >= TYPE_U8    && type <= TYPE_SOFT_INTEGER;                 }
inline bool is_unsigned_integer_type(Type type) { return type >= TYPE_U8    && type <= TYPE_U64;                          }
inline bool is_signed_integer_type  (Type type) { return type >= TYPE_S8    && type <= TYPE_S64;                          }
inline bool is_floating_point_type  (Type type) { return type >= TYPE_F16   && type <= TYPE_SOFT_FLOATING_POINT;          }
inline bool is_numeric_type         (Type type) { return is_integer_type(type) || is_floating_point_type(type);           }
inline bool is_bool_type            (Type type) { return type >= TYPE_BOOL8 && type <= TYPE_SOFT_BOOL;                    }
inline bool is_primitive_type       (Type type) { return type >= TYPE_VOID  && type < TYPE_ONE_PAST_LAST_PRIMITIVE_TYPE;  }
inline bool is_user_defined_type    (Type type) { return type >= TYPE_FIRST_USER_TYPE;                                    }
inline bool is_type_type            (Type type) { return type == TYPE_TYPE  || type == TYPE_SOFT_TYPE;                    }
inline bool is_block_type           (Type type) { return type == TYPE_SOFT_BLOCK;                                         }
inline bool is_soft_type            (Type type) { return type == TYPE_SOFT_ZERO || type == TYPE_SOFT_INTEGER || type == TYPE_SOFT_FLOATING_POINT || type == TYPE_SOFT_BOOL || type == TYPE_SOFT_TYPE || type == TYPE_SOFT_BLOCK; }
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
    EXPRESSION_INTEGER_LITERAL,
    EXPRESSION_FLOATING_POINT_LITERAL,
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
    EXPRESSION_DIVIDE,
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


enum: flags32
{
    BLOCK_IS_MATERIALIZED     = 0x0001,
    BLOCK_IS_PARAMETER_BLOCK  = 0x0002,
    BLOCK_HAS_BLOCK_PARAMETER = 0x0004,
    BLOCK_RUNTIME_ALLOCATED   = 0x0008,
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
    Dynamic_Array<struct Integer> constants;

    Block*     parent_scope;
    Visibility parent_scope_visibility_limit;
    Soft_Block block_parameter;
};



struct Run
{
    Token  from;
    Token  to;
    Block* entry_block;
};


struct Unit
{
    Region memory;
    Compiler* ctx;

    Token initiator_from;
    Token initiator_to;
    Run*  initiator_run;

    Block* entry_block;

    umm pointer_size;
    umm pointer_alignment;
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

    Dynamic_Array<Source_Info,        false> sources;
    Dynamic_Array<Token_Info,         false> token_info_other;
    Dynamic_Array<Token_Info_Integer, false> token_info_integer;
    Dynamic_Array<String,             false> identifiers;

    Atom next_identifier_atom;
    Table(String, Atom, hash_string) atom_table;

    // Parser
    Region parser_memory;
    Dynamic_Array<Run*, false> runs;

    // Inference
    Dynamic_Array<Pipeline_Task> pipeline;
};

void free_compiler(Compiler* compiler);


////////////////////////////////////////////////////////////////////////////////
// Lexer

// Your responsibility that code remains allocated as long as necessary!
bool lex_from_memory(Compiler* ctx, String name, String code, Array<Token>* out_tokens);
bool lex_file(Compiler* ctx, String path, Array<Token>* out_tokens);

void get_line(Compiler* ctx, Token_Info const* info,
              u32* out_line, u32* out_column = NULL, String* out_source_name = NULL);

void get_source_code_slice(Compiler* ctx, Token_Info const* info,
                           umm extra_lines_before, umm extra_lines_after,
                           String* out_source, u32* out_source_offset, u32* out_source_line);

Token_Info dummy_token_info_for_expression(Compiler* ctx, Parsed_Expression const* expr);

inline Token_Info* get_token_info(Compiler* ctx, Token const* token)
{
    if (token->atom == ATOM_INTEGER)
        return &ctx->token_info_integer[token->info_index];
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


String location_report_part(Compiler* ctx, Token_Info const* info, umm lines_before = 2, umm lines_behind = 0);
bool report_error_locationless(Compiler* ctx, String message);
bool report_error(Compiler* ctx, Token const* at, String message);
bool report_error(Compiler* ctx, Token const* at1, String message1, Token const* at2, String message2);
bool report_error(Compiler* ctx, Parsed_Expression const* at, String message);


////////////////////////////////////////////////////////////////////////////////
// Parser

bool parse_top_level(Compiler* ctx, Array<Token> tokens);


////////////////////////////////////////////////////////////////////////////////
// Inference

Unit* materialize_unit(Compiler* ctx, Run* initiator);

bool find_declaration(Unit* unit, Token const* name,
                      Block* scope, Visibility visibility_limit,
                      Block** out_decl_scope, Expression* out_decl_expr);

void allocate_unit_storage(Unit* unit, Type type, u64* out_size, u64* out_offset);

bool pump_pipeline(Compiler* ctx);


////////////////////////////////////////////////////////////////////////////////
// Runtime

void run_unit(Unit* unit);



////////////////////////////////////////////////////////////////////////////////
// Developer output


void html_add_graph(String title, String dot_code);
void html_dump(String path);



ExitApplicationNamespace
