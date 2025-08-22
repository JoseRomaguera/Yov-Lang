#include "inc.h"

internal_fn OpKind op_kind_from_tokens(Parser* parser)
{
    u32 end_of_sentence_index = 0;
    u32 assignment_count = 0;
    u32 colon_count = 0;
    
    while (true) {
        Token t0 = peek_token(parser, end_of_sentence_index);
        
        if (t0.kind == TokenKind_Assignment) assignment_count++;
        if (t0.kind == TokenKind_Colon) colon_count++;
        
        if (t0.kind == TokenKind_NextSentence) break;
        if (t0.kind == TokenKind_None) break;
        end_of_sentence_index++;
    }
    
    Token t0 = peek_token(parser, 0);
    Token t1 = peek_token(parser, 1);
    Token t2 = peek_token(parser, 2);
    Token t3 = peek_token(parser, 3);
    Token t4 = peek_token(parser, 4);
    
    if (t0.kind == TokenKind_IfKeyword) return OpKind_IfStatement;
    if (t0.kind == TokenKind_WhileKeyword) return OpKind_WhileStatement;
    if (t0.kind == TokenKind_ForKeyword) return OpKind_ForStatement;
    if (t0.kind == TokenKind_ReturnKeyword) return OpKind_Return;
    if (t0.kind == TokenKind_ContinueKeyword) return OpKind_Continue;
    if (t0.kind == TokenKind_BreakKeyword) return OpKind_Break;
    if (t0.kind == TokenKind_ImportKeyword) return OpKind_Import;
    
    if (t0.kind == TokenKind_OpenBrace) return OpKind_Block;
    
    if (t0.kind == TokenKind_Identifier) {
        if (t1.kind == TokenKind_OpenParenthesis) return OpKind_FunctionCall;
    }
    
    // Object definition
    if (t0.kind == TokenKind_Identifier && t1.kind == TokenKind_Colon && t2.kind == TokenKind_Identifier) return OpKind_ObjectDefinition;
    if (t0.kind == TokenKind_Identifier && t1.kind == TokenKind_Colon && t2.kind == TokenKind_Assignment) return OpKind_ObjectDefinition;
    if (t0.kind == TokenKind_Identifier && t1.kind == TokenKind_Colon && t2.kind == TokenKind_OpenBracket) return OpKind_ObjectDefinition;
    
    // Array definition
    if (t0.kind == TokenKind_Identifier && t1.kind == TokenKind_OpenBracket && t2.kind == TokenKind_CloseBracket && t3.kind == TokenKind_Identifier) return OpKind_ObjectDefinition;
    
    if (t0.kind == TokenKind_Identifier && t1.kind == TokenKind_Colon && t2.kind == TokenKind_Colon) {
        if (t3.kind == TokenKind_EnumKeyword) return OpKind_EnumDefinition;
        if (t3.kind == TokenKind_StructKeyword) return OpKind_StructDefinition;
        if (t3.kind == TokenKind_ArgKeyword) return OpKind_ArgDefinition;
        if (t3.kind == TokenKind_OpenParenthesis) return OpKind_FunctionDefinition;
        return OpKind_ObjectDefinition;
    }
    
    if (colon_count == 1) return OpKind_ObjectDefinition;
    if (assignment_count == 1) return OpKind_Assignment;
    
    return OpKind_None;
}

void parser_push_state(Parser* parser, Array<Token> tokens) {
    ParserState state{};
    state.tokens = tokens;
    state.token_index = 0;
    array_add(&parser->state_stack, state);
}

void parser_pop_state(Parser* parser) {
    array_erase(&parser->state_stack, parser->state_stack.count - 1);
}

ParserState* parser_get_state(Parser* parser) {
    return &parser->state_stack[parser->state_stack.count - 1];
}

Array<Token> parser_get_tokens_left(Parser* parser) {
    ParserState* state = parser_get_state(parser);
    return array_subarray(state->tokens, state->token_index, state->tokens.count - state->token_index);
}

b32 check_tokens_are_couple(Array<Token> tokens, u32 open_index, u32 close_index, TokenKind open_token, TokenKind close_token)
{
    assert(open_index < tokens.count && close_index < tokens.count);
    assert(tokens[open_index].kind == open_token && tokens[close_index].kind == close_token);
    
    if (close_index <= open_index) return false;
    
    i32 depth = 1;
    for (u32 i = open_index + 1; i < close_index; ++i)
    {
        if (tokens[i].kind == open_token) {
            depth++;
        }
        else if (tokens[i].kind == close_token) {
            depth--;
            if (depth <= 0) return false;
        }
    }
    
    return true;
}

Token peek_token(Parser* parser, i32 offset)
{
    ParserState* state = parser_get_state(parser);
    i32 index = (i32)state->token_index + offset;
    if (index < 0 || index >= state->tokens.count) return {};
    return state->tokens[index];
}

Array<Token> peek_tokens(Parser* parser, i32 offset, u32 count) {
    ParserState* state = parser_get_state(parser);
    i32 index = state->token_index + offset;
    if (index < 0 || index >= state->tokens.count) return {};
    count = MIN(count, state->tokens.count - index);
    return array_subarray(state->tokens, index, count);
}

void skip_tokens(Parser* parser, u32 count)
{
    ParserState* state = parser_get_state(parser);
    if (state->token_index + count > state->tokens.count) {
        assert(0);
        state->token_index = state->tokens.count;
        return;
    }
    state->token_index += count;
}

void skip_tokens_before_op(Parser* parser)
{
    ParserState* state = parser_get_state(parser);
    while (state->token_index < state->tokens.count) {
        Token token = state->tokens[state->token_index];
        if (token.kind != TokenKind_NextLine && token.kind != TokenKind_NextSentence) break;
        state->token_index++;
    }
}

void skip_sentence(Parser* parser)
{
    ParserState* state = parser_get_state(parser);
    while (state->token_index < state->tokens.count) {
        Token token = state->tokens[state->token_index++];
        if (token.kind == TokenKind_NextSentence) break;
    }
}

Token extract_token(Parser* parser)
{
    ParserState* state = parser_get_state(parser);
    if (state->token_index >= state->tokens.count) {
        return {};
    }
    return state->tokens[state->token_index++];
}

Array<Token> extract_tokens(Parser* parser, u32 count)
{
    ParserState* state = parser_get_state(parser);
    if (state->token_index + count > state->tokens.count) {
        assert(0);
        return {};
    }
    Array<Token> tokens = array_subarray(state->tokens, state->token_index, count);
    state->token_index += count;
    return tokens;
}

Array<Token> extract_tokens_until(Parser* parser, b32 require_separator, TokenKind sep0, TokenKind sep1)
{
    ParserState* state = parser_get_state(parser);
    u32 starting_index = state->token_index;
    while (state->token_index < state->tokens.count) {
        if (state->tokens[state->token_index].kind == sep0) break;
        if (state->tokens[state->token_index].kind == sep1) break;
        state->token_index++;
    }
    if (require_separator && state->token_index < state->tokens.count) {
        state->token_index++;
    }
    return array_subarray(state->tokens, starting_index, state->token_index - starting_index);
}

Array<Token> extract_tokens_with_depth(Parser* parser, TokenKind open_token, TokenKind close_token, b32 require_separator)
{
    ParserState* state = parser_get_state(parser);
    u32 starting_index = state->token_index;
    i32 depth = 0;
    
    while (state->token_index < state->tokens.count) {
        Token token = state->tokens[state->token_index];
        if (token.kind == open_token) depth++;
        else if (token.kind == close_token) {
            depth--;
            if (depth == 0) break;
        }
        state->token_index++;
    }
    
    u32 end_index = state->token_index;
    if (state->token_index < state->tokens.count) {
        if (require_separator) end_index++;
        state->token_index++;
    }
    
    return (depth == 0) ? array_subarray(state->tokens, starting_index, end_index - starting_index) : Array<Token>{};
}

