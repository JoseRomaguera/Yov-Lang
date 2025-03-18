#include "inc.h"

internal_fn OpKind op_kind_from_tokens(Array<Token> tokens)
{
    if (tokens.count == 0) return OpKind_None;
    
    Token t0 = tokens[0];
    Token t1 = (tokens.count > 1) ? tokens[1] : Token{};
    Token t2 = (tokens.count > 2) ? tokens[2] : Token{};
    Token t3 = (tokens.count > 3) ? tokens[3] : Token{};
    Token t4 = (tokens.count > 4) ? tokens[4] : Token{};
    
    if (t0.kind == TokenKind_IfKeyword) return OpKind_IfStatement;
    if (t0.kind == TokenKind_WhileKeyword) return OpKind_WhileStatement;
    if (t0.kind == TokenKind_ForKeyword) return OpKind_ForStatement;
    if (t0.kind == TokenKind_ReturnKeyword) return OpKind_Return;
    
    if (t0.kind == TokenKind_OpenBrace) return OpKind_Block;
    
    if (t0.kind == TokenKind_Identifier) {
        if (t1.kind == TokenKind_OpenParenthesis) return OpKind_FunctionCall;
        if (t1.kind == TokenKind_Assignment) return OpKind_VariableAssignment;
    }
    
    // Variable definition
    if (t0.kind == TokenKind_Identifier && t1.kind == TokenKind_Colon && t2.kind == TokenKind_Identifier) return OpKind_ObjectDefinition;
    if (t0.kind == TokenKind_Identifier && t1.kind == TokenKind_Colon && t2.kind == TokenKind_Assignment) return OpKind_ObjectDefinition;
    if (t0.kind == TokenKind_Identifier && t1.kind == TokenKind_Colon && t2.kind == TokenKind_OpenBracket) return OpKind_ObjectDefinition;
    
    // Array definition
    if (t0.kind == TokenKind_Identifier && t1.kind == TokenKind_OpenBracket && t2.kind == TokenKind_CloseBracket && t3.kind == TokenKind_Identifier) return OpKind_ObjectDefinition;
    
    // Array assignment
    if (t0.kind == TokenKind_Identifier && t1.kind == TokenKind_OpenBracket && t2.kind != TokenKind_CloseBracket) return OpKind_ArrayElementAssignment;
    
    if (t0.kind == TokenKind_Identifier && t1.kind == TokenKind_Colon && t2.kind == TokenKind_Colon) {
        if (t3.kind == TokenKind_EnumKeyword) return OpKind_EnumDefinition;
        if (t3.kind == TokenKind_OpenParenthesis) return OpKind_FunctionDefinition;
    }
    
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
    OpNode* node = (OpNode*)arena_push(parser->ctx->static_arena, node_size);
    node->kind = kind;
    node->code = code;
    return node;
}

