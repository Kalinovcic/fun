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
    ATOM_F32,                   // f32
    ATOM_F64,                   // f64
    ATOM_BOOL8,                 // bool8
    ATOM_BOOL16,                // bool16
    ATOM_BOOL32,                // bool32
    ATOM_BOOL64,                // bool64
    ATOM_STRUCT,                // struct

    // keywords
    ATOM_TYPE,                  // type
    ATOM_PROC,                  // proc
    ATOM_MACRO,                 // macro
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
    ATOM_BANG,                  // !
    ATOM_EQUAL_EQUAL,           // ==
    ATOM_BANG_EQUAL,            // !=
    ATOM_LESS,                  // <
    ATOM_LESS_EQUAL,            // <=
    ATOM_GREATER,               // >
    ATOM_GREATER_EQUAL,         // >=

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
    u64 value;
};

CompileTimeAssert(sizeof(Token_Info_Integer) == 16);

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
    TYPE_INVALID,

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
    TYPE_SOFT_INTEGER,

    TYPE_F16,
    TYPE_F32,
    TYPE_F64,
    TYPE_SOFT_FLOATING_POINT,

    TYPE_BOOL8,
    TYPE_BOOL16,
    TYPE_BOOL32,
    TYPE_BOOL64,
    TYPE_SOFT_BOOL,

    TYPE_ONE_PAST_LAST_PRIMITIVE_TYPE,

    TYPE_FIRST_USER_TYPE = TYPE_ONE_PAST_LAST_PRIMITIVE_TYPE,
};

inline bool is_integer_type       (Type type) { return type >= TYPE_U8    && type <= TYPE_SOFT_INTEGER;                 }
inline bool is_floating_point_type(Type type) { return type >= TYPE_F16   && type <= TYPE_SOFT_FLOATING_POINT;          }
inline bool is_numeric_type       (Type type) { return is_integer_type(type) || is_floating_point_type(type);           }
inline bool is_boolean_type       (Type type) { return type >= TYPE_BOOL8 && type <= TYPE_SOFT_BOOL;                    }
inline bool is_primitive_type     (Type type) { return type >= TYPE_VOID  && type <  TYPE_ONE_PAST_LAST_PRIMITIVE_TYPE; }
inline bool is_user_defined_type  (Type type) { return type >= TYPE_FIRST_USER_TYPE;                                    }
inline bool is_soft_type          (Type type) { return type == TYPE_SOFT_ZERO || type == TYPE_SOFT_INTEGER || type == TYPE_SOFT_FLOATING_POINT || type == TYPE_SOFT_BOOL; }



////////////////////////////////////////////////////////////////////////////////
// AST
////////////////////////////////////////////////////////////////////////////////


// enums so we can't just assign any number in assignment
enum Expression:  u32 {};
enum Statement:   u32 {};

enum Block_Index: u32 {};
static constexpr Block_Index NO_BLOCK = (Block_Index) 0xFFFFFFFF;

#if 0
// top bit in declaration is set for global declarations
enum Declaration: u32
{
    DECLARATION_IS_GLOBAL  = 0x80000000,
    DECLARATION_INDEX_MASK = 0x7FFFFFFF,
    DECLARATION_INVALID    = 0xFFFFFFFF,
};


enum Declaration_Kind: u32
{
    DECLARATION_PROCEDURE,
    DECLARATION_VARIABLE,
};

struct Declaration_Data
{
    // Filled out in parsing:
    Declaration_Kind kind;
    Token            name;
    Declaration      next_overload;

    union
    {
        struct Procedure* procedure;
        Type parsed_variable_type;
    };

    // Filled out in typechecking:
    Type inferred_type;
};
#endif


struct Declaration
{
    Token      name;
    Statement  visibility_limit;
    Expression declared_by;
};

struct Block
{
    // Filled out in parsing:
    Token from;
    Token to;

    Array<struct Parsed_Expression const> parsed_expressions;
    Array<struct Parsed_Statement  const> parsed_statements;
    Array<struct Child_Block       const> children_blocks;

    // Filled out in typechecking:
    Block* materialized_from;
    Array<Type> inferred_types;  // parallel to expressions

    Block*    parent_scope;
    Statement parent_scope_visibility_limit;