Array<Array<Token>> split_tokens_in_parameters(Arena* arena, Array<Token> tokens)
{
    SCRATCH(arena);
    
    PooledArray<Array<Token>> params = pooled_array_make<Array<Token>>(scratch.arena, 8);
    
    u32 cursor = 0;
    u32 last_cursor = 0;
    i32 depth = 0;
    
    while (cursor < tokens.count) {
        Token t = tokens[cursor];
        if (t.kind == TokenKind_Comma && depth <= 0) {
            Array<Token> param = array_subarray(tokens, last_cursor, cursor - last_cursor);
            array_add(&params, param);
            last_cursor = cursor + 1;
        }
        if (t.kind == TokenKind_OpenParenthesis) depth++;
        else if (t.kind == TokenKind_CloseParenthesis) depth--;
        else if (t.kind == TokenKind_OpenBrace) depth++;
        else if (t.kind == TokenKind_CloseBrace) depth--;
        else if (t.kind == TokenKind_OpenBracket) depth++;
        else if (t.kind == TokenKind_CloseBracket) depth--;
        cursor++;
    }
    
    Array<Token> param = array_subarray(tokens, last_cursor, cursor - last_cursor);
    array_add(&params, param);
    
    return array_from_pooled_array(arena, params);
}

internal_fn OpNode* alloc_node(Parser* parser, OpKind kind, CodeLocation code)
{
    u32 node_size = get_node_size(kind);
    OpNode* node = (OpNode*)arena_push(yov->static_arena, node_size);
    node->kind = kind;
    node->code = code;
    return node;
}

internal_fn Array<OpNode*> process_parameters(Parser* parser, Array<Token> tokens)
{
    SCRATCH();
    if (tokens.count == 0) return {};
    
    Array<Array<Token>> parameters = split_tokens_in_parameters(scratch.arena, tokens);
    Array<OpNode*> nodes = array_make<OpNode*>(yov->static_arena, parameters.count);
    
    foreach(i, parameters.count) {
        Array<Token> parameter_tokens = parameters[i];
        nodes[i] = extract_expresion_from_array(parser, parameter_tokens);
    }
    
    return nodes;
}

OpNode* process_function_call(Parser* parser, Array<Token> tokens)
{
    Token starting_token = tokens[0];
    
    assert(tokens.count >= 3);
    if (tokens.count < 3) return alloc_node(parser, OpKind_Error, tokens[0].code);
    
    Array<Token> parameters = array_subarray(tokens, 2, tokens.count - 3);
    
    auto node = (OpNode_FunctionCall*)alloc_node(parser, OpKind_FunctionCall, starting_token.code);
    node->identifier = tokens[0].value;
    node->parameters = process_parameters(parser, parameters);
    return node;
}

OpNode* extract_expresion_from_array(Parser* parser, Array<Token> tokens)
{
    parser_push_state(parser, tokens);
    OpNode* res = extract_expresion(parser);
    parser_pop_state(parser);
    return res;
}

