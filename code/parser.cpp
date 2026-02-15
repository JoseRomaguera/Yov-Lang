#include "front.h"

Parser* ParserAlloc(YovScript* script, RangeU64 range)
{
    Parser* parser = ArenaPushStruct<Parser>(context.arena);
    parser->script = script;
    parser->text = (script != NULL) ? script->text : String{};
    parser->script_id = (script != NULL) ? script->id : -1;
    parser->cursor = range.min;
    parser->range = range;
    
    return parser;
}

Parser* ParserSub(Parser* parser, Location location)
{
    if (parser->script == NULL) return ParserAlloc(NULL, {});
    Assert(parser->script->id == location.script_id);
    return ParserAlloc(parser->script, location.range);
}

Location LocationFromParser(Parser* parser, U64 end) {
    if (end == U64_MAX) end = parser->range.max;
    SkipInvalidTokens(parser);
    return LocationMake(parser->cursor, end, parser->script_id);
}

Token PeekToken(Parser* parser, I64 cursor_offset)
{
    Token token = read_valid_token(parser->text, parser->cursor + cursor_offset, parser->range.max, parser->script_id);
    return token;
}

void SkipToken(Parser* parser, Token token)
{
    Assert(parser->cursor == token.cursor);
    parser->cursor += token.skip_size;
    Assert(parser->range.max == 0 || parser->cursor <= parser->range.max);
}

void AssumeToken(Parser* parser, TokenKind kind)
{
    Token token = ConsumeToken(parser);
    Assert(token.kind == kind);
}

void MoveCursor(Parser* parser, U64 cursor)
{
    if (cursor > parser->range.max) {
        InvalidCodepath();
        cursor = parser->range.max;
    }
    
    if (cursor < parser->range.min) {
        InvalidCodepath();
        cursor = parser->range.min;
    }
    
    parser->cursor = cursor;
}

Token ConsumeToken(Parser* parser)
{
    Token token = PeekToken(parser);
    SkipToken(parser, token);
    return token;
}

Array<Token> ConsumeAllTokens(Parser* parser)
{
    PooledArray<Token> tokens = pooled_array_make<Token>(context.arena, 16);
    
    while (true)
    {
        Token token = ConsumeToken(parser);
        if (token.kind == TokenKind_None) break;
        array_add(&tokens, token);
    }
    
    return array_from_pooled_array(context.arena, tokens);
}

void SkipInvalidTokens(Parser* parser)
{
    while (1)
    {
        Token token = read_token(parser->text, parser->cursor, parser->script_id);
        if (TokenIsValid(token.kind)) break;
        parser->cursor += token.skip_size;
    }
}

Location FindUntil(Parser* parser, B32 include_match, TokenKind match0, TokenKind match1)
{
    U64 offset = 0;
    
    Token token;
    while (true)
    {
        token = PeekToken(parser, offset);
        if (token.kind == TokenKind_None) return NO_CODE;
        
        if (token.kind == match0) break;
        if (token.kind == match1) break;
        offset += token.skip_size;
    }
    
    if (include_match) {
        offset += token.skip_size;
    }
    
    return LocationFromParser(parser, parser->cursor + offset);
}

U64 find_token_with_depth_check(Parser* parser, B32 parenthesis, B32 braces, B32 brackets, TokenKind match0, TokenKind match1)
{
    U64 offset = 0;
    
    I32 depth = 0;
    
    while (true)
    {
        Token token = PeekToken(parser, offset);
        if (token.kind == TokenKind_None) return parser->cursor;
        
        if (parenthesis && token.kind == TokenKind_OpenParenthesis) depth++;
        if (parenthesis && token.kind == TokenKind_CloseParenthesis) depth--;
        if (braces && token.kind == TokenKind_OpenBrace) depth++;
        if (braces && token.kind == TokenKind_CloseBrace) depth--;
        if (brackets && token.kind == TokenKind_OpenBracket) depth++;
        if (brackets && token.kind == TokenKind_CloseBracket) depth--;
        
        if (depth == 0) {
            if (token.kind == match0) break;
            if (token.kind == match1) break;
        }
        offset += token.skip_size;
    }
    
    return parser->cursor + offset;
}

Location FindScope(Parser* parser, TokenKind open_token, B32 include_delimiters)
{
    TokenKind close_token = TokenKindFromOpenScope(open_token);
    
    U64 offset = 0;
    I32 depth = 0;
    
    Token token = PeekToken(parser);
    
    if (token.kind != open_token) {
        return NO_CODE;
    }
    
    while (true)
    {
        if (token.kind == TokenKind_None) return NO_CODE;
        
        if (token.kind == open_token) depth++;
        else if (token.kind == close_token) {
            depth--;
            if (depth == 0) break;
        }
        
        offset += token.skip_size;
        token = PeekToken(parser, offset);
    }
    
    U64 begin = parser->cursor;
    if (!include_delimiters) {
        begin += PeekToken(parser).skip_size;
    }
    
    U64 end = parser->cursor + offset;
    if (include_delimiters) {
        end += token.skip_size;
    }
    
    return LocationMake(begin, end, parser->script_id);
}

Location FindCode(Parser* parser)
{
    Token first = PeekToken(parser);
    
    if (first.kind == TokenKind_OpenBrace) {
        return FindScope(parser, TokenKind_OpenBrace, true);
    }
    
    if (TokenIsFlowModifier(first.kind))
    {
        U64 start_cursor = parser->cursor;
        defer(MoveCursor(parser, start_cursor));
        
        ConsumeToken(parser); // Modifier keyword
        
        Location optional_parenthesis_location = FetchScope(parser, TokenKind_OpenParenthesis, true);
        Location body_location = FetchCode(parser);
        
        if (!LocationIsValid(body_location)) {
            return NO_CODE;
        }
        
        if (first.kind == TokenKind_IfKeyword && PeekToken(parser).kind == TokenKind_ElseKeyword) {
            ConsumeToken(parser);
            body_location = FetchCode(parser);
            
            if (!LocationIsValid(body_location)) {
                return NO_CODE;
            }
        }
        
        return LocationMake(start_cursor, parser->cursor, parser->script_id);
    }
    
    return FindUntil(parser, true, TokenKind_NextSentence);
}

Location FetchUntil(Parser* parser, B32 include_match, TokenKind match0, TokenKind match1)
{
    Location location = FindUntil(parser, include_match, match0, match1);
    if (LocationIsValid(location)) {
        MoveCursor(parser, location.range.max);
    }
    return location;
}

Location FetchScope(Parser* parser, TokenKind open_token, B32 include_delimiters)
{
    Location location = FindScope(parser, open_token, include_delimiters);
    if (LocationIsValid(location))
    {
        MoveCursor(parser, location.range.max);
        
        if (!include_delimiters) {
            TokenKind close_token = TokenKindFromOpenScope(open_token);
            AssumeToken(parser, close_token);
        }
    }
    
    return location;
}

Location FetchCode(Parser* parser)
{
    Location location = FindCode(parser);
    if (LocationIsValid(location)) {
        MoveCursor(parser, location.range.max);
    }
    return location;
}

SentenceKind GuessSentenceKind(Parser* parser)
{
    U64 start_cursor = parser->cursor;
    defer(MoveCursor(parser, start_cursor));
    
    U32 assignment_count = 0;
    U32 colon_count = 0;
    
    Token tokens[5] = {};
    TokenKind close_token = TokenKind_None;
    
    U32 index = 0;
    while (true) {
        Token token = ConsumeToken(parser);
        
        if (index < countof(tokens)) {
            tokens[index++] = token;
        }
        
        if (token.kind == TokenKind_Assignment) assignment_count++;
        if (token.kind == TokenKind_Colon) colon_count++;
        
        close_token = token.kind;
        if (token.kind == TokenKind_OpenBrace) break;
        if (token.kind == TokenKind_NextSentence) break;
        if (token.kind == TokenKind_None) break;
    }
    
    Token t0 = tokens[0];
    Token t1 = tokens[1];
    Token t2 = tokens[2];
    Token t3 = tokens[3];
    Token t4 = tokens[4];
    
    if (t0.kind == TokenKind_IfKeyword) return SentenceKind_Unknown;
    if (t0.kind == TokenKind_WhileKeyword) return SentenceKind_Unknown;
    if (t0.kind == TokenKind_ForKeyword) return SentenceKind_Unknown;
    if (t0.kind == TokenKind_OpenBrace) return SentenceKind_Unknown;
    if (t0.kind == TokenKind_ImportKeyword) return SentenceKind_Unknown;
    
    if (t0.kind == TokenKind_ReturnKeyword) return SentenceKind_Return;
    if (t0.kind == TokenKind_ContinueKeyword) return SentenceKind_Continue;
    if (t0.kind == TokenKind_BreakKeyword) return SentenceKind_Break;
    
    if (t0.kind == TokenKind_Identifier && t1.kind == TokenKind_OpenParenthesis) {
        return SentenceKind_FunctionCall;
    }
    
    // Object definition
    if (t0.kind == TokenKind_Identifier && t1.kind == TokenKind_Colon && t2.kind == TokenKind_Identifier) return SentenceKind_ObjectDef;
    if (t0.kind == TokenKind_Identifier && t1.kind == TokenKind_Colon && t2.kind == TokenKind_Assignment) return SentenceKind_ObjectDef;
    if (t0.kind == TokenKind_Identifier && t1.kind == TokenKind_Colon && t2.kind == TokenKind_OpenBracket) return SentenceKind_ObjectDef;
    
    // Array definition
    if (t0.kind == TokenKind_Identifier && t1.kind == TokenKind_OpenBracket && t2.kind == TokenKind_CloseBracket && t3.kind == TokenKind_Identifier)
        return SentenceKind_ObjectDef;
    
    if (t0.kind == TokenKind_Identifier && t1.kind == TokenKind_Colon && t2.kind == TokenKind_Colon) {
        if (t3.kind == TokenKind_EnumKeyword) return SentenceKind_EnumDef;
        if (t3.kind == TokenKind_StructKeyword) return SentenceKind_StructDef;
        if (t3.kind == TokenKind_ArgKeyword) return SentenceKind_ArgDef;
        if (t3.kind == TokenKind_FuncKeyword) return SentenceKind_FunctionDef;
        return SentenceKind_ObjectDef;
    }
    
    if (colon_count == 1) return SentenceKind_ObjectDef;
    if (assignment_count == 1) return SentenceKind_Assignment;
    
    return SentenceKind_Unknown;
}

