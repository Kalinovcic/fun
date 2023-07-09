#include "../src_common/common.h"
#include "../src_common/hash.h"
#include "../src_common/integer.h"
#include "api.h"
#include <stdio.h>


EnterApplicationNamespace



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

static void get_source_code_slice(Compiler* ctx, Token_Info const* info,
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

static String format_line_numbers(bool colored, String source, u32 source_line, String file = {})
{
    String lowlight = colored ? "\x1b[30;1m"_s : ""_s;
    String reset    = colored ? "\x1b[m"_s     : ""_s;

    bool skinny = file;

    u32 last_line = source_line;
    for (umm i = 0; i < source.length; i++)
        if (source[i] == '\n')
            last_line++;
    umm digits = digits_base10_u64(last_line);
    if (digits < 4 && !skinny)
        digits = 4;

    String_Concatenator cat = {};
    while (source)
    {
        FormatAdd(&cat, "%~%~%~% >% %\n",
            lowlight,
            string_format(file, file.length),
            skinny ? (file ? ":"_s : " "_s) : ""_s,
            s64_format(source_line++, digits),
            reset, consume_line_preserve_whitespace(&source));
        file = {};
    }
    return resolve_to_string_and_free(&cat, temp);
}

static void split_source_around_token_info(Token_Info const* info, u32 source_offset, String source,
                                           String* out_before, String* out_inside, String* out_after)
{
    smm amount_before = (smm)(info->offset - source_offset);
    if (amount_before < 0)             amount_before = 0;
    if (amount_before > source.length) amount_before = source.length;
    *out_before = take(&source, amount_before);
    umm amount_highlit = info->length;
    if (amount_highlit > source.length) amount_highlit = source.length;
    *out_inside = take(&source, amount_highlit);
    *out_after = source;
}

static String get_severity_label(bool colored, Severity severity)
{
    if (severity == SEVERITY_ERROR)   return colored ? "\x1b[31;1m[error]\x1b[m"_s   : "[error]"_s;
    if (severity == SEVERITY_WARNING) return colored ? "\x1b[33;1m[warning]\x1b[m"_s : "[warning]"_s;
    return "??????????"_s;
}

Report::Report(Compiler* ctx)
: cat({}),
  ctx(ctx),
  colored(supports_colored_output()),
  indentation(colored ? "\x1b[30;1m .. \x1b[m"_s : " .. "_s),
  first_part(true)
{}

Report::~Report()
{
    String report = resolve_to_string_and_free(&cat, temp);
    if (report)
    {
        fprintf(stderr, "Internal error: This error is reported late:");
        fprintf(stderr, "%.*s", StringArgs(report));
    }
}

Report& Report::intro(Severity severity)
{
    FormatAdd(&cat, "\n%\n", get_severity_label(colored, severity));
    return *this;
}

Report& Report::internal_intro(Severity severity, Token_Info info)
{
    String lowlight = colored ? "\x1b[30;1m"_s : ""_s;
    String reset    = colored ? "\x1b[m"_s     : ""_s;

    String file;
    u32 line, column;
    get_line(ctx, &info, &line, &column, &file);
    FormatAdd(&cat, "\n% % %(%:%)%\n", get_severity_label(colored, severity), file, lowlight, line, column, reset);
    return *this;
}

Report& Report::continuation()
{
    FormatAdd(&cat, "\n");
    return *this;
}

Report& Report::internal_continuation(Token_Info info, bool skinny)
{
    String lowlight = colored ? "\x1b[30;1m"_s : ""_s;
    String reset    = colored ? "\x1b[m"_s     : ""_s;

    String file;
    u32 line, column;
    get_line(ctx, &info, &line, &column, &file);
    FormatAdd(&cat, "%~%~% %(%:%)%\n", skinny ? ""_s : "\n"_s, indentation, file, lowlight, line, column, reset);
    return *this;
}

Report& Report::message(String message)
{
    String indented_newline = concatenate(temp, "\n"_s, indentation);
    while (message)
    {
        String line = consume_line_preserve_whitespace(&message);
        FormatAdd(&cat, "%~%\n", indentation, line);
    }
    return *this;
}

Report& Report::internal_snippet(Token_Info info, bool skinny, umm before, umm after)
{
    if (skinny) before = after = 0;
    String source;
    u32 source_offset, source_line;
    get_source_code_slice(ctx, &info, before, after, &source, &source_offset, &source_line);

    if (colored)
    {
        String code_before, code_inside, code_after;
        split_source_around_token_info(&info, source_offset, source, &code_before, &code_inside, &code_after);

        String highlight = "\x1b[31;1m"_s;
        String reset     = "\x1b[m"_s;
        source = concatenate(temp, code_before, highlight,
                             replace_all_occurances(code_inside, "\n"_s, concatenate(temp, "\n"_s, highlight), temp),
                             reset, code_after);
    }

    String file = skinny ? get_file_name(ctx->sources[info.source_index].name) : ""_s;
    FormatAdd(&cat, "%~%", skinny ? ""_s : "\n"_s, format_line_numbers(colored, source, source_line, file));
    return *this;
}

static String highlight(Region* region, String string)
{
    auto split_whitespace = [](String *s, String* out_leading, String* out_trailing)
    {
        String trimmed_back = trim_back(*s);
        *out_trailing = { s->length - trimmed_back.length, s->data + trimmed_back.length };
        *s = trimmed_back;
        String trimmed_front = trim_front(trimmed_back);
        *out_leading  = { s->length - trimmed_front.length, s->data };
        *s = trimmed_front;
    };

    String leading_ws, trailing_ws;
    split_whitespace(&string,  &leading_ws, &trailing_ws);

    String highlight = "\x1b[32;4;3;1m"_s;
    String reset     = "\x1b[m"_s;
    return concatenate(region, leading_ws,  highlight, string,  reset, trailing_ws);
}

Report& Report::internal_suggestion_insert(String left, Token_Info info, String right, bool skinny, umm before, umm after)
{
    if (skinny) before = after = 0;
    String source;
    u32 source_offset, source_line;
    get_source_code_slice(ctx, &info, before, after, &source, &source_offset, &source_line);

    if (colored)
    {
        left  = highlight(temp, left);
        right = highlight(temp, right);
    }

    String code_before, code_inside, code_after;
    split_source_around_token_info(&info, source_offset, source, &code_before, &code_inside, &code_after);
    source = concatenate(temp, code_before, left, code_inside, right, code_after);

    String file = skinny ? get_file_name(ctx->sources[info.source_index].name) : ""_s;
    FormatAdd(&cat, "%~%", skinny ? ""_s : "\n"_s, format_line_numbers(colored, source, source_line, file));
    return *this;
}

static String string_from_token_info(Compiler* ctx, Token_Info info)
{
    String source;
    u32 source_offset, source_line;
    get_source_code_slice(ctx, &info, 0, 0, &source, &source_offset, &source_line);

    String code_before, code_inside, code_after;
    split_source_around_token_info(&info, source_offset, source, &code_before, &code_inside, &code_after);

    return code_inside;
}

Report& Report::internal_suggestion_replace(Token_Info info, Token_Info replace_with, bool skinny, umm before, umm after)
{
    String replacement = string_from_token_info(ctx, replace_with);
    if (colored)
        replacement = highlight(temp, replacement);

    if (skinny) before = after = 0;
    String source;
    u32 source_offset, source_line;
    get_source_code_slice(ctx, &info, before, after, &source, &source_offset, &source_line);

    String code_before, code_inside, code_after;
    split_source_around_token_info(&info, source_offset, source, &code_before, &code_inside, &code_after);
    source = concatenate(temp, code_before, replacement, code_after);

    String file = skinny ? get_file_name(ctx->sources[info.source_index].name) : ""_s;
    FormatAdd(&cat, "%~%", skinny ? ""_s : "\n"_s, format_line_numbers(colored, source, source_line, file));
    return *this;
}


bool Report::done()
{
    String report = resolve_to_string_and_free(&cat, temp);
    fprintf(stderr, "%.*s", StringArgs(report));
    return false;
}




#if defined OS_WINDOWS

ExitApplicationNamespace

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

EnterApplicationNamespace


bool supports_colored_output()
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

bool supports_colored_output()
{
    // @Incomplete - check color support
    return true;
}

#endif




ExitApplicationNamespace
