#include "front.h"

Parser* ParserAlloc(YovScript* script, RangeU64 range)
{
    Parser* parser = ArenaPushStruct<Parser>(context.arena);
    parser->script = script;
    parser->text = (script != NULL) ? script->text : String{};
    parser->script_id = (script != NULL) ? script->id : -1;
    parser->cursor = range.min;
    parser->range = range;
    
#if DEV
    parser->debug_str = StrHeapCopy(StrSub(parser->text, parser->range.min, parser->range.max - parser->range.min));
#endif
    
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
    Token token = ReadValidToken(parser->text, parser->cursor + cursor_offset, parser->range.max, parser->script_id);
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
    BArray<Token> tokens = BArrayMake<Token>(context.arena, 16);
    
    while (true)
    {
        Token token = ConsumeToken(parser);
        if (token.kind == TokenKind_None) break;
        BArrayAdd(&tokens, token);
    }
    
    return ArrayFromBArray(context.arena, tokens);
}

void SkipInvalidTokens(Parser* parser)
{
    while (1)
    {
        Token token = ReadToken(parser->text, parser->cursor, parser->script_id);
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
    ctx.type = void_type;
    ctx.assignment_count = 0;
    return ctx;
}

ExpresionContext ExpresionContext_from_inference(U32 assignment_count) {
    ExpresionContext ctx{};
    ctx.type = any_type;
    ctx.assignment_count = assignment_count;
    return ctx;
}

ExpresionContext ExpresionContext_from_type(Type* type, U32 assignment_count) {
    ExpresionContext ctx{};
    ctx.type = type;
    ctx.assignment_count = assignment_count;
    return ctx;
}

IR_Group ReadExpression(IR_Context* ir, Parser* parser, ExpresionContext expr_context)
{
    PROFILE_FUNCTION;
    
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
        if (couple) tokens = ArraySub(tokens, 1, tokens.count - 2);
    }
    
    if (tokens.count == 0) {
        report_expr_is_empty(starting_token.location);
        return {};
    }
    
    // If Expression
    if (tokens.count > 3 && tokens[0].kind == TokenKind_IfKeyword)
    {
        I32 then_index = -1;
        I32 else_index = -1;
        I32 depth = 0;
        
        for (U32 i = 0; i < tokens.count; i++) {
            TokenKind t = tokens[i].kind;
            if (depth == 0 && t == TokenKind_ThenKeyword) then_index = i;
            if (depth == 0 && t == TokenKind_ElseKeyword) else_index = i;
            
            if (t == TokenKind_OpenParenthesis) depth++;
            else if (t == TokenKind_CloseParenthesis) depth--;
            if (t == TokenKind_OpenBracket) depth++;
            else if (t == TokenKind_CloseBracket) depth--;
        }
        
        if (then_index < 0) {
            ReportErrorFront(location, "Then keyword not found for if expression");
            return IRFailed();
        }
        
        if (else_index < 0) {
            ReportErrorFront(location, "Else keyword not found for if expression");
            return IRFailed();
        }
        
        if (else_index < then_index) {
            ReportErrorFront(location, "Then keyword not found for if expression");
            return IRFailed();
        }
        
        Array<Token> condition_tokens = ArraySub(tokens, 1, then_index - 1);
        Array<Token> left_tokens = ArraySub(tokens, then_index + 1, else_index - then_index - 1);
        Array<Token> right_tokens = ArraySub(tokens, else_index + 1, tokens.count - else_index - 1);
        
        if (left_tokens.count == 0 || right_tokens.count == 0) {
            ReportErrorFront(location, "Empty values for if expression");
            return IRFailed();
        }
        
        Location condition_location = LocationFromTokens(condition_tokens);
        Location left_location = LocationFromTokens(left_tokens);
        Location right_location = LocationFromTokens(right_tokens);
        
        IR_Group condition = ReadExpressionWithCasting(ir, ParserSub(parser, condition_location), ExpresionContext_from_type(bool_type, 1));
        IR_Group left = ReadExpression(ir, ParserSub(parser, left_location), expr_context);
        IR_Group right = ReadExpression(ir, ParserSub(parser, right_location), expr_context);
        
        if (!condition.success || !left.success || !right.success) return IRFailed();
        
        if (condition.value.type != bool_type) {
            ReportErrorFront(location, "Expecting a boolean for if expression");
            return IRFailed();
        }
        
        if (left.value.type != right.value.type) {
            ReportErrorFront(location, "Types missmatch");
            return IRFailed();
        }
        
        Type* type = left.value.type;
        if (type == void_type || type == any_type || type == nil_type) {
            ReportErrorFront(location, "Unknown type for if expression");
            return IRFailed();
        }
        
        // Assignments
        IR_Group dst = IRFromDefineTemporal(ir, type, location);
        Value dst_value = dst.value;
        left = IRAppend(left, IRFromStore(ir, dst_value, left.value, location));
        right = IRAppend(right, IRFromStore(ir, dst_value, right.value, location));
        
        // Conditional jumps
        IR_Unit* exec_right_unit = IRUnitAlloc_Empty(ir, location);
        IR_Unit* exit_unit = IRUnitAlloc_Empty(ir, location);
        
        Value cmp_value = condition.value;
        IR_Group cmd_jump = IRFromSingle(IRUnitAlloc_Jump(ir, -1, cmp_value, exec_right_unit, location));
        IR_Group exit_jump = IRFromSingle(IRUnitAlloc_Jump(ir, 0, ValueNone(), exit_unit, location));
        
        left = IRAppend(cmd_jump, left);
        right = IRAppend4(exit_jump, IRFromSingle(exec_right_unit, right.value), right, IRFromSingle(exit_unit, right.value));
        
        // Mix & Return
        IR_Group out = IRAppend4(condition, dst, left, right);
        out.value = dst_value;
        return out;
    }
    
    if (tokens.count == 1)
    {
        Token token = tokens[0];
        
        if (token.kind == TokenKind_IntLiteral)
        {
            U64 unsigned_value;
            if (U64FromString(&unsigned_value, token.value)) {
                if (expr_context.type == uint_type) {
                    return IRFromNone(ValueFromUInt(unsigned_value));
                }
                
                return IRFromNone(ValueFromInt(unsigned_value));
            }
            
            InvalidCodepath();
        }
        else if (token.kind == TokenKind_FloatLiteral) {
            F64 v;
            if (F64FromString(&v, token.value)) {
                return IRFromNone(ValueFromFloat(v));
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
            BArray<Value> values = BArrayMake<Value>(context.arena, 8);
            
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
                    else if (codepoint == 'x') {
                        if (cursor + 2 > raw.size) {
                            ReportErrorFront(token.location, "Expecting hexadecimal number after \\x");
                        }
                        else
                        {
                            String hexa = StrSub(raw, cursor, 2);
                            U32 v;
                            if (!U32FromString(&v, hexa, 16)) {
                                ReportErrorFront(token.location, "Invalid hexadecimal number '%S'", hexa);
                            }
                            else {
                                U8 lit = (U8)v;
                                String s = {};
                                s.data = (char*)&lit;
                                s.size = 1;
                                append(&builder, s);
                            }
                            
                            cursor += 2;
                        }
                    }
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
                    
                    IR_Group sub = ReadExpression(ir, ParserSub(parser, subexpression_location), ExpresionContext_from_type(string_type, 1));
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
                            BArrayAdd(&values, ValueFromString(ir->arena, literal));
                        }
                        
                        out = IRAppend(out, sub);
                        BArrayAdd(&values, value);
                    }
                    
                    continue;
                }
                
                append_codepoint(&builder, codepoint);
            }
            
            String literal = string_from_builder(context.arena, &builder);
            if (literal.size > 0) {
                BArrayAdd(&values, ValueFromString(ir->arena, literal));
            }
            
            out.value = ValueFromStringArray(ir->arena, program, ArrayFromBArray(context.arena, values));
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
            else if (StrStarts(raw, "\\x")) {
                String hexa = StrSub(raw, 2, raw.size - 2);
                if (!U32FromString(&v, hexa, 16)) {
                    ReportErrorFront(token.location, "Invalid hexadecimal number '%S'", hexa);
                    return {};
                }
            }
            else
            {
                U64 cursor = 0;
                v = StrGetCodepoint(raw, &cursor);
                
                if (v == 0xFFFD || cursor != raw.size) {
                    report_invalid_codepoint_literal(token.location, raw);
                    return {};
                }
            }
            
            return IRFromNone(ValueFromUInt(v));
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
    if (tokens.count >= 3 && tokens[0].kind == TokenKind_Identifier && tokens[tokens.count - 1].kind == TokenKind_CloseParenthesis)
    {
        B32 is_function_call = false;
        
        if (tokens[1].kind == TokenKind_OpenParenthesis) {
            is_function_call = CheckTokensAreCouple(tokens, 1, tokens.count - 1, TokenKind_OpenParenthesis, TokenKind_CloseParenthesis);
        }
        else
        {
            U32 open_parenthesis_index = tokens.count;
            
            // Check for generic type initialize
            if (tokens[1].kind == TokenKind_OpenBracket) {
                I32 depth = 1;
                for (U32 i = 2; i < tokens.count; i++) {
                    if (tokens[i].kind == TokenKind_OpenBracket) {
                        depth++;
                    }
                    if (tokens[i].kind == TokenKind_CloseBracket) {
                        depth--;
                        if (depth == 0) {
                            open_parenthesis_index = i + 1;
                            break;
                        }
                    }
                }
            }
            
            is_function_call = open_parenthesis_index < tokens.count && 
                tokens[open_parenthesis_index].kind == TokenKind_OpenParenthesis && 
                CheckTokensAreCouple(tokens, open_parenthesis_index, tokens.count - 1, TokenKind_OpenParenthesis, TokenKind_CloseParenthesis);
        }
        
        if (is_function_call)
        {
            Location call_code = LocationFromTokens(tokens);
            return ReadFunctionCall(ir, expr_context, ParserSub(parser, call_code));
        }
    }
    
    // Explicit castings
    if (tokens.count > 0 && (tokens[0].kind == TokenKind_CastKeyword || tokens[0].kind == TokenKind_BitCastKeyword))
    {
        if (tokens.count < 5 || tokens[1].kind != TokenKind_OpenParenthesis)
        {
            ReportErrorFront(location, "Wrong format for explicit casting");
            return IRFailed();
        }
        
        Token keyword_token = tokens[0];
        
        I32 end_parenthesis_index = -1;
        for (I32 i = 2; i < tokens.count; i++)
        {
            if (tokens[i].kind == TokenKind_CloseParenthesis) {
                end_parenthesis_index = i;
                break;
            }
        }
        
        if (end_parenthesis_index < 0) {
            ReportErrorFront(location, "Closing parenthesis not found");
            return IRFailed();
        }
        
        B32 bitcast = keyword_token.kind == TokenKind_BitCastKeyword;
        
        Array<Token> type_tokens = ArraySub(tokens, 2, end_parenthesis_index - 2);
        Array<Token> src_tokens = ArraySub(tokens, end_parenthesis_index + 1, tokens.count - end_parenthesis_index - 1);
        
        Type* type = ReadObjectType(ParserSub(parser, LocationFromTokens(type_tokens)), reporter, program);
        if (type == nil_type) return IRFailed();
        
        IR_Group out = ReadExpression(ir, ParserSub(parser, LocationFromTokens(src_tokens)), ExpresionContext_from_inference(1));
        if (!out.success) return IRFailed();
        
        if (out.value.type != type) {
            out = IRAppend(out, IRFromCasting(ir, out.value, type, bitcast, location));
        }
        
        return out;
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
                    OperatorKind op = OperatorKindFromToken(token.kind);
                    if (op == OperatorKind_Addition) preference = addition_preference;
                    else if (op == OperatorKind_Substraction) preference = addition_preference;
                    else if (op == OperatorKind_Multiplication) preference = multiplication_preference;
                    else if (op == OperatorKind_Division) preference = multiplication_preference;
                    else if (op == OperatorKind_Modulo) preference = multiplication_preference;
                    else if (op == OperatorKind_LogicalNot) preference = logical_preference;
                    else if (op == OperatorKind_LogicalOr) preference = logical_preference;
                    else if (op == OperatorKind_LogicalAnd) preference = logical_preference;
                    else if (op == OperatorKind_Equals) preference = boolean_preference;
                    else if (op == OperatorKind_NotEquals) preference = boolean_preference;
                    else if (op == OperatorKind_LessThan) preference = boolean_preference;
                    else if (op == OperatorKind_LessEqualsThan) preference = boolean_preference;
                    else if (op == OperatorKind_GreaterThan) preference = boolean_preference;
                    else if (op == OperatorKind_GreaterEqualsThan) preference = boolean_preference;
                    else if (op == OperatorKind_Is) preference = addition_preference;
                    else {
                        Assert(0);
                    }
                }
            }
            else if (token.kind == TokenKind_Dot && i == tokens.count - 2) {
                preference = member_preference;
            }
            
            if (preference == I32_MAX || brackets_depth != 0) continue;
            
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
        
        if (min_preference != I32_MAX)
        {
            Token op_token = tokens[preferent_operator_index];
            
            if (token_is_sign_or_binary_op(op_token.kind))
            {
                OperatorKind op = OperatorKindFromToken(op_token.kind);
                expr_context.assignment_count = Min(expr_context.assignment_count, 1);
                
                if (preference_is_sign)
                {
                    Assert(preferent_operator_index == 0);
                    
                    Array<Token> subexpr_tokens = ArraySub(tokens, 1, tokens.count - 1);
                    
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
                        Array<Token> left_expr_tokens = ArraySub(tokens, 0, preferent_operator_index);
                        Array<Token> right_expr_tokens = ArraySub(tokens, preferent_operator_index + 1, tokens.count - (preferent_operator_index + 1));
                        
                        ExpresionContext left_context = expr_context;
                        IR_Group left = ReadExpression(ir, ParserSub(parser, LocationFromTokens(left_expr_tokens)), left_context);
                        
                        ExpresionContext right_context = TypeIsValid(left.value.type) ? ExpresionContext_from_type(left.value.type, 1) : expr_context;
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
                
                Array<Token> expr_tokens = ArraySub(tokens, 0, tokens.count - 2);
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
    if (tokens.count >= 2 && tokens[0].kind == TokenKind_OpenBracket)
    {
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
        
        if (CheckTokensAreCouple(tokens, 0, close_index, TokenKind_OpenBracket, TokenKind_CloseBracket))
        {
            Type* element_type = any_type;
            if (expr_context.type != void_type) {
                element_type = expr_context.type;
                if (TypeIsArray(element_type)) {
                    element_type = TypeGetNext(program, element_type);
                }
            }
            
            if (arrow_index > 0)
            {
                Array<Token> type_tokens = ArraySub(tokens, arrow_index + 1, tokens.count - (arrow_index + 1));
                Location type_code = LocationFromTokens(type_tokens);
                element_type = ReadObjectType(ParserSub(parser, type_code), reporter, program);
                if (element_type == nil_type) return IRFailed();
                
                if (element_type == void_type || element_type == any_type) {
                    ReportErrorFront(type_code, "Invalid type for array expression");
                    return IRFailed();
                }
            }
            
            Type* expr_type = element_type;
            
            Array<Token> expr_tokens = ArraySub(tokens, 1, close_index - 1);
            IR_Group out = ReadExpressionList(context.arena, ir, expr_type, {}, ParserSub(parser, LocationFromTokens(expr_tokens)));
            Array<Value> values = ValuesFromReturn(context.arena, out.value, true);
            
            if (values.count > 0 && element_type == any_type) {
                element_type = values[0].type;
                expr_type = element_type;
            }
            
            if (element_type == any_type || element_type == void_type) {
                ReportErrorFront(location, "Unknown array type");
                return IRFailed();
            }
            
            foreach(i, values.count) {
                if (values[i].type != expr_type) {
                    ReportErrorFront(location, "Expecting an array of '%S' but found an '%S'", expr_type->name, values[i].type->name);
                    return IRFailed();
                }
            }
            
            Type* array_type = TypeFromArray(program, element_type, 1);
            out.value = ValueFromArray(ir->arena, array_type, values);
            
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
                Array<Token> src_tokens = ArraySub(tokens, 0, starting_token);
                Array<Token> index_tokens = ArraySub(tokens, starting_token + 1, tokens.count - starting_token - 2);
                
                if (src_tokens.count > 0 && index_tokens.count > 0)
                {
                    Location src_location = LocationFromTokens(src_tokens);
                    Location index_location = LocationFromTokens(index_tokens);
                    
                    IR_Group src = ReadExpression(ir, ParserSub(parser, src_location), ExpresionContext_from_void());
                    IR_Group index = ReadExpression(ir, ParserSub(parser, index_location), ExpresionContext_from_type(uint_type, 1));
                    
                    if (!src.success || !index.success) {
                        return IRFailed();
                    }
                    
                    if (src.value.kind == ValueKind_None) {
                        report_array_indexing_expects_expresion(tokens[0].location);
                        return IRFailed();
                    }
                    
                    if (!TypeIsAnyInt(index.value.type)) {
                        report_indexing_expects_an_int(location);
                        return IRFailed();
                    }
                    
                    Type* type = src.value.type;
                    
                    IR_Group out = IRAppend(src, index);
                    
                    if (TypeIsArray(type))
                    {
                        Type* element_type = TypeGetNext(program, type);
                        out = IRAppend(out, IRFromChild(ir, src.value, index.value, true, element_type, location));
                    }
                    else
                    {
                        report_indexing_not_allowed(location, type->name);
                        return IRFailed();
                    }
                    
                    return out;
                }
            }
        }
    }
    
    String expresion_string = StringFromTokens(context.arena, tokens);
    report_expr_syntactic_unknown(location, expresion_string);
    return {};
}

internal_fn B32 CastingNeeded(Program* program, Type* dst_type, Type* src_type)
{
    return dst_type != void_type && dst_type != any_type && src_type != any_type && src_type != dst_type;
}

IR_Group ReadExpressionWithCasting(IR_Context* ir, Parser* parser, ExpresionContext expr_context)
{
    PROFILE_FUNCTION;
    
    Reporter* reporter = ir->reporter;
    Program* program = ir->program;
    IR_Group out = ReadExpression(ir, parser, expr_context);
    if (!out.success) return IRFailed();
    
    Value src = out.value;
    
    Type* src_type = src.type;
    Type* dst_type = expr_context.type;
    
    Location location = LocationFromParser(parser);
    
    if (CastingNeeded(ir->program, dst_type, src_type))
    {
        if (TypeIsReference(dst_type)) dst_type = TypeGetNext(program, dst_type);
        
        if (TypeIsReference(src_type)) {
            out = IRAppend(out, IRFromDereference(ir, src, location));
            src = out.value;
            src_type = src.type;
        }
    }
    
    if (CastingNeeded(ir->program, dst_type, src_type))
    {
        B32 casting_solved = false;
        
        if (expr_context.assignment_count == 1)
        {
            if (TypeIsAnyInt(src_type) && TypeIsAnyInt(dst_type))
            {
                B32 dst_sign = dst_type == int_type;
                B32 src_sign = src_type == int_type;
                
                if (dst_sign && !src_sign) {
                    out = IRAppend(out, IRFromCasting(ir, src, dst_type, false, location));
                    casting_solved = true;
                }
            }
            else if (TypeIsAnyInt(src_type) && dst_type == float_type)
            {
                out = IRAppend(out, IRFromCasting(ir, src, dst_type, false, location));
                casting_solved = true;
            }
        }
        
        if (!casting_solved) {
            ReportErrorFront(location, "Implicit casting failed: %S to %S", src_type->name, dst_type->name);
            return IRFailed();
        }
    }
    
    return out;
}

void CheckForAnyAssumptions(IR_Context* ir, IR_Unit* unit, Value value)
{
    Program* program = ir->program;
    I32 reg_index = ValueGetRegister(value);
    
    if (reg_index < 0 || unit == NULL || unit->dst_index != reg_index) {
        return;
    }
    
    if (unit->kind == UnitKind_Is) {
        if (!TypeIsArray(unit->src0.type))
        {
            IR_Object* obj = ir_find_object_from_value(ir, unit->src0);
            Type* type = TypeFromCompiletime(program, unit->src1);
            
            if (obj != NULL && type != nil_type) {
                ir_assume_object(ir, obj, type);
            }
        }
    }
}

IR_Group ReadCode(IR_Context* ir, Parser* parser)
{
    PROFILE_FUNCTION;
    
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
            Location expression_location = FetchUntil(parser, false, TokenKind_OpenBrace, TokenKind_ThenKeyword);
            
            if (!LocationIsValid(expression_location)) {
                ReportErrorFront(first_token.location, "Expecting expression for if statement");
                return IRFailed();
            }
            
            B32 has_then_keyword = false;
            
            if (PeekToken(parser).kind == TokenKind_ThenKeyword) {
                AssumeToken(parser, TokenKind_ThenKeyword);
                has_then_keyword = true;
            }
            
            B32 is_switch = false;
            
            // Check for switch statement
            if (!has_then_keyword)
            {
                Parser* switch_expr_parser = ParserSub(parser, expression_location);
                Array<Token> tokens = ConsumeAllTokens(switch_expr_parser);
                
                if (tokens.count >= 2 && tokens[tokens.count - 1].kind == TokenKind_CompEquals)
                {
                    expression_location = LocationFromTokens(ArraySub(tokens, 0, tokens.count - 1));
                    is_switch = true;
                }
            }
            
            // Switch statement
            if (is_switch)
            {
                IR_Group expression = ReadExpression(ir, ParserSub(parser, expression_location), ExpresionContext_from_inference(1));
                if (!expression.success) return IRFailed();
                out = IRAppend(out, expression);
                
                Location code_location = FetchCode(parser);
                
                if (!LocationIsValid(code_location)) {
                    ReportErrorFront(first_token.location, "Expecting code for switch statement");
                    return IRFailed();
                }
                
                out = IRAppend(out, ReadSwitchCode(ir, ParserSub(parser, code_location), expression.value));
                if (!out.success) return IRFailed();
            }
            else
            {
                IR_Group expression = ReadExpressionWithCasting(ir, ParserSub(parser, expression_location), ExpresionContext_from_type(bool_type, 1));
                if (!expression.success) return IRFailed();
                
                CheckForAnyAssumptions(ir, expression.last, expression.value);
                
                out = IRAppend(out, expression);
                
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
            
            Location expression_location = FetchUntil(parser, false, TokenKind_OpenBrace, TokenKind_ThenKeyword);
            
            if (!LocationIsValid(expression_location)) {
                ReportErrorFront(first_token.location, "Expecting expression for while statement");
                return IRFailed();
            }
            
            if (PeekToken(parser).kind == TokenKind_ThenKeyword)
                AssumeToken(parser, TokenKind_ThenKeyword);
            
            IR_Group expression = ReadExpressionWithCasting(ir, ParserSub(parser, expression_location), ExpresionContext_from_type(bool_type, 1));
            if (!expression.success) return IRFailed();
            
            CheckForAnyAssumptions(ir, expression.last, expression.value);
            
            Location code_location = FetchCode(parser);
            
            if (!LocationIsValid(code_location)) {
                ReportErrorFront(first_token.location, "Expecting code for while statement");
                return IRFailed();
            }
            
            IR_Group code = ReadCode(ir, ParserSub(parser, code_location));
            if (!code.success) return IRFailed();
            
            out = IRAppend(out, IRFromLoop(ir, IRFromNone(), expression, code, IRFromNone(), first_token.location));
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
            
            Array<Location> splits = ArrayAlloc<Location>(context.arena, 8);
            
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
                
                IR_Group iterator = ReadExpressionWithCasting(ir, ParserSub(parser, iterator_location), ExpresionContext_from_inference(1));
                out = IRAppend(out, iterator);
                
                if (!TypeIsArray(iterator.value.type)) {
                    ReportErrorFront(location, "Invalid iterator for a for each statement");
                    return IRFailed();
                }
                
                Type* element_type = TypeGetNext(program, iterator.value.type);
                
                // Init code
                IR_Group init = IRFromDefineObject(ir, RegisterKind_Local, element_identifier, element_type, false, location);
                Value element_value = init.value;
                
                if (index_identifier.size > 0) {
                    init = IRAppend(init, IRFromDefineObject(ir, RegisterKind_Local, index_identifier, uint_type, false, location));
                }
                else {
                    init = IRAppend(init, IRFromDefineTemporal(ir, uint_type, location));
                }
                Value index_value = init.value;
                init = IRAppend(init, IRFromStore(ir, index_value, ValueFromZero(uint_type), location));
                
                // Condition code
                VariableTypeChild count_info = VTypeGetProperty(program, iterator.value.type, "count");
                IR_Group condition = IRFromChild(ir, iterator.value, ValueFromZero(uint_type), count_info.is_member, count_info.type, location);
                Value count_value = condition.value;
                condition = IRAppend(condition, IRFromBinaryOperator(ir, index_value, count_value, OperatorKind_LessThan, false, location));
                
                // Content code
                IR_Group content = IRFromChild(ir, iterator.value, index_value, true, element_type, location);
                content = IRAppend(content, IRFromStore(ir, element_value, content.value, location));
                content = IRAppend(content, ReadCode(ir, ParserSub(parser, content_location)));
                
                // Update code
                IR_Group update = IRFromAssignment(ir, false, index_value, ValueFromUInt(1), OperatorKind_Addition, location);
                
                out = IRAppend(out, IRFromLoop(ir, init, condition, content, update, first_token.location));
            }
            // Traditional C for
            else if (splits.count == 3)
            {
                IR_Group init = ReadSentence(ir, ParserSub(parser, splits[0]));
                IR_Group condition = ReadExpressionWithCasting(ir, ParserSub(parser, splits[1]), ExpresionContext_from_type(bool_type, 1));
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
        else
        {
            SentenceKind kind = GuessSentenceKind(parser);
            
            // Embedded Definitions
            if (kind == SentenceKind_FunctionDef || kind == SentenceKind_StructDef || kind == SentenceKind_EnumDef)
            {
#if 0
                CodeDefinition def;
                if (!ReadCodeDefinition(&def, parser, reporter, kind)) {
                    return IRFailed();
                }
                
                DefinitionIdentify(program, code->index, code->type, code->identifier, code->entire_location);
                
                FrontDefineFunction(front, code);
                
                FrontResolveFunction(front, def, code);
                
                out = IRAppend(out, IRFromNone());
#endif
                return IRFailed();
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
    }
    
    return out;
}

IR_Group ReadSentence(IR_Context* ir, Parser* parser)
{
    PROFILE_FUNCTION;
    
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
        BArray<String> identifiers = BArrayMake<String>(context.arena, 4);
        IR_Group out = IRFromNone();
        
        U64 dst_end_cursor = find_token_with_depth_check(parser, true, true, true, TokenKind_Assignment);
        
        if (dst_end_cursor == parser->cursor) {
            InvalidCodepath();
            return IRFailed();
        }
        
        Location dst_code = LocationMake(parser->cursor, dst_end_cursor, parser->script_id);
        out = IRAppend(out, ReadExpressionList(context.arena, ir, any_type, {}, ParserSub(parser, dst_code)));
        if (!out.success) return IRFailed();
        
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
        
        OperatorKind op = assignment_token.assignment_operator;
        
        IR_Group src;
        {
            
            B32 register_is_any = false;
            if (values.count == 1) {
                Register reg = IRRegisterFromValue(ir, values[0]);
                register_is_any = reg.kind != RegisterKind_None && reg.type == any_type;
            }
            
            Location src_location = LocationMake(parser->cursor, parser->range.max, parser->script_id);
            ExpresionContext expr_context = ExpresionContext_from_type(values[0].type, values.count);
            Parser* expr_parser = ParserSub(parser, src_location);
            if (op == OperatorKind_None && !register_is_any) src = ReadExpressionWithCasting(ir, expr_parser, expr_context);
            else src = ReadExpression(ir, expr_parser, expr_context);
        }
        out = IRAppend(out, src);
        
        if (!out.success) return IRFailed();
        
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
        
        Array<Type*> returns = ReturnsFromRegisters(context.arena, ArrayFromBArray(context.arena, ir->local_registers));
        
        Type* expected_type = void_type;
        if (returns.count == 1) expected_type = returns[0];
        
        ExpresionContext context = (expected_type == void_type) ? ExpresionContext_from_void() : ExpresionContext_from_type(expected_type, 1);
        
        Location expression_location = LocationFromParser(parser, parser->range.max);
        IR_Group expression = ReadExpressionWithCasting(ir, ParserSub(parser, expression_location), context);
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

struct SwitchCase {
    Array<Value> values;
    IR_Group group;
    Location location;
};

IR_Group ReadSwitchCode(IR_Context* ir, Parser* parser, Value src)
{
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    
    Type* type = src.type;
    
    Location location = LocationFromParser(parser);
    
    if (type->kind != VKind_Primitive && type->kind != VKind_Enum)
    {
        ReportErrorFront(location, "Invalid type '%S' for switch statement", type->name);
        return IRFailed();
    }
    
    AssumeToken(parser, TokenKind_OpenBrace);
    
    B32 has_default_case = false;
    SwitchCase default_case = {};
    BArray<SwitchCase> cases = BArrayMake<SwitchCase>(context.arena, 16);
    
    while (1)
    {
        Token first_token = PeekToken(parser);
        
        if (first_token.kind == TokenKind_CloseBrace)
        {
            AssumeToken(parser, TokenKind_CloseBrace);
            
            if (PeekToken(parser).kind != TokenKind_None) {
                ReportErrorFront(first_token.location, "Invalid closing brace");
                return IRFailed();
            }
            
            break;
        }
        
        if (first_token.kind != TokenKind_CaseKeyword)
        {
            ReportErrorFront(first_token.location, "Expecting a case sentence");
            return IRFailed();
        }
        
        AssumeToken(parser, TokenKind_CaseKeyword);
        
        Location case_location = FetchUntil(parser, false, TokenKind_NextSentence);
        if (!LocationIsValid(case_location)) {
            report_expecting_semicolon(first_token.location);
            return IRFailed();
        }
        AssumeToken(parser, TokenKind_NextSentence);
        
        IR_Group case_expression = ReadExpressionList(context.arena, ir, type, {}, ParserSub(parser, case_location));
        if (!case_expression.success) return IRFailed();
        
        Array<Value> case_values = ValuesFromReturn(context.arena, case_expression.value, true);
        for (U32 i = 0; i < case_values.count; i++)
        {
            Value case_value = case_values[i];
            if (case_value.type != type) {
                ReportErrorFront(first_token.location, "Type missmatch, case is a '%S' not a '%S'", case_value.type->name, type->name);
                return IRFailed();
            }
            
            if (case_value.kind != ValueKind_Literal) {
                ReportErrorFront(first_token.location, "Expecting a '%S' literal", type->name);
                return IRFailed();
            }
        }
        
        Location code_location = FetchUntil(parser, false, TokenKind_CaseKeyword, TokenKind_CloseBrace);
        if (!LocationIsValid(code_location)) {
            InvalidCodepath();
            return IRFailed();
        }
        
        IR_Group code = ReadCode(ir, ParserSub(parser, code_location));
        if (!code.success) return IRFailed();
        
        if (case_values.count == 0) {
            if (has_default_case) {
                ReportErrorFront(first_token.location, "Duplicated case");
                return IRFailed();
            }
            has_default_case = true;
        }
        else
        {
            // Check for duplicated cases
            for (U32 i = 0; i < case_values.count; i++)
            {
                Value case_value = case_values[i];
                
                foreach_BArray(it, &cases)
                {
                    SwitchCase c = *it.value;
                    
                    for (U32 i = 0; i < c.values.count; i++)
                    {
                        if (CompiletimeEquals(program, c.values[i], case_value)) {
                            ReportErrorFront(first_token.location, "Duplicated case");
                            return IRFailed();
                        }
                    }
                }
            }
        }
        
        SwitchCase c = {};
        c.values = case_values;
        c.group = code;
        c.location = first_token.location;
        
        if (case_values.count == 0) {
            default_case = c;
        }
        else {
            BArrayAdd(&cases, c);
        }
    }
    
    // Check for all enum values
    if (TypeIsEnum(type) && !has_default_case && cases.count != type->_enum->values.count)
    {
        ReportErrorFront(location, "Missing some values for enum '%S'", type->name);
        return IRFailed();
    }
    
    IR_Group out = IRFromNone();
    
    if (ValueIsCompiletime(src))
    {
        B32 case_match = false;
        
        foreach_BArray(it, &cases)
        {
            SwitchCase c = *it.value;
            
            for (U32 i = 0; i < c.values.count; i++)
            {
                if (CompiletimeEquals(program, src, c.values[i])) {
                    case_match = true;
                    out = IRAppend(out, c.group);
                    break;
                }
            }
        }
        
        if (!case_match && has_default_case) {
            out = IRAppend(out, default_case.group);
        }
    }
    else
    {
        IR_Unit* exit_unit = IRUnitAlloc_Empty(ir, LocationFromParser(parser));
        
        foreach_BArray(it, &cases)
        {
            SwitchCase c = *it.value;
            if (c.group.unit_count == 0) continue;
            
            IR_Unit* fail_unit = IRUnitAlloc_Empty(ir, LocationFromParser(parser));
            
            IR_Group cmp = IRFromNone();
            if (c.values.count == 1) {
                cmp = IRFromBinaryOperator(ir, src, c.values[0], OperatorKind_Equals, false, c.location);
            }
            else
            {
                cmp = IRAppend(cmp, IRFromDefineTemporal(ir, bool_type, c.location));
                Value dst = cmp.value;
                cmp = IRAppend(cmp, IRFromStore(ir, dst, ValueFromBool(false), c.location));
                
                for (U32 i = 0; i < c.values.count; i++)
                {
                    cmp = IRAppend(cmp, IRFromBinaryOperator(ir, src, c.values[i], OperatorKind_Equals, false, c.location));
                    cmp = IRAppend(cmp, IRFromBinaryOperator(ir, dst, cmp.value, OperatorKind_LogicalOr, true, c.location));
                    dst = cmp.value;
                }
            }
            
            if (!cmp.success || cmp.value.type != bool_type) return IRFailed();
            
            IR_Group fail_jump = IRFromSingle(IRUnitAlloc_Jump(ir, -1, cmp.value, fail_unit, c.location));
            IR_Group exit_jump = IRFromSingle(IRUnitAlloc_Jump(ir, 0, ValueNone(), exit_unit, c.location));
            
            out = IRAppend3(out, cmp, fail_jump);
            out = IRAppend4(out, c.group, exit_jump, IRFromSingle(fail_unit));
        }
        
        if (has_default_case) {
            out = IRAppend(out, default_case.group);
        }
        
        out = IRAppend(out, IRFromSingle(exit_unit));
    }
    
    return out;
}

IR_Group ReadFunctionCall(IR_Context* ir, ExpresionContext expr_context, Parser* parser)
{
    PROFILE_FUNCTION;
    
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    Location location = LocationFromParser(parser);
    
    Token identifier_token = PeekToken(parser);
    Assert(identifier_token.kind == TokenKind_Identifier);
    String identifier = identifier_token.value;
    
    // NOTE(Jose): If this isn't true means it's a type default initialization
    if (PeekToken(parser, identifier_token.skip_size).kind != TokenKind_OpenParenthesis || TypeFromName(program, identifier) != nil_type)
    {
        Location type_location = FetchUntil(parser, false, TokenKind_OpenParenthesis);
        Assert(LocationIsValid(type_location));
        
        Type* type = ReadObjectType(ParserSub(parser, type_location), reporter, program);
        if (type == nil_type) return IRFailed();
        
        Location expressions_location = FetchScope(parser, TokenKind_OpenParenthesis, false);
        IR_Group out = ReadExpressionList(context.arena, ir, any_type, {}, ParserSub(parser, expressions_location));
        if (!out.success) return IRFailed();
        
        Array<Value> params = ValuesFromReturn(context.arena, out.value, true);
        
        if (TypeIsArray(type) && params.count > 0)
        {
            Array<Value> dimensions = ArrayAlloc<Value>(context.arena, params.count);
            Type* element_type = TypeGetNext(program, type);
            
            for (U32 i = 0; i < params.count; i++)
            {
                out = IRAppend(out, IRFromOptionalCasting(ir, params[i], uint_type, location));
                params[i] = out.value;
                
                if (params[i].type != uint_type) {
                    ReportErrorFront(expressions_location, "Expected unsigned integers for array dimensions");
                    return IRFailed();
                }
                
                dimensions[i] = params[i];
            }
            
            out = IRAppend(out, IRFromEmptyArray(ir, TypeGetBase(program, type), dimensions, location));
        }
        else if (TypeIsList(type) && params.count > 0)
        {
            Array<Value> dimensions = ArrayAlloc<Value>(context.arena, params.count);
            Type* element_type = TypeGetNext(program, type);
            
            for (U32 i = 0; i < params.count; i++)
            {
                out = IRAppend(out, IRFromOptionalCasting(ir, params[i], uint_type, location));
                params[i] = out.value;
                
                if (params[i].type != uint_type) {
                    ReportErrorFront(expressions_location, "Expected unsigned integers for list dimensions");
                    return IRFailed();
                }
                
                dimensions[i] = params[i];
            }
            
            out = IRAppend(out, IRFromEmptyList(ir, TypeGetBase(program, type), dimensions, location));
        }
        else
        {
            if (params.count != 0) {
                ReportErrorFront(expressions_location, "No parameters supported");
                return IRFailed();
            }
            
            out = IRAppend(out, IRFromDefaultInitializer(ir, type, location));
        }
        
        return out;
    }
    else
    {
        AssumeToken(parser, TokenKind_Identifier);
        FunctionDefinition* fn = FunctionFromIdentifier(program, identifier);
        if (fn == NULL) {
            report_symbol_not_found(location, identifier);
            return IRFailed();
        }
        
        Array<Type*> expected_types = ArrayAlloc<Type*>(context.arena, fn->parameters.count);
        foreach(i, fn->parameters.count) {
            expected_types[i] = fn->parameters[i].type;
        }
        
        Location expressions_location = FetchScope(parser, TokenKind_OpenParenthesis, false);
        IR_Group out = ReadExpressionList(context.arena, ir, any_type, expected_types, ParserSub(parser, expressions_location));
        if (!out.success) return IRFailed();
        
        Array<Value> params = ValuesFromReturn(context.arena, out.value, true);
        
        IR_Group call = IRFromFunctionCall(ir, fn, params, expr_context, location);
        return IRAppend(out, call);
    }
}

internal_fn BArray<String> ExtractObjectIdentifiers(Parser* parser, Reporter* reporter, B32 require_single)
{
    Token starting_token = PeekToken(parser);
    
    BArray<String> identifiers = BArrayMake<String>(context.arena, 4);
    
    // Extract identifiers
    while (true)
    {
        Token token = ConsumeToken(parser);
        
        if (token.kind != TokenKind_Identifier) {
            ReportErrorFront(token.location, "Expecting an identifier\n");
            return {};
        }
        
        BArrayAdd(&identifiers, token.value);
        
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
    PROFILE_FUNCTION;
    
    Location location = LocationFromParser(parser);
    
    ObjectDefinitionResult res = {};
    res.out = IRFromNone();
    res.success = false;
    
    Token starting_token = PeekToken(parser);
    
    BArray<String> identifiers = ExtractObjectIdentifiers(parser, reporter, require_single);
    if (identifiers.count == 0) return res;
    
    // Explicit type
    Type* definition_type = void_type;
    
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
            
            definition_type = ReadObjectType(ParserSub(parser, type_location), reporter, program);
            if (definition_type == nil_type) {
                return res;
            }
            
            if (definition_type == void_type) {
                ReportErrorFront(starting_token.location, "Void is not a valid object: {line}");
                return res;
            }
        }
        else {
            report_objdef_expecting_type_identifier(starting_token.location);
            return res;
        }
    }
    
    res.objects = ArrayAlloc<ObjectDefinition>(arena, identifiers.count);
    
    B32 is_constant = false;
    B32 inference_type = definition_type == void_type;
    
    if (inference_type) {
        ReportErrorFront(starting_token.location, "Unsupported inference type");
        return res;
    }
    
    Token assignment_token = PeekToken(parser);
    is_constant = assignment_token.kind == TokenKind_Colon;
    
    foreach_BArray(it, &identifiers)
    {
        String identifier = StrCopy(arena, *it.value);
        Value value = ValueFromZero(definition_type);
        
        res.objects[it.index] = ObjDefMake(identifier, definition_type, location, is_constant, value);
    }
    
    res.success = true;
    return res;
}

ObjectDefinitionResult ReadObjectDefinitionWithIr(Arena* arena, Parser* parser, IR_Context* ir, B32 require_single, RegisterKind register_kind)
{
    PROFILE_FUNCTION;
    
    Program* program = ir->program;
    Reporter* reporter = ir->reporter;
    Location location = LocationFromParser(parser);
    
    ObjectDefinitionResult res = {};
    res.out = IRFromNone();
    res.success = false;
    
    Token starting_token = PeekToken(parser);
    
    BArray<String> identifiers = ExtractObjectIdentifiers(parser, reporter, require_single);
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
    Type* definition_type = void_type;
    
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
            
            definition_type = ReadObjectType(ParserSub(parser, type_location), reporter, program);
            if (definition_type == nil_type) {
                return res;
            }
            
            if (definition_type == void_type) {
                ReportErrorFront(starting_token.location, "Void is not a valid object: {line}");
                return res;
            }
        }
        else {
            report_objdef_expecting_type_identifier(starting_token.location);
            return res;
        }
    }
    
    res.objects = ArrayAlloc<ObjectDefinition>(arena, identifiers.count);
    
    B32 is_constant = false;
    B32 inference_type = definition_type == void_type;
    
    Token assignment_token = ConsumeToken(parser);
    
    if (assignment_token.kind != TokenKind_None)
    {
        B32 variable_assignment = assignment_token.kind == TokenKind_Assignment && assignment_token.assignment_operator == OperatorKind_None;
        B32 constant_assignment = assignment_token.kind == TokenKind_Colon;
        
        if (!variable_assignment && !constant_assignment) {
            report_objdef_expecting_assignment(starting_token.location);
            return res;
        }
        
        is_constant = constant_assignment;
        
        U64 assignment_end_cursor = parser->range.max;
        
        Location expression_code = LocationFromParser(parser, assignment_end_cursor);
        ExpresionContext expression_context = inference_type ? ExpresionContext_from_inference(identifiers.count) : ExpresionContext_from_type(definition_type, identifiers.count);
        IR_Group src = ReadExpressionWithCasting(ir, ParserSub(parser, expression_code), expression_context);
        if (!src.success) return res;
        
        Array<Type*> types = {};
        
        // Inference
        if (inference_type) {
            Array<Value> returns = ValuesFromReturn(context.arena, src.value, true);
            
            if (returns.count == 1) {
                types = ArrayAlloc<Type*>(context.arena, identifiers.count);
                foreach(i, types.count) types[i] = returns[0].type;
            }
            else if (returns.count > 1) {
                types = ArrayAlloc<Type*>(context.arena, returns.count);
                foreach(i, types.count) types[i] = returns[i].type;
            }
            definition_type = src.value.type;
        }
        else {
            types = ArrayAlloc<Type*>(context.arena, identifiers.count);
            foreach(i, types.count) types[i] = definition_type;
        }
        
        B32 is_any = definition_type == any_type;
        
        if (!is_any && types.count < identifiers.count) {
            ReportErrorFront(location, "Unresolved object type for definition");
            return res;
        }
        
        Array<Value> values = ArrayAlloc<Value>(context.arena, identifiers.count);
        foreach(i, values.count) {
            Type* type = types[i];
            
            if (register_kind == RegisterKind_Global)
            {
                I32 global_index = GlobalIndexFromIdentifier(program, identifiers[i]);
                if (global_index < 0) {
                    ReportErrorFront(location, "Global '%S' not found", identifiers[i]);
                    continue;
                }
                
                res.out = IRAppend(res.out, IRFromStore(ir, ValueFromGlobal(program, global_index), ValueFromZero(type), location));
            }
            else
            {
                res.out = IRAppend(res.out, IRFromDefineObject(ir, register_kind, identifiers[i], type, is_constant, location));
                res.out = IRAppend(res.out, IRFromStore(ir, res.out.value, ValueFromZero(type), location));
            }
            values[i] = res.out.value;
        }
        
        // Default src
        if (src.value.kind == ValueKind_None && !is_any) {
            src = IRFromDefaultInitializer(ir, definition_type, location);
        }
        
        res.out = IRAppend(res.out, src);
        
        if (src.value.kind != ValueKind_None) {
            IR_Group assignment = IRFromMultipleAssignment(ir, true, values, src.value, OperatorKind_None, location);
            res.out = IRAppend(res.out, assignment);
        }
        
        foreach_BArray(it, &identifiers) {
            res.objects[it.index] = ObjDefMake(StrCopy(arena, *it.value), types[it.index], location, is_constant, values[it.index]);
        }
    }
    else
    {
        if (inference_type) {
            ReportErrorFront(location, "Unspecified type");
            return res;
        }
        
        res.out = IRFromNone(ValueFromZero(definition_type));
        
        foreach_BArray(it, &identifiers)
        {
            String identifier = StrCopy(arena, *it.value);
            
            if (register_kind == RegisterKind_Global)
            {
                I32 global_index = GlobalIndexFromIdentifier(program, identifier);
                if (global_index < 0) {
                    ReportErrorFront(location, "Global '%S' not found", identifier);
                    continue;
                }
                
                res.out = IRAppend(res.out, IRFromStore(ir, ValueFromGlobal(program, global_index), ValueFromZero(definition_type), location));
                Value value = res.out.value;
                res.objects[it.index] = ObjDefMake(identifier, definition_type, location, is_constant, value);
            }
            else
            {
                res.out = IRAppend(res.out, IRFromDefineObject(ir, register_kind, identifier, definition_type, is_constant, location));
                Value value = res.out.value;
                if (register_kind != RegisterKind_Parameter) {
                    res.out = IRAppend(res.out, IRFromStore(ir, value, ValueFromZero(definition_type), location));
                }
                res.objects[it.index] = ObjDefMake(identifier, definition_type, location, is_constant, value);
            }
        }
    }
    
    res.success = true;
    return res;
}

ObjectDefinitionResult ReadDefinitionList(Arena* arena, Parser* parser, Reporter* reporter, Program* program, RegisterKind register_kind)
{
    PROFILE_FUNCTION;
    
    ObjectDefinitionResult res = {};
    res.out = IRFromNone();
    
    // Empty list
    if (PeekToken(parser).kind == TokenKind_None)
    {
        res.success = true;
        return res;
    }
    
    BArray<ObjectDefinition> list = BArrayMake<ObjectDefinition>(context.arena, 8);
    
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
            BArrayAdd(&list, res0.objects[i]);
        }
        
        res.out = IRAppend(res.out, res0.out);
        
        ConsumeToken(parser);
    }
    
    res.objects = ArrayFromBArray(arena, list);
    res.success = true;
    return res;
}