ExpresionContext ExpresionContext_from_void() {
    ExpresionContext ctx{};
    ctx.vtype = VType_Void;
    ctx.assignment_count = 0;
    return ctx;
}

ExpresionContext ExpresionContext_from_inference(U32 assignment_count) {
    ExpresionContext ctx{};
    ctx.vtype = VType_Any;
    ctx.assignment_count = assignment_count;
    return ctx;
}

ExpresionContext ExpresionContext_from_vtype(VType vtype, U32 assignment_count) {
    ExpresionContext ctx{};
    ctx.vtype = vtype;
    ctx.assignment_count = assignment_count;
    return ctx;
}

IR_Group ReadExpression(IR_Context* ir, Parser* parser, ExpresionContext expr_context)
{
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    Location location = LocationFromParser(parser);
    
    Array<Token> tokens = ConsumeAllTokens(parser);
    
    if (tokens.count == 0) return IRFromNone();
    
    Token starting_token = tokens[0];
    
    // Remove parentheses around
    if (tokens.count >= 2 && tokens[0].kind == TokenKind_OpenParenthesis && tokens[tokens.count - 1].kind == TokenKind_CloseParenthesis)
    {
        B32 couple = CheckTokensAreCouple(tokens, 0, tokens.count - 1, TokenKind_OpenParenthesis, TokenKind_CloseParenthesis);
        if (couple) tokens = array_subarray(tokens, 1, tokens.count - 2);
    }
    
    if (tokens.count == 0) {
        report_expr_is_empty(starting_token.location);
        return {};
    }
    
    if (tokens.count == 1)
    {
        Token token = tokens[0];
        
        if (token.kind == TokenKind_IntLiteral) {
            U32 v;
            if (U32FromString(&v, token.value)) {
                return IRFromNone(ValueFromInt(v));
            }
            else {
                InvalidCodepath();
            }
        }
        else if (token.kind == TokenKind_BoolLiteral) {
            return IRFromNone(ValueFromBool(StrEquals(token.value, "true")));
        }
        else if (token.kind == TokenKind_StringLiteral)
        {
            IR_Group out = IRFromNone();
            PooledArray<Value> values = pooled_array_make<Value>(context.arena, 8);
            
            String raw = token.value;
            StringBuilder builder = string_builder_make(context.arena);
            
            U64 cursor = 0;
            while (cursor < raw.size)
            {
                U32 codepoint = StrGetCodepoint(raw, &cursor);
                
                if (codepoint == '\\')
                {
                    codepoint = 0;
                    if (cursor < raw.size) {
                        codepoint = StrGetCodepoint(raw, &cursor);
                    }
                    
                    if (codepoint == '\\') append(&builder, "\\");
                    else if (codepoint == 'n') append(&builder, "\n");
                    else if (codepoint == 'r') append(&builder, "\r");
                    else if (codepoint == 't') append(&builder, "\t");
                    else if (codepoint == '"') append(&builder, "\"");
                    else if (codepoint == '{') append(&builder, "{");
                    else if (codepoint == '}') append(&builder, "}");
                    else {
                        String sequence = StringFromCodepoint(context.arena, codepoint);
                        report_invalid_escape_sequence(token.location, sequence);
                    }
                    
                    continue;
                }
                
                if (codepoint == '{')
                {
                    U64 start_identifier = cursor;
                    I32 depth = 1;
                    
                    while (cursor < raw.size) {
                        U32 codepoint = StrGetCodepoint(raw, &cursor);
                        if (codepoint == '{') depth++;
                        else if (codepoint == '}') {
                            depth--;
                            if (depth == 0) break;
                        }
                    }
                    
                    U64 state_cursor = (raw.data + start_identifier) - parser->text.data;
                    U64 state_count = cursor - start_identifier - 1;
                    
                    Location subexpression_location = LocationMake(state_cursor, state_cursor + state_count, token.location.script_id);
                    
                    IR_Group sub = ReadExpression(ir, ParserSub(parser, subexpression_location), ExpresionContext_from_vtype(VType_String, 1));
                    if (!sub.success) return IRFailed();
                    
                    Value value = sub.value;
                    
                    if (ValueIsCompiletime(value)) {
                        String ct_str = StringFromCompiletime(context.arena, program, value);
                        append(&builder, ct_str);
                    }
                    else
                    {
                        String literal = string_from_builder(context.arena, &builder);
                        if (literal.size > 0) {
                            builder = string_builder_make(context.arena);
                            array_add(&values, ValueFromString(ir->arena, literal));
                        }
                        
                        out = IRAppend(out, sub);
                        array_add(&values, value);
                    }
                    
                    continue;
                }
                
                append_codepoint(&builder, codepoint);
            }
            
            String literal = string_from_builder(context.arena, &builder);
            if (literal.size > 0) {
                array_add(&values, ValueFromString(ir->arena, literal));
            }
            
            out.value = ValueFromStringArray(ir->arena, program, array_from_pooled_array(context.arena, values));
            return out;
        }
        else if (token.kind == TokenKind_CodepointLiteral)
        {
            String raw = token.value;
            
            if (raw.size <= 2) {
                report_invalid_codepoint_literal(token.location, raw);
                return {};
            }
            
            raw = StrSub(raw, 1, raw.size - 2);
            
            U32 v = U32_MAX;
            if (StrEquals(raw, "\\\\")) v = '\\';
            else if (StrEquals(raw, "\\'")) v = '\'';
            else if (StrEquals(raw, "\\n")) v = '\n';
            else if (StrEquals(raw, "\\r")) v = '\r';
            else if (StrEquals(raw, "\\t")) v = '\t';
            else
            {
                U64 cursor = 0;
                v = StrGetCodepoint(raw, &cursor);
                
                if (v == 0xFFFD || cursor != raw.size) {
                    report_invalid_codepoint_literal(token.location, raw);
                    return {};
                }
            }
            
            return IRFromNone(ValueFromInt(v));
        }
        else if (token.kind == TokenKind_NullKeyword) {
            return IRFromNone(ValueNull());
        }
    }
    
    // Symbol
    if (tokens.count >= 1 && tokens.count % 2 == 1 && tokens[0].kind == TokenKind_Identifier)
    {
        Token token = tokens[0];
        
        B32 valid = true;
        for (U32 i = 1; i < tokens.count; ++i) {
            B32 expect_open = i % 2 == 1;
            TokenKind expected = expect_open ? TokenKind_OpenBracket : TokenKind_CloseBracket;
            if (tokens[i].kind != expected) {
                valid = false;
                break;
            }
        }
        
        if (valid)
        {
            return IRFromSymbol(ir, StringFromTokens(context.arena, tokens), location);
        }
    }
    
    
    // Function call
    if (tokens.count >= 3 && tokens[0].kind == TokenKind_Identifier && tokens[1].kind == TokenKind_OpenParenthesis && tokens[tokens.count - 1].kind == TokenKind_CloseParenthesis)
    {
        B32 couple = CheckTokensAreCouple(tokens, 1, tokens.count - 1, TokenKind_OpenParenthesis, TokenKind_CloseParenthesis);
        
        if (couple)
        {
            Location call_code = LocationFromTokens(tokens);
            return ReadFunctionCall(ir, expr_context, ParserSub(parser, call_code));
        }
    }
    
    // Binary operations & Signs
    if (tokens.count >= 2)
    {
        // NOTE(Jose): This might seem counterintuitive, but we need to create nodes for lower priority operations first, since expresions at the bottom of the tree are resolved first
        
        I32 min_preference = I32_MAX;
        I32 preferent_operator_index = -1;
        
        const I32 logical_preference = 1;
        const I32 boolean_preference = 2;
        const I32 addition_preference = 3;
        const I32 multiplication_preference = 4;
        const I32 function_call_preference = 5;
        const I32 sign_preference = 6;
        const I32 member_preference = 7;
        const I32 depth_mult = 8;
        
        I32 parenthesis_depth = 0;
        I32 braces_depth = 0;
        I32 brackets_depth = 0;
        B32 preference_is_sign = false;
        
        for (I32 i = tokens.count - 1; i >= 0; --i)
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
                    report_common_missing_closing_parenthesis(token.location);
                    return IRFailed();
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
                    report_common_missing_closing_brace(token.location);
                    return IRFailed();
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
                    report_common_missing_closing_bracket(token.location);
                    return IRFailed();
                }
                continue;
            }
            
            B32 is_sign = false;
            
            I32 preference = I32_MAX;
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
                    else if (op == BinaryOperator_Is) preference = addition_preference;
                    else {
                        Assert(0);
                    }
                }
            }
            else if (token.kind == TokenKind_Dot && i == tokens.count - 2) {
                preference = member_preference;
            }
            
            if (preference == I32_MAX || braces_depth != 0 || brackets_depth != 0) continue;
            
            preference += parenthesis_depth * depth_mult;
            
            if (preference < min_preference) {
                min_preference = preference;
                preferent_operator_index = i;
                preference_is_sign = is_sign;
            }
        }
        
        if (parenthesis_depth > 0) {
            report_common_missing_opening_parenthesis(tokens[0].location);
            return IRFailed();
        }
        if (braces_depth > 0) {
            report_common_missing_opening_brace(tokens[0].location);
            return IRFailed();
        }
        
        if (min_preference != I32_MAX)
        {
            Token op_token = tokens[preferent_operator_index];
            
            if (token_is_sign_or_binary_op(op_token.kind))
            {
                BinaryOperator op = binary_operator_from_token(op_token.kind);
                expr_context.assignment_count = Min(expr_context.assignment_count, 1);
                
                if (preference_is_sign)
                {
                    Assert(preferent_operator_index == 0);
                    
                    Array<Token> subexpr_tokens = array_subarray(tokens, 1, tokens.count - 1);
                    
                    IR_Group out = ReadExpression(ir, ParserSub(parser, LocationFromTokens(subexpr_tokens)), expr_context);
                    if (!out.success) return IRFailed();
                    
                    Value src = out.value;
                    
                    if (op_token.kind == TokenKind_Ampersand)
                    {
                        out = IRAppend(out, IRFromReference(ir, true, src, location));
                    }
                    else
                    {
                        out = IRAppend(out, IRFromSignOperator(ir, src, op, location));
                    }
                    
                    return out;
                }
                else
                {
                    if (preferent_operator_index <= 0 || preferent_operator_index >= tokens.count - 1) {
                        String op_string = StringFromTokens(context.arena, tokens);
                        report_expr_invalid_binary_operation(op_token.location, op_string);
                        return IRFailed();
                    }
                    else
                    {
                        Array<Token> left_expr_tokens = array_subarray(tokens, 0, preferent_operator_index);
                        Array<Token> right_expr_tokens = array_subarray(tokens, preferent_operator_index + 1, tokens.count - (preferent_operator_index + 1));
                        
                        ExpresionContext left_context = expr_context;
                        IR_Group left = ReadExpression(ir, ParserSub(parser, LocationFromTokens(left_expr_tokens)), left_context);
                        
                        ExpresionContext right_context = VTypeValid(left.value.vtype) ? ExpresionContext_from_vtype(left.value.vtype, 1) : expr_context;
                        IR_Group right = ReadExpression(ir, ParserSub(parser, LocationFromTokens(right_expr_tokens)), right_context);
                        
                        IR_Group out = IRAppend(left, right);
                        if (!out.success) return IRFailed();
                        
                        out = IRAppend(out, IRFromBinaryOperator(ir, left.value, right.value, op, false, location));
                        return out;
                    }
                }
            }
            // Member access
            else if (op_token.kind == TokenKind_Dot)
            {
                Assert(preferent_operator_index == tokens.count - 2);
                
                String member_value = tokens[tokens.count - 1].value;
                
                if (member_value.size == 0) {
                    report_expr_empty_member(tokens[0].location);
                    return IRFailed();
                }
                
                Array<Token> expr_tokens = array_subarray(tokens, 0, tokens.count - 2);
                Location expr_location = LocationFromTokens(expr_tokens);
                
                IR_Group out = IRFromNone();
                
                if (LocationIsValid(expr_location)) {
                    out = ReadExpression(ir, ParserSub(parser, expr_location), ExpresionContext_from_inference(1));
                    if (!out.success) return IRFailed();
                }
                
                Value src = out.value;
                out = IRAppend(out, IRFromChildAccess(ir, src, member_value, expr_context, location));
                return out;
            }
        }
    }
    
    // Array Expresions
    if (tokens.count >= 2 && (tokens[0].kind == TokenKind_OpenBrace || tokens[0].kind == TokenKind_OpenBracket))
    {
        B32 is_empty = tokens[0].kind == TokenKind_OpenBracket;
        
        TokenKind open_token_kind = is_empty ? TokenKind_OpenBracket : TokenKind_OpenBrace;
        TokenKind close_token_kind = is_empty ? TokenKind_CloseBracket : TokenKind_CloseBrace;
        
        I32 arrow_index = -1;
        foreach(i, tokens.count) {
            if (tokens[i].kind == TokenKind_Arrow) {
                arrow_index = i;
                break;
            }
        }
        
        U32 close_index = tokens.count - 1;
        if (arrow_index > 0) {
            close_index = arrow_index - 1;
        }
        
        if (CheckTokensAreCouple(tokens, 0, close_index, open_token_kind, close_token_kind))
        {
            VType element_vtype = VType_Any;
            if (!TypeIsVoid(expr_context.vtype)) {
                element_vtype = expr_context.vtype;
                if (TypeIsArray(element_vtype)) {
                    element_vtype = VTypeNext(program, element_vtype);
                }
            }
            
            if (arrow_index > 0)
            {
                Array<Token> type_tokens = array_subarray(tokens, arrow_index + 1, tokens.count - (arrow_index + 1));
                Location type_code = LocationFromTokens(type_tokens);
                element_vtype = ReadObjectType(ParserSub(parser, type_code), reporter, program);
                if (TypeIsNil(element_vtype)) return IRFailed();
                
                if (TypeIsVoid(element_vtype) || TypeIsAny(element_vtype)) {
                    ReportErrorFront(type_code, "Invalid type for array expression");
                    return IRFailed();
                }
            }
            
            VType expr_vtype = is_empty ? VType_Int : element_vtype;
            
            Array<Token> expr_tokens = array_subarray(tokens, 1, close_index - 1);
            IR_Group out = ReadExpressionList(context.arena, ir, expr_vtype, {}, ParserSub(parser, LocationFromTokens(expr_tokens)));
            Array<Value> values = ValuesFromReturn(context.arena, out.value, true);
            
            if (!is_empty && values.count > 0 && TypeIsAny(element_vtype)) {
                element_vtype = values[0].vtype;
                expr_vtype = element_vtype;
            }
            
            if (TypeIsAny(element_vtype) || TypeIsVoid(element_vtype)) {
                ReportErrorFront(location, "Unknown array type");
                return IRFailed();
            }
            
            foreach(i, values.count) {
                if (!TypeEquals(program, values[i].vtype, expr_vtype)) {
                    if (is_empty) {
                        ReportErrorFront(location, "Expecting an integer for empty array expression but found a '%S'", VTypeGetName(program, values[i].vtype));
                        return IRFailed();
                    }
                    else {
                        ReportErrorFront(location, "Expecting an array of '%S' but found an '%S'", VTypeGetName(program, expr_vtype), VTypeGetName(program, values[i].vtype));
                        return IRFailed();
                    }
                }
            }
            
            if (is_empty)
            {
                VType array_vtype = vtype_from_dimension(element_vtype, values.count);
                Array<Value> dimensions = array_make<Value>(context.arena, array_vtype.array_dimensions);
                
                foreach(i, dimensions.count) {
                    if (i < values.count) dimensions[i] = values[i];
                    else dimensions[i] = ValueFromInt(0);
                }
                
                out.value = ValueFromEmptyArray(ir->arena, TypeFromIndex(ir->program, array_vtype.base_index), dimensions);
            }
            else {
                VType array_vtype = vtype_from_dimension(element_vtype, 1);
                out.value = ValueFromArray(ir->arena, array_vtype, values);
            }
            
            return out;
        }
    }
    
    // Indexing
    if (tokens.count >= 2 && tokens[tokens.count-1].kind == TokenKind_CloseBracket)
    {
        I32 starting_token = 0;
        while (starting_token < tokens.count && tokens[starting_token].kind != TokenKind_OpenBracket)
            starting_token++;
        
        if (starting_token < tokens.count)
        {
            B32 couple = CheckTokensAreCouple(tokens, starting_token, tokens.count - 1, TokenKind_OpenBracket, TokenKind_CloseBracket);
            
            if (couple)
            {
                Array<Token> src_tokens = array_subarray(tokens, 0, starting_token);
                Array<Token> index_tokens = array_subarray(tokens, starting_token + 1, tokens.count - starting_token - 2);
                
                if (src_tokens.count > 0 && index_tokens.count > 0)
                {
                    Location src_location = LocationFromTokens(src_tokens);
                    Location index_location = LocationFromTokens(index_tokens);
                    
                    IR_Group src = ReadExpression(ir, ParserSub(parser, src_location), ExpresionContext_from_void());
                    IR_Group index = ReadExpression(ir, ParserSub(parser, index_location), ExpresionContext_from_vtype(VType_Int, 1));
                    
                    if (!src.success || !index.success) {
                        return IRFailed();
                    }
                    
                    if (src.value.kind == ValueKind_None) {
                        report_array_indexing_expects_expresion(tokens[0].location);
                        return IRFailed();
                    }
                    
                    if (!TypeIsInt(index.value.vtype)) {
                        report_indexing_expects_an_int(location);
                        return IRFailed();
                    }
                    
                    VType vtype = src.value.vtype;
                    
                    IR_Group out = IRAppend(src, index);
                    
                    if (TypeIsArray(vtype))
                    {
                        VType element_vtype = VTypeNext(program, vtype);
                        out = IRAppend(out, IRFromChild(ir, src.value, index.value, true, element_vtype, location));
                    }
                    else
                    {
                        report_indexing_not_allowed(location, VTypeGetName(program, vtype));
                        return IRFailed();
                    }
                    
                    return out;
                }
            }
        }
    }
    
    String expresion_string = StringFromTokens(context.arena, tokens);
    report_expr_syntactic_unknown(tokens[0].location, expresion_string);
    return {};
}