    // Used in execution:
    Array<union Runtime_Value> values;  // parallel to expressions
};

struct Child_Block
{
    Block*    child;
    Statement visibility_limit;
};


struct Expression_List
{
    u32        count;
    Expression expressions[0];
};

enum Expression_Kind: u16
{
    EXPRESSION_INVALID,

    // literal expressions
    EXPRESSION_ZERO,
    EXPRESSION_TRUE,
    EXPRESSION_FALSE,
    EXPRESSION_INTEGER_LITERAL,
    EXPRESSION_FLOATING_POINT_LITERAL,

    EXPRESSION_NAME,

    // unary operators
    EXPRESSION_CAST,
    EXPRESSION_NEGATE,

    // binary operators
    EXPRESSION_DECLARATION,
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

    // calling expressions
    EXPRESSION_CALL,
};

struct Parsed_Expression
{
    Expression_Kind kind;
    flags16         flags;

    Statement       visibility_limit;
    u32             _padding;

    Token from;
    Token to;

    // Weirdly split into two unions to remove unnecessary padding.

    union
    {
        Expression binary_lhs;
        Expression call_lhs;
        Type       parsed_type;
    };

    union
    {
        Token name;
        Token literal;
        Expression unary_operand;
        Expression binary_rhs;
        Expression_List const* arguments;
    };
};

CompileTimeAssert(sizeof(Parsed_Expression) == 40);


enum Statement_Kind: u16
{
    STATEMENT_DECLARATION,
    STATEMENT_EXPRESSION,
    STATEMENT_IF,
    STATEMENT_WHILE,
    // STATEMENT_YIELD,
    STATEMENT_DEBUG_OUTPUT,
};

enum: flags16
{
    STATEMENT_IS_DEFERRED = 0x0001,
};

struct Parsed_Statement
{
    Statement_Kind kind;
    flags16        flags;
    Token          from;
    Token          to;
    Expression     expression;
    union
    {
        struct
        {
            Block_Index true_block;
            Block_Index false_block;
        };
    };
};

CompileTimeAssert(sizeof(Parsed_Statement) == 32);


struct Run
{
    Token  from;
    Token  to;
    Block* entry_block;
};




struct Pipeline_Task
{
    Block* block;
};

struct Unit
{
    Region memory;
    Compiler* ctx;

    Token initiator_from;
    Token initiator_to;
    Run*  initiator_run;

    Block* entry_block;

    Dynamic_Array<Pipeline_Task> pipeline;
};


////////////////////////////////////////////////////////////////////////////////
// API
////////////////////////////////////////////////////////////////////////////////


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
};

void free_compiler(Compiler* compiler);


////////////////////////////////////////////////////////////////////////////////
// Lexer

// Your responsibility that code remains allocated as long as necessary!
bool lex_from_memory(Compiler* ctx, String name, String code, Array<Token>* out_tokens);
bool lex_file(Compiler* ctx, String path, Array<Token>* out_tokens);

void get_line_and_column(Compiler* ctx, Token_Info const* info,
                         u32* out_line,
                         u32* out_column = NULL,
                         Array<String> out_source_lines = {});

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


String location_report_part(Compiler* ctx, Token_Info const* info);
bool report_error_locationless(Compiler* ctx, String message);
bool report_error(Compiler* ctx, Token const* at, String message);
bool report_error(Compiler* ctx, Token const* at1, String message1, Token const* at2, String message2);


////////////////////////////////////////////////////////////////////////////////
// Parser

bool parse_top_level(Compiler* ctx, Array<Token> tokens);


////////////////////////////////////////////////////////////////////////////////
// Typechecker

Unit* materialize_unit(Compiler* ctx, Run* initiator);

bool check_unit(Unit* unit);

bool find_declaration(Unit* unit, Token const* name,
                      Block* scope, Statement visibility_limit,
                      Block** out_decl_scope, Expression* out_decl_expr);


////////////////////////////////////////////////////////////////////////////////
// Execution

void run_unit(Unit* unit);



////////////////////////////////////////////////////////////////////////////////
// Developer output


void html_add_graph(String title, String dot_code);
void html_dump(String path);



ExitApplicationNamespace