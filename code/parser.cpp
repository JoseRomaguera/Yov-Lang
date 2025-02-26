#include "inc.h"

internal_fn OpKind op_kind_from_one_token(Token token)
{
    if (token.kind == TokenKind_Keyword) {
        if (token.keyword == KeywordType_If) return OpKind_IfStatement;
        if (token.keyword == KeywordType_While) return OpKind_WhileStatement;
        if (token.keyword == KeywordType_For) return OpKind_ForStatement;
    }
    else if (token.kind == TokenKind_OpenBrace) {
        return OpKind_Block;
    }
    
    return OpKind_None;
}

internal_fn OpKind op_kind_from_two_tokens(Token token0, Token token1)
{
    if (token0.kind == TokenKind_Identifier) {
        if (token1.kind == TokenKind_OpenParenthesis) return OpKind_FunctionCall;
        if (token1.kind == TokenKind_Assignment) return OpKind_VariableAssignment;
    }
    
    return OpKind_None;
}

internal_fn OpKind op_kind_from_tokens(Array<Token> t)
{
    if (t.count == 0) return OpKind_None;
    OpKind kind = op_kind_from_one_token(t[0]);
    if (kind == OpKind_None && t.count >= 2) kind = op_kind_from_two_tokens(t[0], t[1]);
    if (kind != OpKind_None) return kind;
    
    // Variable definition
    if (t.count >= 3 && t[0].kind == TokenKind_Identifier && t[1].kind == TokenKind_Colon && t[2].kind == TokenKind_Identifier) return OpKind_VariableDefinition;
    if (t.count >= 3 && t[0].kind == TokenKind_Identifier && t[1].kind == TokenKind_Colon && t[2].kind == TokenKind_Assignment) return OpKind_VariableDefinition;
    if (t.count >= 3 && t[0].kind == TokenKind_Identifier && t[1].kind == TokenKind_Colon && t[2].kind == TokenKind_OpenBracket) return OpKind_VariableDefinition;
    
    // Array definition
    if (t.count >= 4) {
        if (t[0].kind == TokenKind_Identifier && t[1].kind == TokenKind_OpenBracket && t[2].kind == TokenKind_CloseBracket && t[3].kind == TokenKind_Identifier) return OpKind_VariableDefinition;
    }
    
    // Array assignment
    if (t.count >= 5) {
        if (t[0].kind == TokenKind_Identifier && t[1].kind == TokenKind_OpenBracket && t[2].kind != TokenKind_CloseBracket) return OpKind_ArrayElementAssignment;
    }
    
    return OpKind_None;
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
    i32 index = (i32)parser->token_index + offset;
    if (index < 0 || index >= parser->tokens.count) return {};
    return parser->tokens[index];
}

Array<Token> peek_tokens(Parser* parser, i32 offset, u32 count) {
    i32 index = parser->token_index + offset;
    if (index < 0 || index >= parser->tokens.count) return {};
    count = MIN(count, parser->tokens.count - index);
    return array_subarray(parser->tokens, index, count);
}

void skip_tokens(Parser* parser, u32 count)
{
    if (parser->token_index + count > parser->tokens.count) {
        assert(0);
        parser->token_index = parser->tokens.count;
        return;
    }
    parser->token_index += count;
}

void skip_tokens_before_op(Parser* parser)
{
    while (parser->token_index < parser->tokens.count) {
        Token token = parser->tokens[parser->token_index];
        if (token.kind != TokenKind_NextLine && token.kind != TokenKind_NextSentence) break;
        parser->token_index++;
    }
}

Token extract_token(Parser* parser)
{
    if (parser->token_index >= parser->tokens.count) {
        assert(0);
        return {};
    }
    return parser->tokens[parser->token_index++];
}

Array<Token> extract_tokens(Parser* parser, u32 count)
{
    if (parser->token_index + count >= parser->tokens.count) {
        assert(0);
        return {};
    }
    Array<Token> tokens = array_subarray(parser->tokens, parser->token_index, count);
    parser->token_index += count;
    return tokens;
}

Array<Token> extract_tokens_until(Parser* parser, TokenKind separator, b32 require_separator)
{
    u32 starting_index = parser->token_index;
    while (parser->token_index < parser->tokens.count) {
        if (parser->tokens[parser->token_index].kind == separator) break;
        parser->token_index++;
    }
    if (require_separator && parser->token_index < parser->tokens.count) {
        parser->token_index++;
    }
    return array_subarray(parser->tokens, starting_index, parser->token_index - starting_index);
}