void CheckForAnyAssumptions(IR_Context* ir, IR_Unit* unit, Value value)
{
    Program* program = ir->program;
    I32 reg_index = ValueGetRegister(value);
    
    if (reg_index < 0 || unit == NULL || unit->dst_index != reg_index) {
        return;
    }
    
    if (unit->kind == UnitKind_BinaryOperation && unit->binary_op.op == BinaryOperator_Is) {
        if (!TypeIsArray(unit->src.vtype))
        {
            IR_Object* obj = ir_find_object_from_value(ir, unit->src);
            VType vtype = TypeFromCompiletime(program, unit->binary_op.src1);
            
            if (obj != NULL && !TypeIsNil(vtype)) {
                ir_assume_object(ir, obj, vtype);
            }
        }
    }
}

IR_Group ReadCode(IR_Context* ir, Parser* parser)
{
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    
    ir_scope_push(ir);
    defer(ir_scope_pop(ir));
    
    if (PeekToken(parser).kind == TokenKind_OpenBrace) {
        AssumeToken(parser, TokenKind_OpenBrace);
    }
    
    IR_Group out = IRFromNone();
    
    while (true)
    {
        Token first_token = PeekToken(parser);
        
        if (first_token.kind == TokenKind_None) {
            break;
        }
        
        if (first_token.kind == TokenKind_CloseBrace)
        {
            AssumeToken(parser, TokenKind_CloseBrace);
            if (PeekToken(parser).kind != TokenKind_None) {
                ReportErrorFront(first_token.location, "Invalid closing brace");
                return IRFailed();
            }
            break;
        }
        
        // Control flow
        if (first_token.kind == TokenKind_IfKeyword)
        {
            AssumeToken(parser, TokenKind_IfKeyword);
            
            ir_scope_push(ir);
            defer(ir_scope_pop(ir));
            
            // Read expression
            Location expression_location = FetchScope(parser, TokenKind_OpenParenthesis, false);
            
            if (!LocationIsValid(expression_location)) {
                ReportErrorFront(first_token.location, "Expecting parenthesis for if statement");
                return IRFailed();
            }
            
            IR_Group expression = ReadExpression(ir, ParserSub(parser, expression_location), ExpresionContext_from_vtype(VType_Bool, 1));
            if (!expression.success) return IRFailed();
            
            CheckForAnyAssumptions(ir, expression.last, expression.value);
            
            out = IRAppend(out, expression);
            
            // Read success code
            Location success_location = FetchCode(parser);
            
            if (!LocationIsValid(success_location)) {
                ReportErrorFront(first_token.location, "Expecting code for if statement");
                return IRFailed();
            }
            
            IR_Group success = ReadCode(ir, ParserSub(parser, success_location));
            if (!success.success) return IRFailed();
            
            IR_Group failure = IRFromNone();
            
            // Failure code
            if (PeekToken(parser).kind == TokenKind_ElseKeyword)
            {
                Token else_token = ConsumeToken(parser);
                
                Location failure_location = FetchCode(parser);
                
                if (!LocationIsValid(failure_location)) {
                    ReportErrorFront(else_token.location, "Expecting code for else statement");
                    return IRFailed();
                }
                
                failure = ReadCode(ir, ParserSub(parser, failure_location));
                if (!failure.success) return IRFailed();
            }
            
            out = IRAppend(out, IRFromIfStatement(ir, expression.value, success, failure, first_token.location));
        }
        else if (first_token.kind == TokenKind_ElseKeyword) {
            ReportErrorFront(first_token.location, "No if statement found");
            return IRFailed();
        }
        else if (first_token.kind == TokenKind_WhileKeyword)
        {
            AssumeToken(parser, TokenKind_WhileKeyword);
            
            ir_looping_scope_push(ir, first_token.location);
            defer(ir_looping_scope_pop(ir));
            
            // Read expression
            Location expression_location = FetchScope(parser, TokenKind_OpenParenthesis, false);
            
            if (!LocationIsValid(expression_location)) {
                ReportErrorFront(first_token.location, "Expecting parenthesis for while statement");
                return IRFailed();
            }
            
            IR_Group condition = ReadExpression(ir, ParserSub(parser, expression_location), ExpresionContext_from_vtype(VType_Bool, 1));
            if (!condition.success) return IRFailed();
            
            // Read code
            Location code_location = FetchCode(parser);
            
            if (!LocationIsValid(code_location)) {
                ReportErrorFront(first_token.location, "Expecting code for while statement");
                return IRFailed();
            }
            
            IR_Group code = ReadCode(ir, ParserSub(parser, code_location));
            if (!code.success) return IRFailed();
            
            out = IRAppend(out, IRFromLoop(ir, IRFromNone(), condition, code, IRFromNone(), first_token.location));
        }
        else if (first_token.kind == TokenKind_ForKeyword)
        {
            AssumeToken(parser, TokenKind_ForKeyword);
            
            ir_looping_scope_push(ir, first_token.location);
            defer(ir_looping_scope_pop(ir));
            
            Location parenthesis_location = FetchScope(parser, TokenKind_OpenParenthesis, false);
            if (!LocationIsValid(parenthesis_location)) {
                ReportErrorFront(first_token.location, "Expecting parenthesis for for");
                return IRFailed();
            }
            
            Location content_location = FetchCode(parser);
            if (!LocationIsValid(content_location)) {
                ReportErrorFront(first_token.location, "Expecting code for for statement");
                return IRFailed();
            }
            
            Array<Location> splits = array_make<Location>(context.arena, 8);
            
            // Split location by ';'
            {
                U32 split_count = 0;
                
                Parser* last_parser = parser;
                defer(parser = last_parser);
                parser = ParserSub(parser, parenthesis_location);
                
                while (1)
                {
                    Location split = FetchUntil(parser, false, TokenKind_NextSentence);
                    
                    B32 last_split = !LocationIsValid(split);
                    
                    if (last_split) {
                        split = LocationFromParser(parser, parser->range.max);
                    }
                    
                    if (split_count >= splits.count) {
                        break;
                    }
                    
                    splits[split_count++] = split;
                    
                    if (last_split) break;
                    
                    AssumeToken(parser, TokenKind_NextSentence);
                }
                
                splits.count = split_count;
            }
            
            // For each
            if (splits.count == 1)
            {
                Location location = splits[0];
                
                Parser* last_parser = parser;
                defer(parser = last_parser);
                parser = ParserSub(parser, location);
                
                Token element_token = ConsumeToken(parser);
                if (element_token.kind != TokenKind_Identifier) {
                    ReportErrorFront(location, "Expecting element identifier of the for each statement");
                    return IRFailed();
                }
                
                String element_identifier = element_token.value;
                String index_identifier = {};
                
                if (PeekToken(parser).kind == TokenKind_Comma)
                {
                    AssumeToken(parser, TokenKind_Comma);
                    Token index_token = ConsumeToken(parser);
                    
                    if (index_token.kind != TokenKind_Identifier) {
                        ReportErrorFront(location, "Expecting index identifier for the for each statement");
                        return IRFailed();
                    }
                    
                    index_identifier = index_token.value;
                }
                
                if (PeekToken(parser).kind != TokenKind_Colon) {
                    ReportErrorFront(location, "Expecting colon after for each identifiers");
                    return IRFailed();
                }
                AssumeToken(parser, TokenKind_Colon);
                
                Location iterator_location = LocationFromParser(parser, parser->range.max);
                
                IR_Group iterator = ReadExpression(ir, ParserSub(parser, iterator_location), ExpresionContext_from_inference(1));
                
                if (!TypeIsArray(iterator.value.vtype)) {
                    ReportErrorFront(location, "Invalid iterator for a for each statement");
                    return IRFailed();
                }
                
                VType element_vtype = VTypeNext(program, iterator.value.vtype);
                
                // Init code
                IR_Group init = IRFromDefineObject(ir, RegisterKind_Local, element_identifier, element_vtype, false, location);
                Value element_value = init.value;
                
                if (index_identifier.size > 0) {
                    init = IRAppend(init, IRFromDefineObject(ir, RegisterKind_Local, index_identifier, VType_Int, false, location));
                }
                else {
                    init = IRAppend(init, IRFromDefineTemporal(ir, VType_Int, location));
                }
                Value index_value = init.value;
                init = IRAppend(init, IRFromStore(ir, index_value, ValueFromInt(0), location));
                
                // Condition code
                VariableTypeChild count_info = VTypeGetProperty(program, iterator.value.vtype, "count");
                IR_Group condition = IRFromChild(ir, iterator.value, ValueFromInt(count_info.index), count_info.is_member, count_info.vtype, location);
                Value count_value = condition.value;
                condition = IRAppend(condition, IRFromBinaryOperator(ir, index_value, count_value, BinaryOperator_LessThan, false, location));
                
                // Content code
                IR_Group content = IRFromChild(ir, iterator.value, index_value, true, element_vtype, location);
                content = IRAppend(content, IRFromStore(ir, element_value, content.value, location));
                content = IRAppend(content, ReadCode(ir, ParserSub(parser, content_location)));
                
                // Update code
                IR_Group update = IRFromAssignment(ir, false, index_value, ValueFromInt(1), BinaryOperator_Addition, location);
                
                out = IRAppend(out, IRFromLoop(ir, init, condition, content, update, first_token.location));
            }
            // Traditional C for
            else if (splits.count == 3)
            {
                IR_Group init = ReadSentence(ir, ParserSub(parser, splits[0]));
                IR_Group condition = ReadExpression(ir, ParserSub(parser, splits[1]), ExpresionContext_from_vtype(VType_Bool, 1));
                IR_Group update = ReadSentence(ir, ParserSub(parser, splits[2]));
                IR_Group content = ReadCode(ir, ParserSub(parser, content_location));
                
                out = IRAppend(out, IRFromLoop(ir, init, condition, content, update, first_token.location));
            }
            else
            {
                ReportErrorFront(first_token.location, "Unknown for format");
                return IRFailed();
            }
        }
        else if (first_token.kind == TokenKind_OpenBrace)
        {
            Location block_location = FetchCode(parser);
            
            if (!LocationIsValid(block_location)) {
                report_common_missing_closing_brace(first_token.location);
                return IRFailed();
            }
            
            IR_Group block = ReadCode(ir, ParserSub(parser, block_location));
            if (!block.success) return IRFailed();
            
            out = IRAppend(out, block);
        }
        
        // Sentence
        else
        {
            Location sentence_location = FetchUntil(parser, false, TokenKind_NextSentence);
            
            if (!LocationIsValid(sentence_location)) {
                report_expecting_semicolon(first_token.location);
                return IRFailed();
            }
            
            out = IRAppend(out, ReadSentence(ir, ParserSub(parser, sentence_location)));
            
            AssumeToken(parser, TokenKind_NextSentence);
        }
    }
    
    return out;
}