OpNode* extract_expresion(Parser* parser)
{
    SCRATCH();
    
    Array<Token> tokens = parser_get_tokens_left(parser);
    
    if (tokens.count == 0) return alloc_node(parser, OpKind_None, {});
    
    Token starting_token = tokens[0];
    
    // Remove parentheses around
    if (tokens.count >= 2 && tokens[0].kind == TokenKind_OpenParenthesis && tokens[tokens.count - 1].kind == TokenKind_CloseParenthesis)
    {
        b32 couple = check_tokens_are_couple(tokens, 0, tokens.count - 1, TokenKind_OpenParenthesis, TokenKind_CloseParenthesis);
        if (couple) tokens = array_subarray(tokens, 1, tokens.count - 2);
    }
    
    if (tokens.count == 0) {
        report_expr_is_empty(starting_token.code);
        return alloc_node(parser, OpKind_Error, {});
    }
    
    if (tokens.count == 1)
    {
        Token token = tokens[0];
        
        if (token.kind == TokenKind_IntLiteral) {
            u32 v;
            if (u32_from_string(&v, token.value)) {
                auto node = (OpNode_NumericLiteral*)alloc_node(parser, OpKind_IntLiteral, token.code);
                node->int_literal = v;
                return node;
            }
            else {
                assert(0);
            }
        }
        else if (token.kind == TokenKind_BoolLiteral) {
            auto node = (OpNode_NumericLiteral*)alloc_node(parser, OpKind_BoolLiteral, token.code);
            node->bool_literal = (b8)string_equals(token.value, STR("true"));
            return node;
        }
        else if (token.kind == TokenKind_StringLiteral)
        {
            PooledArray<OpNode*> expresions = pooled_array_make<OpNode*>(scratch.arena, 8);
            
            String raw = token.value;
            StringBuilder builder = string_builder_make(scratch.arena);
            
            u64 cursor = 0;
            while (cursor < raw.size)
            {
                u32 codepoint = string_get_codepoint(raw, &cursor);
                
                if (codepoint == '%') {
                    append(&builder, "\\%");
                    continue;
                }
                
                if (codepoint == '\\')
                {
                    codepoint = 0;
                    if (cursor < raw.size) {
                        codepoint = string_get_codepoint(raw, &cursor);
                    }
                    
                    if (codepoint == '\\') {
                        // This is resolved on interpretation
                        append(&builder, "\\\\");
                        continue;
                    }
                    
                    if (codepoint == 'n') append(&builder, "\n");
                    else if (codepoint == 'r') append(&builder, "\r");
                    else if (codepoint == 't') append(&builder, "\t");
                    else if (codepoint == '"') append(&builder, "\"");
                    else if (codepoint == '{') append(&builder, "{");
                    else if (codepoint == '}') append(&builder, "}");
                    else {
                        String sequence = string_from_codepoint(scratch.arena, codepoint);
                        report_invalid_escape_sequence(token.code, sequence);
                    }
                    
                    continue;
                }
                
                if (codepoint == '{')
                {
                    u64 start_identifier = cursor;
                    i32 depth = 1;
                    
                    while (cursor < raw.size) {
                        u32 codepoint = string_get_codepoint(raw, &cursor);
                        if (codepoint == '{') depth++;
                        else if (codepoint == '}') {
                            depth--;
                            if (depth == 0) break;
                        }
                    }
                    
                    String expresion_string = string_substring(raw, start_identifier, cursor - start_identifier - 1);
                    Array<Token> tokens = lexer_generate_tokens(scratch.arena, expresion_string, true, token.code);
                    
                    OpNode* expresion = extract_expresion_from_array(parser, tokens);
                    append(&builder, "%");
                    
                    array_add(&expresions, expresion);
                    continue;
                }
                
                append_codepoint(&builder, codepoint);
            }
            
            String value = string_from_builder(yov->static_arena, &builder);
            
            auto node = (OpNode_StringLiteral*)alloc_node(parser, OpKind_StringLiteral, token.code);
            node->raw_value = raw;
            node->value = value;
            node->expresions = array_from_pooled_array(yov->static_arena, expresions);
            return node;
        }
        else if (token.kind == TokenKind_CodepointLiteral)
        {
            String raw = token.value;
            
            if (raw.size <= 2) {
                report_invalid_codepoint_literal(token.code, raw);
                return alloc_node(parser, OpKind_Error, token.code);
            }
            
            raw = string_substring(raw, 1, raw.size - 2);
            
            u32 v = u32_max;
            if (string_equals(raw, "\\\\")) v = '\\';
            else if (string_equals(raw, "\\'")) v = '\'';
            else if (string_equals(raw, "\\n")) v = '\n';
            else if (string_equals(raw, "\\r")) v = '\r';
            else if (string_equals(raw, "\\t")) v = '\t';
            else
            {
                u64 cursor;
                v = string_get_codepoint(raw, &cursor);
                
                if (v == 0xFFFD || cursor != raw.size) {
                    report_invalid_codepoint_literal(token.code, raw);
                    return alloc_node(parser, OpKind_Error, token.code);
                }
            }
            
            auto node = (OpNode_NumericLiteral*)alloc_node(parser, OpKind_CodepointLiteral, token.code);
            node->codepoint_literal = v;
            return node;
        }
    }
    
    // Parameter List
    if (tokens.count > 2)
    {
        Array<Array<Token>> parameters = split_tokens_in_parameters(scratch.arena, tokens);
        
        if (parameters.count >= 2)
        {
            Array<OpNode*> nodes = array_make<OpNode*>(yov->static_arena, parameters.count);
            
            b32 valid = true;
            
            foreach(i, nodes.count) {
                nodes[i] = extract_expresion_from_array(parser, parameters[i]);
                if (nodes[i]->kind == OpKind_Error) valid = false;
            }
            
            if (!valid) return alloc_node(parser, OpKind_Error, tokens[0].code);
            
            OpNode_ParameterList* node = (OpNode_ParameterList*)alloc_node(parser, OpKind_ParameterList, tokens[0].code);
            node->nodes = nodes;
            return node;
        }
    }
    
    // Symbol
    if (tokens.count >= 1 && tokens.count % 2 == 1 && tokens[0].kind == TokenKind_Identifier)
    {
        Token token = tokens[0];
        
        b32 valid = true;
        for (u32 i = 1; i < tokens.count; ++i) {
            b32 expect_open = i % 2 == 1;
            TokenKind expected = expect_open ? TokenKind_OpenBracket : TokenKind_CloseBracket;
            if (tokens[i].kind != expected) {
                valid = false;
                break;
            }
        }
        
        if (valid)
        {
            auto node = (OpNode_Symbol*)alloc_node(parser, OpKind_Symbol, token.code);
            node->identifier = string_from_tokens(yov->static_arena, tokens);
            return node;
        }
    }
    
    // Function call
    if (tokens.count >= 2 && tokens[0].kind == TokenKind_Identifier && tokens[1].kind == TokenKind_OpenParenthesis && tokens[tokens.count - 1].kind == TokenKind_CloseParenthesis) {
        b32 couple = check_tokens_are_couple(tokens, 1, tokens.count - 1, TokenKind_OpenParenthesis, TokenKind_CloseParenthesis);
        if (couple) return process_function_call(parser, tokens);
    }
    
    // Binary operations & Signs
    if (tokens.count >= 2)
    {
        // NOTE(Jose): This might seem counterintuitive, but we need to create nodes for lower priority operations first, since expresions at the bottom of the tree are resolved first
        
        i32 min_preference = i32_max;
        i32 preferent_operator_index = -1;
        
        const i32 logical_preference = 1;
        const i32 boolean_preference = 2;
        const i32 addition_preference = 3;
        const i32 multiplication_preference = 4;
        const i32 function_call_preference = 5;
        const i32 sign_preference = 6;
        const i32 member_preference = 7;
        const i32 depth_mult = 8;
        
        i32 parenthesis_depth = 0;
        i32 braces_depth = 0;
        i32 brackets_depth = 0;
        b32 preference_is_sign = false;
        
        for (i32 i = tokens.count - 1; i >= 0; --i)
        {
            Token token = tokens[i];
            Token left_token = (i == 0) ? Token{} : tokens[i - 1];
            
            if (token.kind == TokenKind_CloseParenthesis) {
                parenthesis_depth++;
                continue;
            }
            if (token.kind == TokenKind_OpenParenthesis) {
                parenthesis_depth--;
                if (parenthesis_depth < 0) {
                    report_common_missing_closing_parenthesis(token.code);
                    return alloc_node(parser, OpKind_Error, token.code);
                }
                continue;
            }
            
            if (token.kind == TokenKind_CloseBrace) {
                braces_depth++;
                continue;
            }
            if (token.kind == TokenKind_OpenBrace) {
                braces_depth--;
                if (braces_depth < 0) {
                    report_common_missing_closing_brace(token.code);
                    return alloc_node(parser, OpKind_Error, token.code);
                }
                continue;
            }
            
            if (token.kind == TokenKind_CloseBracket) {
                brackets_depth++;
                continue;
            }
            if (token.kind == TokenKind_OpenBracket) {
                brackets_depth--;
                if (brackets_depth < 0) {
                    report_common_missing_closing_bracket(token.code);
                    return alloc_node(parser, OpKind_Error, token.code);
                }
                continue;
            }
            
            b32 is_sign = false;
            
            i32 preference = i32_max;
            if (token_is_sign_or_binary_op(token.kind))
            {
                if (token_is_sign_or_binary_op(left_token.kind)) is_sign = true;
                if (left_token.kind == TokenKind_OpenParenthesis) is_sign = true;
                if (left_token.kind == TokenKind_None) is_sign = true;
                
                if (is_sign) {
                    preference = sign_preference;
                }
                else {
                    BinaryOperator op = binary_operator_from_token(token.kind);
                    if (op == BinaryOperator_Addition) preference = addition_preference;
                    else if (op == BinaryOperator_Substraction) preference = addition_preference;
                    else if (op == BinaryOperator_Multiplication) preference = multiplication_preference;
                    else if (op == BinaryOperator_Division) preference = multiplication_preference;
                    else if (op == BinaryOperator_Modulo) preference = multiplication_preference;
                    else if (op == BinaryOperator_LogicalNot) preference = logical_preference;
                    else if (op == BinaryOperator_LogicalOr) preference = logical_preference;
                    else if (op == BinaryOperator_LogicalAnd) preference = logical_preference;
                    else if (op == BinaryOperator_Equals) preference = boolean_preference;
                    else if (op == BinaryOperator_NotEquals) preference = boolean_preference;
                    else if (op == BinaryOperator_LessThan) preference = boolean_preference;
                    else if (op == BinaryOperator_LessEqualsThan) preference = boolean_preference;
                    else if (op == BinaryOperator_GreaterThan) preference = boolean_preference;
                    else if (op == BinaryOperator_GreaterEqualsThan) preference = boolean_preference;
                    else {
                        assert(0);
                    }
                }
            }
            else if (token.kind == TokenKind_Dot && i == tokens.count - 2) {
                preference = member_preference;
            }
            
            if (preference == i32_max || braces_depth != 0 || brackets_depth != 0) continue;
            
            preference += parenthesis_depth * depth_mult;
            
            if (preference < min_preference) {
                min_preference = preference;
                preferent_operator_index = i;
                preference_is_sign = is_sign;
            }
        }
        
        if (parenthesis_depth > 0) {
            report_common_missing_opening_parenthesis(tokens[0].code);
            return alloc_node(parser, OpKind_Error, tokens[0].code);
        }
        if (braces_depth > 0) {
            report_common_missing_opening_brace(tokens[0].code);
            return alloc_node(parser, OpKind_Error, tokens[0].code);
        }
        
        if (min_preference != i32_max)
        {
            Token op_token = tokens[preferent_operator_index];
            
            if (token_is_sign_or_binary_op(op_token.kind))
            {
                if (preference_is_sign)
                {
                    assert(preferent_operator_index == 0);
                    
                    Token sign_token = tokens[preferent_operator_index];
                    OpNode* expresion = extract_expresion_from_array(parser, array_subarray(tokens, 1, tokens.count - 1));
                    
                    if (sign_token.kind == TokenKind_Ampersand)
                    {
                        auto node = (OpNode_Reference*)alloc_node(parser, OpKind_Reference, sign_token.code);
                        node->expresion = expresion;
                        return node;
                    }
                    else
                    {
                        BinaryOperator op = binary_operator_from_token(sign_token.kind);
                        
                        assert(op != BinaryOperator_None);
                        
                        auto node = (OpNode_Sign*)alloc_node(parser, OpKind_Sign, sign_token.code);
                        node->op = op;
                        node->expresion = expresion;
                        return node;
                    }
                }
                else
                {
                    if (preferent_operator_index <= 0 || preferent_operator_index >= tokens.count - 1) {
                        String op_string = string_from_tokens(scratch.arena, tokens);
                        report_expr_invalid_binary_operation(op_token.code, op_string);
                        return alloc_node(parser, OpKind_Error, tokens[0].code);
                    }
                    else
                    {
                        OpNode* left_expresion = extract_expresion_from_array(parser, array_subarray(tokens, 0, preferent_operator_index));
                        OpNode* right_expresion = extract_expresion_from_array(parser, array_subarray(tokens, preferent_operator_index + 1, tokens.count - (preferent_operator_index + 1)));
                        
                        auto node = (OpNode_Binary*)alloc_node(parser, OpKind_Binary, tokens[0].code);
                        node->left = left_expresion;
                        node->right = right_expresion;
                        node->op = binary_operator_from_token(op_token.kind);
                        return node;
                    }
                }
            }
            // Member access
            else if (op_token.kind == TokenKind_Dot)
            {
                assert(preferent_operator_index == tokens.count - 2);
                
                String member_value = tokens[tokens.count - 1].value;
                
                if (member_value.size == 0) {
                    report_expr_empty_member(tokens[0].code);
                    return alloc_node(parser, OpKind_Error, tokens[0].code);
                }
                
                Array<Token> expresion_tokens = array_subarray(tokens, 0, tokens.count - 2);
                OpNode* expresion = extract_expresion_from_array(parser, expresion_tokens);
                
                auto node = (OpNode_MemberValue*)alloc_node(parser, OpKind_MemberValue, tokens[0].code);
                node->expresion = expresion;
                node->member = member_value;
                return node;
            }
        }
    }
    
    // Array Expresions
    if (tokens.count >= 2 && (tokens[0].kind == TokenKind_OpenBrace || tokens[0].kind == TokenKind_OpenBracket))
    {
        b32 is_empty = tokens[0].kind == TokenKind_OpenBracket;
        
        TokenKind open_token = is_empty ? TokenKind_OpenBracket : TokenKind_OpenBrace;
        TokenKind close_token = is_empty ? TokenKind_CloseBracket : TokenKind_CloseBrace;
        
        Array<Token> expresions_tokens = extract_tokens_with_depth(parser, open_token, close_token, true);
        
        if (expresions_tokens.count >= 2)
        {
            OpNode* type_node = NULL;
            Token arrow = extract_token(parser);
            
            if (arrow.kind == TokenKind_Arrow) {
                type_node = extract_object_type(parser);
            }
            else if (arrow.kind != TokenKind_None && arrow.kind != TokenKind_NextSentence) {
                report_array_expr_expects_an_arrow(starting_token.code);
                return alloc_node(parser, OpKind_Error, starting_token.code);
            }
            
            if (type_node == NULL) {
                type_node = alloc_node(parser, OpKind_None, starting_token.code);
            }
            
            Array<OpNode*> expresions = process_parameters(parser, array_subarray(expresions_tokens, 1, expresions_tokens.count - 2));
            
            auto node = (OpNode_ArrayExpresion*)alloc_node(parser, OpKind_ArrayExpresion, starting_token.code);
            node->nodes = expresions;
            node->type = type_node;
            node->is_empty = (b8)is_empty;
            return node;
        }
    }
    
    // Indexing
    if (tokens.count >= 2 && tokens[tokens.count-1].kind == TokenKind_CloseBracket)
    {
        i32 starting_token = 0;
        while (starting_token < tokens.count && tokens[starting_token].kind != TokenKind_OpenBracket)
            starting_token++;
        
        if (starting_token < tokens.count)
        {
            b32 couple = check_tokens_are_couple(tokens, starting_token, tokens.count - 1, TokenKind_OpenBracket, TokenKind_CloseBracket);
            
            if (couple)
            {
                Array<Token> value_tokens = array_subarray(tokens, 0, starting_token);
                Array<Token> indexing_tokens = array_subarray(tokens, starting_token + 1, tokens.count - starting_token - 2);
                
                if (value_tokens.count > 0 && indexing_tokens.count > 0)
                {
                    OpNode* value_node = extract_expresion_from_array(parser, value_tokens);
                    OpNode* index_node = extract_expresion_from_array(parser, indexing_tokens);
                    
                    if (index_node->kind == OpKind_None) {
                        report_array_indexing_expects_expresion(tokens[0].code);
                        return alloc_node(parser, OpKind_Error, tokens[0].code);
                    }
                    
                    if (value_node->kind == OpKind_Error) return value_node;
                    if (index_node->kind == OpKind_Error) return index_node;
                    
                    auto node = (OpNode_Indexing*)alloc_node(parser, OpKind_Indexing, tokens[0].code);
                    node->value = value_node;
                    node->index = index_node;
                    return node;
                }
            }
        }
    }
    
    String expresion_string = string_from_tokens(scratch.arena, tokens);
    report_expr_syntactic_unknown(tokens[0].code, expresion_string);
    return alloc_node(parser, OpKind_Error, tokens[0].code);
}