internal_fn Array<OpNode*> process_parameters(Parser* parser, Array<Token> tokens)
{
    SCRATCH();
    if (tokens.count == 0) return {};
    
    Array<Array<Token>> parameters = split_tokens_in_parameters(scratch.arena, tokens);
    Array<OpNode*> nodes = array_make<OpNode*>(parser->ctx->static_arena, parameters.count);
    
    foreach(i, parameters.count) {
        Array<Token> parameter_tokens = parameters[i];
        nodes[i] = process_expresion(parser, parameter_tokens);
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

OpNode* process_expresion(Parser* parser, Array<Token> tokens)
{
    SCRATCH();
    
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
    
    // Array
    if (tokens.count >= 2 && tokens[0].kind == TokenKind_OpenBrace && tokens[tokens.count - 1].kind == TokenKind_CloseBrace)
    {
        b32 couple = check_tokens_are_couple(tokens, 0, tokens.count - 1, TokenKind_OpenBrace, TokenKind_CloseBrace);
        
        if (couple) {
            Array<Token> parameters = array_subarray(tokens, 1, tokens.count - 2);
            Array<OpNode*> expresions = process_parameters(parser, parameters);
            
            auto node = (OpNode_ArrayExpresion*)alloc_node(parser, OpKind_ArrayExpresion, tokens[0].code);
            node->nodes = expresions;
            return node;
        }
    }
    
    // Function call
    if (tokens.count >= 2 && tokens[0].kind == TokenKind_Identifier && tokens[1].kind == TokenKind_OpenParenthesis && tokens[tokens.count - 1].kind == TokenKind_CloseParenthesis) {
        b32 couple = check_tokens_are_couple(tokens, 1, tokens.count - 1, TokenKind_OpenParenthesis, TokenKind_CloseParenthesis);
        if (couple) return process_function_call(parser, tokens);
    }
    
    // Array indexing
    if (tokens.count >= 3 && tokens[0].kind == TokenKind_Identifier && tokens[1].kind == TokenKind_OpenBracket && tokens[tokens.count - 1].kind == TokenKind_CloseBracket) {
        b32 couple = check_tokens_are_couple(tokens, 1, tokens.count - 1, TokenKind_OpenBracket, TokenKind_CloseBracket);
        if (couple) {
            Array<Token> indexing_expresion = array_subarray(tokens, 2, tokens.count - 3);
            OpNode* expresion = process_expresion(parser, indexing_expresion);
            
            if (expresion->kind == OpKind_None) {
                report_array_indexing_expects_expresion(tokens[0].code);
                return alloc_node(parser, OpKind_Error, tokens[0].code);
            }
            
            auto node = (OpNode_ArrayElementValue*)alloc_node(parser, OpKind_ArrayElementValue, tokens[0].code);
            node->identifier = tokens[0].value;
            node->expresion = expresion;
            return node;
        }
    }
    
    // Member access
    if (tokens.count >= 2 && tokens[tokens.count - 2].kind == TokenKind_Dot && tokens[tokens.count - 1].kind == TokenKind_Identifier)
    {
        String member_value = tokens[tokens.count - 1].value;
        
        if (member_value.size == 0) {
            report_expr_empty_member(tokens[0].code);
            return alloc_node(parser, OpKind_Error, tokens[0].code);
        }
        
        Array<Token> expresion_tokens = array_subarray(tokens, 0, tokens.count - 2);
        OpNode* expresion = process_expresion(parser, expresion_tokens);
        
        auto node = (OpNode_MemberValue*)alloc_node(parser, OpKind_MemberValue, tokens[0].code);
        node->expresion = expresion;
        node->member = member_value;
        return node;
    }
    
    if (tokens.count == 1)
    {
        Token token = tokens[0];
        
        if (token.kind == TokenKind_IntLiteral) {
            u32 v;
            if (u32_from_string(&v, token.value)) {
                auto node = (OpNode_Literal*)alloc_node(parser, OpKind_IntLiteral, token.code);
                node->int_literal = v;
                return node;
            }
            else {
                assert(0);
            }
        }
        else if (token.kind == TokenKind_StringLiteral) {
            auto node = (OpNode_Literal*)alloc_node(parser, OpKind_StringLiteral, token.code);
            node->string_literal = token.value;
            return node;
        }
        else if (token.kind == TokenKind_BoolLiteral) {
            auto node = (OpNode_Literal*)alloc_node(parser, OpKind_BoolLiteral, token.code);
            node->bool_literal = (b8)string_equals(token.value, STR("true"));
            return node;
        }
        else if (token.kind == TokenKind_Identifier) {
            auto node = (OpNode_IdentifierValue*)alloc_node(parser, OpKind_IdentifierValue, token.code);
            node->identifier = token.value;
            return node;
        }
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
        const i32 depth_mult = 7;
        
        i32 parenthesis_depth = 0;
        i32 braces_depth = 0;
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
            
            b32 is_sign = false;
            
            i32 preference = i32_max;
            if (token.kind == TokenKind_BinaryOperator)
            {
                if (left_token.kind == TokenKind_BinaryOperator) is_sign = true;
                if (left_token.kind == TokenKind_OpenParenthesis) is_sign = true;
                if (left_token.kind == TokenKind_None) is_sign = true;
                
                if (is_sign) {
                    preference = sign_preference;
                }
                else {
                    if (token.binary_operator == BinaryOperator_Addition) preference = addition_preference;
                    else if (token.binary_operator == BinaryOperator_Substraction) preference = addition_preference;
                    else if (token.binary_operator == BinaryOperator_Multiplication) preference = multiplication_preference;
                    else if (token.binary_operator == BinaryOperator_Division) preference = multiplication_preference;
                    else if (token.binary_operator == BinaryOperator_Modulo) preference = multiplication_preference;
                    else if (token.binary_operator == BinaryOperator_LogicalNot) preference = logical_preference;
                    else if (token.binary_operator == BinaryOperator_LogicalOr) preference = logical_preference;
                    else if (token.binary_operator == BinaryOperator_LogicalAnd) preference = logical_preference;
                    else if (token.binary_operator == BinaryOperator_Equals) preference = boolean_preference;
                    else if (token.binary_operator == BinaryOperator_NotEquals) preference = boolean_preference;
                    else if (token.binary_operator == BinaryOperator_LessThan) preference = boolean_preference;
                    else if (token.binary_operator == BinaryOperator_LessEqualsThan) preference = boolean_preference;
                    else if (token.binary_operator == BinaryOperator_GreaterThan) preference = boolean_preference;
                    else if (token.binary_operator == BinaryOperator_GreaterEqualsThan) preference = boolean_preference;
                    else {
                        assert(0);
                    }
                }
            }
            
            if (preference == i32_max) continue;
            
            preference += parenthesis_depth * depth_mult;
            preference += braces_depth * depth_mult;
            
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
            
            if (op_token.kind == TokenKind_BinaryOperator)
            {
                if (preference_is_sign)
                {
                    assert(preferent_operator_index == 0);
                    BinaryOperator op = tokens[preferent_operator_index].binary_operator;
                    
                    auto node = (OpNode_Sign*)alloc_node(parser, OpKind_Sign, tokens[preferent_operator_index].code);
                    node->op = op;
                    node->expresion = process_expresion(parser, array_subarray(tokens, 1, tokens.count - 1));
                    return node;
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
                        OpNode* left_expresion = process_expresion(parser, array_subarray(tokens, 0, preferent_operator_index));
                        OpNode* right_expresion = process_expresion(parser, array_subarray(tokens, preferent_operator_index + 1, tokens.count - (preferent_operator_index + 1)));
                        
                        auto node = (OpNode_Binary*)alloc_node(parser, OpKind_Binary, tokens[0].code);
                        node->left = left_expresion;
                        node->right = right_expresion;
                        node->op = op_token.binary_operator;
                        return node;
                    }
                }
            }
        }
    }
    
    String expresion_string = string_from_tokens(scratch.arena, tokens);
    report_expr_syntactic_unknown(tokens[0].code, expresion_string);
    return alloc_node(parser, OpKind_Error, tokens[0].code);
}