IR_Group ReadSentence(IR_Context* ir, Parser* parser)
{
    Reporter* reporter = ir->reporter;
    Location location = LocationFromParser(parser);
    SentenceKind kind = GuessSentenceKind(parser);
    
    if (kind == SentenceKind_ObjectDef)
    {
        ObjectDefinitionResult res = ReadObjectDefinitionWithIr(context.arena, parser, ir, false, RegisterKind_Local);
        if (!res.success) return IRFailed();
        return res.out;
    }
    
    if (kind == SentenceKind_FunctionCall)
    {
        return ReadFunctionCall(ir, ExpresionContext_from_void(), parser);
    }
    
    if (kind == SentenceKind_Assignment)
    {
        PooledArray<String> identifiers = pooled_array_make<String>(context.arena, 4);
        IR_Group out = IRFromNone();
        
        U64 dst_end_cursor = find_token_with_depth_check(parser, true, true, true, TokenKind_Assignment);
        
        if (dst_end_cursor == parser->cursor) {
            InvalidCodepath();
            return IRFailed();
        }
        
        Location dst_code = LocationMake(parser->cursor, dst_end_cursor, parser->script_id);
        out = IRAppend(out, ReadExpressionList(context.arena, ir, VType_Any, {}, ParserSub(parser, dst_code)));
        
        MoveCursor(parser, dst_end_cursor);
        Token assignment_token = ConsumeToken(parser);
        Assert(assignment_token.kind == TokenKind_Assignment);
        
        Array<Value> values = ValuesFromReturn(context.arena, out.value, true);
        
        if (values.count == 0) {
            ReportErrorFront(location, "Empty assignment");
            return IRFailed();
        }
        
        foreach(i, values.count)
        {
            Register reg = IRRegisterGet(ir, ValueGetRegister(values[i]));
            if (RegisterIsValid(reg) && reg.is_constant) {
                ReportErrorFront(location, "Can't modify a constant: {line}");
            }
        }
        
        Location src_code = LocationMake(parser->cursor, parser->range.max, parser->script_id);
        
        IR_Group src = ReadExpression(ir, ParserSub(parser, src_code), ExpresionContext_from_vtype(values[0].vtype, values.count));
        out = IRAppend(out, src);
        
        if (!out.success) return IRFailed();
        
        BinaryOperator op = assignment_token.assignment_binary_operator;
        IR_Group assignment = IRFromMultipleAssignment(ir, true, values, src.value, op, location);
        return IRAppend(out, assignment);
    }
    else if (kind == SentenceKind_Continue || kind == SentenceKind_Break)
    {
        return IRFromFlowModifier(ir, kind == SentenceKind_Break, location);
    }
    else if (kind == SentenceKind_Return)
    {
        AssumeToken(parser, TokenKind_ReturnKeyword);
        
        Array<VType> returns = ReturnsFromRegisters(context.arena, array_from_pooled_array(context.arena, ir->local_registers));
        
        VType expected_vtype = VType_Void;
        if (returns.count == 1) expected_vtype = returns[0];
        
        ExpresionContext context = (TypeIsVoid(expected_vtype)) ? ExpresionContext_from_void() : ExpresionContext_from_vtype(expected_vtype, 1);
        
        Location expression_location = LocationFromParser(parser, parser->range.max);
        IR_Group expression = ReadExpression(ir, ParserSub(parser, expression_location), context);
        if (!expression.success) return IRFailed();
        return IRFromReturn(ir, expression, location);
    }
    
    if (kind == SentenceKind_Unknown) {
        ReportErrorFront(location, "Unknown sentence: {line}");
        return IRFailed();
    }
    else {
        ReportErrorFront(location, "Invalid sentence: {line}");
        return IRFailed();
    }
}