OpNode* extract_if_statement(Parser* parser)
{
    Token starting_token = peek_token(parser);
    
    Array<Token> sentence = extract_tokens_with_depth(parser, TokenKind_OpenParenthesis, TokenKind_CloseParenthesis, true);
    
    assert(sentence.count >= 4);
    if (sentence.count < 4) return alloc_node(parser, OpKind_Error, starting_token.code);
    
    if (sentence[1].kind != TokenKind_OpenParenthesis) {
        report_common_expecting_parenthesis(starting_token.code, "if-statement");
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    if (sentence[sentence.count - 1].kind != TokenKind_CloseParenthesis) {
        report_common_missing_closing_parenthesis(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    Array<Token> expresion = array_subarray(sentence, 2, sentence.count - 3);
    OpNode* expresion_node = extract_expresion_from_array(parser, expresion);
    
    auto node = (OpNode_IfStatement*)alloc_node(parser, OpKind_IfStatement, starting_token.code);
    node->expresion = expresion_node;
    node->success = extract_op(parser);
    
    skip_tokens_before_op(parser);
    
    Token else_token = peek_token(parser);
    if (else_token.kind == TokenKind_ElseKeyword) {
        extract_token(parser); // Skip else
        node->failure = extract_op(parser);
    }
    
    if (node->failure == NULL) 
        node->failure = alloc_node(parser, OpKind_None, starting_token.code);
    
    return node;
}

OpNode* extract_while_statement(Parser* parser)
{
    Token starting_token = peek_token(parser);
    
    Array<Token> sentence = extract_tokens_until(parser, true, TokenKind_CloseParenthesis);
    
    assert(sentence.count >= 4);
    if (sentence.count < 4) return alloc_node(parser, OpKind_Error, starting_token.code);
    
    if (sentence[1].kind != TokenKind_OpenParenthesis) {
        report_common_expecting_parenthesis(starting_token.code, "while-statement");
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    if (sentence[sentence.count - 1].kind != TokenKind_CloseParenthesis) {
        report_common_missing_closing_parenthesis(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    Array<Token> expresion = array_subarray(sentence, 2, sentence.count - 3);
    OpNode* expresion_node = extract_expresion_from_array(parser, expresion);
    
    auto node = (OpNode_WhileStatement*)alloc_node(parser, OpKind_WhileStatement, starting_token.code);
    node->expresion = expresion_node;
    node->content = extract_op(parser);
    
    return node;
}

OpNode* extract_for_statement(Parser* parser)
{
    Token starting_token = peek_token(parser);
    
    Array<Token> sentence = extract_tokens_until(parser, true, TokenKind_CloseParenthesis);
    
    assert(sentence.count >= 5);
    if (sentence.count < 5) return alloc_node(parser, OpKind_Error, starting_token.code);
    
    if (sentence[1].kind != TokenKind_OpenParenthesis) {
        report_common_expecting_parenthesis(starting_token.code, "for-statement");
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    if (sentence[sentence.count - 1].kind != TokenKind_CloseParenthesis) {
        report_common_missing_closing_parenthesis(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    Array<Token> expresions = array_subarray(sentence, 2, sentence.count - 3);
    i32 separator_indices[2];
    i32 separator_count = 0;
    
    foreach(i, expresions.count) {
        if (expresions[i].kind == TokenKind_NextSentence)
        {
            if (separator_count >= countof(separator_indices)) {
                separator_count++;
                break;
            }
            
            separator_indices[separator_count++] = i;
        }
    }
    
    // Foreach Array Statement
    if (separator_count == 0 && expresions.count >= 3)
    {
        Token element_token = expresions[0];
        
        if (element_token.kind != TokenKind_Identifier) {
            report_foreach_expecting_identifier(starting_token.code);
            return alloc_node(parser, OpKind_Error, starting_token.code);
        }
        
        i32 colon_index = -1;
        foreach(i, expresions.count) {
            if (expresions[i].kind == TokenKind_Colon) {
                colon_index = i;
                break;
            }
        }
        
        if (colon_index < 0) {
            report_foreach_expecting_colon(starting_token.code);
            return alloc_node(parser, OpKind_Error, starting_token.code);
        }
        
        String index_name = {};
        
        if (colon_index > 1)
        {
            if (colon_index == 3 && expresions[1].kind == TokenKind_Comma && expresions[2].kind == TokenKind_Identifier) {
                index_name = expresions[2].value;
            }
            else {
                report_foreach_expecting_comma_separated_identifiers(starting_token.code);
                return alloc_node(parser, OpKind_Error, starting_token.code);
            }
        }
        
        Array<Token> expresion_tokens = array_subarray(expresions, colon_index + 1, expresions.count - (colon_index + 1));
        
        auto node = (OpNode_ForeachArrayStatement*)alloc_node(parser, OpKind_ForeachArrayStatement, starting_token.code);
        node->element_name = element_token.value;
        node->index_name = index_name;
        node->expresion = extract_expresion_from_array(parser, expresion_tokens);
        node->content = extract_op(parser);
        return node;
    }
    // For Statement
    else if (separator_count == 2)
    {
        Array<Token> initialize_sentence_tokens = array_subarray(expresions, 0, separator_indices[0]);
        Array<Token> condition_expresion_tokens = array_subarray(expresions, separator_indices[0] + 1, separator_indices[1] - (separator_indices[0] + 1));
        Array<Token> update_sentence_tokens = array_subarray(expresions, separator_indices[1] + 1, expresions.count - (separator_indices[1] + 1));
        
        auto node = (OpNode_ForStatement*)alloc_node(parser, OpKind_ForStatement, starting_token.code);
        
        parser_push_state(parser, initialize_sentence_tokens);
        node->initialize_sentence = extract_op(parser);
        parser_pop_state(parser);
        
        node->condition_expresion = extract_expresion_from_array(parser, condition_expresion_tokens);
        
        parser_push_state(parser, update_sentence_tokens);
        node->update_sentence = extract_op(parser);
        parser_pop_state(parser);
        
        node->content = extract_op(parser);
        return node;
    }
    else
    {
        report_for_unknown(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
}

OpNode* extract_enum_definition(Parser* parser)
{
    SCRATCH();
    
    Token starting_token = peek_token(parser);
    
    Token identifier_token = extract_token(parser);
    assert(identifier_token.kind == TokenKind_Identifier);
    
    Token colon0_token = extract_token(parser);
    assert(colon0_token.kind == TokenKind_Colon);
    Token colon1_token = extract_token(parser);
    assert(colon1_token.kind == TokenKind_Colon);
    
    Token enum_keyword_token = extract_token(parser);
    assert(enum_keyword_token.kind == TokenKind_EnumKeyword);
    
    Token open_brace_token = extract_token(parser);
    if (open_brace_token.kind != TokenKind_OpenBrace) {
        report_common_missing_opening_bracket(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    PooledArray<String> names = pooled_array_make<String>(scratch.arena, 16);
    PooledArray<OpNode*> values = pooled_array_make<OpNode*>(scratch.arena, 16);
    
    do {
        Token name_token = extract_token(parser);
        if (name_token.kind == TokenKind_CloseBrace) break;
        if (name_token.kind != TokenKind_Identifier) {
            report_enumdef_expecting_comma_separated_identifier(name_token.code);
            return alloc_node(parser, OpKind_Error, starting_token.code);
        }
        
        OpNode* value{};
        Token assignment_token = peek_token(parser);
        
        if (assignment_token.kind == TokenKind_Assignment && assignment_token.assignment_binary_operator == BinaryOperator_None) {
            extract_token(parser);
            // TODO(Jose): // TODO(Jose): Unitl: CloseBrace or Comma
            Array<Token> expresion_tokens = extract_tokens_until(parser, false, TokenKind_CloseBrace, TokenKind_Comma);
            
            OpNode* node = extract_expresion_from_array(parser, expresion_tokens);
            
            if (node->kind == OpKind_Error) return alloc_node(parser, OpKind_Error, starting_token.code);
            if (node->kind == OpKind_None) {
                report_common_expecting_valid_expresion(name_token.code);
                return alloc_node(parser, OpKind_Error, starting_token.code);
            }
            
            value = node;
        }
        
        array_add(&names, name_token.value);
        array_add(&values, value);
        
        Token comma_token = extract_token(parser);
        if (comma_token.kind == TokenKind_CloseBrace) break;
        if (comma_token.kind != TokenKind_Comma) {
            report_enumdef_expecting_comma_separated_identifier(comma_token.code);
            return alloc_node(parser, OpKind_Error, starting_token.code);
        }
    }
    while (true);
    
    OpNode_EnumDefinition* node = (OpNode_EnumDefinition*)alloc_node(parser, OpKind_EnumDefinition, starting_token.code);
    node->identifier = identifier_token.value;
    node->names = array_from_pooled_array(yov->static_arena, names);
    node->values = array_from_pooled_array(yov->static_arena, values);
    return node;
}

OpNode* extract_struct_definition(Parser* parser)
{
    SCRATCH();
    
    Token starting_token = peek_token(parser);
    
    Token identifier_token = extract_token(parser);
    assert(identifier_token.kind == TokenKind_Identifier);
    
    Token colon0_token = extract_token(parser);
    assert(colon0_token.kind == TokenKind_Colon);
    Token colon1_token = extract_token(parser);
    assert(colon1_token.kind == TokenKind_Colon);
    
    Token struct_keyword_token = extract_token(parser);
    assert(struct_keyword_token.kind == TokenKind_StructKeyword);
    
    Token open_brace_token = extract_token(parser);
    if (open_brace_token.kind != TokenKind_OpenBrace) {
        report_common_missing_opening_bracket(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    PooledArray<OpNode_ObjectDefinition*> members = pooled_array_make<OpNode_ObjectDefinition*>(scratch.arena, 16);
    
    while (peek_token(parser).kind != TokenKind_CloseBrace) {
        
        OpNode* member = extract_object_definition(parser);
        
        if (member->kind == OpKind_Error) return member;
        
        if (member->kind != OpKind_ObjectDefinition) {
            report_expecting_object_definition(member->code);
            return alloc_node(parser, OpKind_Error, member->code);
        }
        
        array_add(&members, (OpNode_ObjectDefinition*)member);
    }
    
    skip_tokens(parser, 1);
    
    OpNode_StructDefinition* node = (OpNode_StructDefinition*)alloc_node(parser, OpKind_StructDefinition, starting_token.code);
    node->identifier = identifier_token.value;
    node->members = array_from_pooled_array(yov->static_arena, members);
    return node;
}

OpNode* extract_arg_definition(Parser* parser)
{
    Token starting_token = peek_token(parser);
    
    Token identifier_token = extract_token(parser);
    assert(identifier_token.kind == TokenKind_Identifier);
    
    Token colon0_token = extract_token(parser);
    assert(colon0_token.kind == TokenKind_Colon);
    Token colon1_token = extract_token(parser);
    assert(colon1_token.kind == TokenKind_Colon);
    
    Token arg_keyword_token = extract_token(parser);
    assert(arg_keyword_token.kind == TokenKind_ArgKeyword);
    
    OpNode* none_node = alloc_node(parser, OpKind_None, starting_token.code);
    OpNode* type = (OpNode_ObjectType*)none_node;
    OpNode* name = (OpNode_Assignment*)none_node;
    OpNode* description = (OpNode_Assignment*)none_node;
    OpNode* required = (OpNode_Assignment*)none_node;
    OpNode* default_value = (OpNode_Assignment*)none_node;
    
    if (peek_token(parser).kind == TokenKind_Arrow)
    {
        extract_token(parser);
        type = extract_object_type(parser);
    }
    
    Token open_brace_token = extract_token(parser);
    if (open_brace_token.kind != TokenKind_OpenBrace) {
        report_common_missing_opening_bracket(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    while (peek_token(parser).kind != TokenKind_CloseBrace) {
        
        OpNode_Assignment* assignment = (OpNode_Assignment*)extract_assignment(parser);
        
        if (assignment->kind == OpKind_Error) {
            extract_tokens_until(parser, false, TokenKind_CloseBrace);
            return assignment;
        }
        assert(assignment->kind == OpKind_Assignment);
        
        if (assignment->binary_operator != BinaryOperator_None || assignment->destination->kind != OpKind_Symbol) {
            report_expecting_assignment(assignment->code);// TODO(Jose): 
            return alloc_node(parser, OpKind_Error, assignment->code);
        }
        
        String identifier = ((OpNode_Symbol*)assignment->destination)->identifier;
        
        if (string_equals(identifier, "name")) name = assignment;
        else if (string_equals(identifier, "description")) description = assignment;
        else if (string_equals(identifier, "required")) required = assignment;
        else if (string_equals(identifier, "default")) default_value = assignment;
        else {
            report_unknown_parameter(assignment->code, identifier);
            return alloc_node(parser, OpKind_Error, assignment->code);
        }
    }
    
    skip_tokens(parser, 1);
    
    OpNode_ArgDefinition* node = (OpNode_ArgDefinition*)alloc_node(parser, OpKind_ArgDefinition, starting_token.code);
    node->identifier = identifier_token.value;
    node->type = type;
    node->name = name;
    node->description = description;
    node->required = required;
    node->default_value = default_value;
    return node;
}

OpNode* extract_function_definition(Parser* parser)
{
    SCRATCH();
    
    Token starting_token = peek_token(parser);
    
    Token identifier_token = extract_token(parser);
    assert(identifier_token.kind == TokenKind_Identifier);
    
    Token colon0_token = extract_token(parser);
    assert(colon0_token.kind == TokenKind_Colon);
    Token colon1_token = extract_token(parser);
    assert(colon1_token.kind == TokenKind_Colon);
    
    assert(peek_token(parser).kind == TokenKind_OpenParenthesis);
    Array<Token> parameter_tokens = extract_tokens_with_depth(parser, TokenKind_OpenParenthesis, TokenKind_CloseParenthesis, true);
    
    if (parameter_tokens.count < 2) {
        report_common_missing_closing_parenthesis(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    Array<OpNode_ObjectDefinition*> params{};
    Array<OpNode*> returns{};
    
    // Parameters
    if (parameter_tokens.count > 2)
    {
        parameter_tokens = array_subarray(parameter_tokens, 1, parameter_tokens.count - 2);
        
        Array<Array<Token>> parameters = split_tokens_in_parameters(scratch.arena, parameter_tokens);
        params = array_make<OpNode_ObjectDefinition*>(yov->static_arena, parameters.count);
        
        b32 valid = true;
        
        foreach (i, parameters.count)
        {
            Array<Token> tokens = parameters[i];
            
            parser_push_state(parser, tokens);
            OpNode* node = extract_object_definition(parser);
            parser_pop_state(parser);
            
            if (node->kind != OpKind_ObjectDefinition)
            {
                valid = false;
                
                if (node->kind != OpKind_Error) {
                    report_expecting_object_definition(node->code);
                    node = alloc_node(parser, OpKind_Error, node->code);
                }
            }
            
            params[i] = (OpNode_ObjectDefinition*)node;
        }
        
        if (!valid) {
            return alloc_node(parser, OpKind_Error, starting_token.code);
        }
    }
    
    // Return definition
    if (peek_token(parser).kind == TokenKind_Arrow) {
        extract_token(parser);
        
        Token return_start_node = peek_token(parser);
        
        if (return_start_node.kind == TokenKind_OpenParenthesis)
        {
            Array<Token> return_tokens = extract_tokens_with_depth(parser, TokenKind_OpenParenthesis, TokenKind_CloseParenthesis, true);
            
            if (return_tokens.count < 2) {
                report_common_missing_closing_parenthesis(return_start_node.code);
                return alloc_node(parser, OpKind_Error, return_start_node.code);
            }
            
            return_tokens = array_subarray(return_tokens, 1, return_tokens.count - 2);
            
            Array<Array<Token>> return_tokens_array = split_tokens_in_parameters(scratch.arena, return_tokens);
            returns = array_make<OpNode*>(yov->static_arena, return_tokens_array.count);
            
            b32 valid = true;
            
            foreach (i, returns.count)
            {
                Array<Token> tokens = return_tokens_array[i];
                
                parser_push_state(parser, tokens);
                OpNode* node = extract_object_definition(parser);
                parser_pop_state(parser);
                
                if (node->kind != OpKind_ObjectDefinition)
                {
                    valid = false;
                    
                    if (node->kind != OpKind_Error) {
                        report_expecting_object_definition(node->code);
                        node = alloc_node(parser, OpKind_Error, node->code);
                    }
                }
                
                returns[i] = (OpNode_ObjectDefinition*)node;
            }
            
            if (!valid) {
                return alloc_node(parser, OpKind_Error, starting_token.code);
            }
        }
        else
        {
            returns = array_make<OpNode*>(yov->static_arena, 1);
            returns[0] = extract_object_type(parser);
            if (returns[0]->kind != OpKind_ObjectType) {
                return alloc_node(parser, OpKind_Error, return_start_node.code);
            }
        }
    }
    
    Token start_block_token = peek_token(parser);
    
    OpNode* block = NULL;
    
    if (start_block_token.kind == TokenKind_NextSentence) {
        block = alloc_node(parser, OpKind_None, start_block_token.code);
    }
    else if (start_block_token.kind == TokenKind_OpenBrace)
    {
        Array<Token> block_tokens = extract_tokens_with_depth(parser, TokenKind_OpenBrace, TokenKind_CloseBrace, true);
        
        if (block_tokens.count < 2) {
            report_common_missing_closing_brace(starting_token.code);
            return alloc_node(parser, OpKind_Error, starting_token.code);
        }
        
        if (block_tokens.count == 2) {
            block = alloc_node(parser, OpKind_Block, block_tokens[0].code);
        }
        else {
            block_tokens = array_subarray(block_tokens, 1, block_tokens.count - 2);
            
            parser_push_state(parser, block_tokens);
            block = extract_block(parser, true);
            parser_pop_state(parser);
            
            if (block->kind != OpKind_Block) {
                report_common_missing_block(block_tokens[0].code);
                return alloc_node(parser, OpKind_Error, starting_token.code);
            }
        }
    }
    else {
        report_common_missing_opening_brace(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    
    
    OpNode_FunctionDefinition* node = (OpNode_FunctionDefinition*)alloc_node(parser, OpKind_FunctionDefinition, starting_token.code);
    node->identifier = identifier_token.value;
    node->block = block;
    node->parameters = params;
    node->returns = returns;
    return node;
}

OpNode* extract_assignment(Parser* parser)
{
    Token starting_token = peek_token(parser);
    
    if (op_kind_from_tokens(parser) != OpKind_Assignment) {
        report_expecting_assignment(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    Array<Token> destination_tokens = extract_tokens_until(parser, false, TokenKind_Assignment);
    
    Token assignment_token = extract_token(parser);
    if (assignment_token.kind != TokenKind_Assignment) {
        assert(0);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    Array<Token> source_tokens = extract_tokens_until(parser, false, TokenKind_NextSentence, TokenKind_None);
    extract_token(parser);
    
    OpNode* destination = extract_expresion_from_array(parser, destination_tokens);
    OpNode* source = extract_expresion_from_array(parser, source_tokens);
    
    if (destination->kind == OpKind_Error) return destination;
    if (source->kind == OpKind_Error) return source;
    
    auto node = (OpNode_Assignment*)alloc_node(parser, OpKind_Assignment, starting_token.code);
    node->destination = destination;
    node->source = source;
    node->binary_operator = assignment_token.assignment_binary_operator;
    return node;
}

OpNode* extract_object_type(Parser* parser)
{
    String name{};
    u32 array_dimensions = 0;
    
    Token starting_token = peek_token(parser);
    
    Token identifier_token = extract_token(parser);
    if (identifier_token.kind != TokenKind_Identifier) {
        report_objdef_expecting_type_identifier(identifier_token.code);
        return alloc_node(parser, OpKind_Error, identifier_token.code);
    }
    name = identifier_token.value;
    
    Token open_bracket = peek_token(parser, 0);
    
    if (open_bracket.kind == TokenKind_OpenBracket)
    {
        i32 starting_cursor = parser_get_state(parser)->token_index;
        
        while (peek_token(parser).kind == TokenKind_OpenBracket) {
            Array<Token> tokens = extract_tokens_with_depth(parser, TokenKind_OpenBracket, TokenKind_CloseBracket, true);
            if (tokens.count != 2) {
                report_common_missing_closing_bracket(starting_token.code);
                return alloc_node(parser, OpKind_Error, starting_token.code);
            }
            array_dimensions++;
        }
    }
    
    b32 is_reference = false;
    Token ref_token = peek_token(parser, 0);
    if (ref_token.kind == TokenKind_Ampersand) {
        skip_tokens(parser, 1);
        is_reference = true;
    }
    
    OpNode_ObjectType* node = (OpNode_ObjectType*)alloc_node(parser, OpKind_ObjectType, starting_token.code);
    node->name = name;
    node->array_dimensions = array_dimensions;
    node->is_reference = is_reference;
    return node;
}

OpNode* extract_object_definition(Parser* parser)
{
    Token starting_token = peek_token(parser);
    
    Array<Token> destination_tokens = extract_tokens_until(parser, false, TokenKind_Colon);
    
    Token colon_token = extract_token(parser);
    if (colon_token.kind != TokenKind_Colon) {
        report_objdef_expecting_colon(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    OpNode* destination_node = extract_expresion_from_array(parser, destination_tokens);
    
    Array<String> names = {};
    if (destination_node->kind == OpKind_Symbol) {
        names = array_make<String>(yov->static_arena, 1);
        names[0] = ((OpNode_Symbol*)destination_node)->identifier;
    }
    else if (destination_node->kind == OpKind_ParameterList)
    {
        OpNode_ParameterList* params = (OpNode_ParameterList*)destination_node;
        
        foreach(i, params->nodes.count) {
            if (params->nodes[i]->kind == OpKind_Error) return params->nodes[i];
            if (params->nodes[i]->kind != OpKind_Symbol) {
                invalid_codepath();// TODO(Jose): 
                report_error(params->code, "Expecing a symbol");
                return alloc_node(parser, OpKind_Error, params->code);
            }
        }
        
        names = array_make<String>(yov->static_arena, params->nodes.count);
        foreach(i, names.count) {
            names[i] = ((OpNode_Symbol*)params->nodes[i])->identifier;
        }
    }
    else if (destination_node->kind == OpKind_Error) return destination_node;
    else {
        invalid_codepath();
    }
    
    OpNode_ObjectType* node_type = NULL;
    
    Token type_or_assignment_token = peek_token(parser);
    
    if (type_or_assignment_token.kind == TokenKind_Assignment) {}
    else if (type_or_assignment_token.kind == TokenKind_Colon) {}
    else if (type_or_assignment_token.kind == TokenKind_Identifier)
    {
        node_type = (OpNode_ObjectType*)extract_object_type(parser);
        if (node_type->kind != OpKind_ObjectType) {
            return alloc_node(parser, OpKind_Error, starting_token.code);
        }
    }
    else {
        report_objdef_expecting_type_identifier(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    Token assignment_token = extract_token(parser);
    
    OpNode* assignment_node = NULL;
    b32 is_constant = false;
    
    if (assignment_token.kind != TokenKind_NextSentence && assignment_token.kind != TokenKind_None)
    {
        b32 variable_assignment = assignment_token.kind == TokenKind_Assignment && assignment_token.assignment_binary_operator == BinaryOperator_None;
        b32 constant_assignment = assignment_token.kind == TokenKind_Colon;
        
        if (!variable_assignment && !constant_assignment) {
            report_objdef_expecting_assignment(starting_token.code);
            return alloc_node(parser, OpKind_Error, starting_token.code);
        }
        
        is_constant = constant_assignment;
        
        Array<Token> assignment_tokens = extract_tokens_until(parser, false, TokenKind_NextSentence);
        assignment_node = extract_expresion_from_array(parser, assignment_tokens);
        
        if (assignment_node->kind == OpKind_None) {
            report_common_expecting_valid_expresion(starting_token.code);
            return alloc_node(parser, OpKind_Error, starting_token.code);
        }
    }
    else {
        assignment_node = alloc_node(parser, OpKind_None, starting_token.code);
    }
    
    if (node_type == NULL) node_type = (OpNode_ObjectType*)alloc_node(parser, OpKind_ObjectType, starting_token.code);
    
    skip_tokens_before_op(parser);
    
    auto node = (OpNode_ObjectDefinition*)alloc_node(parser, OpKind_ObjectDefinition, starting_token.code);
    node->assignment = assignment_node;
    node->type = node_type;
    node->names = names;
    node->is_constant = (b8)is_constant;
    
    return node;
}

OpNode* extract_function_call(Parser* parser)
{
    Token starting_token = peek_token(parser);
    Array<Token> sentence = extract_tokens_with_depth(parser, TokenKind_OpenParenthesis, TokenKind_CloseParenthesis, true);
    return process_function_call(parser, sentence);
}

OpNode* extract_return(Parser* parser)
{
    Token return_token = extract_token(parser);
    assert(return_token.kind == TokenKind_ReturnKeyword);
    
    Array<Token> expresion_tokens = extract_tokens_until(parser, false, TokenKind_NextSentence);
    OpNode* expresion = extract_expresion_from_array(parser, expresion_tokens);
    
    OpNode_Return* node = (OpNode_Return*)alloc_node(parser, OpKind_Return, return_token.code);
    node->expresion = expresion;
    return node;
}

OpNode* extract_continue(Parser* parser)
{
    Token continue_token = extract_token(parser);
    assert(continue_token.kind == TokenKind_ContinueKeyword);
    
    OpNode* node = alloc_node(parser, OpKind_Continue, continue_token.code);
    return node;
}

OpNode* extract_break(Parser* parser)
{
    Token break_token = extract_token(parser);
    assert(break_token.kind == TokenKind_BreakKeyword);
    
    OpNode* node = alloc_node(parser, OpKind_Break, break_token.code);
    return node;
}

OpNode* extract_import(Parser* parser)
{
    Token import_token = extract_token(parser);
    assert(import_token.kind == TokenKind_ImportKeyword);
    
    Token literal_token = extract_token(parser);
    
    if (literal_token.kind != TokenKind_StringLiteral) {
        report_expecting_string_literal(import_token.code);
        return alloc_node(parser, OpKind_Error, import_token.code);
    }
    
    Token semicolon_token = peek_token(parser);
    
    if (semicolon_token.kind != TokenKind_NextSentence) {
        report_expecting_semicolon(import_token.code);
        return alloc_node(parser, OpKind_Error, import_token.code);
    }
    skip_tokens(parser, 1);
    
    OpNode_Import* node = (OpNode_Import*)alloc_node(parser, OpKind_Import, import_token.code);
    node->path = literal_token.value;
    return node;
}

OpNode* extract_block(Parser* parser, b32 between_braces)
{
    SCRATCH();
    
    Token starting_token = peek_token(parser);
    Array<Token> block_tokens = parser_get_state(parser)->tokens;
    
    if (between_braces && starting_token.kind == TokenKind_OpenBrace)
    {
        block_tokens = extract_tokens_with_depth(parser, TokenKind_OpenBrace, TokenKind_CloseBrace, true);
        b32 close_bracket_found = block_tokens.count >= 2;
        
        if (!close_bracket_found) {
            report_common_missing_closing_bracket(starting_token.code);
            return alloc_node(parser, OpKind_Error, starting_token.code);
        }
        
        block_tokens = array_subarray(block_tokens, 1, block_tokens.count - 2);
    }
    
    parser_push_state(parser, block_tokens);
    
    PooledArray<OpNode*> ops = pooled_array_make<OpNode*>(scratch.arena, 64);
    auto block_node = (OpNode_Block*)alloc_node(parser, OpKind_Block, starting_token.code);
    
    while (true)
    {
        OpNode* node = extract_op(parser);
        if (node == NULL) break;
        array_add(&ops, node);
    }
    
    block_node->ops = array_from_pooled_array(yov->static_arena, ops);
    
    parser_pop_state(parser);
    
    return block_node;
}

OpNode* extract_op(Parser* parser)
{
    skip_tokens_before_op(parser);
    
    ParserState* state = parser_get_state(parser);
    if (state->token_index >= state->tokens.count) return NULL;
    
    if (peek_token(parser).kind == TokenKind_ElseKeyword) {
        Token token = extract_token(parser);
        report_else_not_found_if(token.code);
        return alloc_node(parser, OpKind_Error, token.code);
    }
    
    OpKind kind = op_kind_from_tokens(parser);
    
    if (kind == OpKind_None || kind == OpKind_Error) {
        Token token = peek_token(parser);
        report_syntactic_unknown_op(token.code);
        skip_sentence(parser);
        return alloc_node(parser, OpKind_Error, token.code);
    }
    else {
        OpNode* node = NULL;
        if (kind == OpKind_Assignment) node = extract_assignment(parser);
        if (kind == OpKind_IfStatement) node = extract_if_statement(parser);
        if (kind == OpKind_WhileStatement) node = extract_while_statement(parser);
        if (kind == OpKind_ForStatement) node = extract_for_statement(parser);
        if (kind == OpKind_EnumDefinition) node = extract_enum_definition(parser);
        if (kind == OpKind_StructDefinition) node = extract_struct_definition(parser);
        if (kind == OpKind_ArgDefinition) node = extract_arg_definition(parser);
        if (kind == OpKind_FunctionDefinition) node = extract_function_definition(parser);
        if (kind == OpKind_FunctionCall) node = extract_function_call(parser);
        if (kind == OpKind_ObjectDefinition) node = extract_object_definition(parser);
        if (kind == OpKind_Return) node = extract_return(parser);
        if (kind == OpKind_Continue) node = extract_continue(parser);
        if (kind == OpKind_Break) node = extract_break(parser);
        if (kind == OpKind_Import) node = extract_import(parser);
        if (kind == OpKind_Block) node = extract_block(parser, true);
        assert(node != NULL);
        if (node == NULL) return NULL;
        
        if (node->kind == OpKind_Error) {
            skip_sentence(parser);
        }
        
        return node;
    }
    
    return NULL;
}

OpNode_Block* generate_ast(Array<Token> tokens) {
    Parser* parser = parser_alloc(tokens);
    return (OpNode_Block*)extract_block(parser, false);
}

Parser* parser_alloc(Array<Token> tokens)
{
    Parser* parser = arena_push_struct<Parser>(yov->temp_arena);
    parser->state_stack = pooled_array_make<ParserState>(yov->temp_arena, 8);
    parser_push_state(parser, tokens);
    return parser;
}

Array<OpNode_Import*> get_imports(Arena* arena, OpNode* ast)
{
    SCRATCH(arena);
    
    if (ast->kind != OpKind_Block) return {};
    OpNode_Block* node = (OpNode_Block*)ast;
    
    PooledArray<OpNode_Import*> to_resolve = pooled_array_make<OpNode_Import*>(scratch.arena, 8);
    
    foreach(i, node->ops.count) {
        OpNode* import0 = node->ops[i];
        if (import0->kind != OpKind_Import) continue;
        OpNode_Import* import = (OpNode_Import*)import0;
        array_add(&to_resolve, import);
    }
    
    return array_from_pooled_array(arena, to_resolve);
}

void log_ast(OpNode* node, i32 depth)
{
    SCRATCH();
    
    if (node == NULL) return;
    
    foreach(i, depth) print_info("  ");
    
    if (node->kind == OpKind_Block) print_info("block");
    else if (node->kind == OpKind_Error) print_error("error");
    else if (node->kind == OpKind_None) print_info("none");
    else if (node->kind == OpKind_IfStatement) print_info("if-statement");
    else if (node->kind == OpKind_WhileStatement) print_info("while-statement");
    else if (node->kind == OpKind_ForStatement) print_info("for-statement");
    else if (node->kind == OpKind_ForeachArrayStatement) print_info("foreach-statement");
    else if (node->kind == OpKind_Assignment) print_info("assignment");
    else if (node->kind == OpKind_FunctionCall) print_info("function call");
    else if (node->kind == OpKind_ObjectDefinition) {
        auto node0 = (OpNode_ObjectDefinition*)node;
        print_info("objdef: ");
        foreach(i, node0->names.count) {
            print_info("'%S'", node0->names[i]);
            if (i + 1 < node0->names.count) print_info(", ");
        }
    }
    else if (node->kind == OpKind_ObjectType) {
        auto node0 = (OpNode_ObjectType*)node;
        print_info("type: '%S%s", node0->name, node0->is_reference ? "&" : "");
        foreach(i, node0->array_dimensions) print_info("[]");
        print_info("'");
    }
    else if (node->kind == OpKind_Binary) {
        auto node0 = (OpNode_Binary*)node;
        print_info("binary %S", string_from_binary_operator(node0->op));
    }
    else if (node->kind == OpKind_Sign) {
        auto node0 = (OpNode_Sign*)node;
        print_info("sign %S", string_from_binary_operator(node0->op));
    }
    else if (node->kind == OpKind_IntLiteral) {
        auto node0 = (OpNode_NumericLiteral*)node;
        print_info("int literal: %u", node0->int_literal);
    }
    else if (node->kind == OpKind_StringLiteral) {
        auto node0 = (OpNode_StringLiteral*)node;
        print_info("str literal: %S", node0->raw_value);
    }
    else if (node->kind == OpKind_BoolLiteral) {
        auto node0 = (OpNode_NumericLiteral*)node;
        print_info("bool literal: %s", node0->bool_literal ? "true" : "false");
    }
    else if (node->kind == OpKind_Symbol) {
        auto node0 = (OpNode_Symbol*)node;
        print_info("Symbol: %S", node0->identifier);
    }
    else if (node->kind == OpKind_MemberValue) {
        auto node0 = (OpNode_MemberValue*)node;
        print_info("member_value: %S", node0->member);
    }
    else if (node->kind == OpKind_ArrayExpresion) { print_info("array expresion"); }
    else if (node->kind == OpKind_Indexing) print_info("indexing");
    else if (node->kind == OpKind_StructDefinition) {
        auto node0 = (OpNode_StructDefinition*)node;
        print_info("struct def: %S", node0->identifier);
    }
    else if (node->kind == OpKind_ArgDefinition) {
        auto node0 = (OpNode_ArgDefinition*)node;
        print_info("arg def: %S", node0->identifier);
    }
    else if (node->kind == OpKind_EnumDefinition) {
        auto node0 = (OpNode_EnumDefinition*)node;
        print_info("enum def: %S", node0->identifier);
    }
    else {
        assert(0);
    }
    
    print_info("\n");
    
    Array<OpNode*> childs = get_node_childs(scratch.arena, node);
    
    foreach(i, childs.count) {
        log_ast(childs[i], depth + 1);
    }
}