ObjectDefinitionResult ReadDefinitionListWithIr(Arena* arena, Parser* parser, IR_Context* ir, RegisterKind register_kind)
{
    PROFILE_FUNCTION;
    
    ObjectDefinitionResult res = {};
    res.out = IRFromNone();
    
    // Empty list
    if (PeekToken(parser).kind == TokenKind_None)
    {
        res.success = true;
        return res;
    }
    
    BArray<ObjectDefinition> list = BArrayMake<ObjectDefinition>(context.arena, 8);
    
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
            BArrayAdd(&list, res0.objects[i]);
        }
        
        res.out = IRAppend(res.out, res0.out);
        
        ConsumeToken(parser);
    }
    
    res.objects = ArrayFromBArray(arena, list);
    res.success = true;
    return res;
}

IR_Group ReadExpressionList(Arena* arena, IR_Context* ir, Type* type, Array<Type*> expected_types, Parser* parser)
{
    PROFILE_FUNCTION;
    
    IR_Group out = IRFromNone();
    BArray<Value> list = BArrayMake<Value>(context.arena, 8);
    
    while (parser->cursor < parser->range.max)
    {
        U64 expression_end_cursor = find_token_with_depth_check(parser, true, true, true, TokenKind_Comma);
        
        if (parser->cursor == expression_end_cursor) {
            expression_end_cursor = parser->range.max;
        }
        
        U32 index = list.count;
        Type* expected_type = type;
        if (index < expected_types.count) expected_type = expected_types[index];
        
        Location expression_location = LocationMake(parser->cursor, expression_end_cursor, parser->script_id);
        
        out = IRAppend(out, ReadExpressionWithCasting(ir, ParserSub(parser, expression_location), ExpresionContext_from_type(expected_type, 1)));
        if (!out.success) return IRFailed();
        
        BArrayAdd(&list, out.value);
        
        MoveCursor(parser, expression_end_cursor);
        ConsumeToken(parser);
    }
    
    out.value = ValueFromReturn(arena, ArrayFromBArray(context.arena, list));
    
    return out;
}

