#include "inc.h"

inline_fn b32 codepoint_is_separator(u32 codepoint) {
    if (codepoint == ' ') return true;
    if (codepoint == '\t') return true;
    if (codepoint == '\r') return true;
    return false;
}

inline_fn b32 codepoint_is_number(u32 codepoint) {
    return codepoint >= '0' && codepoint <= '9';
}
inline_fn b32 codepoint_is_text(u32 codepoint) {
    if (codepoint >= 'a' && codepoint <= 'z') return true;
    if (codepoint >= 'A' && codepoint <= 'Z') return true;
    return false;
}

inline_fn Token extract_dynamic_token(Lexer* lexer, TokenKind kind, u64 size)
{
    assert(size > 0);
    
    if (lexer->cursor + size > lexer->text.size) {
        assert(0);
        return {};
    }
    
    Token token{};
    token.value = string_substring(lexer->text, lexer->cursor, size);
    token.code = code_location_make(lexer->cursor, lexer->code_start_line_offset, lexer->code_line, lexer->code_column, lexer->script_id);
    
    if (kind == TokenKind_Identifier) {
        if (string_equals(STR("if"), token.value)) kind = TokenKind_IfKeyword;
        else if (string_equals(STR("else"), token.value)) kind = TokenKind_ElseKeyword;
        else if (string_equals(STR("while"), token.value)) kind = TokenKind_WhileKeyword;
        else if (string_equals(STR("for"), token.value)) kind = TokenKind_ForKeyword;
        else if (string_equals(STR("enum"), token.value)) kind = TokenKind_EnumKeyword;
        else if (string_equals(STR("true"), token.value)) kind = TokenKind_BoolLiteral;
        else if (string_equals(STR("false"), token.value)) kind = TokenKind_BoolLiteral;
    }
    
    token.kind = kind;
    
    // Discard double quotes
    if (token.kind == TokenKind_StringLiteral) {
        assert(token.value.size >= 2);
        token.value = string_substring(token.value, 1, token.value.size - 2);
    }
    
    foreach(i, size) {
        char c = lexer->text[lexer->cursor + i];
        if (c == '\n') {
            lexer->code_line++;
            lexer->code_start_line_offset = lexer->cursor + i + 1;
            lexer->code_column = 0;
        }
        else {
            lexer->code_column++;
        }
    }
    
    lexer->cursor += size;
    
    return token;
}

inline_fn Token extract_token(Lexer* lexer, TokenKind kind, u32 codepoint_length)
{
    u64 start_cursor = lexer->cursor;
    u64 cursor = lexer->cursor;
    
    u32 index = 0;
    while (index < codepoint_length && cursor < lexer->text.size) {
        index++;
        string_get_codepoint(lexer->text, &cursor);
    }
    
    return extract_dynamic_token(lexer, kind, cursor - start_cursor);
}

inline_fn Token extract_token_with_binary_op(Lexer* lexer, TokenKind kind, BinaryOperator binary_op, u32 codepoint_length)
{
    Token token = extract_token(lexer, kind, codepoint_length);
    token.binary_operator = binary_op;
    return token;
}

