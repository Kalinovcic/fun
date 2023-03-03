#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
#include "api.h"
#include <stdio.h>

EnterApplicationNamespace


static void lex_init(Compiler* ctx)
{
    if (ctx->lexer_initialized)
        return;

    ctx->lexer_initialized = true;
    ctx->lexer_memory.page_size = Megabyte(64);
    set_capacity(&ctx->token_info_other,  Megabyte(256) / sizeof(ctx->token_info_other [0]));
    set_capacity(&ctx->token_info_number, Megabyte(64)  / sizeof(ctx->token_info_number[0]));
    set_capacity(&ctx->token_info_string, Megabyte(32)  / sizeof(ctx->token_info_string[0]));
    set_capacity(&ctx->identifiers,       Megabyte(32)  / sizeof(ctx->identifiers      [0]));

    ctx->next_identifier_atom = ATOM_FIRST_IDENTIFIER;

#define AddKeyword(atom, string) { String k = string; Atom v = atom; set(&ctx->atom_table, &k, &v); }
    AddKeyword(ATOM_ZERO,         "zero"_s);
    AddKeyword(ATOM_TRUE,         "true"_s);
    AddKeyword(ATOM_FALSE,        "false"_s);
    AddKeyword(ATOM_VOID,         "void"_s);
    AddKeyword(ATOM_U8,           "u8"_s);
    AddKeyword(ATOM_U16,          "u16"_s);
    AddKeyword(ATOM_U32,          "u32"_s);
    AddKeyword(ATOM_U64,          "u64"_s);
    AddKeyword(ATOM_UMM,          "umm"_s);
    AddKeyword(ATOM_S8,           "s8"_s);
    AddKeyword(ATOM_S16,          "s16"_s);
    AddKeyword(ATOM_S32,          "s32"_s);
    AddKeyword(ATOM_S64,          "s64"_s);
    AddKeyword(ATOM_SMM,          "smm"_s);
    AddKeyword(ATOM_F16,          "f16"_s);
    AddKeyword(ATOM_F32,          "f32"_s);
    AddKeyword(ATOM_F64,          "f64"_s);
    AddKeyword(ATOM_BOOL,         "bool"_s);
    AddKeyword(ATOM_STRUCT,       "struct"_s);
    AddKeyword(ATOM_STRING,       "string"_s);
    AddKeyword(ATOM_IMPORT,       "import"_s);
    AddKeyword(ATOM_TYPE,         "type"_s);
    AddKeyword(ATOM_BLOCK,        "block"_s);
    AddKeyword(ATOM_CODE_BLOCK,   "code_block"_s);
    AddKeyword(ATOM_GLOBAL,       "global"_s);
    AddKeyword(ATOM_THREAD_LOCAL, "thread_local"_s);
    AddKeyword(ATOM_UNIT,         "unit"_s);
    AddKeyword(ATOM_UNIT_LOCAL,   "unit_local"_s);
    AddKeyword(ATOM_UNIT_DATA,    "unit_data"_s);
    AddKeyword(ATOM_UNIT_CODE,    "unit_code"_s);
    AddKeyword(ATOM_LABEL,        "label"_s);
    AddKeyword(ATOM_GOTO,         "goto"_s);
    AddKeyword(ATOM_DEBUG,        "debug"_s);
    AddKeyword(ATOM_DEBUG_ALLOC,  "debug_alloc"_s);
    AddKeyword(ATOM_DEBUG_FREE,   "debug_free"_s);
    AddKeyword(ATOM_IF,           "if"_s);
    AddKeyword(ATOM_ELSE,         "else"_s);
    AddKeyword(ATOM_WHILE,        "while"_s);
    AddKeyword(ATOM_DO,           "do"_s);
    AddKeyword(ATOM_RUN,          "run"_s);
    AddKeyword(ATOM_RETURN,       "return"_s);
    AddKeyword(ATOM_YIELD,        "yield"_s);
    AddKeyword(ATOM_DEFER,        "defer"_s);
    AddKeyword(ATOM_CAST,         "cast"_s);
    AddKeyword(ATOM_SIZEOF,       "sizeof"_s);
    AddKeyword(ATOM_ALIGNOF,      "alignof"_s);
    AddKeyword(ATOM_CODEOF,       "codeof"_s);
    AddKeyword(ATOM_INTRINSIC,    "intrinsic"_s);
#undef AddKeyword
}