Type* ReadObjectType(Parser* parser, Reporter* reporter, Program* program)
{
    PROFILE_FUNCTION;
    
    Location location = LocationFromParser(parser);
    Array<Token> tokens = ConsumeAllTokens(parser);
    
    if (tokens.count == 0)
    {
        ReportErrorFront(location, "Expecting a type");
        return nil_type;
    }
    
    Token identifier_token = tokens[0];
    if (identifier_token.kind != TokenKind_Identifier) {
        report_objdef_expecting_type_identifier(identifier_token.location);
        return nil_type;
    }
    
    tokens = ArraySub(tokens, 1, tokens.count - 1);
    
    B32 is_reference = false;
    if (tokens.count > 0 && tokens[tokens.count - 1].kind == TokenKind_Ampersand)
    {
        is_reference = true;
        tokens = ArraySub(tokens, 0, tokens.count - 1);
    }
    
    U32 generic_params_expected = 0;
    
    if (identifier_token.value == "Array" || identifier_token.value == "List") generic_params_expected = 1;
    
    Type* base_type = nil_type;
    
    if (generic_params_expected > 0)
    {
        if (tokens.count <= 2) {
            ReportErrorFront(location, "Invalid type format");
            return nil_type;
        }
        if (tokens[0].kind != TokenKind_OpenBracket || tokens[tokens.count - 1].kind != TokenKind_CloseBracket) {
            ReportErrorFront(location, "Missing brackets for generic type: %S", identifier_token.value);
            return nil_type;
        }
        
        tokens = ArraySub(tokens, 1, tokens.count - 2);
        Assert(tokens.count > 0);
        
        Array<Type*> subtypes = ArrayAlloc<Type*>(context.arena, generic_params_expected);
        U32 type_index = 0;
        
        while (tokens.count)
        {
            U32 end_index = tokens.count;
            for (U32 i = 0; i < tokens.count; i++) {
                if (tokens[i].kind == TokenKind_Comma) {
                    end_index = i;
                    break;
                }
            }
            
            Location subtype_location = LocationFromTokens(ArraySub(tokens, 0, end_index));
            tokens = ArraySub(tokens, end_index, tokens.count - end_index);
            
            Type* subtype = ReadObjectType(ParserSub(parser, subtype_location), reporter, program);
            if (subtype == nil_type) return nil_type;
            
            if (subtype->kind != VKind_Primitive && !TypeIsStruct(subtype) && !TypeIsEnum(subtype)) {
                ReportErrorFront(location, "Generics don't support generics as a pararameter");
                return nil_type;
            }
            
            if (type_index >= subtypes.count) {
                ReportErrorFront(location, "Expected %u params for generic '%S'", generic_params_expected, identifier_token.value);
                return nil_type;
            }
            
            subtypes[type_index++] = subtype;
        }
        
        if (identifier_token.value == "Array") {
            base_type = TypeFromArray(program, subtypes[0], 1);
        }
        else if (identifier_token.value == "List") {
            base_type = TypeFromList(program, subtypes[0], 1);
        }
        else {
            InvalidCodepath();
            return nil_type;
        }
    }
    else
    {
        if (tokens.count != 0) {
            ReportErrorFront(location, "Invalid type format");
            return nil_type;
        }
        
        base_type = TypeFromName(program, identifier_token.value);
    }
    
    if (base_type == nil_type) {
        ReportErrorFront(location, "Unknown type");
        return nil_type;
    }
    
    Type* type = base_type;
    if (is_reference) type = TypeFromReference(program, type);
    return type;
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
    if (k == TokenKind_ThenKeyword) return "then";
    if (k == TokenKind_CaseKeyword) return "case";
    if (k == TokenKind_ElseKeyword) return "else";
    if (k == TokenKind_WhileKeyword) return "while";
    if (k == TokenKind_ForKeyword) return "for";
    if (k == TokenKind_EnumKeyword) return "enum";
    if (k == TokenKind_IntLiteral) return StrFormat(arena, "Int Literal: %S", token.value);
    if (k == TokenKind_FloatLiteral) return StrFormat(arena, "Float Literal: %S", token.value);
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
        if (token.assignment_operator == OperatorKind_None) return "=";
        return StrFormat(arena, "%S=", StringFromOperatorKind(token.assignment_operator));
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

OperatorKind OperatorKindFromToken(TokenKind token)
{
    if (token == TokenKind_PlusSign) return OperatorKind_Addition;
    if (token == TokenKind_MinusSign) return OperatorKind_Substraction;
    if (token == TokenKind_Asterisk) return OperatorKind_Multiplication;
    if (token == TokenKind_Slash) return OperatorKind_Division;
    if (token == TokenKind_Modulo) return OperatorKind_Modulo;
    if (token == TokenKind_Ampersand) return OperatorKind_None;
    if (token == TokenKind_Exclamation) return OperatorKind_LogicalNot;
    if (token == TokenKind_LogicalOr) return OperatorKind_LogicalOr;
    if (token == TokenKind_LogicalAnd) return OperatorKind_LogicalAnd;
    if (token == TokenKind_CompEquals) return OperatorKind_Equals;
    if (token == TokenKind_CompNotEquals) return OperatorKind_NotEquals;
    if (token == TokenKind_CompLess) return OperatorKind_LessThan;
    if (token == TokenKind_CompLessEquals) return OperatorKind_LessEqualsThan;
    if (token == TokenKind_CompGreater) return OperatorKind_GreaterThan;
    if (token == TokenKind_CompGreaterEquals) return OperatorKind_GreaterEqualsThan;
    if (token == TokenKind_IsKeyword) return OperatorKind_Is;
    return OperatorKind_None;
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

Location LocationFromToken(Token token) {
    return LocationMake(token.cursor, token.cursor + token.skip_size, token.location.script_id);
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


internal_fn Token TokenMakeDynamic(String text, U64 cursor, TokenKind kind, U64 size, I32 script_id)
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
        else if (StrEquals("then", token.value)) kind = TokenKind_ThenKeyword;
        else if (StrEquals("case", token.value)) kind = TokenKind_CaseKeyword;
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
        else if (StrEquals("cast", token.value)) kind = TokenKind_CastKeyword;
        else if (StrEquals("bitcast", token.value)) kind = TokenKind_BitCastKeyword;
    }
    
    token.kind = kind;
    
    // Discard double quotes
    if (token.kind == TokenKind_StringLiteral) {
        Assert(token.value.size >= 2);
        token.value = StrSub(token.value, 1, token.value.size - 2);
    }
    
    return token;
}

internal_fn Token TokenMakeFixed(String text, U64 cursor, TokenKind kind, U32 codepoint_length, I32 script_id)
{
    U64 start_cursor = cursor;
    
    U32 index = 0;
    while (index < codepoint_length && cursor < text.size) {
        index++;
        StrGetCodepoint(text, &cursor);
    }
    
    return TokenMakeDynamic(text, start_cursor, kind, cursor - start_cursor, script_id);
}

internal_fn Token TokenFromAssignment(String text, U64 cursor, OperatorKind op, U32 codepoint_length, I32 script_id)
{
    Token token = TokenMakeFixed(text, cursor, TokenKind_Assignment, codepoint_length, script_id);
    token.assignment_operator = op;
    return token;
}

Token ReadToken(String text, U64 start_cursor, I32 script_id)
{
    PROFILE_FUNCTION;
    
    U32 c0, c1;
    
    {
        U64 cursor = start_cursor;
        c0 = (cursor < text.size) ? StrGetCodepoint(text, &cursor) : 0;
        c1 = (cursor < text.size) ? StrGetCodepoint(text, &cursor) : 0;
    }
    
    if (c0 == 0) {
        return TokenMakeFixed(text, start_cursor, TokenKind_NextLine, 1, script_id);
    }
    
    if (CodepointIsSeparator(c0))
    {
        U64 cursor = start_cursor;
        while (cursor < text.size) {
            U64 next_cursor = cursor;
            U32 codepoint = StrGetCodepoint(text, &next_cursor);
            if (!CodepointIsSeparator(codepoint)) {
                break;
            }
            cursor = next_cursor;
        }
        return TokenMakeDynamic(text, start_cursor, TokenKind_Separator, cursor - start_cursor, script_id);
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
        return TokenMakeDynamic(text, start_cursor, TokenKind_Comment, cursor - start_cursor, script_id);
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
        return TokenMakeDynamic(text, start_cursor, TokenKind_Comment, cursor - start_cursor, script_id);
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
        return TokenMakeDynamic(text, start_cursor, TokenKind_StringLiteral, cursor - start_cursor, script_id);
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
        return TokenMakeDynamic(text, start_cursor, TokenKind_CodepointLiteral, cursor - start_cursor, script_id);
    }
    
    if (c0 == '-' && c1 == '>') return TokenMakeFixed(text, start_cursor, TokenKind_Arrow, 2, script_id);
    
    if (c0 == ',') return TokenMakeFixed(text, start_cursor, TokenKind_Comma, 1, script_id);
    if (c0 == '.') return TokenMakeFixed(text, start_cursor, TokenKind_Dot, 1, script_id);
    if (c0 == '{') return TokenMakeFixed(text, start_cursor, TokenKind_OpenBrace, 1, script_id);
    if (c0 == '}') return TokenMakeFixed(text, start_cursor, TokenKind_CloseBrace, 1, script_id);
    if (c0 == '[') return TokenMakeFixed(text, start_cursor, TokenKind_OpenBracket, 1, script_id);
    if (c0 == ']') return TokenMakeFixed(text, start_cursor, TokenKind_CloseBracket, 1, script_id);
    if (c0 == '"') return TokenMakeFixed(text, start_cursor, TokenKind_OpenString, 1, script_id);
    if (c0 == '(') return TokenMakeFixed(text, start_cursor, TokenKind_OpenParenthesis, 1, script_id);
    if (c0 == ')') return TokenMakeFixed(text, start_cursor, TokenKind_CloseParenthesis, 1, script_id);
    if (c0 == ':') return TokenMakeFixed(text, start_cursor, TokenKind_Colon, 1, script_id);
    if (c0 == ';') return TokenMakeFixed(text, start_cursor, TokenKind_NextSentence, 1, script_id);
    if (c0 == '\n') return TokenMakeFixed(text, start_cursor, TokenKind_NextLine, 1, script_id);
    if (c0 == '_') return TokenMakeFixed(text, start_cursor, TokenKind_Identifier, 1, script_id);
    
    if (c0 == '+' && c1 == '=') return TokenFromAssignment(text, start_cursor, OperatorKind_Addition, 2, script_id);
    if (c0 == '-' && c1 == '=') return TokenFromAssignment(text, start_cursor, OperatorKind_Substraction, 2, script_id);
    if (c0 == '*' && c1 == '=') return TokenFromAssignment(text, start_cursor, OperatorKind_Multiplication, 2, script_id);
    if (c0 == '/' && c1 == '=') return TokenFromAssignment(text, start_cursor, OperatorKind_Division, 2, script_id);
    if (c0 == '%' && c1 == '=') return TokenFromAssignment(text, start_cursor, OperatorKind_Modulo, 2, script_id);
    
    if (c0 == '=' && c1 == '=') return TokenMakeFixed(text, start_cursor, TokenKind_CompEquals, 2, script_id);
    if (c0 == '!' && c1 == '=') return TokenMakeFixed(text, start_cursor, TokenKind_CompNotEquals, 2, script_id);
    if (c0 == '<' && c1 == '=') return TokenMakeFixed(text, start_cursor, TokenKind_CompLessEquals, 2, script_id);
    if (c0 == '>' && c1 == '=') return TokenMakeFixed(text, start_cursor, TokenKind_CompGreaterEquals, 2, script_id);
    
    if (c0 == '|' && c1 == '|') return TokenMakeFixed(text, start_cursor, TokenKind_LogicalOr, 2, script_id);
    if (c0 == '&' && c1 == '&') return TokenMakeFixed(text, start_cursor, TokenKind_LogicalAnd, 2, script_id);
    
    if (c0 == '=') return TokenFromAssignment(text, start_cursor, OperatorKind_None, 1, script_id);
    
    if (c0 == '<') return TokenMakeFixed(text, start_cursor, TokenKind_CompLess, 1, script_id);
    if (c0 == '>') return TokenMakeFixed(text, start_cursor, TokenKind_CompGreater, 1, script_id);
    
    if (CodepointIsNumber(c0))
    {
        U32 dot_count = 0;
        U64 cursor = start_cursor;
        
        while (cursor < text.size) {
            U64 next_cursor = cursor;
            U32 codepoint = StrGetCodepoint(text, &next_cursor);
            
            if (CodepointIsNumber(codepoint)) { }
            else if (codepoint == '.') {
                dot_count++;
            }
            else {
                break;
            }
            
            cursor = next_cursor;
        }
        
        TokenKind kind;
        
        if (dot_count == 0) {
            kind = TokenKind_IntLiteral;
        }
        else if (dot_count == 1) {
            kind = TokenKind_FloatLiteral;
        }
        else {
            kind = TokenKind_Error;
        }
        
        return TokenMakeDynamic(text, start_cursor, kind, cursor - start_cursor, script_id);
    }
    
    if (CodepointIsText(c0))
    {
        U64 cursor = start_cursor;
        while (cursor < text.size) {
            U64 next_cursor = cursor;
            U32 codepoint = StrGetCodepoint(text, &next_cursor);
            if (!CodepointIsText(codepoint) && !CodepointIsNumber(codepoint) && codepoint != '_') {
                break;
            }
            cursor = next_cursor;
        }
        return TokenMakeDynamic(text, start_cursor, TokenKind_Identifier, cursor - start_cursor, script_id);
    }
    
    if (c0 == '+') return TokenMakeFixed(text, start_cursor, TokenKind_PlusSign, 1, script_id);
    if (c0 == '-') return TokenMakeFixed(text, start_cursor, TokenKind_MinusSign, 1, script_id);
    if (c0 == '*') return TokenMakeFixed(text, start_cursor, TokenKind_Asterisk, 1, script_id);
    if (c0 == '/') return TokenMakeFixed(text, start_cursor, TokenKind_Slash, 1, script_id);
    if (c0 == '%') return TokenMakeFixed(text, start_cursor, TokenKind_Modulo, 1, script_id);
    
    if (c0 == '&') return TokenMakeFixed(text, start_cursor, TokenKind_Ampersand, 1, script_id);
    if (c0 == '!') return TokenMakeFixed(text, start_cursor, TokenKind_Exclamation, 1, script_id);
    
    return TokenMakeFixed(text, start_cursor, TokenKind_Error, 1, script_id);
}

Token ReadValidToken(String text, U64 start_cursor, U64 end_cursor, I32 script_id)
{
    Assert(end_cursor <= text.size);
    
    U32 total_skip_size = 0;
    Token token;
    
    while (true) {
        token = {};
        I64 cursor = (I64)start_cursor + total_skip_size;
        if (cursor < 0 || cursor >= end_cursor) break;
        
        token = ReadToken(text, cursor, script_id);
        
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