IR_Group ReadFunctionCall(IR_Context* ir, ExpresionContext expr_context, Parser* parser)
{
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    Location location = LocationFromParser(parser);
    
    Token identifier_token = ConsumeToken(parser);
    Assert(identifier_token.kind == TokenKind_Identifier);
    String identifier = identifier_token.value;
    
    Location expressions_location = FetchScope(parser, TokenKind_OpenParenthesis, false);
    
    if (!LocationIsValid(expressions_location)) {
        report_common_missing_closing_parenthesis(location);
        return IRFailed();
    }
    
    Array<VType> expected_vtypes = {};
    
    {
        FunctionDefinition* fn = FunctionFromIdentifier(program, identifier);
        if (fn != NULL)
        {
            expected_vtypes = array_make<VType>(context.arena, fn->parameters.count);
            foreach(i, fn->parameters.count) {
                expected_vtypes[i] = fn->parameters[i].vtype;
            }
        }
    }
    
    IR_Group out = ReadExpressionList(context.arena, ir, VType_Any, expected_vtypes, ParserSub(parser, expressions_location));
    Array<Value> params = ValuesFromReturn(context.arena, out.value, true);
    
    IR_Group call = IRFromFunctionCall(ir, identifier, params, expr_context, location);
    return IRAppend(out, call);
}

internal_fn PooledArray<String> ExtractObjectIdentifiers(Parser* parser, Reporter* reporter, B32 require_single)
{
    Token starting_token = PeekToken(parser);
    
    PooledArray<String> identifiers = pooled_array_make<String>(context.arena, 4);
    
    // Extract identifiers
    while (true)
    {
        Token token = ConsumeToken(parser);
        
        if (token.kind != TokenKind_Identifier) {
            ReportErrorFront(token.location, "Expecting an identifier\n");
            return {};
        }
        
        array_add(&identifiers, token.value);
        
        token = ConsumeToken(parser);
        
        if (token.kind == TokenKind_Colon) {
            break;
        }
        
        if (require_single || token.kind != TokenKind_Comma) {
            report_objdef_expecting_colon(starting_token.location);
            return {};
        }
    }
    
    return identifiers;
}

ObjectDefinitionResult ReadObjectDefinition(Arena* arena, Parser* parser, Reporter* reporter, Program* program, B32 require_single, RegisterKind register_kind)
{
    Location location = LocationFromParser(parser);
    
    ObjectDefinitionResult res = {};
    res.out = IRFromNone();
    res.success = false;
    
    Token starting_token = PeekToken(parser);
    
    PooledArray<String> identifiers = ExtractObjectIdentifiers(parser, reporter, require_single);
    if (identifiers.count == 0) return res;
    
    // Explicit type
    VType definition_vtype = VType_Void;
    
    {
        Token type_or_assignment_token = PeekToken(parser);
        
        if (type_or_assignment_token.kind == TokenKind_Assignment) {}
        else if (type_or_assignment_token.kind == TokenKind_Colon) {}
        else if (type_or_assignment_token.kind == TokenKind_Identifier)
        {
            Location type_location = FetchUntil(parser, false, TokenKind_Assignment, TokenKind_Colon);
            
            if (!LocationIsValid(type_location)) {
                type_location = LocationMake(parser->cursor, parser->range.max, parser->script_id);
                MoveCursor(parser, type_location.range.max);
            }
            
            definition_vtype = ReadObjectType(ParserSub(parser, type_location), reporter, program);
            if (TypeIsNil(definition_vtype)) {
                return res;
            }
            
            if (TypeIsVoid(definition_vtype)) {
                ReportErrorFront(starting_token.location, "Void is not a valid object: {line}");
                return res;
            }
        }
        else {
            report_objdef_expecting_type_identifier(starting_token.location);
            return res;
        }
    }
    
    res.objects = array_make<ObjectDefinition>(arena, identifiers.count);
    
    B32 is_constant = false;
    B32 inference_type = TypeIsVoid(definition_vtype);
    
    if (inference_type) {
        ReportErrorFront(starting_token.location, "Unsupported inference type");
        return res;
    }
    
    Token assignment_token = PeekToken(parser);
    is_constant = assignment_token.kind == TokenKind_Colon;
    
    for (auto it = pooled_array_make_iterator(&identifiers); it.valid; ++it)
    {
        String identifier = StrCopy(arena, *it.value);
        Value value = ValueFromZero(definition_vtype);
        
        res.objects[it.index] = ObjDefMake(identifier, definition_vtype, location, is_constant, value);
    }
    
    res.success = true;
    return res;
}

