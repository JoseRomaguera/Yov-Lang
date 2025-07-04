#include "inc.h"

internal_fn Token lexer_extract_dynamic_token(Lexer* lexer, TokenKind kind, u64 size)
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
        if (string_equals("if", token.value)) kind = TokenKind_IfKeyword;
        else if (string_equals("else", token.value)) kind = TokenKind_ElseKeyword;
        else if (string_equals("while", token.value)) kind = TokenKind_WhileKeyword;
        else if (string_equals("for", token.value)) kind = TokenKind_ForKeyword;
        else if (string_equals("enum", token.value)) kind = TokenKind_EnumKeyword;
        else if (string_equals("struct", token.value)) kind = TokenKind_StructKeyword;
        else if (string_equals("arg", token.value)) kind = TokenKind_ArgKeyword;
        else if (string_equals("return", token.value)) kind = TokenKind_ReturnKeyword;
        else if (string_equals("break", token.value)) kind = TokenKind_BreakKeyword;
        else if (string_equals("continue", token.value)) kind = TokenKind_ContinueKeyword;
        else if (string_equals("import", token.value)) kind = TokenKind_ImportKeyword;
        else if (string_equals("true", token.value)) kind = TokenKind_BoolLiteral;
        else if (string_equals("false", token.value)) kind = TokenKind_BoolLiteral;
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

internal_fn Token lexer_extract_token(Lexer* lexer, TokenKind kind, u32 codepoint_length)
{
    u64 start_cursor = lexer->cursor;
    u64 cursor = lexer->cursor;
    
    u32 index = 0;
    while (index < codepoint_length && cursor < lexer->text.size) {
        index++;
        string_get_codepoint(lexer->text, &cursor);
    }
    
    return lexer_extract_dynamic_token(lexer, kind, cursor - start_cursor);
}

internal_fn Token lexer_extract_token_assignment(Lexer* lexer, BinaryOperator binary_op, u32 codepoint_length)
{
    Token token = lexer_extract_token(lexer, TokenKind_Assignment, codepoint_length);
    token.assignment_binary_operator = binary_op;
    return token;
}

internal_fn Token lexer_extract_next_token(Lexer* lexer)
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
        return lexer_extract_token(lexer, TokenKind_NextLine, 1);
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
        return lexer_extract_dynamic_token(lexer, TokenKind_Separator, cursor - lexer->cursor);
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
        return lexer_extract_dynamic_token(lexer, TokenKind_Comment, cursor - lexer->cursor);
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
        return lexer_extract_dynamic_token(lexer, TokenKind_Comment, cursor - lexer->cursor);
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
        return lexer_extract_dynamic_token(lexer, TokenKind_StringLiteral, cursor - lexer->cursor);
    }
    
    if (c0 == '\'') {
        b32 ignore_next = false;
        u64 cursor = lexer->cursor + 1;
        while (cursor < lexer->text.size) {
            u32 codepoint = string_get_codepoint(lexer->text, &cursor);
            
            if (ignore_next) {
                ignore_next = false;
                continue;
            }
            
            if (codepoint == '\\') ignore_next = true;
            if (codepoint == '\'') break;
        }
        return lexer_extract_dynamic_token(lexer, TokenKind_CodepointLiteral, cursor - lexer->cursor);
    }
    
    if (c0 == '-' && c1 == '>') return lexer_extract_token(lexer, TokenKind_Arrow, 2);
    
    if (c0 == ',') return lexer_extract_token(lexer, TokenKind_Comma, 1);
    if (c0 == '.') return lexer_extract_token(lexer, TokenKind_Dot, 1);
    if (c0 == '{') return lexer_extract_token(lexer, TokenKind_OpenBrace, 1);
    if (c0 == '}') return lexer_extract_token(lexer, TokenKind_CloseBrace, 1);
    if (c0 == '[') return lexer_extract_token(lexer, TokenKind_OpenBracket, 1);
    if (c0 == ']') return lexer_extract_token(lexer, TokenKind_CloseBracket, 1);
    if (c0 == '"') return lexer_extract_token(lexer, TokenKind_OpenString, 1);
    if (c0 == '(') return lexer_extract_token(lexer, TokenKind_OpenParenthesis, 1);
    if (c0 == ')') return lexer_extract_token(lexer, TokenKind_CloseParenthesis, 1);
    if (c0 == ':') return lexer_extract_token(lexer, TokenKind_Colon, 1);
    if (c0 == ';') return lexer_extract_token(lexer, TokenKind_NextSentence, 1);
    if (c0 == '\n') return lexer_extract_token(lexer, TokenKind_NextLine, 1);
    if (c0 == '_') return lexer_extract_token(lexer, TokenKind_Identifier, 1);
    
    if (c0 == '+' && c1 == '=') return lexer_extract_token_assignment(lexer, BinaryOperator_Addition, 2);
    if (c0 == '-' && c1 == '=') return lexer_extract_token_assignment(lexer, BinaryOperator_Substraction, 2);
    if (c0 == '*' && c1 == '=') return lexer_extract_token_assignment(lexer, BinaryOperator_Multiplication, 2);
    if (c0 == '/' && c1 == '=') return lexer_extract_token_assignment(lexer, BinaryOperator_Division, 2);
    if (c0 == '%' && c1 == '=') return lexer_extract_token_assignment(lexer, BinaryOperator_Modulo, 2);
    
    if (c0 == '=' && c1 == '=') return lexer_extract_token(lexer, TokenKind_CompEquals, 2);
    if (c0 == '!' && c1 == '=') return lexer_extract_token(lexer, TokenKind_CompNotEquals, 2);
    if (c0 == '<' && c1 == '=') return lexer_extract_token(lexer, TokenKind_CompLessEquals, 2);
    if (c0 == '>' && c1 == '=') return lexer_extract_token(lexer, TokenKind_CompGreaterEquals, 2);
    
    if (c0 == '|' && c1 == '|') return lexer_extract_token(lexer, TokenKind_LogicalOr, 2);
    if (c0 == '&' && c1 == '&') return lexer_extract_token(lexer, TokenKind_LogicalAnd, 2);
    
    if (c0 == '=') return lexer_extract_token_assignment(lexer, BinaryOperator_None, 1);
    
    if (c0 == '<') return lexer_extract_token(lexer, TokenKind_CompLess, 1);
    if (c0 == '>') return lexer_extract_token(lexer, TokenKind_CompGreater, 1);
    
    if (c0 == '+') return lexer_extract_token(lexer, TokenKind_PlusSign, 1);
    if (c0 == '-') return lexer_extract_token(lexer, TokenKind_MinusSign, 1);
    if (c0 == '*') return lexer_extract_token(lexer, TokenKind_Asterisk, 1);
    if (c0 == '/') return lexer_extract_token(lexer, TokenKind_Slash, 1);
    if (c0 == '%') return lexer_extract_token(lexer, TokenKind_Modulo, 1);
    
    if (c0 == '&') return lexer_extract_token(lexer, TokenKind_Ampersand, 1);
    if (c0 == '!') return lexer_extract_token(lexer, TokenKind_Exclamation, 1);
    
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
        return lexer_extract_dynamic_token(lexer, TokenKind_IntLiteral, cursor - lexer->cursor);
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
        return lexer_extract_dynamic_token(lexer, TokenKind_Identifier, cursor - lexer->cursor);
    }
    
    return lexer_extract_token(lexer, TokenKind_Error, 1);
}