internal_fn OpNode* extract_if_statement(Parser* parser)
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
    OpNode* expresion_node = process_expresion(parser, expresion);
    
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

internal_fn OpNode* extract_while_statement(Parser* parser)
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
    OpNode* expresion_node = process_expresion(parser, expresion);
    
    auto node = (OpNode_WhileStatement*)alloc_node(parser, OpKind_WhileStatement, starting_token.code);
    node->expresion = expresion_node;
    node->content = extract_op(parser);
    
    return node;
}

internal_fn OpNode* extract_for_statement(Parser* parser)
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
            if (separator_count >= array_count(separator_indices)) {
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
        node->expresion = process_expresion(parser, expresion_tokens);
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
        
        node->condition_expresion = process_expresion(parser, condition_expresion_tokens);
        
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

internal_fn OpNode* extract_enum_definition(Parser* parser)
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
        
        if (assignment_token.kind == TokenKind_Assignment && assignment_token.binary_operator == BinaryOperator_None) {
            extract_token(parser);
            // TODO(Jose): // TODO(Jose): Unitl: CloseBrace or Comma
            Array<Token> expresion_tokens = extract_tokens_until(parser, false, TokenKind_CloseBrace, TokenKind_Comma);
            
            OpNode* node = process_expresion(parser, expresion_tokens);
            
            if (node->kind == OpKind_Error) return alloc_node(parser, OpKind_Error, starting_token.code);
            if (node->kind == OpKind_None) {
                report_common_expecting_valid_expresion(name_token.code);
                return alloc_node(parser, OpKind_Error, starting_token.code);
            }
            
            value = node;
        }
        
        Token comma_token = extract_token(parser);
        if (comma_token.kind == TokenKind_CloseBrace) break;
        if (comma_token.kind != TokenKind_Comma) {
            report_enumdef_expecting_comma_separated_identifier(comma_token.code);
            return alloc_node(parser, OpKind_Error, starting_token.code);
        }
        
        array_add(&names, name_token.value);
        array_add(&values, value);
    }
    while (true);
    
    OpNode_EnumDefinition* node = (OpNode_EnumDefinition*)alloc_node(parser, OpKind_EnumDefinition, starting_token.code);
    node->identifier = identifier_token.value;
    node->names = array_from_pooled_array(parser->ctx->static_arena, names);
    node->values = array_from_pooled_array(parser->ctx->static_arena, values);
    return node;
}