bool lex_from_memory(Compiler* ctx, String name, String code, Array<Token>* out_tokens)
{
    lex_init(ctx);

    static const constexpr u8 CHARACTER_IDENTIFIER_START        =  1;
    static const constexpr u8 CHARACTER_IDENTIFIER_CONTINUATION =  2;
    static const constexpr u8 CHARACTER_DIGIT_BASE2             =  4;
    static const constexpr u8 CHARACTER_DIGIT_BASE8             =  8;
    static const constexpr u8 CHARACTER_DIGIT_BASE10            = 16;
    static const constexpr u8 CHARACTER_DIGIT_BASE16            = 32;
    static const constexpr u8 CHARACTER_WHITE                   = 64;
    static const constexpr u8 CHARACTER_CLASS[256] =
    {
         0, 0, 0, 0, 0, 0, 0, 0, 0,64,64, 0, 0,64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,62,62,58,58,58,58,58,58,50,50, 0, 0, 0, 0, 0, 0,
         0,35,35,35,35,35,35, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 2,
         0,35,35,35,35,35,35, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0,
         3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
         3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
         3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
         3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    };
    static const constexpr u8 DIGIT_VALUE[256] =
    {
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0,
         0,10,11,12,13,14,15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0,10,11,12,13,14,15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };


    if (code.length >= U32_MAX)
    {
        fprintf(stderr, "File %.*s is too large, limit is 4GB.\n", StringArgs(name));
        return false;
    }

    if (ctx->sources.count > U16_MAX)
    {
        fprintf(stderr, "Too many files, limit is 65536.\n");
        return false;
    }

    u16 source_index = ctx->sources.count;
    Source_Info* source = reserve_item(&ctx->sources);
    source->name = allocate_string(&ctx->lexer_memory, name);
    source->code = code;

    Concatenator<u32> line_offsets = {};
    ensure_space(&line_offsets, 4096);
    Defer(source->line_offsets = resolve_to_array_and_free(&line_offsets, &ctx->lexer_memory));
    *reserve_item(&line_offsets) = 0;

    Concatenator<Token> tokens = {};
    ensure_space(&tokens, Megabyte(16) / sizeof(Token));
    Defer(*out_tokens = resolve_to_array_and_free(&tokens, &ctx->lexer_memory));

    u8* start  = code.data;
    u8* end    = start + code.length;
    u8* cursor = start;

#define LexError(...)                                           \
    {                                                           \
        String error_message = Format(temp, __VA_ARGS__);       \
        u32 line        = line_offsets.count;                   \
        u32 line_offset = line_offsets.base[line - 1];          \
        u32 column      = (cursor - start) - line_offset + 1;   \
        fprintf(stderr, "%.*s\n"                                \
                        " .. in %.*s @ %u:%u\n",                \
            StringArgs(error_message), StringArgs(name),        \
            line, column);                                      \
        return false;                                           \
    }

    // shebang support - skip the line
    if (cursor + 1 < end && cursor[0] == '#' && cursor[1] == '!')
        while (cursor < end && *cursor != '\n') cursor++;

    Integer integer_ten     = {};
    Integer integer_base    = {};
    Integer integer_digit   = {};
    Defer(int_free(&integer_ten));
    Defer(int_free(&integer_base));
    Defer(int_free(&integer_digit));
    int_set16(&integer_ten, 10);

    while (cursor < end)
    {
        u8 c  = *cursor;
        u8 cc = CHARACTER_CLASS[c];
        if (c == '\n')
        {
            cursor++;
            *reserve_item(&line_offsets) = cursor - start;
            continue;
        }

        if (c == '\t')
            LexError("Tab characters are not allowed as whitespace.")

        if (cc & CHARACTER_WHITE)
        {
            cursor++;
            continue;
        }

        if (cc & CHARACTER_DIGIT_BASE10)
        {
            u8* start_cursor = cursor;

            // parse 0b 0o 0x prefix
            u32 base = 10;
            u8 digit_class = CHARACTER_DIGIT_BASE10;
            if (cursor + 1 < end && cursor[0] == '0')
            {
                c = cursor[1];
                     if (c == 'b' || c == 'B') digit_class = CHARACTER_DIGIT_BASE2,  base =  2, cursor += 2;
                else if (c == 'o' || c == 'O') digit_class = CHARACTER_DIGIT_BASE8,  base =  8, cursor += 2;
                else if (c == 'x' || c == 'X') digit_class = CHARACTER_DIGIT_BASE16, base = 16, cursor += 2;
            }

            int_set16(&integer_base, base);

            // parse integer part
            Integer numerator = {};
            int_set_zero(&numerator);
            Defer(int_free(&numerator));
            while (cursor < end)
            {
                c  = *cursor;
                cc = CHARACTER_CLASS[c];
                if (cc & digit_class)
                {
                    int_set16(&integer_digit, DIGIT_VALUE[c]);
                    int_mul(&numerator, &integer_base);
                    int_add(&numerator, &integer_digit);
                }
                else if (cc & CHARACTER_DIGIT_BASE16 && c != 'e' && c != 'E')
                    LexError("'%' is not a base % digit.", memory_as_string(&c), base)
                else if (c != '_')
                    break;
                cursor++;
            }

            // parse fractional part if it exists
            Integer denominator = {};
            int_set16(&denominator, 1);
            Defer(int_free(&denominator));
            if (cursor < end && *cursor == '.')
            {
                cursor++;
                while (cursor < end)
                {
                    c  = *cursor;
                    cc = CHARACTER_CLASS[c];
                    if (cc & digit_class)
                    {
                        int_set16(&integer_digit, DIGIT_VALUE[c]);
                        int_mul(&numerator, &integer_base);
                        int_add(&numerator, &integer_digit);
                        int_mul(&denominator, &integer_base);
                    }
                    else if (cc & CHARACTER_DIGIT_BASE16 && c != 'e' && c != 'E')
                        LexError("'%' is not a base % digit.", memory_as_string(&c), base)
                    else if (c != '_')
                        break;
                    cursor++;
                }
            }

            // parse exponent part if it exists
            char exponent_char = (base == 16) ? 'p' : 'e';
            if (cursor < end && (*cursor == exponent_char || *cursor == (exponent_char - 'a' + 'A')))
            {
                cursor++;

                bool positive = true;
                if (cursor < end)
                {
                         if (*cursor == '+') cursor++;
                    else if (*cursor == '-') cursor++, positive = false;
                }

                Integer exponent = {};
                int_set_zero(&exponent);
                Defer(int_free(&exponent));
                bool found_at_least_one_digit = false;
                while (cursor < end)
                {
                    c  = *cursor;
                    cc = CHARACTER_CLASS[c];
                    if (cc & CHARACTER_DIGIT_BASE10)
                    {
                        found_at_least_one_digit = true;

                        int_set16(&integer_digit, DIGIT_VALUE[c]);
                        int_mul(&exponent, &integer_ten);
                        int_add(&exponent, &integer_digit);
                    }
                    else if (c != '_')
                        break;
                    cursor++;
                }
                if (!found_at_least_one_digit)
                    LexError("Missing exponent in numeric literal.")

                Integer multiplier = {};
                int_pow(&multiplier, &integer_base, &exponent);
                int_mul(positive ? &numerator : &denominator, &multiplier);
                int_free(&multiplier);
            }

            Token* token = reserve_item(&tokens);
            token->atom       = ATOM_NUMBER_LITERAL;
            token->info_index = ctx->token_info_number.count;

            Token_Info_Number* info = reserve_item(&ctx->token_info_number);
            info->source_index = source_index;
            info->offset       = start_cursor - start;
            info->length       = cursor - start_cursor;
            info->value        = fract_make(&numerator, &denominator);

            continue;
        }

        if (cc & CHARACTER_IDENTIFIER_START)
        {
            u8* start_cursor = cursor;

            umm  identifier_length       = 0;
            bool previous_was_underscore = false;
            while (cursor < end && (CHARACTER_CLASS[c = *cursor] & CHARACTER_IDENTIFIER_CONTINUATION))
            {
                bool underscore = (c == '_');
                identifier_length += !underscore || !previous_was_underscore;
                previous_was_underscore = underscore;
                cursor++;
            }

            umm token_length = cursor - start_cursor;
            String identifier = { token_length, start_cursor };
            if (token_length != identifier_length)
            {
                identifier = allocate_uninitialized_string(&ctx->lexer_memory, identifier_length);
                previous_was_underscore = false;

                umm j = 0;
                for (umm i = 0; i < token_length; i++)
                {
                    bool underscore = (start_cursor[i] == '_');
                    if (!underscore || !previous_was_underscore)
                        identifier.data[j++] = start_cursor[i];
                    previous_was_underscore = underscore;
                }
                assert(j == identifier_length);
            }

            Atom atom = get(&ctx->atom_table, &identifier);
            if (!atom)
            {
                atom = ctx->next_identifier_atom;
                ctx->next_identifier_atom = (Atom)(ctx->next_identifier_atom + 1);
                set(&ctx->atom_table, &identifier, &atom);
                add_item(&ctx->identifiers, &identifier);
            }

            Token* token = reserve_item(&tokens);
            token->atom       = atom;
            token->info_index = ctx->token_info_other.count;

            Token_Info* info = reserve_item(&ctx->token_info_other);
            info->source_index = source_index;
            info->length       = token_length;
            info->offset       = start_cursor - start;
            continue;
        }

        if (c == '"')
        {
            u8* start_cursor = cursor;
            cursor++;

            umm literal_length = 0;
            while (true)
            {
                if (cursor >= end) LexError("Unexpected EOF in string literal.")
                c = *(cursor++);
                if (c == '"') break;
                else if (c == '\n') LexError("Unexpected newline in string literal.")
                else if (c == '\r') LexError("Unexpected CR character in string literal.")
                else if (c == '\\')
                {
                    if (cursor >= end) LexError("Unexpected EOF in string literal.")
                    switch (c = *(cursor++))
                    {
                    case '0': case 'a': case 'r': case 'e':
                    case 'f': case 'n': case 't': case 'v':
                        literal_length++;
                        break;
                    case 'x': case 'u': case 'U':
                    {
                        umm digits = (c == 'x') ? 2 : ((c == 'u') ? 4 : 8);
                        u32 number = 0;
                        while (digits--)
                        {
                            if (cursor >= end) LexError("Unexpected EOF in string literal")
                            cc = CHARACTER_CLASS[c = *(cursor++)];
                            if (!(cc & CHARACTER_DIGIT_BASE16)) LexError("Expected a hexadecimal digit in escape sequence.");
                            number = (number << 4) | DIGIT_VALUE[c];
                        }
                        literal_length += (c == 'x') ? 1 : get_utf8_sequence_length(number);
                    } break;
                    default:
                        LexError("Unrecognized escape sequence in string literal.")
                    }
                }
                else literal_length++;
            }

            umm token_length = cursor - start_cursor;
            assert(token_length >= 2);  // opening and closing "
            String literal = { token_length - 2, start_cursor + 1 };
            if (token_length != literal_length + 2)
            {
                literal = allocate_uninitialized_string(&ctx->lexer_memory, literal_length);
                umm j = 0;
                for (umm i = 1; i < token_length - 1; i++)
                {
                    if (start_cursor[i] != '\\')
                    {
                        literal[j++] = start_cursor[i];
                        continue;
                    }
                    switch (c = start_cursor[++i])
                    {
                    case '0': literal[j++] = 0;    break;
                    case 'a': literal[j++] = '\a'; break;
                    case 'r': literal[j++] = '\r'; break;
                    case 'e': literal[j++] = '\e'; break;
                    case 'f': literal[j++] = '\f'; break;
                    case 'n': literal[j++] = '\n'; break;
                    case 't': literal[j++] = '\t'; break;
                    case 'v': literal[j++] = '\v'; break;
                    case 'x': case 'u': case 'U':
                    {
                        umm digits = (c == 'x') ? 2 : ((c == 'u') ? 4 : 8);
                        u32 number = 0;
                        while (digits--)
                            number = (number << 4) | DIGIT_VALUE[start_cursor[++i]];
                        if (c == 'x')
                            literal[j++] = number;
                        else
                        {
                            u32 length = get_utf8_sequence_length(number);
                            assert(literal.length - j >= length);
                            encode_utf8_sequence(number, &literal[j], length);
                            j += length;
                        }
                    } break;
                    IllegalDefaultCase;
                    }
                }
                assert(j == literal_length);
            }

            Token* token = reserve_item(&tokens);
            token->atom       = ATOM_STRING_LITERAL;
            token->info_index = ctx->token_info_string.count;

            Token_Info_String* info = reserve_item(&ctx->token_info_string);
            info->source_index = source_index;
            info->offset       = start_cursor - start;
            info->length       = cursor - start_cursor;
            info->value        = literal;

            continue;
        }

        if (c == '`')
        {
            u8* start_cursor = cursor++;

            u32 line_offset = line_offsets.base[line_offsets.count - 1];
            u32 indentation = (start_cursor - start) - line_offset;
            u32 literal_newlines = 0;
            u32 literal_cr_bytes = 0;
            u32 delimiter_length = 1;

            while (true) Loop(backtick_string)
            {
                while (cursor < end)
                {
                    c = *cursor;
                    if (c == '\n') break;
                    cursor++;
                    if (c == '\r')
                    {
                        if (cursor < end && *cursor == '\n')
                        {
                            printf("yep!");
                            literal_cr_bytes++;
                            break;
                        }
                        LexError("Unexpected CR character in string literal.")
                    }
                    if (!literal_newlines && c == '`')
                        BreakLoop(backtick_string);
                }
                if (cursor >= end) LexError("Unexpected EOF in string literal")

                if (!literal_newlines)
                    delimiter_length = cursor - start_cursor - literal_cr_bytes;
                literal_newlines++;

                assert(*cursor == '\n');
                cursor++;
                *reserve_item(&line_offsets) = cursor - start;

                for (u32 i = 0; i < indentation; i++)
                {
                    if (cursor >= end) LexError("Unexpected EOF in string literal")
                    if (*cursor != ' ')
                        LexError("Only space characters are allowed in the multiline string literal indentation.")
                    cursor++;
                }

                if (cursor + delimiter_length <= end &&
                    memcmp(start_cursor + 1, cursor, delimiter_length -1) == 0 &&
                    cursor[delimiter_length - 1] == '`')
                {
                    cursor += delimiter_length;
                    BreakLoop(backtick_string);
                }
            }

            u32 literal_length = cursor - start_cursor;
            literal_length -= 2 * delimiter_length;
            if (literal_newlines)
                literal_length -= literal_newlines * indentation + 2;
            literal_length -= literal_cr_bytes;

            String literal;
            if (literal_newlines == 0)
            {
                assert(literal_newlines == 0);
                assert(literal_cr_bytes == 0);
                assert(delimiter_length == 1);
                literal = { literal_length, start_cursor + 1 };
                assert(literal.data + literal.length + 1 == cursor);
            }
            else
            {
                literal = allocate_uninitialized_string(&ctx->lexer_memory, literal_length);

                byte* read  = start_cursor + delimiter_length;
                byte* write = literal.data;
                for (u32 line = 1; line < literal_newlines; line++)
                {
                    if (line > 1) *(write++) = '\n';
                    if (*read == '\r') read++;
                    assert(*read == '\n');
                    read += indentation + 1;
                    while (*read != '\r' && *read != '\n')
                        *(write++) = *(read++);
                }
                assert(write - literal.data == literal_length);
            }

            if (cursor < end && *cursor == '@')
            {
                cursor++;

                Atom atom = ctx->next_identifier_atom;
                ctx->next_identifier_atom = (Atom)(ctx->next_identifier_atom + 1);
                add_item(&ctx->identifiers, &literal);

                Token* token = reserve_item(&tokens);
                token->atom       = atom;
                token->info_index = ctx->token_info_other.count;

                Token_Info* info = reserve_item(&ctx->token_info_other);
                info->source_index = source_index;
                info->offset       = start_cursor - start;
                info->length       = cursor - start_cursor;
            }
            else
            {
                Token* token = reserve_item(&tokens);
                token->atom       = ATOM_STRING_LITERAL;
                token->info_index = ctx->token_info_string.count;

                Token_Info_String* info = reserve_item(&ctx->token_info_string);
                info->source_index = source_index;
                info->offset       = start_cursor - start;
                info->length       = cursor - start_cursor;
                info->value        = literal;
            }
            continue;
        }

        u8* start_cursor = cursor;

        Atom atom = ATOM_INVALID;
             if (c == '.') cursor++, atom = ATOM_DOT;
        else if (c == ',') cursor++, atom = ATOM_COMMA;
        else if (c == ';') cursor++, atom = ATOM_SEMICOLON;
        else if (c == ':') cursor++, atom = ATOM_COLON;
        else if (c == '(') cursor++, atom = ATOM_LEFT_PARENTHESIS;
        else if (c == ')') cursor++, atom = ATOM_RIGHT_PARENTHESIS;
        else if (c == '[') cursor++, atom = ATOM_LEFT_BRACKET;
        else if (c == ']') cursor++, atom = ATOM_RIGHT_BRACKET;
        else if (c == '{') cursor++, atom = ATOM_LEFT_BRACE;
        else if (c == '}') cursor++, atom = ATOM_RIGHT_BRACE;
        else if (c == '+') cursor++, atom = ATOM_PLUS;
        else if (c == '-') cursor++, atom = ATOM_MINUS;
        else if (c == '*') cursor++, atom = ATOM_STAR;
        else if (c == '/')
        {
            cursor++, atom = ATOM_SLASH;
            if (cursor < end && *cursor == '/')  // single-line comment
            {
                cursor++;
                while (cursor < end && *cursor != '\n') cursor++;
                continue;
            }
            else if (cursor < end && *cursor == '*')  // multi-line comment
            {
                cursor++;

                u32 line        = line_offsets.count;
                u32 line_offset = line_offsets.base[line - 1];

                u32 depth = 1;
                while (cursor + 1 < end)
                {
                    c = *cursor;
                    if (c == '*' && cursor[1] == '/')
                    {
                        cursor += 2;
                        depth--;
                        if (!depth) break;
                    }
                    else if (c == '/' && cursor[1] == '*')
                    {
                        cursor += 2;
                        depth++;
                    }
                    else
                    {
                        cursor++;
                        if (c == '\n')
                            *reserve_item(&line_offsets) = cursor - start;
                    }
                }
                if (!depth)
                    continue;

                u32 column = (start_cursor - start) - line_offset + 1;
                fprintf(stderr, "Multi-line not closed at the end of file.\n"
                                " .. Started at %.*s @ %u:%u\n",
                                StringArgs(name), line, column);
                return false;
            }
        }
        else if (c == '%')
        {
            cursor++, atom = ATOM_PERCENT;
            if (cursor < end && *cursor == '/') cursor++, atom = ATOM_PERCENT_SLASH;
        }
        else if (c == '&')
        {
            cursor++, atom = ATOM_AMPERSAND;
                 if (cursor < end && *cursor == '+') cursor++, atom = ATOM_AMPERSAND_PLUS;
            else if (cursor < end && *cursor == '-') cursor++, atom = ATOM_AMPERSAND_MINUS;
        }
        else if (c == '!')
        {
            cursor++, atom = ATOM_BANG;
                 if (cursor < end && *cursor == '=') cursor++, atom = ATOM_BANG_EQUAL;
            else if (cursor < end && *cursor == '/') cursor++, atom = ATOM_BANG_SLASH;
        }
        else if (c == '=')
        {
            cursor++, atom = ATOM_EQUAL;
                 if (cursor < end && *cursor == '>') cursor++, atom = ATOM_EQUAL_GREATER;
            else if (cursor < end && *cursor == '=') cursor++, atom = ATOM_EQUAL_EQUAL;
        }
        else if (c == '<')
        {
            cursor++, atom = ATOM_LESS;
            if (cursor < end && *cursor == '=') cursor++, atom = ATOM_LESS_EQUAL;
        }
        else if (c == '>')
        {
            cursor++, atom = ATOM_GREATER;
            if (cursor < end && *cursor == '=') cursor++, atom = ATOM_GREATER_EQUAL;
        }
        else if (c == '$') cursor++, atom = ATOM_DOLLAR;
        else if (c == '_')
        {
            cursor++, atom = ATOM_UNDERSCORE;
            if (cursor < end && (CHARACTER_CLASS[*cursor] & CHARACTER_IDENTIFIER_CONTINUATION))
                LexError("The underscore character '_' is not a valid start of an identifier.")
        }
        else
        {
            LexError("Unrecognized token.")
        }

        Token* token = reserve_item(&tokens);
        token->atom       = atom;
        token->info_index = ctx->token_info_other.count;

        Token_Info* info = reserve_item(&ctx->token_info_other);
        info->source_index = source_index;
        info->length       = cursor - start_cursor;
        info->offset       = start_cursor - start;
    }

#undef LexError

    return true;
}

bool lex_file(Compiler* ctx, String path, Array<Token>* out_tokens)
{
    lex_init(ctx);

    String code;
    if (!read_entire_file(path, &code, &ctx->lexer_memory))
    {
        fprintf(stderr, "Failed to read file %.*s\n", StringArgs(path));
        return false;
    }

    return lex_from_memory(ctx, get_file_name(path), code, out_tokens);
}



ExitApplicationNamespace