ObjectDefinitionResult ReadObjectDefinitionWithIr(Arena* arena, Parser* parser, IR_Context* ir, B32 require_single, RegisterKind register_kind)
{
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    Location location = LocationFromParser(parser);
    
    ObjectDefinitionResult res = {};
    res.out = IRFromNone();
    res.success = false;
    
    Token starting_token = PeekToken(parser);
    
    PooledArray<String> identifiers = ExtractObjectIdentifiers(parser, reporter, require_single);
    if (identifiers.count == 0) return res;
    
    // Validation for duplicated symbols
    foreach(i, identifiers.count) {
        String identifier = identifiers[i];
        if (ir_find_object(ir, identifier, false) != NULL) {
            report_symbol_duplicated(location, identifier);
            return res;
        }
    }
    
    // Explicit type
    VType definition_vtype = VType_Void;
    
    {
        Token type_or_assignment_token = PeekToken(parser);
        
        if (type_or_assignment_token.kind == TokenKind_Assignment) {}
        else if (type_or_assignment_token.kind == TokenKind_Colon) {}
        else if (type_or_assignment_token.kind == TokenKind_Identifier)
        {
            Location type_location = FetchUntil(parser, false, TokenKind_Assignment, TokenKind_Colon);
            
            if (!LocationIsValid(type_location)) {
                type_location = LocationMake(parser->cursor, parser->range.max, parser->script_id);
                MoveCursor(parser, type_location.range.max);
            }
            
            definition_vtype = ReadObjectType(ParserSub(parser, type_location), reporter, program);
            if (TypeIsNil(definition_vtype)) {
                return res;
            }
            
            if (TypeIsVoid(definition_vtype)) {
                ReportErrorFront(starting_token.location, "Void is not a valid object: {line}");
                return res;
            }
        }
        else {
            report_objdef_expecting_type_identifier(starting_token.location);
            return res;
        }
    }
    
    res.objects = array_make<ObjectDefinition>(arena, identifiers.count);
    
    B32 is_constant = false;
    B32 inference_type = TypeIsVoid(definition_vtype);
    
    Token assignment_token = ConsumeToken(parser);
    
    if (assignment_token.kind != TokenKind_None)
    {
        B32 variable_assignment = assignment_token.kind == TokenKind_Assignment && assignment_token.assignment_binary_operator == BinaryOperator_None;
        B32 constant_assignment = assignment_token.kind == TokenKind_Colon;
        
        if (!variable_assignment && !constant_assignment) {
            report_objdef_expecting_assignment(starting_token.location);
            return res;
        }
        
        is_constant = constant_assignment;
        
        U64 assignment_end_cursor = parser->range.max;
        
        Location expression_code = LocationFromParser(parser, assignment_end_cursor);
        ExpresionContext expression_context = inference_type ? ExpresionContext_from_inference(identifiers.count) : ExpresionContext_from_vtype(definition_vtype, identifiers.count);
        IR_Group src = ReadExpression(ir, ParserSub(parser, expression_code), expression_context);
        if (!src.success) return res;
        
        Array<VType> vtypes = {};
        
        // Inference
        if (inference_type) {
            Array<Value> returns = ValuesFromReturn(context.arena, src.value, true);
            
            if (returns.count == 1) {
                vtypes = array_make<VType>(context.arena, identifiers.count);
                foreach(i, vtypes.count) vtypes[i] = returns[0].vtype;
            }
            else if (returns.count > 1) {
                vtypes = array_make<VType>(context.arena, returns.count);
                foreach(i, vtypes.count) vtypes[i] = returns[i].vtype;
            }
            definition_vtype = src.value.vtype;
        }
        else {
            vtypes = array_make<VType>(context.arena, identifiers.count);
            foreach(i, vtypes.count) vtypes[i] = definition_vtype;
        }
        
        B32 is_any = TypeIsAny(definition_vtype);
        
        if (!is_any && vtypes.count < identifiers.count) {
            ReportErrorFront(location, "Unresolved object type for definition");
            return res;
        }
        
        Array<Value> values = array_make<Value>(context.arena, identifiers.count);
        foreach(i, values.count) {
            VType vtype = vtypes[i];
            
            if (register_kind == RegisterKind_Global)
            {
                I32 global_index = GlobalIndexFromIdentifier(program, identifiers[i]);
                if (global_index < 0) {
                    ReportErrorFront(location, "Global '%S' not found", identifiers[i]);
                    continue;
                }
                
                res.out = IRAppend(res.out, IRFromStore(ir, ValueFromGlobal(program, global_index), ValueFromZero(vtype), location));
            }
            else
            {
                res.out = IRAppend(res.out, IRFromDefineObject(ir, register_kind, identifiers[i], vtype, is_constant, location));
                res.out = IRAppend(res.out, IRFromStore(ir, res.out.value, ValueFromZero(vtype), location));
            }
            values[i] = res.out.value;
        }
        
        // Default src
        if (src.value.kind == ValueKind_None && !is_any) {
            src = IRFromDefaultInitializer(ir, definition_vtype, location);
        }
        
        res.out = IRAppend(res.out, src);
        
        if (src.value.kind != ValueKind_None) {
            IR_Group assignment = IRFromMultipleAssignment(ir, true, values, src.value, BinaryOperator_None, location);
            res.out = IRAppend(res.out, assignment);
        }
        
        for (auto it = pooled_array_make_iterator(&identifiers); it.valid; ++it) {
            res.objects[it.index] = ObjDefMake(StrCopy(arena, *it.value), vtypes[it.index], location, is_constant, values[it.index]);
        }
    }
    else
    {
        if (inference_type) {
            ReportErrorFront(location, "Unspecified type");
            return res;
        }
        
        res.out = IRFromNone(ValueFromZero(definition_vtype));
        
        for (auto it = pooled_array_make_iterator(&identifiers); it.valid; ++it)
        {
            String identifier = StrCopy(arena, *it.value);
            
            if (register_kind == RegisterKind_Global)
            {
                I32 global_index = GlobalIndexFromIdentifier(program, identifier);
                if (global_index < 0) {
                    ReportErrorFront(location, "Global '%S' not found", identifier);
                    continue;
                }
                
                res.out = IRAppend(res.out, IRFromStore(ir, ValueFromGlobal(program, global_index), ValueFromZero(definition_vtype), location));
                Value value = res.out.value;
                res.objects[it.index] = ObjDefMake(identifier, definition_vtype, location, is_constant, value);
            }
            else
            {
                res.out = IRAppend(res.out, IRFromDefineObject(ir, register_kind, identifier, definition_vtype, is_constant, location));
                Value value = res.out.value;
                if (register_kind != RegisterKind_Parameter) {
                    res.out = IRAppend(res.out, IRFromStore(ir, value, ValueFromZero(definition_vtype), location));
                }
                res.objects[it.index] = ObjDefMake(identifier, definition_vtype, location, is_constant, value);
            }
        }
    }
    
    res.success = true;
    return res;
}

ObjectDefinitionResult ReadDefinitionList(Arena* arena, Parser* parser, Reporter* reporter, Program* program, RegisterKind register_kind)
{
    ObjectDefinitionResult res = {};
    res.out = IRFromNone();
    
    // Empty list
    if (PeekToken(parser).kind == TokenKind_None)
    {
        res.success = true;
        return res;
    }
    
    PooledArray<ObjectDefinition> list = pooled_array_make<ObjectDefinition>(context.arena, 8);
    
    while (parser->cursor < parser->range.max)
    {
        Location parameter_location = FetchUntil(parser, false, TokenKind_Comma);
        
        if (!LocationIsValid(parameter_location)) {
            parameter_location = LocationMake(parser->cursor, parser->range.max, parser->script_id);
            MoveCursor(parser, parameter_location.range.max);
        }
        
        ObjectDefinitionResult res0 = ReadObjectDefinition(context.arena, ParserSub(parser, parameter_location), reporter, program, false, register_kind);
        if (!res0.success) return {};
        
        foreach(i, res0.objects.count) {
            array_add(&list, res0.objects[i]);
        }
        
        res.out = IRAppend(res.out, res0.out);
        
        ConsumeToken(parser);
    }
    
    res.objects = array_from_pooled_array(arena, list);
    res.success = true;
    return res;
}

ObjectDefinitionResult ReadDefinitionListWithIr(Arena* arena, Parser* parser, IR_Context* ir, RegisterKind register_kind)
{
    ObjectDefinitionResult res = {};
    res.out = IRFromNone();
    
    // Empty list
    if (PeekToken(parser).kind == TokenKind_None)
    {
        res.success = true;
        return res;
    }
    
    PooledArray<ObjectDefinition> list = pooled_array_make<ObjectDefinition>(context.arena, 8);
    
    while (parser->cursor < parser->range.max)
    {
        Location parameter_location = FetchUntil(parser, false, TokenKind_Comma);
        
        if (!LocationIsValid(parameter_location)) {
            parameter_location = LocationMake(parser->cursor, parser->range.max, parser->script_id);
            MoveCursor(parser, parameter_location.range.max);
        }
        
        ObjectDefinitionResult res0 = ReadObjectDefinitionWithIr(context.arena, ParserSub(parser, parameter_location), ir, false, register_kind);
        if (!res0.success) return {};
        
        foreach(i, res0.objects.count) {
            array_add(&list, res0.objects[i]);
        }
        
        res.out = IRAppend(res.out, res0.out);
        
        ConsumeToken(parser);
    }
    
    res.objects = array_from_pooled_array(arena, list);
    res.success = true;
    return res;
}

IR_Group ReadExpressionList(Arena* arena, IR_Context* ir, VType vtype, Array<VType> expected_vtypes, Parser* parser)
{
    IR_Group out = IRFromNone();
    PooledArray<Value> list = pooled_array_make<Value>(context.arena, 8);
    
    while (parser->cursor < parser->range.max)
    {
        U64 expression_end_cursor = find_token_with_depth_check(parser, true, true, true, TokenKind_Comma);
        
        if (parser->cursor == expression_end_cursor) {
            expression_end_cursor = parser->range.max;
        }
        
        U32 index = list.count;
        VType expected_vtype = vtype;
        if (index < expected_vtypes.count) expected_vtype = expected_vtypes[index];
        
        Location expression_location = LocationMake(parser->cursor, expression_end_cursor, parser->script_id);
        
        out = IRAppend(out, ReadExpression(ir, ParserSub(parser, expression_location), ExpresionContext_from_vtype(expected_vtype, 1)));
        if (!out.success) return IRFailed();
        
        array_add(&list, out.value);
        
        MoveCursor(parser, expression_end_cursor);
        ConsumeToken(parser);
    }
    
    out.value = value_from_return(arena, array_from_pooled_array(context.arena, list));
    
    return out;
}