internal_fn OpNode* extract_function_definition(Parser* parser)
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
    
    // Parameters
    if (parameter_tokens.count > 2)
    {
        parameter_tokens = array_subarray(parameter_tokens, 1, parameter_tokens.count - 2);
        
        Array<Array<Token>> parameters = split_tokens_in_parameters(scratch.arena, parameter_tokens);
        params = array_make<OpNode_ObjectDefinition*>(parser->ctx->static_arena, parameters.count);
        
        u32 param_index = 0;
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
            
            params[param_index++] = (OpNode_ObjectDefinition*)node;
        }
        
        if (!valid) {
            return alloc_node(parser, OpKind_Error, starting_token.code);
        }
    }
    
    OpNode* return_node;
    
    // Return definition
    if (peek_token(parser).kind == TokenKind_Arrow) {
        extract_token(parser);
        
        Token return_start_node = peek_token(parser);
        
        return_node = extract_object_type(parser);
        if (return_node->kind != OpKind_ObjectType) {
            return alloc_node(parser, OpKind_Error, return_start_node.code);
        }
    }
    else return_node = alloc_node(parser, OpKind_None, starting_token.code);
    
    if (peek_token(parser).kind != TokenKind_OpenBrace) {
        report_common_missing_opening_brace(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    Array<Token> block_tokens = extract_tokens_with_depth(parser, TokenKind_OpenBrace, TokenKind_CloseBrace, true);
    
    if (block_tokens.count < 2) {
        report_common_missing_closing_brace(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    OpNode_Block* block;
    if (block_tokens.count == 2) {
        block = (OpNode_Block*)alloc_node(parser, OpKind_Block, block_tokens[0].code);
    }
    else {
        block_tokens = array_subarray(block_tokens, 1, block_tokens.count - 2);
        
        parser_push_state(parser, block_tokens);
        block = (OpNode_Block*)extract_block(parser);
        parser_pop_state(parser);
        
        if (block->kind != OpKind_Block) {
            report_common_missing_block(block_tokens[0].code);
            return alloc_node(parser, OpKind_Error, starting_token.code);
        }
    }
    
    OpNode_FunctionDefinition* node = (OpNode_FunctionDefinition*)alloc_node(parser, OpKind_FunctionDefinition, starting_token.code);
    node->identifier = identifier_token.value;
    node->block = block;
    node->parameters = params;
    node->return_node = return_node;
    return node;
}

internal_fn OpNode* extract_variable_assignment(Parser* parser)
{
    Token starting_token = peek_token(parser);
    
    Array<Token> sentence = extract_tokens_until(parser, false, TokenKind_NextSentence);
    
    assert(sentence.count >= 2);
    if (sentence.count < 2) return alloc_node(parser, OpKind_Error, starting_token.code);
    
    Array<Token> expresion = array_subarray(sentence, 2, sentence.count - 2);
    OpNode* value_node = process_expresion(parser, expresion);
    
    if (value_node->kind == OpKind_Error) {
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    if (value_node->kind == OpKind_None) {
        report_expr_is_empty(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    auto node = (OpNode_Assignment*)alloc_node(parser, OpKind_VariableAssignment, starting_token.code);
    node->value = value_node;
    node->identifier = starting_token.value;
    node->binary_operator = sentence[1].binary_operator;
    return node;
}

internal_fn OpNode* extract_array_element_assignment(Parser* parser)
{
    Token starting_token = peek_token(parser);
    Array<Token> sentence = extract_tokens_until(parser, false, TokenKind_NextSentence);
    
    assert(sentence.count >= 6);
    if (sentence.count < 6) return alloc_node(parser, OpKind_Error, starting_token.code);
    
    i32 operator_index = -1;
    
    for (i32 i = 1; i < sentence.count; ++i) {
        if (sentence[i].kind == TokenKind_Assignment) {
            operator_index = i;
        }
    }
    
    if (operator_index < 0) {
        report_assign_operator_not_found(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    
    Array<Token> indexing_tokens = array_subarray(sentence, 1, operator_index - 1);
    if (indexing_tokens.count < 3 || indexing_tokens[0].kind != TokenKind_OpenBracket || indexing_tokens[indexing_tokens.count - 1].kind != TokenKind_CloseBracket) {
        report_common_missing_closing_bracket(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    indexing_tokens = array_subarray(indexing_tokens, 1, indexing_tokens.count - 2);
    OpNode* indexing_node = process_expresion(parser, indexing_tokens);
    
    Array<Token> value_tokens = array_subarray(sentence, operator_index + 1, sentence.count - (operator_index + 1));
    OpNode* value_node = process_expresion(parser, value_tokens);
    
    if (value_node->kind == OpKind_Error) {
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    if (value_node->kind == OpKind_None) {
        report_expr_is_empty(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    auto node = (OpNode_ArrayElementAssignment*)alloc_node(parser, OpKind_ArrayElementAssignment, starting_token.code);
    node->identifier = starting_token.value;
    node->value = value_node;
    node->indexing_expresion = indexing_node;
    node->binary_operator = sentence[operator_index].binary_operator;
    return node;
}

OpNode* extract_object_type(Parser* parser)
{
    String name{};
    b32 is_array = false;
    Array<OpNode*> array_dimensions{};
    
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
        is_array = true;
        
        i32 starting_cursor = parser_get_state(parser)->token_index;
        u32 dimensions = 0;
        
        while (peek_token(parser).kind == TokenKind_OpenBracket) {
            Array<Token> tokens = extract_tokens_with_depth(parser, TokenKind_OpenBracket, TokenKind_CloseBracket, true);
            if (tokens.count < 2) {
                report_common_missing_closing_bracket(starting_token.code);
                return alloc_node(parser, OpKind_Error, starting_token.code);
            }
            dimensions++;
        }
        
        parser_get_state(parser)->token_index = starting_cursor;
        
        // Check array dimensions
        {
            array_dimensions = array_make<OpNode*>(parser->ctx->static_arena, dimensions);
            
            foreach(i, dimensions)
            {
                Array<Token> tokens = extract_tokens_with_depth(parser, TokenKind_OpenBracket, TokenKind_CloseBracket, true);
                assert(tokens.count >= 2);
                tokens = array_subarray(tokens, 1, tokens.count - 2);
                array_dimensions[i] = process_expresion(parser, tokens);
            }
        }
    }
    
    OpNode_ObjectType* node = (OpNode_ObjectType*)alloc_node(parser, OpKind_ObjectType, starting_token.code);
    node->name = name;
    node->is_array = (b8)is_array;
    node->array_dimensions = array_dimensions;
    return node;
}

OpNode* extract_object_definition(Parser* parser)
{
    Token starting_token = peek_token(parser);
    
    Token identifier_token = extract_token(parser);
    
    Token colon_token = extract_token(parser);
    if (colon_token.kind != TokenKind_Colon) {
        report_objdef_expecting_colon(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    OpNode_ObjectType* node_type = (OpNode_ObjectType*)alloc_node(parser, OpKind_ObjectType, starting_token.code);
    
    Token type_or_assignment_token = peek_token(parser);
    
    if (type_or_assignment_token.kind == TokenKind_Assignment) {}
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
    
    if (assignment_token.kind != TokenKind_NextSentence && assignment_token.kind != TokenKind_None)
    {
        if (assignment_token.kind != TokenKind_Assignment || assignment_token.binary_operator != BinaryOperator_None) {
            report_objdef_expecting_assignment(starting_token.code);
            return alloc_node(parser, OpKind_Error, starting_token.code);
        }
        
        Array<Token> assignment_tokens = extract_tokens_until(parser, false, TokenKind_NextSentence);
        assignment_node = process_expresion(parser, assignment_tokens);
        
        if (assignment_node->kind == OpKind_None) {
            report_common_expecting_valid_expresion(starting_token.code);
            return alloc_node(parser, OpKind_Error, starting_token.code);
        }
        
        foreach(i, node_type->array_dimensions.count) {
            if (node_type->array_dimensions[i]->kind != OpKind_None) {
                report_objdef_redundant_array_dimensions(starting_token.code);
                return alloc_node(parser, OpKind_Error, starting_token.code);
            }
        }
    }
    else {
        assignment_node = alloc_node(parser, OpKind_None, starting_token.code);
    }
    
    auto node = (OpNode_ObjectDefinition*)alloc_node(parser, OpKind_ObjectDefinition, starting_token.code);
    node->assignment = assignment_node;
    node->type = node_type;
    node->object_name = identifier_token.value;
    
    return node;
}

internal_fn OpNode* extract_function_call(Parser* parser)
{
    Token starting_token = peek_token(parser);
    Array<Token> sentence = extract_tokens_with_depth(parser, TokenKind_OpenParenthesis, TokenKind_CloseParenthesis, true);
    return process_function_call(parser, sentence);
}

internal_fn OpNode* extract_return(Parser* parser)
{
    Token return_token = extract_token(parser);
    assert(return_token.kind == TokenKind_ReturnKeyword);
    
    Array<Token> expresion_tokens = extract_tokens_until(parser, false, TokenKind_NextSentence);
    OpNode* expresion = process_expresion(parser, expresion_tokens);
    
    OpNode_Return* node = (OpNode_Return*)alloc_node(parser, OpKind_Return, return_token.code);
    node->expresion = expresion;
    return node;
}

OpNode* extract_block(Parser* parser)
{
    SCRATCH();
    
    Token starting_token = peek_token(parser);
    Array<Token> block_tokens = parser_get_state(parser)->tokens;
    
    if (starting_token.kind == TokenKind_OpenBrace)
    {
        block_tokens = extract_tokens_with_depth(parser, TokenKind_OpenBrace, TokenKind_CloseBrace, true);
        b32 close_bracket_found = block_tokens.count >= 2;
        
        if (!close_bracket_found) {
            report_common_missing_closing_bracket(starting_token.code);
            return alloc_node(parser, OpKind_Error, starting_token.code);
        }
        
        block_tokens = array_subarray(block_tokens, 1, block_tokens.count - 2);
    }
    
    // Empty block
    if (block_tokens.count == 0) {
        return alloc_node(parser, OpKind_Block, starting_token.code);
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
    
    block_node->ops = array_from_pooled_array(parser->ctx->static_arena, ops);
    
    parser_pop_state(parser);
    
    return block_node;
}

OpNode* extract_op(Parser* parser)
{
    skip_tokens_before_op(parser);
    
    ParserState* state = parser_get_state(parser);
    if (state->token_index >= state->tokens.count) return NULL;
    
    Array<Token> analyze_tokens = peek_tokens(parser, 0, 5);
    
    if (analyze_tokens.count > 0 && analyze_tokens[0].kind == TokenKind_ElseKeyword) {
        report_else_not_found_if(analyze_tokens[0].code);
        state->token_index++;
        return alloc_node(parser, OpKind_Error, analyze_tokens[0].code);
    }
    
    OpKind kind = op_kind_from_tokens(analyze_tokens);
    
    if (kind == OpKind_None || kind == OpKind_Error) {
        report_syntactic_unknown_op(state->tokens[state->token_index].code);
        skip_sentence(parser);
        return alloc_node(parser, OpKind_Error, analyze_tokens[0].code);
    }
    else {
        OpNode* node = NULL;
        if (kind == OpKind_IfStatement) node = extract_if_statement(parser);
        if (kind == OpKind_WhileStatement) node = extract_while_statement(parser);
        if (kind == OpKind_ForStatement) node = extract_for_statement(parser);
        if (kind == OpKind_EnumDefinition) node = extract_enum_definition(parser);
        if (kind == OpKind_FunctionDefinition) node = extract_function_definition(parser);
        if (kind == OpKind_VariableAssignment) node = extract_variable_assignment(parser);
        if (kind == OpKind_ArrayElementAssignment) node = extract_array_element_assignment(parser);
        if (kind == OpKind_FunctionCall) node = extract_function_call(parser);
        if (kind == OpKind_ObjectDefinition) node = extract_object_definition(parser);
        if (kind == OpKind_Return) node = extract_return(parser);
        if (kind == OpKind_Block) node = extract_block(parser);
        assert(node != NULL);
        if (node == NULL) return NULL;
        
        if (node->kind == OpKind_Error) {
            skip_sentence(parser);
        }
        
        return node;
    }
    
    return NULL;
}

OpNode* generate_ast(Yov* ctx, Array<Token> tokens, b32 is_block) {
    Parser* parser = arena_push_struct<Parser>(ctx->temp_arena);
    parser->ctx = ctx;
    parser->state_stack = pooled_array_make<ParserState>(ctx->temp_arena, 8);
    
    parser_push_state(parser, tokens);
    if (is_block) return extract_block(parser);
    else return extract_op(parser);
}