inline_fn Token extract_next_token(Lexer* lexer)
{
    SCRATCH();
    
    Array<u32> codepoints = array_make<u32>(scratch.arena, 5);
    
    // Fill codepoint pool
    {
        u32 index = 0;
        u64 cursor = lexer->cursor;
        while (index < codepoints.count && cursor < lexer->text.size)
            codepoints[index++] = string_get_codepoint(lexer->text, &cursor);
    }
    
    u32 c0 = codepoints[0];
    u32 c1 = codepoints[1];
    u32 c2 = codepoints[2];
    u32 c3 = codepoints[3];
    u32 c4 = codepoints[4];
    
    if (c0 == 0) {
        return extract_token(lexer, TokenKind_NextLine, 1);
    }
    
    if (codepoint_is_separator(c0))
    {
        u64 cursor = lexer->cursor;
        while (cursor < lexer->text.size) {
            u64 next_cursor = cursor;
            u32 codepoint = string_get_codepoint(lexer->text, &next_cursor);
            if (!codepoint_is_separator(codepoint)) {
                break;
            }
            cursor = next_cursor;
        }
        return extract_dynamic_token(lexer, TokenKind_Separator, cursor - lexer->cursor);
    }
    
    if (c0 == '/' && c1 == '/')
    {
        u64 cursor = lexer->cursor;
        while (cursor < lexer->text.size) {
            u64 next_cursor = cursor;
            u32 codepoint = string_get_codepoint(lexer->text, &next_cursor);
            if (codepoint == '\n') break;
            cursor = next_cursor;
        }
        return extract_dynamic_token(lexer, TokenKind_Comment, cursor - lexer->cursor);
    }
    
    if (c0 == '/' && c1 == '*')
    {
        u64 cursor = lexer->cursor;
        u64 last_codepoint = 0;
        i32 depth = 0;
        while (cursor < lexer->text.size) 
        {
            u32 codepoint = string_get_codepoint(lexer->text, &cursor);
            if (last_codepoint == '*' && codepoint == '/') {
                depth--;
                if (depth == 0) {
                    break;
                }
            }
            if (last_codepoint == '/' && codepoint == '*') {
                depth++;
            }
            last_codepoint = codepoint;
        }
        return extract_dynamic_token(lexer, TokenKind_Comment, cursor - lexer->cursor);
    }
    
    if (c0 == '"') {
        b32 ignore_next = false;
        u64 cursor = lexer->cursor + 1;
        while (cursor < lexer->text.size) {
            u32 codepoint = string_get_codepoint(lexer->text, &cursor);
            
            if (ignore_next) {
                ignore_next = false;
                continue;
            }
            
            if (codepoint == '\\') ignore_next = true;
            if (codepoint == '"') break;
        }
        return extract_dynamic_token(lexer, TokenKind_StringLiteral, cursor - lexer->cursor);
    }
    
    if (c0 == ',') return extract_token(lexer, TokenKind_Comma, 1);
    if (c0 == '.') return extract_token(lexer, TokenKind_Dot, 1);
    if (c0 == '{') return extract_token(lexer, TokenKind_OpenBrace, 1);
    if (c0 == '}') return extract_token(lexer, TokenKind_CloseBrace, 1);
    if (c0 == '[') return extract_token(lexer, TokenKind_OpenBracket, 1);
    if (c0 == ']') return extract_token(lexer, TokenKind_CloseBracket, 1);
    if (c0 == '"') return extract_token(lexer, TokenKind_OpenString, 1);
    if (c0 == '(') return extract_token(lexer, TokenKind_OpenParenthesis, 1);
    if (c0 == ')') return extract_token(lexer, TokenKind_CloseParenthesis, 1);
    if (c0 == ':') return extract_token(lexer, TokenKind_Colon, 1);
    if (c0 == ';') return extract_token(lexer, TokenKind_NextSentence, 1);
    if (c0 == '\n') return extract_token(lexer, TokenKind_NextLine, 1);
    if (c0 == '_') return extract_token(lexer, TokenKind_Identifier, 1);
    
    if (c0 == '+' && c1 == '=') return extract_token_with_binary_op(lexer, TokenKind_Assignment, BinaryOperator_Addition, 2);
    if (c0 == '-' && c1 == '=') return extract_token_with_binary_op(lexer, TokenKind_Assignment, BinaryOperator_Substraction, 2);
    if (c0 == '*' && c1 == '=') return extract_token_with_binary_op(lexer, TokenKind_Assignment, BinaryOperator_Multiplication, 2);
    if (c0 == '/' && c1 == '=') return extract_token_with_binary_op(lexer, TokenKind_Assignment, BinaryOperator_Division, 2);
    if (c0 == '%' && c1 == '=') return extract_token_with_binary_op(lexer, TokenKind_Assignment, BinaryOperator_Modulo, 2);
    
    if (c0 == '=' && c1 == '=') return extract_token_with_binary_op(lexer, TokenKind_BinaryOperator, BinaryOperator_Equals, 2);
    if (c0 == '!' && c1 == '=') return extract_token_with_binary_op(lexer, TokenKind_BinaryOperator, BinaryOperator_NotEquals, 2);
    if (c0 == '<' && c1 == '=') return extract_token_with_binary_op(lexer, TokenKind_BinaryOperator, BinaryOperator_LessEqualsThan, 2);
    if (c0 == '>' && c1 == '=') return extract_token_with_binary_op(lexer, TokenKind_BinaryOperator, BinaryOperator_GreaterEqualsThan, 2);
    
    if (c0 == '|' && c1 == '|') return extract_token_with_binary_op(lexer, TokenKind_BinaryOperator, BinaryOperator_LogicalOr, 2);
    if (c0 == '&' && c1 == '&') return extract_token_with_binary_op(lexer, TokenKind_BinaryOperator, BinaryOperator_LogicalAnd, 2);
    
    if (c0 == '=') return extract_token_with_binary_op(lexer, TokenKind_Assignment, BinaryOperator_None, 1);
    
    if (c0 == '<') return extract_token_with_binary_op(lexer, TokenKind_BinaryOperator, BinaryOperator_LessThan, 1);
    if (c0 == '>') return extract_token_with_binary_op(lexer, TokenKind_BinaryOperator, BinaryOperator_GreaterThan, 1);
    
    if (c0 == '+') return extract_token_with_binary_op(lexer, TokenKind_BinaryOperator, BinaryOperator_Addition, 1);
    if (c0 == '-') return extract_token_with_binary_op(lexer, TokenKind_BinaryOperator, BinaryOperator_Substraction, 1);
    if (c0 == '*') return extract_token_with_binary_op(lexer, TokenKind_BinaryOperator, BinaryOperator_Multiplication, 1);
    if (c0 == '/') return extract_token_with_binary_op(lexer, TokenKind_BinaryOperator, BinaryOperator_Division, 1);
    if (c0 == '%') return extract_token_with_binary_op(lexer, TokenKind_BinaryOperator, BinaryOperator_Modulo, 1);
    
    if (c0 == '!') return extract_token_with_binary_op(lexer, TokenKind_BinaryOperator, BinaryOperator_LogicalNot, 1);
    
    if (codepoint_is_number(c0))
    {
        u64 cursor = lexer->cursor;
        while (cursor < lexer->text.size) {
            u64 next_cursor = cursor;
            u32 codepoint = string_get_codepoint(lexer->text, &next_cursor);
            if (!codepoint_is_number(codepoint)) {
                break;
            }
            cursor = next_cursor;
        }
        return extract_dynamic_token(lexer, TokenKind_IntLiteral, cursor - lexer->cursor);
    }
    
    if (codepoint_is_text(c0))
    {
        u64 cursor = lexer->cursor;
        while (cursor < lexer->text.size) {
            u64 next_cursor = cursor;
            u32 codepoint = string_get_codepoint(lexer->text, &next_cursor);
            if (!codepoint_is_text(codepoint) && !codepoint_is_number(codepoint) && codepoint != '_') {
                break;
            }
            cursor = next_cursor;
        }
        return extract_dynamic_token(lexer, TokenKind_Identifier, cursor - lexer->cursor);
    }
    
    return extract_token(lexer, TokenKind_Error, 1);
}

Array<Token> generate_tokens(Yov* ctx, String text, b32 discard_tokens)
{
    Lexer* lexer = arena_push_struct<Lexer>(ctx->temp_arena);
    lexer->ctx = ctx;
    lexer->tokens = pooled_array_make<Token>(lexer->ctx->temp_arena, 1024);
    lexer->text = text;
    lexer->script_id = 0;
    
    lexer->code_line = 1;
    lexer->code_column = 0;
    
    while (lexer->cursor < lexer->text.size)
    {
        Token token = extract_next_token(lexer);
        
        b32 discard = false;
        
        if (discard_tokens)
        {
            if (token.kind == TokenKind_Separator) discard = true;
            if (token.kind == TokenKind_Comment) discard = true;
            if (token.kind == TokenKind_NextLine) discard = true;
        }
        
        if (!discard) array_add(&lexer->tokens, token);
    }
    
    return array_from_pooled_array(lexer->ctx->static_arena, lexer->tokens);
}