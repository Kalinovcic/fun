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
    set_capacity(&ctx->token_info_other,   Megabyte(256) / sizeof(ctx->token_info_other  [0]));
    set_capacity(&ctx->token_info_integer, Megabyte(64)  / sizeof(ctx->token_info_integer[0]));
    set_capacity(&ctx->identifiers,        Megabyte(32)  / sizeof(ctx->identifiers       [0]));

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
    AddKeyword(ATOM_S8,           "s8"_s);
    AddKeyword(ATOM_S16,          "s16"_s);
    AddKeyword(ATOM_S32,          "s32"_s);
    AddKeyword(ATOM_S64,          "s64"_s);
    AddKeyword(ATOM_F16,          "f16"_s);
    AddKeyword(ATOM_F32,          "f32"_s);
    AddKeyword(ATOM_F64,          "f64"_s);
    AddKeyword(ATOM_BOOL8,        "bool8"_s);
    AddKeyword(ATOM_BOOL16,       "bool16"_s);
    AddKeyword(ATOM_BOOL32,       "bool32"_s);
    AddKeyword(ATOM_BOOL64,       "bool64"_s);
    AddKeyword(ATOM_STRUCT,       "struct"_s);
    AddKeyword(ATOM_TYPE,         "type"_s);
    AddKeyword(ATOM_BLOCK,        "block"_s);
    AddKeyword(ATOM_CODE_BLOCK,   "code_block"_s);
    AddKeyword(ATOM_GLOBAL,       "global"_s);
    AddKeyword(ATOM_THREAD_LOCAL, "thread_local"_s);
    AddKeyword(ATOM_UNIT_LOCAL,   "unit_local"_s);
    AddKeyword(ATOM_UNIT_DATA,    "unit_data"_s);
    AddKeyword(ATOM_UNIT_CODE,    "unit_code"_s);
    AddKeyword(ATOM_LABEL,        "label"_s);
    AddKeyword(ATOM_GOTO,         "goto"_s);
    AddKeyword(ATOM_DEBUG,        "debug"_s);
    AddKeyword(ATOM_IF,           "if"_s);
    AddKeyword(ATOM_ELSE,         "else"_s);
    AddKeyword(ATOM_WHILE,        "while"_s);
    AddKeyword(ATOM_DO,           "do"_s);
    AddKeyword(ATOM_RUN,          "run"_s);
    AddKeyword(ATOM_RETURN,       "return"_s);
    AddKeyword(ATOM_YIELD,        "yield"_s);
    AddKeyword(ATOM_DEFER,        "defer"_s);
    AddKeyword(ATOM_CAST,         "cast"_s);
    AddKeyword(ATOM_UNDERSCORE,   "_"_s);
#undef AddKeyword
}