Array<Token> lexer_generate_tokens(Arena* arena, String text, b32 discard_tokens, CodeLocation code)
{
    Lexer* lexer = arena_push_struct<Lexer>(yov->temp_arena);
    lexer->tokens = pooled_array_make<Token>(yov->temp_arena, 1024);
    lexer->text = text;
    lexer->script_id = code.script_id;
    
    lexer->code_line = code.line;
    lexer->code_column = code.column;
    
    while (lexer->cursor < lexer->text.size)
    {
        Token token = lexer_extract_next_token(lexer);
        
        b32 discard = false;
        
        if (discard_tokens)
        {
            if (token.kind == TokenKind_Separator) discard = true;
            if (token.kind == TokenKind_Comment) discard = true;
            if (token.kind == TokenKind_NextLine) discard = true;
        }
        
        if (!discard) array_add(&lexer->tokens, token);
    }
    
    return array_from_pooled_array(arena, lexer->tokens);
}

BinaryOperator binary_operator_from_token(TokenKind token)
{
    if (token == TokenKind_PlusSign) return BinaryOperator_Addition;
    if (token == TokenKind_MinusSign) return BinaryOperator_Substraction;
    if (token == TokenKind_Asterisk) return BinaryOperator_Multiplication;
    if (token == TokenKind_Slash) return BinaryOperator_Division;
    if (token == TokenKind_Modulo) return BinaryOperator_Modulo;
    if (token == TokenKind_Ampersand) return BinaryOperator_None;
    if (token == TokenKind_Exclamation) return BinaryOperator_LogicalNot;
    if (token == TokenKind_LogicalOr) return BinaryOperator_LogicalOr;
    if (token == TokenKind_LogicalAnd) return BinaryOperator_LogicalAnd;
    if (token == TokenKind_CompEquals) return BinaryOperator_Equals;
    if (token == TokenKind_CompNotEquals) return BinaryOperator_NotEquals;
    if (token == TokenKind_CompLess) return BinaryOperator_LessThan;
    if (token == TokenKind_CompLessEquals) return BinaryOperator_LessEqualsThan;
    if (token == TokenKind_CompGreater) return BinaryOperator_GreaterThan;
    if (token == TokenKind_CompGreaterEquals) return BinaryOperator_GreaterEqualsThan;
    return BinaryOperator_None;
};

b32 token_is_sign_or_binary_op(TokenKind token)
{
    TokenKind tokens[] = {
        TokenKind_PlusSign,
        TokenKind_MinusSign,
        TokenKind_Asterisk,
        TokenKind_Slash,
        TokenKind_Modulo,
        TokenKind_Ampersand,
        TokenKind_Exclamation,
        
        TokenKind_LogicalOr,
        TokenKind_LogicalAnd,
        
        TokenKind_CompEquals,
        TokenKind_CompNotEquals,
        TokenKind_CompLess,
        TokenKind_CompLessEquals,
        TokenKind_CompGreater,
        TokenKind_CompGreaterEquals,
    };
    
    foreach(i, array_count(tokens)) {
        if (tokens[i] == token) return true;
    }
    return false;
}