VType ReadObjectType(Parser* parser, Reporter* reporter, Program* program)
{
    Location location = LocationFromParser(parser);
    Array<Token> tokens = ConsumeAllTokens(parser);
    
    if (tokens.count == 0)
    {
        ReportErrorFront(location, "Expecting a type");
        return VType_Nil;
    }
    
    Token identifier_token = tokens[0];
    if (identifier_token.kind != TokenKind_Identifier) {
        report_objdef_expecting_type_identifier(identifier_token.location);
        return VType_Nil;
    }
    
    tokens = array_subarray(tokens, 1, tokens.count - 1);
    
    B32 is_reference = false;
    if (tokens.count > 0 && tokens[tokens.count - 1].kind == TokenKind_Ampersand)
    {
        is_reference = true;
        tokens = array_subarray(tokens, 0, tokens.count - 1);
    }
    
    U32 array_dimensions = 0;
    if (tokens.count > 0)
    {
        if (tokens.count % 2 != 0) {
            ReportErrorFront(location, "Invalid type format");
            return VType_Nil;
        }
        
        foreach(i, tokens.count)
        {
            TokenKind kind = (i % 2 == 0) ? TokenKind_OpenBracket : TokenKind_CloseBracket;
            if (tokens[i].kind != kind) {
                ReportErrorFront(location, "Invalid type format");
                return VType_Nil;
            }
        }
        
        array_dimensions = tokens.count / 2;
    }
    
    VType vtype = TypeFromName(program, identifier_token.value);
    if (TypeIsNil(vtype)) {
        ReportErrorFront(location, "Unknown type");
        return VType_Nil;
    }
    
    vtype = vtype_from_dimension(vtype, array_dimensions);
    if (is_reference) vtype = vtype_from_reference(vtype);
    return vtype;
}


String StringFromTokens(Arena* arena, Array<Token> tokens)
{
    StringBuilder builder = string_builder_make(context.arena);
    
    foreach(i, tokens.count) {
        append(&builder, tokens[i].value);
    }
    
    String res = string_from_builder(context.arena, &builder);
    res = StrReplace(context.arena, res, "\n", "\\n");
    res = StrReplace(context.arena, res, "\r", "");
    res = StrReplace(context.arena, res, "\t", " ");
    
    return StrCopy(arena, res);
}

String DebugInfoFromToken(Arena* arena, Token token)
{
    TokenKind k = token.kind;
    
    if (k == TokenKind_Separator) return "separator";
    if (k == TokenKind_Identifier) return StrFormat(arena, "identifier: %S", token.value);
    if (k == TokenKind_IfKeyword) return "if";
    if (k == TokenKind_ElseKeyword) return "else";
    if (k == TokenKind_WhileKeyword) return "while";
    if (k == TokenKind_ForKeyword) return "for";
    if (k == TokenKind_EnumKeyword) return "enum";
    if (k == TokenKind_IntLiteral) return StrFormat(arena, "Int Literal: %S", token.value);
    if (k == TokenKind_BoolLiteral) { return StrFormat(arena, "Bool Literal: %S", token.value); }
    if (k == TokenKind_StringLiteral) { return StrFormat(arena, "String Literal: %S", token.value); }
    if (k == TokenKind_Comment) { return StrFormat(arena, "Comment: %S", token.value); }
    if (k == TokenKind_Comma) return ",";
    if (k == TokenKind_Dot) return ".";
    if (k == TokenKind_Colon) return ":";
    if (k == TokenKind_OpenBrace) return "{";
    if (k == TokenKind_CloseBrace) return "}";
    if (k == TokenKind_OpenBracket) return "[";
    if (k == TokenKind_CloseBracket) return "]";
    if (k == TokenKind_OpenParenthesis) return "(";
    if (k == TokenKind_CloseParenthesis) return ")";
    if (k == TokenKind_Assignment) {
        if (token.assignment_binary_operator == BinaryOperator_None) return "=";
        return StrFormat(arena, "%S=", StringFromBinaryOperator(token.assignment_binary_operator));
    }
    if (k == TokenKind_PlusSign) return "+";
    if (k == TokenKind_MinusSign) return "-";
    if (k == TokenKind_Asterisk) return "*";
    if (k == TokenKind_Slash) return "/";
    if (k == TokenKind_Modulo) return "%";
    if (k == TokenKind_Ampersand) return "&";
    if (k == TokenKind_Exclamation) return "!";
    if (k == TokenKind_LogicalOr) return "||";
    if (k == TokenKind_LogicalAnd) return "&&";
    if (k == TokenKind_CompEquals) return "==";
    if (k == TokenKind_CompNotEquals) return "!=";
    if (k == TokenKind_CompLess) return "<";
    if (k == TokenKind_CompLessEquals) return "<=";
    if (k == TokenKind_CompGreater) return ">";
    if (k == TokenKind_CompGreaterEquals) return ">=";
    if (k == TokenKind_OpenString) return "Open String";
    if (k == TokenKind_NextLine) return "Next Line";
    if (k == TokenKind_CloseString) return "Close String";
    if (k == TokenKind_NextSentence) return ";";
    if (k == TokenKind_Error) return "Error";
    if (k == TokenKind_None) return "None";
    
    return "?";
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
    if (token == TokenKind_IsKeyword) return BinaryOperator_Is;
    return BinaryOperator_None;
};

B32 token_is_sign_or_binary_op(TokenKind token)
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
        
        TokenKind_IsKeyword,
    };
    
    foreach(i, countof(tokens)) {
        if (tokens[i] == token) return true;
    }
    return false;
}

B32 TokenIsFlowModifier(TokenKind token) {
    if (token == TokenKind_IfKeyword) return true;
    if (token == TokenKind_ElseKeyword) return true;
    if (token == TokenKind_WhileKeyword) return true;
    if (token == TokenKind_ForKeyword) return true;
    return false;
}

TokenKind TokenKindFromOpenScope(TokenKind open_token)
{
    if (open_token == TokenKind_OpenParenthesis) return TokenKind_CloseParenthesis;
    if (open_token == TokenKind_OpenBracket) return TokenKind_CloseBracket;
    if (open_token == TokenKind_OpenBrace) return TokenKind_CloseBrace;
    
    InvalidCodepath();
    return TokenKind_Error;
}

Location LocationFromTokens(Array<Token> tokens)
{
    if (tokens.count == 0) return {};
    
#if DEV
    foreach(i, tokens.count - 1)
    {
        Token t0 = tokens[i + 0];
        Token t1 = tokens[i + 1];
        Assert(t0.cursor + t0.skip_size == t1.cursor);
        Assert(t0.location.script_id == t1.location.script_id);
    }
#endif
    
    Token first_token = tokens[0];
    Token last_token = tokens[tokens.count - 1];
    
    I32 script_id = first_token.location.script_id;
    return LocationMake(first_token.cursor, last_token.cursor + last_token.skip_size, script_id);
}


internal_fn Token token_make_dynamic(String text, U64 cursor, TokenKind kind, U64 size, I32 script_id)
{
    Assert(size > 0);
    
    if (cursor + size > text.size) {
        Assert(0);
        return {};
    }
    
    Token token{};
    token.value = StrSub(text, cursor, size);
    token.skip_size = (U32)token.value.size;
    token.cursor = cursor;
    token.location = LocationMake(cursor, cursor + token.value.size, script_id);
    
    if (kind == TokenKind_Identifier) {
        if (StrEquals("null", token.value)) kind = TokenKind_NullKeyword;
        else if (StrEquals("if", token.value)) kind = TokenKind_IfKeyword;
        else if (StrEquals("else", token.value)) kind = TokenKind_ElseKeyword;
        else if (StrEquals("while", token.value)) kind = TokenKind_WhileKeyword;
        else if (StrEquals("for", token.value)) kind = TokenKind_ForKeyword;
        else if (StrEquals("is", token.value)) kind = TokenKind_IsKeyword;
        else if (StrEquals("func", token.value)) kind = TokenKind_FuncKeyword;
        else if (StrEquals("enum", token.value)) kind = TokenKind_EnumKeyword;
        else if (StrEquals("struct", token.value)) kind = TokenKind_StructKeyword;
        else if (StrEquals("arg", token.value)) kind = TokenKind_ArgKeyword;
        else if (StrEquals("return", token.value)) kind = TokenKind_ReturnKeyword;
        else if (StrEquals("break", token.value)) kind = TokenKind_BreakKeyword;
        else if (StrEquals("continue", token.value)) kind = TokenKind_ContinueKeyword;
        else if (StrEquals("import", token.value)) kind = TokenKind_ImportKeyword;
        else if (StrEquals("true", token.value)) kind = TokenKind_BoolLiteral;
        else if (StrEquals("false", token.value)) kind = TokenKind_BoolLiteral;
    }
    
    token.kind = kind;
    
    // Discard double quotes
    if (token.kind == TokenKind_StringLiteral) {
        Assert(token.value.size >= 2);
        token.value = StrSub(token.value, 1, token.value.size - 2);
    }
    
    return token;
}

internal_fn Token token_make_fixed(String text, U64 cursor, TokenKind kind, U32 codepoint_length, I32 script_id)
{
    U64 start_cursor = cursor;
    
    U32 index = 0;
    while (index < codepoint_length && cursor < text.size) {
        index++;
        StrGetCodepoint(text, &cursor);
    }
    
    return token_make_dynamic(text, start_cursor, kind, cursor - start_cursor, script_id);
}