Array<Token> extract_tokens_with_depth(Parser* parser, TokenKind open_token, TokenKind close_token, b32 require_separator)
{
    u32 starting_index = parser->token_index;
    i32 depth = 0;
    
    while (parser->token_index < parser->tokens.count) {
        Token token = parser->tokens[parser->token_index];
        if (token.kind == open_token) depth++;
        else if (token.kind == close_token) {
            depth--;
            if (depth == 0) break;
        }
        parser->token_index++;
    }
    
    u32 end_index = parser->token_index;
    if (parser->token_index < parser->tokens.count) {
        if (require_separator) end_index++;
        parser->token_index++;
    }
    
    return (depth == 0) ? array_subarray(parser->tokens, starting_index, end_index - starting_index) : Array<Token>{};
}

inline_fn OpNode* alloc_node(Parser* parser, OpKind kind, CodeLocation code)
{
    u32 node_size = get_node_size(kind);
    OpNode* node = (OpNode*)arena_push(parser->ctx->static_arena, node_size);
    node->kind = kind;
    node->code = code;
    return node;
}

inline_fn Array<OpNode*> process_parameters(Parser* parser, Array<Token> tokens)
{
    if (tokens.count == 0) return {};
    
    u32 parameter_count = 1;
    foreach(i, tokens.count) if (tokens[i].kind == TokenKind_Comma) parameter_count++;
    Array<OpNode*> nodes = array_make<OpNode*>(parser->ctx->static_arena, parameter_count);
    
    u32 parameter_index = 0;
    u32 start_parameter_token_index = 0;
    u32 index = 0;
    
    while (true)
    {
        if (index >= tokens.count || tokens[index].kind == TokenKind_Comma) {
            Array<Token> parameter_tokens = array_subarray(tokens, start_parameter_token_index, index - start_parameter_token_index);
            nodes[parameter_index++] = process_expresion(parser, parameter_tokens);
            start_parameter_token_index = index + 1;
        }
        
        if (index >= tokens.count) break;
        
        index++;
    }
    
    assert(parameter_index == parameter_count);
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
    if (tokens.count == 3 && tokens[0].kind == TokenKind_Identifier && tokens[1].kind == TokenKind_Dot && tokens[2].kind == TokenKind_Identifier)
    {
        String member_value = tokens[2].value;
        
        if (member_value.size == 0) {
            report_expr_empty_member(tokens[0].code);
            return alloc_node(parser, OpKind_Error, tokens[0].code);
        }
        
        auto node = (OpNode_MemberValue*)alloc_node(parser, OpKind_MemberValue, tokens[0].code);
        node->identifier = tokens[0].value;
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

inline_fn OpNode* extract_if_statement(Parser* parser)
{
    Token starting_token = parser->tokens[parser->token_index];
    
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
    if (parser->token_index < parser->tokens.count) {
        Token else_token = parser->tokens[parser->token_index];
        if (else_token.kind == TokenKind_Keyword && else_token.keyword == KeywordType_Else) {
            parser->token_index++; // Skip else
            node->failure = extract_op(parser);
        }
    }
    
    if (node->failure == NULL) 
        node->failure = alloc_node(parser, OpKind_None, starting_token.code);
    
    return node;
}

inline_fn OpNode* extract_while_statement(Parser* parser)
{
    Token starting_token = parser->tokens[parser->token_index];
    
    Array<Token> sentence = extract_tokens_until(parser, TokenKind_CloseParenthesis, true);
    
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

inline_fn OpNode* extract_for_statement(Parser* parser)
{
    Token starting_token = parser->tokens[parser->token_index];
    
    Array<Token> sentence = extract_tokens_until(parser, TokenKind_CloseParenthesis, true);
    
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
        node->initialize_sentence = generate_ast_from_sentence(parser->ctx, initialize_sentence_tokens);
        node->condition_expresion = process_expresion(parser, condition_expresion_tokens);
        node->update_sentence = generate_ast_from_sentence(parser->ctx, update_sentence_tokens);
        node->content = extract_op(parser);
        return node;
    }
    else
    {
        report_for_unknown(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
}

inline_fn OpNode* extract_variable_assignment(Parser* parser)
{
    Token starting_token = parser->tokens[parser->token_index];
    
    Array<Token> sentence = extract_tokens_until(parser, TokenKind_NextSentence, false);
    
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

inline_fn OpNode* extract_array_element_assignment(Parser* parser)
{
    Token starting_token = parser->tokens[parser->token_index];
    Array<Token> sentence = extract_tokens_until(parser, TokenKind_NextSentence, false);
    
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

inline_fn OpNode* extract_variable_definition(Parser* parser)
{
    Token starting_token = parser->tokens[parser->token_index];
    
    Token identifier_token = extract_token(parser);
    
    Token colon_token = extract_token(parser);
    assert(colon_token.kind == TokenKind_Colon);
    
    String type_identifier = STR("");
    b32 type_is_array = false;
    Array<OpNode*> array_dimensions{};
    
    Token type_or_assignment_token = peek_token(parser);
    
    if (type_or_assignment_token.kind == TokenKind_Assignment) {}
    else if (type_or_assignment_token.kind == TokenKind_Identifier)
    {
        type_identifier = type_or_assignment_token.value;
        
        skip_tokens(parser, 1);
        Token open_bracket = peek_token(parser, 0);
        
        if (open_bracket.kind == TokenKind_OpenBracket)
        {
            type_is_array = true;
            
            u32 index = 0;
            i32 depth = 0;
            u32 dimensions = 0;
            while (1) {
                Token t = peek_token(parser, index);
                if (t.kind == TokenKind_OpenBracket) {
                    depth++;
                    dimensions++;
                }
                else if (t.kind == TokenKind_CloseBracket) depth--;
                else if (t.kind == TokenKind_NextSentence) break;
                else if (t.kind == TokenKind_Assignment) break;
                index++;
            }
            
            if (depth > 0) {
                report_common_missing_closing_bracket(starting_token.code);
                return alloc_node(parser, OpKind_Error, starting_token.code);
            }
            if (depth < 0) {
                report_common_missing_opening_bracket(starting_token.code);
                return alloc_node(parser, OpKind_Error, starting_token.code);
            }
            
            Array<Token> array_tokens = extract_tokens(parser, index);
            
            // Check array dimensions
            {
                array_dimensions = array_make<OpNode*>(parser->ctx->static_arena, dimensions);
                
                u32 dimension = 0;
                u32 last_dimension_index = 0;
                
                foreach(i, array_tokens.count) {
                    Token t = array_tokens[i];
                    if (t.kind == TokenKind_CloseBracket)
                    {
                        Array<Token> dimension_tokens = array_subarray(array_tokens, last_dimension_index + 1, i - (last_dimension_index + 1));
                        array_dimensions[dimension] = process_expresion(parser, dimension_tokens);
                        
                        last_dimension_index = i + 1;
                        dimension++;
                        continue;
                    }
                }
            }
        }
    }
    else {
        report_objdef_expecting_type_identifier(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    Token assignment_token = extract_token(parser);
    
    OpNode* assignment_node = NULL;
    
    if (assignment_token.kind != TokenKind_NextSentence)
    {
        if (assignment_token.kind != TokenKind_Assignment || assignment_token.binary_operator != BinaryOperator_None) {
            report_objdef_expecting_assignment(starting_token.code);
            return alloc_node(parser, OpKind_Error, starting_token.code);
        }
        
        Array<Token> assignment_tokens = extract_tokens_until(parser, TokenKind_NextSentence, false);
        assignment_node = process_expresion(parser, assignment_tokens);
        
        if (assignment_node->kind == OpKind_None) {
            report_common_expecting_valid_expresion(starting_token.code);
            return alloc_node(parser, OpKind_Error, starting_token.code);
        }
        
        foreach(i, array_dimensions.count) {
            if (array_dimensions[i]->kind != OpKind_None) {
                report_objdef_redundant_array_dimensions(starting_token.code);
                return alloc_node(parser, OpKind_Error, starting_token.code);
            }
        }
    }
    else {
        assignment_node = alloc_node(parser, OpKind_None, starting_token.code);
    }
    
    auto node = (OpNode_VariableDefinition*)alloc_node(parser, OpKind_VariableDefinition, starting_token.code);
    node->assignment = assignment_node;
    node->type = type_identifier;
    node->identifier = identifier_token.value;
    node->is_array = (b8)type_is_array;
    node->array_dimensions = array_dimensions;
    
    return node;
}

inline_fn OpNode* extract_function_call(Parser* parser)
{
    Token starting_token = parser->tokens[parser->token_index];
    Array<Token> sentence = extract_tokens_with_depth(parser, TokenKind_OpenParenthesis, TokenKind_CloseParenthesis, true);
    return process_function_call(parser, sentence);
}

inline_fn OpNode* extract_block(Parser* parser)
{
    Token starting_token = parser->tokens[parser->token_index];
    Array<Token> block_tokens = extract_tokens_with_depth(parser, TokenKind_OpenBrace, TokenKind_CloseBrace, true);
    b32 close_bracket_found = block_tokens.count > 2;
    
    if (!close_bracket_found) {
        report_common_missing_closing_bracket(starting_token.code);
        return alloc_node(parser, OpKind_Error, starting_token.code);
    }
    
    assert(block_tokens.count >= 2);
    Array<Token> block_tokens_without_brakets = array_subarray(block_tokens, 1, block_tokens.count - 2);
    return generate_ast_from_block(parser->ctx, block_tokens_without_brakets);
}

void skip_sentence(Parser* parser)
{
    while (parser->token_index < parser->tokens.count) {
        Token token = parser->tokens[parser->token_index++];
        if (token.kind == TokenKind_NextSentence) break;
    }
}

OpNode* extract_op(Parser* parser)
{
    skip_tokens_before_op(parser);
    
    if (parser->token_index >= parser->tokens.count) return NULL;
    
    Array<Token> analyze_tokens = peek_tokens(parser, 0, 5);
    
    if (analyze_tokens.count > 0 && analyze_tokens[0].kind == TokenKind_Keyword && analyze_tokens[0].keyword == KeywordType_Else) {
        report_else_not_found_if(analyze_tokens[0].code);
        parser->token_index++;
        return alloc_node(parser, OpKind_Error, analyze_tokens[0].code);
    }
    
    OpKind kind = op_kind_from_tokens(analyze_tokens);
    
    if (kind == OpKind_None || kind == OpKind_Error) {
        report_syntactic_unknown_op(parser->tokens[parser->token_index].code);
        skip_sentence(parser);
        return alloc_node(parser, OpKind_Error, analyze_tokens[0].code);
    }
    else {
        OpNode* node = NULL;
        if (kind == OpKind_IfStatement) node = extract_if_statement(parser);
        if (kind == OpKind_WhileStatement) node = extract_while_statement(parser);
        if (kind == OpKind_ForStatement) node = extract_for_statement(parser);
        if (kind == OpKind_VariableAssignment) node = extract_variable_assignment(parser);
        if (kind == OpKind_ArrayElementAssignment) node = extract_array_element_assignment(parser);
        if (kind == OpKind_FunctionCall) node = extract_function_call(parser);
        if (kind == OpKind_VariableDefinition) node = extract_variable_definition(parser);
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

OpNode* generate_ast_from_block(Yov* ctx, Array<Token> tokens)
{
    Parser* parser = arena_push_struct<Parser>(ctx->temp_arena);
    parser->ctx = ctx;
    parser->tokens = tokens;
    
    parser->ops = pooled_array_make<OpNode*>(ctx->temp_arena, 64);
    
    auto block_node = (OpNode_Block*)alloc_node(parser, OpKind_Block, tokens[0].code);
    
    while (true)
    {
        OpNode* node = extract_op(parser);
        if (node == NULL) break;
        array_add(&parser->ops, node);
    }
    
    block_node->ops = array_from_pooled_array(ctx->static_arena, parser->ops);
    return block_node;
}

OpNode* generate_ast_from_sentence(Yov* ctx, Array<Token> tokens)
{
    Parser* parser = arena_push_struct<Parser>(ctx->temp_arena);
    parser->ctx = ctx;
    parser->tokens = tokens;
    
    OpNode* node = extract_op(parser);
    return node;
}

OpNode* generate_ast(Yov* ctx, Array<Token> tokens) {
    return generate_ast_from_block(ctx, tokens);
}