bool lex_from_memory(Compiler* ctx, String name, String code, Array<Token>* out_tokens)
{
    lex_init(ctx);

    static const constexpr u8 CHARACTER_IDENTIFIER_START        =  1;
    static const constexpr u8 CHARACTER_IDENTIFIER_CONTINUATION =  2;
    static const constexpr u8 CHARACTER_DIGIT_BASE2             =  4;
    static const constexpr u8 CHARACTER_DIGIT_BASE10            =  8;
    static const constexpr u8 CHARACTER_DIGIT_BASE16            = 16;
    static const constexpr u8 CHARACTER_WHITE                   = 32;
    static const constexpr u8 CHARACTER_CLASS[256] =
    {
         0, 0, 0, 0, 0, 0, 0, 0, 0,32,32, 0, 0,32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,14,14,10,10,10,10,10,10,10,10, 0, 0, 0, 0, 0, 0,
         0,19,19,19,19,19,19, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 3,
         0,19,19,19,19,19,19, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };

    /*
    for (umm i = 0; i < 256; i++)
    {
        int x = 0;
        if (i >= 'a' && i <= 'z') x |= CHARACTER_IDENTIFIER_START | CHARACTER_IDENTIFIER_CONTINUATION;
        if (i >= 'A' && i <= 'Z') x |= CHARACTER_IDENTIFIER_START | CHARACTER_IDENTIFIER_CONTINUATION;
        if (i == '_') x |= CHARACTER_IDENTIFIER_START | CHARACTER_IDENTIFIER_CONTINUATION;
        if (i >= '0' && i <= '9') x |= CHARACTER_DIGIT_BASE10 | CHARACTER_IDENTIFIER_CONTINUATION;
        if (i >= '0' && i <= '1') x |= CHARACTER_DIGIT_BASE2;
        if (i >= 'a' && i <= 'f') x |= CHARACTER_DIGIT_BASE16;
        if (i >= 'A' && i <= 'F') x |= CHARACTER_DIGIT_BASE16;
        if (i == ' ' || i == '\t' || i == '\r' || i == '\n') x |= CHARACTER_WHITE;
        printf("%2d,", x);
        if (i % 32 == 31) printf("\n");
    }
    */


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

    Integer integer_ten = {};
    Integer integer_digit = {};
    Defer(int_free(&integer_ten));
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

        if (cc & CHARACTER_WHITE)
        {
            cursor++;
            continue;
        }

        if (cc & CHARACTER_DIGIT_BASE10)
        {
            Token* token = reserve_item(&tokens);
            token->atom       = ATOM_INTEGER;
            token->info_index = ctx->token_info_integer.count;

            Token_Info_Integer* info = reserve_item(&ctx->token_info_integer);
            info->source_index = source_index;
            info->offset       = cursor - start;

            Integer value = {};
            int_set_zero(&value);
            while (cursor < end)
            {
                c  = *cursor;
                cc = CHARACTER_CLASS[c];
                if (cc & CHARACTER_DIGIT_BASE10)
                {
                    int_set16(&integer_digit, c - '0');
                    int_mul(&value, &integer_ten);
                    int_add(&value, &integer_digit);
                }
                else if (c != '_')
                    break;
                cursor++;
            }

            if (cursor < end && (*cursor == 'e' || *cursor == 'E'))
            {
                cursor++;

                Integer exponent = {};
                Defer(int_free(&exponent));
                bool found_at_least_one_digit = false;
                while (cursor < end)
                {
                    c  = *cursor;
                    cc = CHARACTER_CLASS[c];
                    if (cc & CHARACTER_DIGIT_BASE10)
                    {
                        found_at_least_one_digit = true;

                        int_set16(&integer_digit, c - '0');
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
                int_pow(&multiplier, &integer_ten, &exponent);
                int_mul(&value, &multiplier);
                int_free(&multiplier);
            }

            info->length = (cursor - start) - info->offset;
            info->value = value;
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

        u8* start_cursor = cursor;

        Atom atom = ATOM_INVALID;
             if (c == ',') cursor++, atom = ATOM_COMMA;
        else if (c == ';') cursor++, atom = ATOM_SEMICOLON;
        else if (c == ':') cursor++, atom = ATOM_COLON;
        else if (c == '`') cursor++, atom = ATOM_BACKTICK;
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
        else if (c == '%') cursor++, atom = ATOM_PERCENT;
        else if (c == '&') cursor++, atom = ATOM_AMPERSAND;
        else if (c == '!')
        {
            cursor++, atom = ATOM_BANG;
            if (cursor < end && *cursor == '=') cursor++, atom = ATOM_BANG_EQUAL;
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
        return false;

    return lex_from_memory(ctx, get_file_name(path), code, out_tokens);
}

void get_line(Compiler* ctx, Token_Info const* info, u32* out_line, u32* out_column, String* out_source_name)
{
    Source_Info* source = &ctx->sources[info->source_index];
    Array<u32> line_offsets = source->line_offsets;
    u32 offset = info->offset;

    u32 line_low  = 1;  // at the end, this is the first line *after* the offset, so can't be 0
    u32 line_high = line_offsets.count;
    while (line_low < line_high)
    {
        u32 line = line_low + ((line_high - line_low) >> 1);
        if (line_offsets.address[line] <= offset)
            line_low = line + 1;
        else
            line_high = line;
    }
    u32 line = line_low - 1;  // get the line *before* the offset
    if (out_line)        *out_line   = line + 1;                         // +1 because 1-indexed
    if (out_column)      *out_column = offset - line_offsets[line] + 1;  // +1 because 1-indexed
    if (out_source_name) *out_source_name = source->name;
}

void get_source_code_slice(Compiler* ctx, Token_Info const* info,
                           umm extra_lines_before, umm extra_lines_after,
                           String* out_source, u32* out_source_offset, u32* out_source_line)
{
    Source_Info* source = &ctx->sources[info->source_index];
    Array<u32> line_offsets = source->line_offsets;

    u32 line;
    get_line(ctx, info, &line);
    line--;  // because 1-indexed
    u32 source_start_line = (line < extra_lines_before) ? 0 : (line - extra_lines_before);
    u32 source_end_line   = line + extra_lines_after + 1;
    if (source_end_line > line_offsets.count)
        source_end_line = line_offsets.count;
    while (source_end_line < line_offsets.count && line_offsets[source_end_line] < (info->offset + info->length))
        source_end_line++;

    assert(source_start_line < line_offsets.count);
    assert(source_start_line < source_end_line);
    u32 source_start_offset = line_offsets[source_start_line];
    u32 source_end_offset   = source_end_line < line_offsets.count ? line_offsets[source_end_line] : source->code.length;

    if (out_source) *out_source = substring(source->code, source_start_offset, source_end_offset - source_start_offset);
    if (out_source_offset) *out_source_offset = source_start_offset;
    if (out_source_line)   *out_source_line   = source_start_line + 1;  // +1 because 1-indexed
}

Token_Info dummy_token_info_for_expression(Compiler* ctx, Parsed_Expression const* expr)
{
    Token_Info* from_info = get_token_info(ctx, &expr->from);
    Token_Info* to_info   = get_token_info(ctx, &expr->to);

    Token_Info result = *from_info;
    u32 length = to_info->offset + to_info->length - from_info->offset;
    if (length > U16_MAX) length = U16_MAX;
    result.length = length;
    return result;
}




static bool enable_color_output();

String location_report_part(Compiler* ctx, Token_Info const* info, umm lines_before, umm lines_after)
{
    bool supports_color = enable_color_output();
    String lowlight  = supports_color ? "\x1b[30;1m"_s : ""_s;
    String highlight = supports_color ? "\x1b[31;1m"_s : ""_s;
    String reset     = supports_color ? "\x1b[m"_s     : ""_s;

    String source;
    u32 source_offset, source_line;
    get_source_code_slice(ctx, info, lines_before, lines_after, &source, &source_offset, &source_line);

    u32 last_line = source_line;
    for (umm i = 0; i < source.length; i++)
        if (source[i] == '\n')
            last_line++;

    umm digits = digits_base10_u64(last_line);
    if (digits < 4)
        digits = 4;

    if (supports_color)
    {
        smm amount_before = (smm)(info->offset - source_offset);
        if (amount_before < 0)             amount_before = 0;
        if (amount_before > source.length) amount_before = source.length;
        String before = take(&source, amount_before);
        umm amount_highlit = info->length;
        if (amount_highlit > source.length) amount_highlit = source.length;
        String highlit = take(&source, amount_highlit);
        String after = source;

        source = concatenate(temp, before, highlight, highlit, reset, after);
    }

    String_Concatenator cat = {};
    while (source)
        FormatAdd(&cat, "%~% >% %\n", lowlight, s64_format(source_line++, digits),
            reset, consume_line_preserve_whitespace(&source));
    return resolve_to_string_and_free(&cat, temp);
}

bool report_error_locationless(Compiler* ctx, String message)
{
    bool supports_color = enable_color_output();
    String lowlight  = supports_color ? "\x1b[30;1m"_s : ""_s;
    String highlight = supports_color ? "\x1b[31;1m"_s : ""_s;
    String reset     = supports_color ? "\x1b[m"_s     : ""_s;

    String indented_newline = Format(temp, "\n% ..% ", lowlight, reset);
    String output = Format(temp, "\n"
                                 "%[error]%\n"
                                 "% ..% %\n",
        highlight, reset,
        lowlight, reset, replace_all_occurances(message, "\n"_s, indented_newline, temp));

    fprintf(stderr, "%.*s", StringArgs(output));
    return false;
}

bool report_error(Compiler* ctx, Token const* at, String message)
{
    bool supports_color = enable_color_output();
    String lowlight  = supports_color ? "\x1b[30;1m"_s : ""_s;
    String highlight = supports_color ? "\x1b[31;1m"_s : ""_s;
    String reset     = supports_color ? "\x1b[m"_s     : ""_s;

    String file;
    u32 line, column;
    Token_Info* info = get_token_info(ctx, at);
    get_line(ctx, info, &line, &column, &file);

    String indented_newline = Format(temp, "\n% ..% ", lowlight, reset);
    String output = Format(temp, "\n"
                                 "%[error]% %~% (%:%)%\n"
                                 "% ..% %\n"
                                 "\n"
                                 "%",
        highlight, reset, file, lowlight, line, column, reset,
        lowlight, reset, replace_all_occurances(message, "\n"_s, indented_newline, temp),
        location_report_part(ctx, info));

    fprintf(stderr, "%.*s", StringArgs(output));
    return false;
}

bool report_error(Compiler* ctx, Token const* at1, String message1, Token const* at2, String message2)
{
    bool supports_color = enable_color_output();
    String lowlight  = supports_color ? "\x1b[30;1m"_s : ""_s;
    String highlight = supports_color ? "\x1b[31;1m"_s : ""_s;
    String reset     = supports_color ? "\x1b[m"_s     : ""_s;

    String file1;
    u32 line1, column1;
    Token_Info* info1 = get_token_info(ctx, at1);
    get_line(ctx, info1, &line1, &column1, &file1);
    message1 = replace_all_occurances(message1, "\n"_s, Format(temp, "\n% ..% ", lowlight, reset), temp);

    String file2;
    u32 line2, column2;
    Token_Info* info2 = get_token_info(ctx, at2);
    get_line(ctx, info2, &line2, &column2, &file2);
    message2 = replace_all_occurances(message2, "\n"_s, Format(temp, "\n% ..% ", lowlight, reset), temp);

    String output = Format(temp, "\n"
                                 "%[error]% %~% (%:%)%\n"
                                 "% ..% %\n"
                                 "\n"
                                 "%\n"
                                 "% ..% %~% (%:%)%\n"
                                 "% ..% %\n"
                                 "\n"
                                 "%",
        highlight, reset, file1, lowlight, line1, column1, reset,
        lowlight, reset, message1,
        location_report_part(ctx, info1),

        lowlight, reset, file2, lowlight, line2, column2, reset,
        lowlight, reset, message2,
        location_report_part(ctx, info2));

    fprintf(stderr, "%.*s", StringArgs(output));
    return false;
}

bool report_error(Compiler* ctx, Parsed_Expression const* at, String message)
{
    bool supports_color = enable_color_output();
    String lowlight  = supports_color ? "\x1b[30;1m"_s : ""_s;
    String highlight = supports_color ? "\x1b[31;1m"_s : ""_s;
    String reset     = supports_color ? "\x1b[m"_s     : ""_s;

    Token_Info info = dummy_token_info_for_expression(ctx, at);

    String file;
    u32 line, column;
    get_line(ctx, &info, &line, &column, &file);

    String indented_newline = Format(temp, "\n% ..% ", lowlight, reset);
    String output = Format(temp, "\n"
                                 "%[error]% %~% (%:%)%\n"
                                 "% ..% %\n"
                                 "\n"
                                 "%",
        highlight, reset, file, lowlight, line, column, reset,
        lowlight, reset, replace_all_occurances(message, "\n"_s, indented_newline, temp),
        location_report_part(ctx, &info));

    fprintf(stderr, "%.*s", StringArgs(output));
    return false;
}


#if defined OS_WINDOWS

ExitApplicationNamespace

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

EnterApplicationNamespace


static bool enable_color_output()
{
    OnlyOnce
    {
        HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode;
        GetConsoleMode(handle, &mode);
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        mode |= DISABLE_NEWLINE_AUTO_RETURN;
        SetConsoleMode(handle, mode);
    };
    // @Incomplete - check color support
    return true;
}

#else

static bool enable_color_output()
{
    // @Incomplete - check color support
    return true;
}

#endif



ExitApplicationNamespace