internal_fn Token token_from_assignment(String text, U64 cursor, BinaryOperator binary_op, U32 codepoint_length, I32 script_id)
{
    Token token = token_make_fixed(text, cursor, TokenKind_Assignment, codepoint_length, script_id);
    token.assignment_binary_operator = binary_op;
    return token;
}

Token read_token(String text, U64 start_cursor, I32 script_id)
{
    U32 c0, c1;
    
    {
        U64 cursor = start_cursor;
        c0 = (cursor < text.size) ? StrGetCodepoint(text, &cursor) : 0;
        c1 = (cursor < text.size) ? StrGetCodepoint(text, &cursor) : 0;
    }
    
    if (c0 == 0) {
        return token_make_fixed(text, start_cursor, TokenKind_NextLine, 1, script_id);
    }
    
    if (codepoint_is_separator(c0))
    {
        U64 cursor = start_cursor;
        while (cursor < text.size) {
            U64 next_cursor = cursor;
            U32 codepoint = StrGetCodepoint(text, &next_cursor);
            if (!codepoint_is_separator(codepoint)) {
                break;
            }
            cursor = next_cursor;
        }
        return token_make_dynamic(text, start_cursor, TokenKind_Separator, cursor - start_cursor, script_id);
    }
    
    if (c0 == '/' && c1 == '/')
    {
        U64 cursor = start_cursor;
        while (cursor < text.size) {
            U64 next_cursor = cursor;
            U32 codepoint = StrGetCodepoint(text, &next_cursor);
            if (codepoint == '\n') break;
            cursor = next_cursor;
        }
        return token_make_dynamic(text, start_cursor, TokenKind_Comment, cursor - start_cursor, script_id);
    }
    
    if (c0 == '/' && c1 == '*')
    {
        U64 cursor = start_cursor;
        U64 last_codepoint = 0;
        I32 depth = 0;
        while (cursor < text.size) 
        {
            U32 codepoint = StrGetCodepoint(text, &cursor);
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
        return token_make_dynamic(text, start_cursor, TokenKind_Comment, cursor - start_cursor, script_id);
    }
    
    if (c0 == '"') {
        B32 ignore_next = false;
        U64 cursor = start_cursor + 1;
        while (cursor < text.size) {
            U32 codepoint = StrGetCodepoint(text, &cursor);
            
            if (ignore_next) {
                ignore_next = false;
                continue;
            }
            
            if (codepoint == '\\') ignore_next = true;
            if (codepoint == '"') break;
        }
        return token_make_dynamic(text, start_cursor, TokenKind_StringLiteral, cursor - start_cursor, script_id);
    }
    
    if (c0 == '\'') {
        B32 ignore_next = false;
        U64 cursor = start_cursor + 1;
        while (cursor < text.size) {
            U32 codepoint = StrGetCodepoint(text, &cursor);
            
            if (ignore_next) {
                ignore_next = false;
                continue;
            }
            
            if (codepoint == '\\') ignore_next = true;
            if (codepoint == '\'') break;
        }
        return token_make_dynamic(text, start_cursor, TokenKind_CodepointLiteral, cursor - start_cursor, script_id);
    }
    
    if (c0 == '-' && c1 == '>') return token_make_fixed(text, start_cursor, TokenKind_Arrow, 2, script_id);
    
    if (c0 == ',') return token_make_fixed(text, start_cursor, TokenKind_Comma, 1, script_id);
    if (c0 == '.') return token_make_fixed(text, start_cursor, TokenKind_Dot, 1, script_id);
    if (c0 == '{') return token_make_fixed(text, start_cursor, TokenKind_OpenBrace, 1, script_id);
    if (c0 == '}') return token_make_fixed(text, start_cursor, TokenKind_CloseBrace, 1, script_id);
    if (c0 == '[') return token_make_fixed(text, start_cursor, TokenKind_OpenBracket, 1, script_id);
    if (c0 == ']') return token_make_fixed(text, start_cursor, TokenKind_CloseBracket, 1, script_id);
    if (c0 == '"') return token_make_fixed(text, start_cursor, TokenKind_OpenString, 1, script_id);
    if (c0 == '(') return token_make_fixed(text, start_cursor, TokenKind_OpenParenthesis, 1, script_id);
    if (c0 == ')') return token_make_fixed(text, start_cursor, TokenKind_CloseParenthesis, 1, script_id);
    if (c0 == ':') return token_make_fixed(text, start_cursor, TokenKind_Colon, 1, script_id);
    if (c0 == ';') return token_make_fixed(text, start_cursor, TokenKind_NextSentence, 1, script_id);
    if (c0 == '\n') return token_make_fixed(text, start_cursor, TokenKind_NextLine, 1, script_id);
    if (c0 == '_') return token_make_fixed(text, start_cursor, TokenKind_Identifier, 1, script_id);
    
    if (c0 == '+' && c1 == '=') return token_from_assignment(text, start_cursor, BinaryOperator_Addition, 2, script_id);
    if (c0 == '-' && c1 == '=') return token_from_assignment(text, start_cursor, BinaryOperator_Substraction, 2, script_id);
    if (c0 == '*' && c1 == '=') return token_from_assignment(text, start_cursor, BinaryOperator_Multiplication, 2, script_id);
    if (c0 == '/' && c1 == '=') return token_from_assignment(text, start_cursor, BinaryOperator_Division, 2, script_id);
    if (c0 == '%' && c1 == '=') return token_from_assignment(text, start_cursor, BinaryOperator_Modulo, 2, script_id);
    
    if (c0 == '=' && c1 == '=') return token_make_fixed(text, start_cursor, TokenKind_CompEquals, 2, script_id);
    if (c0 == '!' && c1 == '=') return token_make_fixed(text, start_cursor, TokenKind_CompNotEquals, 2, script_id);
    if (c0 == '<' && c1 == '=') return token_make_fixed(text, start_cursor, TokenKind_CompLessEquals, 2, script_id);
    if (c0 == '>' && c1 == '=') return token_make_fixed(text, start_cursor, TokenKind_CompGreaterEquals, 2, script_id);
    
    if (c0 == '|' && c1 == '|') return token_make_fixed(text, start_cursor, TokenKind_LogicalOr, 2, script_id);
    if (c0 == '&' && c1 == '&') return token_make_fixed(text, start_cursor, TokenKind_LogicalAnd, 2, script_id);
    
    if (c0 == '=') return token_from_assignment(text, start_cursor, BinaryOperator_None, 1, script_id);
    
    if (c0 == '<') return token_make_fixed(text, start_cursor, TokenKind_CompLess, 1, script_id);
    if (c0 == '>') return token_make_fixed(text, start_cursor, TokenKind_CompGreater, 1, script_id);
    
    if (c0 == '+') return token_make_fixed(text, start_cursor, TokenKind_PlusSign, 1, script_id);
    if (c0 == '-') return token_make_fixed(text, start_cursor, TokenKind_MinusSign, 1, script_id);
    if (c0 == '*') return token_make_fixed(text, start_cursor, TokenKind_Asterisk, 1, script_id);
    if (c0 == '/') return token_make_fixed(text, start_cursor, TokenKind_Slash, 1, script_id);
    if (c0 == '%') return token_make_fixed(text, start_cursor, TokenKind_Modulo, 1, script_id);
    
    if (c0 == '&') return token_make_fixed(text, start_cursor, TokenKind_Ampersand, 1, script_id);
    if (c0 == '!') return token_make_fixed(text, start_cursor, TokenKind_Exclamation, 1, script_id);
    
    if (codepoint_is_number(c0))
    {
        U64 cursor = start_cursor;
        while (cursor < text.size) {
            U64 next_cursor = cursor;
            U32 codepoint = StrGetCodepoint(text, &next_cursor);
            if (!codepoint_is_number(codepoint)) {
                break;
            }
            cursor = next_cursor;
        }
        return token_make_dynamic(text, start_cursor, TokenKind_IntLiteral, cursor - start_cursor, script_id);
    }
    
    if (codepoint_is_text(c0))
    {
        U64 cursor = start_cursor;
        while (cursor < text.size) {
            U64 next_cursor = cursor;
            U32 codepoint = StrGetCodepoint(text, &next_cursor);
            if (!codepoint_is_text(codepoint) && !codepoint_is_number(codepoint) && codepoint != '_') {
                break;
            }
            cursor = next_cursor;
        }
        return token_make_dynamic(text, start_cursor, TokenKind_Identifier, cursor - start_cursor, script_id);
    }
    
    return token_make_fixed(text, start_cursor, TokenKind_Error, 1, script_id);
}

Token read_valid_token(String text, U64 start_cursor, U64 end_cursor, I32 script_id)
{
    Assert(end_cursor <= text.size);
    
    U32 total_skip_size = 0;
    Token token;
    
    while (true) {
        token = {};
        I64 cursor = (I64)start_cursor + total_skip_size;
        if (cursor < 0 || cursor >= end_cursor) break;
        
        token = read_token(text, cursor, script_id);
        
        total_skip_size += token.skip_size;
        
        if (TokenIsValid(token.kind)) break;
    }
    
    token.cursor = start_cursor;
    token.skip_size = total_skip_size;
    
    return token;
}

B32 TokenIsValid(TokenKind token)
{
    if (token == TokenKind_Separator) return false;
    if (token == TokenKind_Comment) return false;
    if (token == TokenKind_NextLine) return false;
    return true;
}

B32 CheckTokensAreCouple(Array<Token> tokens, U32 open_index, U32 close_index, TokenKind open_token, TokenKind close_token)
{
    Assert(open_index < tokens.count && close_index < tokens.count);
    Assert(tokens[open_index].kind == open_token && tokens[close_index].kind == close_token);
    
    if (close_index <= open_index) return false;
    
    I32 depth = 1;
    for (U32 i = open_index + 1; i < close_index; ++i)
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
