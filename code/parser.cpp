#include "inc.h"

Parser* ParserAlloc(String text, RangeU64 range, I32 script_id)
{
    Parser* parser = ArenaPushStruct<Parser>(context.arena);
    parser->script_id = script_id;
    parser->text = text;
    parser->cursor = range.min;
    parser->range = range;
    
    return parser;
}

Parser* ParserFromLocation(Location location)
{
    YovScript* script = GetScript(location.script_id);
    if (script == NULL) {
        InvalidCodepath();
        return ParserAlloc("", {}, -1);
    }
    
    return ParserAlloc(script->text, location.range, location.script_id);
}

Location LocationFromParser(Parser* parser, U64 end) {
    return LocationMake(parser->cursor, end, parser->script_id);
}

Token peek_token(Parser* parser, I64 cursor_offset)
{
    Token token = read_valid_token(parser->text, parser->cursor + cursor_offset, parser->range.max, parser->script_id);
    return token;
}

void skip_token(Parser* parser, Token token)
{
    Assert(parser->cursor == token.cursor);
    parser->cursor += token.skip_size;
    Assert(parser->cursor <= parser->range.max);
}

void assume_token(Parser* parser, TokenKind kind)
{
    Token token = consume_token(parser);
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

Token consume_token(Parser* parser)
{
    Token token = peek_token(parser);
    skip_token(parser, token);
    return token;
}

Array<Token> ConsumeAllTokens(Parser* parser)
{
    PooledArray<Token> tokens = pooled_array_make<Token>(context.arena, 16);
    
    while (true)
    {
        Token token = consume_token(parser);
        if (token.kind == TokenKind_None) break;
        array_add(&tokens, token);
    }
    
    return array_from_pooled_array(context.arena, tokens);
}

Location FindUntil(Parser* parser, B32 include_match, TokenKind match0, TokenKind match1)
{
    U64 offset = 0;
    
    Token token;
    while (true)
    {
        token = peek_token(parser, offset);
        if (token.kind == TokenKind_None) return NO_CODE;
        
        if (token.kind == match0) break;
        if (token.kind == match1) break;
        offset += token.skip_size;
    }
    
    if (include_match) {
        offset += token.skip_size;
    }
    
    return LocationMake(parser->cursor, parser->cursor + offset, parser->script_id);
}

U64 find_token_with_depth_check(Parser* parser, B32 parenthesis, B32 braces, B32 brackets, TokenKind match0, TokenKind match1)
{
    U64 offset = 0;
    
    I32 depth = 0;
    
    while (true)
    {
        Token token = peek_token(parser, offset);
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
    
    Token token = peek_token(parser);
    
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
        token = peek_token(parser, offset);
    }
    
    U64 begin = parser->cursor;
    if (!include_delimiters) {
        begin += peek_token(parser).skip_size;
    }
    
    U64 end = parser->cursor + offset;
    if (include_delimiters) {
        end += token.skip_size;
    }
    
    return LocationMake(begin, end, parser->script_id);
}

Location FindCode(Parser* parser)
{
    Token first = peek_token(parser);
    
    if (first.kind == TokenKind_OpenBrace) {
        return FindScope(parser, TokenKind_OpenBrace, true);
    }
    
    if (TokenIsFlowModifier(first.kind))
    {
        U64 start_cursor = parser->cursor;
        defer(MoveCursor(parser, start_cursor));
        
        consume_token(parser); // Modifier keyword
        
        Location optional_parenthesis_location = FetchScope(parser, TokenKind_OpenParenthesis, true);
        Location body_location = FetchCode(parser);
        
        if (!LocationIsValid(body_location)) {
            return NO_CODE;
        }
        
        if (first.kind == TokenKind_IfKeyword && peek_token(parser).kind == TokenKind_ElseKeyword) {
            consume_token(parser);
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
            assume_token(parser, close_token);
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

SentenceKind find_sentence_kind(Parser* parser)
{
    U64 start_cursor = parser->cursor;
    defer(MoveCursor(parser, start_cursor));
    
    U32 assignment_count = 0;
    U32 colon_count = 0;
    
    Token tokens[5] = {};
    TokenKind close_token = TokenKind_None;
    
    U32 index = 0;
    while (true) {
        Token token = consume_token(parser);
        
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

void ReadEnumDefinition(CodeDefinition* code)
{
    EnumDefinition* def = EnumFromIndex(code->index);
    if (def == NULL) {
        InvalidCodepath();
        return;
    }
    
    Parser* parser = ParserFromLocation(code->enum_or_struct.body_location);
    
    Location starting_location = peek_token(parser).location;
    
    assume_token(parser, TokenKind_OpenBrace);
    
    PooledArray<String> names = pooled_array_make<String>(context.arena, 16);
    PooledArray<Location> expression_locations = pooled_array_make<Location>(context.arena, 16);
    
    while (true)
    {
        Token name_token = consume_token(parser);
        if (name_token.kind == TokenKind_CloseBrace) break;
        if (name_token.kind != TokenKind_Identifier) {
            report_enumdef_expecting_comma_separated_identifier(name_token.location);
            return;
        }
        
        Token assignment_token = peek_token(parser);
        
        Location expression_location = NO_CODE;
        
        if (assignment_token.kind == TokenKind_Assignment && assignment_token.assignment_binary_operator == BinaryOperator_None) {
            skip_token(parser, assignment_token);
            
            expression_location = FetchUntil(parser, false, TokenKind_CloseBrace, TokenKind_Comma);
            
            if (!LocationIsValid(expression_location)) {
                report_error(assignment_token.location, "Expecting expression for enum value");
                return;
            }
        }
        
        array_add(&names, name_token.value);
        array_add(&expression_locations, expression_location);
        
        Token comma_token = consume_token(parser);
        if (comma_token.kind == TokenKind_CloseBrace) break;
        if (comma_token.kind != TokenKind_Comma) {
            report_enumdef_expecting_comma_separated_identifier(comma_token.location);
            return;
        }
    }
    
    EnumDefine(def, array_from_pooled_array(context.arena, names), array_from_pooled_array(context.arena, expression_locations));
}

void ResolveEnumDefinition(EnumDefinition* def)
{
    if (def->stage == DefinitionStage_Ready) {
        return;
    }
    
    if (def->stage != DefinitionStage_Defined) {
        InvalidCodepath();
        return;
    }
    
    IR_Context* ir_context = ir_context_alloc();
    
    Array<I64> values = array_make<I64>(context.arena, def->names.count);
    
    for (U32 i = 0; i < values.count; i++)
    {
        values[i] = i;
        
        if (i < def->expression_locations.count)
        {
            Location expression_location = def->expression_locations[i];
            
            if (!LocationIsValid(expression_location)) continue;
            
            IR ir = ir_generate_from_value(ValueFromInt(values[i]));
            
            ExpresionContext expr_context = ExpresionContext_from_vtype(VType_Int, 1);
            IR_Group group = ReadExpression(ir_context, expression_location, expr_context);
            ir = MakeIR(context.arena, array_from_pooled_array(context.arena, ir_context->local_registers), group);
            if (!ir.success) return;
            
            if (ir.value.kind != ValueKind_Literal || ir.value.vtype != VType_Int) {
                report_error(expression_location, "Enum value expects an Int literal");
                return;
            }
            
            values[i] = ir.value.literal_int;
        }
    }
    
    EnumResolve(def, values);
}

void ReadStructDefinition(CodeDefinition* code)
{
    StructDefinition* def = StructFromIndex(code->index);
    if (def == NULL) {
        InvalidCodepath();
        return;
    }
    
    Parser* parser = ParserFromLocation(code->enum_or_struct.body_location);
    
    Location starting_location = peek_token(parser).location;
    assume_token(parser, TokenKind_OpenBrace);
    
    U32 member_index = 0;
    
    PooledArray<ObjectDefinition> members = pooled_array_make<ObjectDefinition>(context.arena, 16);
    
    while (peek_token(parser).kind != TokenKind_CloseBrace)
    {
        Location member_location = FetchUntil(parser, false, TokenKind_NextSentence);
        if (!LocationIsValid(member_location)) {
            report_expecting_semicolon(LocationFromParser(parser, parser->cursor));
            return;
        }
        
        ObjectDefinitionResult read_result = ReadObjectDefinition(context.arena, member_location, false, RegisterKind_Local, NULL);
        if (!read_result.success) return;
        
        foreach(i, read_result.objects.count) {
            ObjectDefinition member = read_result.objects[i];
            array_add(&members, member);
            
            //out = ir_append(out, ir_from_child(ir_context, ret, value_from_int(member_index), true, member.vtype, member.location));
            //Value dst = out.value;
            //out = ir_append(out, ir_from_assignment(ir_context, true, dst, member.value, BinaryOperator_None, member.location));
            
            member_index++;
        }
        
        assume_token(parser, TokenKind_NextSentence);
    }
    
    consume_token(parser);
    
    VType struct_vtype = vtype_from_name(def->identifier);
    Assert(struct_vtype.kind == VKind_Struct);
    
    B32 valid = true;
    
    for(auto it = pooled_array_make_iterator(&members); it.valid; ++it)
    {
        ObjectDefinition* def = it.value;
        
        VType vtype = def->vtype;
        if (vtype == VType_Nil) {
            valid = false;
            continue;
        }
        
        if (vtype.kind == VKind_Any) {
            report_error(def->location, "Any is not a valid member for a struct");
            valid = false;
            continue;
        }
        
        if (vtype == struct_vtype) {
            report_struct_recursive(def->location);
            valid = false;
            continue;
        }
    }
    
    if (!valid) return;
    
    StructDefine(def, array_from_pooled_array(context.arena, members));
}

B32 ResolveStructDefinition(StructDefinition* def)
{
    if (def->stage == DefinitionStage_Ready) {
        return true;
    }
    
    if (def->stage != DefinitionStage_Defined) {
        InvalidCodepath();
        return false;
    }
    
    // Check for dependencies
    foreach(i, def->vtypes.count)
    {
        VType vtype = def->vtypes[i];
        
        if (!VTypeIsSizeReady(vtype)) {
            return false;
        }
    }
    
    StructResolve(def);
    return true;
}

void ReadFunctionDefinition(CodeDefinition* code)
{
    FunctionDefinition* def = FunctionFromIndex(code->index);
    if (def == NULL) {
        InvalidCodepath();
        return;
    }
    
    Array<ObjectDefinition> parameters = {};
    Array<ObjectDefinition> returns = {};
    
    if (LocationIsValid(code->function.parameters_location)) {
        ObjectDefinitionResult res = ReadDefinitionList(context.arena, code->function.parameters_location, RegisterKind_Parameter, NULL);
        if (!res.success) return;
        parameters = res.objects;
    }
    
    if (LocationIsValid(code->function.returns_location)) {
        if (code->function.return_is_list) {
            ObjectDefinitionResult res = ReadDefinitionList(context.arena, code->function.returns_location, RegisterKind_Return, NULL);
            if (!res.success) return;
            returns = res.objects;
        }
        else {
            VType vtype = ReadObjectType(code->function.returns_location);
            
            if (vtype != VType_Nil) {
                returns = array_make<ObjectDefinition>(context.arena, 1);
                returns[0] = ObjDefMake("return", vtype, code->function.returns_location, false, ValueFromZero(vtype));
            }
        }
        
        if (returns.count == 0) return;
    }
    
    FunctionDefine(def, parameters, returns);
}

void ResolveFunctionDefinition(FunctionDefinition* def, CodeDefinition* code)
{
    if (def->stage == DefinitionStage_Ready) {
        return;
    }
    
    if (def->stage != DefinitionStage_Defined) {
        InvalidCodepath();
        return;
    }
    
    B32 is_intrinsic = !LocationIsValid(code->function.body_location);
    
    if (is_intrinsic)
    {
        IntrinsicFunction* fn = IntrinsicFromIdentifier(def->identifier);
        
        if (fn == NULL) {
            report_intrinsic_not_resolved(code->entire_location, def->identifier);
            return;
        }
        
        FunctionResolveIntrinsic(def, fn);
    }
    else
    {
        Location block_location = code->function.body_location;
        
        IR_Context* ir = ir_context_alloc();
        IR_Group out = ir_from_none();
        
        if (LocationIsValid(code->function.parameters_location)) {
            ObjectDefinitionResult params = ReadDefinitionList(context.arena, code->function.parameters_location, RegisterKind_Parameter, ir);
            if (!params.success) return;
            
            out = ir_append(out, params.out);
        }
        
        // Define returns
        foreach(i, def->returns.count)
        {
            ObjectDefinition obj = def->returns[i];
            out = ir_append(out, IRFromDefineObject(ir, RegisterKind_Return, obj.name, obj.vtype, false, obj.location));
            out = ir_append(out, ir_from_store(ir, out.value, ValueFromZero(obj.vtype), obj.location));
        }
        
        out = ir_append(out, ReadCode(ir, block_location));
        
        IR res = MakeIR(yov->arena, array_from_pooled_array(context.arena, ir->local_registers), out);
        
        // Validation
        if (res.success)
        {
            Array<Unit> units = res.instructions;
            
            // TODO(Jose): Check for infinite loops
            
            // Check all paths have a return
            B32 expects_return = def->returns.count == 1 && StrEquals(def->returns[0].name, "return");
            if (expects_return && !ir_validate_return_path(units)) {
                report_function_no_return(block_location, def->identifier);
            }
        }
        
        FunctionResolve(def, res);
    }
}

internal_fn B32 ValidateArgName(String name, Location location)
{
    B32 valid_chars = true;
    
    U64 cursor = 0;
    while (cursor < name.size) {
        U32 codepoint = string_get_codepoint(name, &cursor);
        
        B32 valid = false;
        if (codepoint_is_text(codepoint)) valid = true;
        if (codepoint_is_number(codepoint)) valid = true;
        if (codepoint == '-') valid = true;
        if (codepoint == '_') valid = true;
        
        if (!valid) {
            valid_chars = false;
            break;
        }
    }
    
    if (!valid_chars || name.size == 0) {
        report_arg_invalid_name(location, name);
        return false;
    }
    
    return true;
}

void ReadArgDefinition(CodeDefinition* code)
{
    ArgDefinition* def = ArgFromIndex(code->index);
    if (def == NULL) {
        InvalidCodepath();
        return;
    }
    
    VType vtype = VType_Bool;
    
    if (LocationIsValid(code->arg.type_location))
    {
        vtype = ReadObjectType(code->arg.type_location);
        
        if (vtype == VType_Nil) return;
        
        if (!VTypeValid(vtype)) {
            report_error(code->arg.type_location, "Invalid type '%S' for an argument", VTypeGetName(vtype));
            return;
        }
    }
    
    ArgDefine(def, vtype);
}

void ResolveArgDefinition(CodeDefinition* code)
{
    ArgDefinition* def = ArgFromIndex(code->index);
    
    if (def == NULL) {
        InvalidCodepath();
        return;
    }
    
    if (def->stage == DefinitionStage_Ready) {
        return;
    }
    
    String name = StrFormat(context.arena, "-%S", code->identifier);
    String description = {};
    B32 required = false;
    Value default_value = ValueFromZero(def->vtype);
    
    Parser* parser = ParserFromLocation(code->arg.body_location);
    
    assume_token(parser, TokenKind_OpenBrace);
    
    while (true)
    {
        Token identifier_token = consume_token(parser);
        
        if (identifier_token.kind == TokenKind_CloseBrace) break;
        if (identifier_token.kind == TokenKind_None) {
            report_error(identifier_token.location, "Missing close brace for arg definition");
            return;
        }
        
        if (identifier_token.kind != TokenKind_Identifier) {
            report_error(identifier_token.location, "Expecting property list");
            return;
        }
        
        Token assignment_token = consume_token(parser);
        if (assignment_token.kind != TokenKind_Assignment || assignment_token.assignment_binary_operator != BinaryOperator_None)
        {
            report_error(assignment_token.location, "Expecting a property assignment");
            return;
        }
        
        Location expression_location = FetchUntil(parser, false, TokenKind_NextSentence);
        if (!LocationIsValid(expression_location)) {
            report_error(identifier_token.location, "Missing semicolon");
            return;
        }
        
        assume_token(parser, TokenKind_NextSentence);
        
        String identifier = identifier_token.value;
        
        ExpresionContext expr_context = ExpresionContext_from_inference(1);
        
        if (identifier == "name") expr_context = ExpresionContext_from_vtype(VType_String, 1);
        else if (identifier == "description") expr_context = ExpresionContext_from_vtype(VType_String, 1);
        else if (identifier == "required") expr_context = ExpresionContext_from_vtype(VType_Bool, 1);
        else if (identifier == "default") expr_context = ExpresionContext_from_vtype(def->vtype, 1);
        else {
            report_error(identifier_token.location, "Unknown property '%S'", identifier);
            return;
        }
        
        IR_Context* ir_context = ir_context_alloc();
        IR_Group group = ReadExpression(ir_context, expression_location, expr_context);
        
        IR ir = MakeIR(context.arena, array_from_pooled_array(context.arena, ir_context->local_registers), group);
        if (!ir.success) {
            return;
        }
        
        Value value = ir.value;
        
        if (value.vtype != expr_context.vtype) {
            report_type_missmatch_assign(expression_location, value.vtype, expr_context.vtype);
            return;
        }
        
        if (!ValueIsCompiletime(value)) {
            report_error(expression_location, "Expecting a compile-time value");
            return;
        }
        
        if (identifier == "name") {
            name = StringFromCompiletime(context.arena, value);
        }
        else if (identifier == "description") {
            description = StringFromCompiletime(context.arena, value);
        }
        else if (identifier == "required") {
            required = B32FromCompiletime(value);
        }
        else if (identifier == "default") {
            default_value = value;
        }
    }
    
    if (!ValidateArgName(name, code->entire_location)) return;
    
    ArgResolve(def, name, description, required, default_value);
}

IR_Group ReadExpression(IR_Context* ir, Location location, ExpresionContext expr_context)
{
    if (location.range.min == location.range.max) {
        return ir_from_none();
    }
    
    Parser* parser = ParserFromLocation(location);
    
    Array<Token> tokens = ConsumeAllTokens(parser);
    
    if (tokens.count == 0) return {};
    
    Token starting_token = tokens[0];
    
    // Remove parentheses around
    if (tokens.count >= 2 && tokens[0].kind == TokenKind_OpenParenthesis && tokens[tokens.count - 1].kind == TokenKind_CloseParenthesis)
    {
        B32 couple = check_tokens_are_couple(tokens, 0, tokens.count - 1, TokenKind_OpenParenthesis, TokenKind_CloseParenthesis);
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
            if (u32_from_string(&v, token.value)) {
                return ir_from_none(ValueFromInt(v));
            }
            else {
                InvalidCodepath();
            }
        }
        else if (token.kind == TokenKind_BoolLiteral) {
            return ir_from_none(ValueFromBool(StrEquals(token.value, "true")));
        }
        else if (token.kind == TokenKind_StringLiteral)
        {
            IR_Group out = ir_from_none();
            PooledArray<Value> values = pooled_array_make<Value>(context.arena, 8);
            
            String raw = token.value;
            StringBuilder builder = string_builder_make(context.arena);
            
            U64 cursor = 0;
            while (cursor < raw.size)
            {
                U32 codepoint = string_get_codepoint(raw, &cursor);
                
                if (codepoint == '\\')
                {
                    codepoint = 0;
                    if (cursor < raw.size) {
                        codepoint = string_get_codepoint(raw, &cursor);
                    }
                    
                    if (codepoint == '\\') append(&builder, "\\");
                    else if (codepoint == 'n') append(&builder, "\n");
                    else if (codepoint == 'r') append(&builder, "\r");
                    else if (codepoint == 't') append(&builder, "\t");
                    else if (codepoint == '"') append(&builder, "\"");
                    else if (codepoint == '{') append(&builder, "{");
                    else if (codepoint == '}') append(&builder, "}");
                    else {
                        String sequence = string_from_codepoint(context.arena, codepoint);
                        report_invalid_escape_sequence(token.location, sequence);
                    }
                    
                    continue;
                }
                
                if (codepoint == '{')
                {
                    U64 start_identifier = cursor;
                    I32 depth = 1;
                    
                    while (cursor < raw.size) {
                        U32 codepoint = string_get_codepoint(raw, &cursor);
                        if (codepoint == '{') depth++;
                        else if (codepoint == '}') {
                            depth--;
                            if (depth == 0) break;
                        }
                    }
                    
                    U64 state_cursor = (raw.data + start_identifier) - parser->text.data;
                    U64 state_count = cursor - start_identifier - 1;
                    
                    Location subexpression_code = LocationMake(state_cursor, state_cursor + state_count, token.location.script_id);
                    
                    IR_Group sub = ReadExpression(ir, subexpression_code, ExpresionContext_from_vtype(VType_String, 1));
                    if (!sub.success) return ir_failed();
                    
                    Value value = sub.value;
                    
                    if (ValueIsCompiletime(value)) {
                        String ct_str = StringFromCompiletime(context.arena, value);
                        append(&builder, ct_str);
                    }
                    else
                    {
                        String literal = string_from_builder(context.arena, &builder);
                        if (literal.size > 0) {
                            builder = string_builder_make(context.arena);
                            array_add(&values, ValueFromString(ir->arena, literal));
                        }
                        
                        out = ir_append(out, sub);
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
            
            out.value = ValueFromStringArray(ir->arena, array_from_pooled_array(context.arena, values));
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
                v = string_get_codepoint(raw, &cursor);
                
                if (v == 0xFFFD || cursor != raw.size) {
                    report_invalid_codepoint_literal(token.location, raw);
                    return {};
                }
            }
            
            return ir_from_none(ValueFromInt(v));
        }
        else if (token.kind == TokenKind_NullKeyword) {
            return ir_from_none(ValueNull());
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
            return ir_from_symbol(ir, string_from_tokens(context.arena, tokens), location);
        }
    }
    
    
    // Function call
    if (tokens.count >= 3 && tokens[0].kind == TokenKind_Identifier && tokens[1].kind == TokenKind_OpenParenthesis && tokens[tokens.count - 1].kind == TokenKind_CloseParenthesis)
    {
        B32 couple = check_tokens_are_couple(tokens, 1, tokens.count - 1, TokenKind_OpenParenthesis, TokenKind_CloseParenthesis);
        
        if (couple)
        {
            Location call_code = LocationFromTokens(tokens);
            return ReadFunctionCall(ir, expr_context, call_code);
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
                    return ir_failed();
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
                    return ir_failed();
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
                    return ir_failed();
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
            return ir_failed();
        }
        if (braces_depth > 0) {
            report_common_missing_opening_brace(tokens[0].location);
            return ir_failed();
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
                    
                    IR_Group out = ReadExpression(ir, LocationFromTokens(subexpr_tokens), expr_context);
                    if (!out.success) return ir_failed();
                    
                    Value src = out.value;
                    
                    if (op_token.kind == TokenKind_Ampersand)
                    {
                        out = ir_append(out, ir_from_reference(ir, true, src, location));
                    }
                    else
                    {
                        out = ir_append(out, ir_from_sign_operator(ir, src, op, location));
                    }
                    
                    return out;
                }
                else
                {
                    if (preferent_operator_index <= 0 || preferent_operator_index >= tokens.count - 1) {
                        String op_string = string_from_tokens(context.arena, tokens);
                        report_expr_invalid_binary_operation(op_token.location, op_string);
                        return ir_failed();
                    }
                    else
                    {
                        Array<Token> left_expr_tokens = array_subarray(tokens, 0, preferent_operator_index);
                        Array<Token> right_expr_tokens = array_subarray(tokens, preferent_operator_index + 1, tokens.count - (preferent_operator_index + 1));
                        
                        ExpresionContext left_context = expr_context;
                        IR_Group left = ReadExpression(ir, LocationFromTokens(left_expr_tokens), left_context);
                        
                        ExpresionContext right_context = VTypeValid(left.value.vtype) ? ExpresionContext_from_vtype(left.value.vtype, 1) : expr_context;
                        IR_Group right = ReadExpression(ir, LocationFromTokens(right_expr_tokens), right_context);
                        
                        IR_Group out = ir_append(left, right);
                        if (!out.success) return ir_failed();
                        
                        out = ir_append(out, ir_from_binary_operator(ir, left.value, right.value, op, false, location));
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
                    return ir_failed();
                }
                
                Array<Token> expr_tokens = array_subarray(tokens, 0, tokens.count - 2);
                
                IR_Group out = ReadExpression(ir, LocationFromTokens(expr_tokens), ExpresionContext_from_inference(1));
                if (!out.success) return ir_failed();
                
                Value src = out.value;
                out = ir_append(out, ir_from_child_access(ir, src, member_value, expr_context, location));
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
        
        if (check_tokens_are_couple(tokens, 0, close_index, open_token_kind, close_token_kind))
        {
            VType element_vtype = VType_Any;
            if (expr_context.vtype != VType_Void) {
                element_vtype = expr_context.vtype;
                if (vtype_is_array(element_vtype)) {
                    element_vtype = VTypeNext(element_vtype);
                }
            }
            
            if (arrow_index > 0)
            {
                Array<Token> type_tokens = array_subarray(tokens, arrow_index + 1, tokens.count - (arrow_index + 1));
                Location type_code = LocationFromTokens(type_tokens);
                element_vtype = ReadObjectType(type_code);
                if (element_vtype == VType_Nil) return ir_failed();
                
                if (element_vtype == VType_Void || element_vtype == VType_Any) {
                    report_error(type_code, "Invalid type for array expression");
                    return ir_failed();
                }
            }
            
            VType expr_vtype = is_empty ? VType_Int : element_vtype;
            
            Array<Token> expr_tokens = array_subarray(tokens, 1, close_index - 1);
            IR_Group out = ReadExpressionList(context.arena, ir, expr_vtype, {}, LocationFromTokens(expr_tokens));
            Array<Value> values = ValuesFromReturn(context.arena, out.value, true);
            
            if (!is_empty && values.count > 0 && element_vtype == VType_Any) {
                element_vtype = values[0].vtype;
                expr_vtype = element_vtype;
            }
            
            if (element_vtype == VType_Any || element_vtype == VType_Void) {
                report_error(location, "Unknown array type");
                return ir_failed();
            }
            
            foreach(i, values.count) {
                if (values[i].vtype != expr_vtype) {
                    if (is_empty) {
                        report_error(location, "Expecting an integer for empty array expression but found a '%S'", VTypeGetName(values[i].vtype));
                        return ir_failed();
                    }
                    else {
                        report_error(location, "Expecting an array of '%S' but found an '%S'", VTypeGetName(expr_vtype), VTypeGetName(values[i].vtype));
                        return ir_failed();
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
                
                out.value = ValueFromEmptyArray(ir->arena, vtype_from_index(array_vtype.base_index), dimensions);
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
            B32 couple = check_tokens_are_couple(tokens, starting_token, tokens.count - 1, TokenKind_OpenBracket, TokenKind_CloseBracket);
            
            if (couple)
            {
                Array<Token> src_tokens = array_subarray(tokens, 0, starting_token);
                Array<Token> index_tokens = array_subarray(tokens, starting_token + 1, tokens.count - starting_token - 2);
                
                if (src_tokens.count > 0 && index_tokens.count > 0)
                {
                    Location src_location = LocationFromTokens(src_tokens);
                    Location index_location = LocationFromTokens(index_tokens);
                    
                    IR_Group src = ReadExpression(ir, src_location, ExpresionContext_from_void());
                    IR_Group index = ReadExpression(ir, index_location, ExpresionContext_from_vtype(VType_Int, 1));
                    
                    if (!src.success || !index.success) {
                        return ir_failed();
                    }
                    
                    if (src.value.kind == ValueKind_None) {
                        report_array_indexing_expects_expresion(tokens[0].location);
                        return ir_failed();
                    }
                    
                    if (index.value.vtype != VType_Int) {
                        report_indexing_expects_an_int(location);
                        return ir_failed();
                    }
                    
                    VType vtype = src.value.vtype;
                    
                    IR_Group out = ir_append(src, index);
                    
                    if (vtype_is_array(vtype))
                    {
                        VType element_vtype = VTypeNext(vtype);
                        out = ir_append(out, ir_from_child(ir, src.value, index.value, true, element_vtype, location));
                    }
                    else
                    {
                        report_indexing_not_allowed(location, VTypeGetName(vtype));
                        return ir_failed();
                    }
                    
                    return out;
                }
            }
        }
    }
    
    String expresion_string = string_from_tokens(context.arena, tokens);
    report_expr_syntactic_unknown(tokens[0].location, expresion_string);
    return {};
}

void CheckForAnyAssumptions(IR_Context* ir, IR_Unit* unit, Value value)
{
    I32 reg_index = ValueGetRegister(value);
    
    if (reg_index < 0 || unit == NULL || unit->dst_index != reg_index) {
        return;
    }
    
    if (unit->kind == UnitKind_BinaryOperation && unit->binary_op.op == BinaryOperator_Is) {
        if (unit->src.vtype == VType_Any)
        {
            IR_Object* obj = ir_find_object_from_value(ir, unit->src);
            VType vtype = TypeFromCompiletime(unit->binary_op.src1);
            
            if (obj != NULL && vtype != VType_Nil) {
                ir_assume_object(ir, obj, vtype);
            }
        }
    }
}

IR_Group ReadCode(IR_Context* ir, Location location)
{
    Parser* parser = ParserFromLocation(location);
    
    ir_scope_push(ir);
    defer(ir_scope_pop(ir));
    
    if (peek_token(parser).kind == TokenKind_OpenBrace) {
        assume_token(parser, TokenKind_OpenBrace);
    }
    
    IR_Group out = ir_from_none();
    
    while (true)
    {
        Token first_token = peek_token(parser);
        
        if (first_token.kind == TokenKind_None) {
            break;
        }
        
        if (first_token.kind == TokenKind_CloseBrace)
        {
            assume_token(parser, TokenKind_CloseBrace);
            if (peek_token(parser).kind != TokenKind_None) {
                report_error(first_token.location, "Invalid closing brace");
                return ir_failed();
            }
            break;
        }
        
        // Control flow
        if (first_token.kind == TokenKind_IfKeyword)
        {
            assume_token(parser, TokenKind_IfKeyword);
            
            ir_scope_push(ir);
            defer(ir_scope_pop(ir));
            
            // Read expression
            Location expression_location = FetchScope(parser, TokenKind_OpenParenthesis, false);
            
            if (!LocationIsValid(expression_location)) {
                report_error(first_token.location, "Expecting parenthesis for if statement");
                return ir_failed();
            }
            
            IR_Group expression = ReadExpression(ir, expression_location, ExpresionContext_from_vtype(VType_Bool, 1));
            if (!expression.success) return ir_failed();
            
            CheckForAnyAssumptions(ir, expression.last, expression.value);
            
            out = ir_append(out, expression);
            
            // Read success code
            Location success_location = FetchCode(parser);
            
            if (!LocationIsValid(success_location)) {
                report_error(first_token.location, "Expecting code for if statement");
                return ir_failed();
            }
            
            IR_Group success = ReadCode(ir, success_location);
            if (!success.success) return ir_failed();
            
            IR_Group failure = ir_from_none();
            
            // Failure code
            if (peek_token(parser).kind == TokenKind_ElseKeyword)
            {
                Token else_token = consume_token(parser);
                
                Location failure_location = FetchCode(parser);
                
                if (!LocationIsValid(failure_location)) {
                    report_error(else_token.location, "Expecting code for else statement");
                    return ir_failed();
                }
                
                failure = ReadCode(ir, failure_location);
                if (!failure.success) return ir_failed();
            }
            
            out = ir_append(out, IRFromIfStatement(ir, expression.value, success, failure, first_token.location));
        }
        else if (first_token.kind == TokenKind_ElseKeyword) {
            report_error(first_token.location, "No if statement found");
            return ir_failed();
        }
        else if (first_token.kind == TokenKind_WhileKeyword)
        {
            assume_token(parser, TokenKind_WhileKeyword);
            
            ir_looping_scope_push(ir, first_token.location);
            defer(ir_looping_scope_pop(ir));
            
            // Read expression
            Location expression_location = FetchScope(parser, TokenKind_OpenParenthesis, false);
            
            if (!LocationIsValid(expression_location)) {
                report_error(first_token.location, "Expecting parenthesis for while statement");
                return ir_failed();
            }
            
            IR_Group condition = ReadExpression(ir, expression_location, ExpresionContext_from_vtype(VType_Bool, 1));
            if (!condition.success) return ir_failed();
            
            // Read code
            Location code_location = FetchCode(parser);
            
            if (!LocationIsValid(code_location)) {
                report_error(first_token.location, "Expecting code for while statement");
                return ir_failed();
            }
            
            IR_Group code = ReadCode(ir, code_location);
            if (!code.success) return ir_failed();
            
            out = ir_append(out, IRFromLoop(ir, ir_from_none(), condition, code, ir_from_none(), first_token.location));
        }
        else if (first_token.kind == TokenKind_ForKeyword)
        {
            assume_token(parser, TokenKind_ForKeyword);
            
            ir_looping_scope_push(ir, first_token.location);
            defer(ir_looping_scope_pop(ir));
            
            Location parenthesis_location = FetchScope(parser, TokenKind_OpenParenthesis, false);
            if (!LocationIsValid(parenthesis_location)) {
                report_error(first_token.location, "Expecting parenthesis for for");
                return ir_failed();
            }
            
            Location content_location = FetchCode(parser);
            if (!LocationIsValid(content_location)) {
                report_error(first_token.location, "Expecting code for for statement");
                return ir_failed();
            }
            
            Array<Location> splits = array_make<Location>(context.arena, 8);
            
            // Split location by ';'
            {
                U32 split_count = 0;
                
                Parser* parser = ParserFromLocation(parenthesis_location);
                
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
                    
                    assume_token(parser, TokenKind_NextSentence);
                }
                
                splits.count = split_count;
            }
            
            // For each
            if (splits.count == 1)
            {
                Location location = splits[0];
                Parser* parser = ParserFromLocation(location);
                
                Token element_token = consume_token(parser);
                if (element_token.kind != TokenKind_Identifier) {
                    report_error(location, "Expecting element identifier of the for each statement");
                    return ir_failed();
                }
                
                String element_identifier = element_token.value;
                String index_identifier = {};
                
                if (peek_token(parser).kind == TokenKind_Comma)
                {
                    assume_token(parser, TokenKind_Comma);
                    Token index_token = consume_token(parser);
                    
                    if (index_token.kind != TokenKind_Identifier) {
                        report_error(location, "Expecting index identifier for the for each statement");
                        return ir_failed();
                    }
                    
                    index_identifier = index_token.value;
                }
                
                if (peek_token(parser).kind != TokenKind_Colon) {
                    report_error(location, "Expecting colon after for each identifiers");
                    return ir_failed();
                }
                assume_token(parser, TokenKind_Colon);
                
                Location iterator_location = LocationFromParser(parser, parser->range.max);
                
                IR_Group iterator = ReadExpression(ir, iterator_location, ExpresionContext_from_inference(1));
                
                if (!vtype_is_array(iterator.value.vtype)) {
                    report_error(location, "Invalid iterator for a for each statement");
                    return ir_failed();
                }
                
                VType element_vtype = VTypeNext(iterator.value.vtype);
                
                // Init code
                IR_Group init = IRFromDefineObject(ir, RegisterKind_Local, element_identifier, element_vtype, false, location);
                Value element_value = init.value;
                
                if (index_identifier.size > 0) {
                    init = ir_append(init, IRFromDefineObject(ir, RegisterKind_Local, index_identifier, VType_Int, false, location));
                }
                else {
                    init = ir_append(init, IRFromDefineTemporal(ir, VType_Int, location));
                }
                Value index_value = init.value;
                init = ir_append(init, ir_from_store(ir, index_value, ValueFromInt(0), location));
                
                // Condition code
                VariableTypeChild count_info = VTypeGetProperty(iterator.value.vtype, "count");
                IR_Group condition = ir_from_child(ir, iterator.value, ValueFromInt(count_info.index), count_info.is_member, count_info.vtype, location);
                Value count_value = condition.value;
                condition = ir_append(condition, ir_from_binary_operator(ir, index_value, count_value, BinaryOperator_LessThan, false, location));
                
                // Content code
                IR_Group content = ir_from_child(ir, iterator.value, index_value, true, element_vtype, location);
                content = ir_append(content, ir_from_store(ir, element_value, content.value, location));
                content = ir_append(content, ReadCode(ir, content_location));
                
                // Update code
                IR_Group update = ir_from_assignment(ir, false, index_value, ValueFromInt(1), BinaryOperator_Addition, location);
                
                out = ir_append(out, IRFromLoop(ir, init, condition, content, update, first_token.location));
            }
            // Traditional C for
            else if (splits.count == 3)
            {
                IR_Group init = ReadSentence(ir, splits[0]);
                IR_Group condition = ReadExpression(ir, splits[1], ExpresionContext_from_vtype(VType_Bool, 1));
                IR_Group update = ReadSentence(ir, splits[2]);
                IR_Group content = ReadCode(ir, content_location);
                
                out = ir_append(out, IRFromLoop(ir, init, condition, content, update, first_token.location));
            }
            else
            {
                report_error(first_token.location, "Unknown for format");
                return ir_failed();
            }
        }
        else if (first_token.kind == TokenKind_OpenBrace)
        {
            Location block_location = FetchCode(parser);
            
            if (!LocationIsValid(block_location)) {
                report_common_missing_closing_brace(first_token.location);
                return ir_failed();
            }
            
            IR_Group block = ReadCode(ir, block_location);
            if (!block.success) return ir_failed();
            
            out = ir_append(out, block);
        }
        
        // Sentence
        else
        {
            Location sentence_location = FetchUntil(parser, false, TokenKind_NextSentence);
            
            if (!LocationIsValid(sentence_location)) {
                report_expecting_semicolon(first_token.location);
                return ir_failed();
            }
            
            out = ir_append(out, ReadSentence(ir, sentence_location));
            
            assume_token(parser, TokenKind_NextSentence);
        }
    }
    
    return out;
}

IR_Group ReadSentence(IR_Context* ir, Location location)
{
    Parser* parser = ParserFromLocation(location);
    
    SentenceKind kind = find_sentence_kind(parser);
    
    if (kind == SentenceKind_ObjectDef)
    {
        ObjectDefinitionResult res = ReadObjectDefinition(context.arena, location, false, RegisterKind_Local, ir);
        if (!res.success) return ir_failed();
        return res.out;
    }
    
    if (kind == SentenceKind_FunctionCall)
    {
        return ReadFunctionCall(ir, ExpresionContext_from_void(), location);
    }
    
    if (kind == SentenceKind_Assignment)
    {
        PooledArray<String> identifiers = pooled_array_make<String>(context.arena, 4);
        IR_Group out = ir_from_none();
        
        U64 dst_end_cursor = find_token_with_depth_check(parser, true, true, true, TokenKind_Assignment);
        
        if (dst_end_cursor == parser->cursor) {
            InvalidCodepath();
            return ir_failed();
        }
        
        Location dst_code = LocationMake(parser->cursor, dst_end_cursor, parser->script_id);
        out = ir_append(out, ReadExpressionList(context.arena, ir, VType_Any, {}, dst_code));
        
        MoveCursor(parser, dst_end_cursor);
        Token assignment_token = consume_token(parser);
        Assert(assignment_token.kind == TokenKind_Assignment);
        
        Array<Value> values = ValuesFromReturn(context.arena, out.value, true);
        
        if (values.count == 0) {
            report_error(location, "Empty assignment");
            return ir_failed();
        }
        
        foreach(i, values.count)
        {
            Register reg = IRRegisterGet(ir, ValueGetRegister(values[i]));
            if (RegisterIsValid(reg) && reg.is_constant) {
                report_error(location, "Can't modify a constant: {line}");
            }
        }
        
        Location src_code = LocationMake(parser->cursor, parser->range.max, parser->script_id);
        
        IR_Group src = ReadExpression(ir, src_code, ExpresionContext_from_vtype(values[0].vtype, values.count));
        out = ir_append(out, src);
        
        if (!out.success) return ir_failed();
        
        BinaryOperator op = assignment_token.assignment_binary_operator;
        IR_Group assignment = ir_from_multiple_assignment(ir, true, values, src.value, op, location);
        return ir_append(out, assignment);
    }
    else if (kind == SentenceKind_Continue || kind == SentenceKind_Break)
    {
        return IRFromFlowModifier(ir, kind == SentenceKind_Break, location);
    }
    else if (kind == SentenceKind_Return)
    {
        assume_token(parser, TokenKind_ReturnKeyword);
        
        Array<VType> returns = ReturnsFromRegisters(context.arena, array_from_pooled_array(context.arena, ir->local_registers));
        
        VType expected_vtype = VType_Void;
        if (returns.count == 1) expected_vtype = returns[0];
        
        ExpresionContext context = (expected_vtype == VType_Void) ? ExpresionContext_from_void() : ExpresionContext_from_vtype(expected_vtype, 1);
        
        Location expression_location = LocationFromParser(parser, parser->range.max);
        IR_Group expression = ReadExpression(ir, expression_location, context);
        if (!expression.success) return ir_failed();
        return IRFromReturn(ir, expression, location);
    }
    
    if (kind == SentenceKind_Unknown) {
        report_error(location, "Unknown sentence: {line}");
        return ir_failed();
    }
    else {
        report_error(location, "Invalid sentence: {line}");
        return ir_failed();
    }
}

IR_Group ReadFunctionCall(IR_Context* ir, ExpresionContext expr_context, Location location)
{
    Parser* parser = ParserFromLocation(location);
    
    Token identifier_token = consume_token(parser);
    Assert(identifier_token.kind == TokenKind_Identifier);
    String identifier = identifier_token.value;
    
    Location expressions_location = FetchScope(parser, TokenKind_OpenParenthesis, false);
    
    if (!LocationIsValid(expressions_location)) {
        report_common_missing_closing_parenthesis(location);
        return ir_failed();
    }
    
    Array<VType> expected_vtypes = {};
    
    {
        FunctionDefinition* fn = FunctionFromIdentifier(identifier);
        if (fn != NULL)
        {
            expected_vtypes = array_make<VType>(context.arena, fn->parameters.count);
            foreach(i, fn->parameters.count) {
                expected_vtypes[i] = fn->parameters[i].vtype;
            }
        }
    }
    
    IR_Group out = ReadExpressionList(context.arena, ir, VType_Any, expected_vtypes, expressions_location);
    Array<Value> params = ValuesFromReturn(context.arena, out.value, true);
    
    IR_Group call = ir_from_function_call(ir, identifier, params, expr_context, location);
    return ir_append(out, call);
}

ObjectDefinitionResult ReadObjectDefinition(Arena* arena, Location location, B32 require_single, RegisterKind register_kind, IR_Context* ir)
{
    Parser* parser = ParserFromLocation(location);
    
    ObjectDefinitionResult res = {};
    res.out = ir_from_none();
    res.success = false;
    
    Token starting_token = peek_token(parser);
    
    PooledArray<String> identifiers = pooled_array_make<String>(context.arena, 4);
    
    // Extract identifiers
    while (true)
    {
        Token token = consume_token(parser);
        
        if (token.kind != TokenKind_Identifier) {
            report_error(token.location, "Expecting an identifier\n");
            return res;
        }
        
        array_add(&identifiers, token.value);
        
        token = consume_token(parser);
        
        if (token.kind == TokenKind_Colon) {
            break;
        }
        
        if (require_single || token.kind != TokenKind_Comma) {
            report_objdef_expecting_colon(starting_token.location);
            return res;
        }
    }
    
    // Validation for duplicated symbols
    if (ir != NULL)
    {
        foreach(i, identifiers.count) {
            String identifier = identifiers[i];
            if (ir_find_object(ir, identifier, false) != NULL) {
                report_symbol_duplicated(location, identifier);
                return res;
            }
        }
    }
    
    // Explicit type
    VType definition_vtype = VType_Void;
    
    {
        Token type_or_assignment_token = peek_token(parser);
        
        if (type_or_assignment_token.kind == TokenKind_Assignment) {}
        else if (type_or_assignment_token.kind == TokenKind_Colon) {}
        else if (type_or_assignment_token.kind == TokenKind_Identifier)
        {
            Location type_location = FetchUntil(parser, false, TokenKind_Assignment, TokenKind_Colon);
            
            if (!LocationIsValid(type_location)) {
                type_location = LocationMake(parser->cursor, parser->range.max, parser->script_id);
                MoveCursor(parser, type_location.range.max);
            }
            
            definition_vtype = ReadObjectType(type_location);
            if (definition_vtype == VType_Nil) {
                return res;
            }
            
            if (definition_vtype == VType_Void) {
                report_error(starting_token.location, "Void is not a valid object: {line}");
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
    B32 inference_type = definition_vtype == VType_Void;
    
    if (ir == NULL)
    {
        if (inference_type) {
            report_error(starting_token.location, "Unsupported inference type");
            return res;
        }
        
        Token assignment_token = peek_token(parser);
        is_constant = assignment_token.kind == TokenKind_Colon;
        
        for (auto it = pooled_array_make_iterator(&identifiers); it.valid; ++it)
        {
            String identifier = StrCopy(yov->arena, *it.value);
            Value value = ValueFromZero(definition_vtype);
            
            res.objects[it.index] = ObjDefMake(identifier, definition_vtype, location, is_constant, value);
        }
    }
    else
    {
        Token assignment_token = consume_token(parser);
        
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
            IR_Group src = ReadExpression(ir, expression_code, expression_context);
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
            
            B32 is_any = definition_vtype == VType_Any;
            
            if (!is_any && vtypes.count < identifiers.count) {
                report_error(location, "Unresolved object type for definition");
                return res;
            }
            
            Array<Value> values = array_make<Value>(context.arena, identifiers.count);
            foreach(i, values.count) {
                VType vtype = vtypes[i];
                
                if (register_kind == RegisterKind_Global)
                {
                    I32 global_index = GlobalIndexFromIdentifier(identifiers[i]);
                    if (global_index < 0) {
                        report_error(location, "Global '%S' not found", identifiers[i]);
                        continue;
                    }
                    
                    res.out = ir_append(res.out, ir_from_store(ir, ValueFromGlobal(global_index), ValueFromZero(vtype), location));
                }
                else
                {
                    res.out = ir_append(res.out, IRFromDefineObject(ir, register_kind, identifiers[i], vtype, is_constant, location));
                    res.out = ir_append(res.out, ir_from_store(ir, res.out.value, ValueFromZero(vtype), location));
                }
                values[i] = res.out.value;
            }
            
            // Default src
            if (src.value.kind == ValueKind_None && !is_any) {
                src = ir_from_default_initializer(ir, definition_vtype, location);
            }
            
            res.out = ir_append(res.out, src);
            
            if (src.value.kind != ValueKind_None) {
                IR_Group assignment = ir_from_multiple_assignment(ir, true, values, src.value, BinaryOperator_None, location);
                res.out = ir_append(res.out, assignment);
            }
            
            for (auto it = pooled_array_make_iterator(&identifiers); it.valid; ++it) {
                res.objects[it.index] = ObjDefMake(StrCopy(yov->arena, *it.value), vtypes[it.index], location, is_constant, values[it.index]);
            }
        }
        else
        {
            if (inference_type) {
                report_error(location, "Unspecified type");
                return res;
            }
            
            res.out = ir_from_none(ValueFromZero(definition_vtype));
            
            for (auto it = pooled_array_make_iterator(&identifiers); it.valid; ++it)
            {
                String identifier = StrCopy(yov->arena, *it.value);
                
                if (register_kind == RegisterKind_Global)
                {
                    I32 global_index = GlobalIndexFromIdentifier(identifier);
                    if (global_index < 0) {
                        report_error(location, "Global '%S' not found", identifier);
                        continue;
                    }
                    
                    res.out = ir_append(res.out, ir_from_store(ir, ValueFromGlobal(global_index), ValueFromZero(definition_vtype), location));
                    Value value = res.out.value;
                    res.objects[it.index] = ObjDefMake(identifier, definition_vtype, location, is_constant, value);
                }
                else
                {
                    res.out = ir_append(res.out, IRFromDefineObject(ir, register_kind, identifier, definition_vtype, is_constant, location));
                    Value value = res.out.value;
                    if (register_kind != RegisterKind_Parameter) {
                        res.out = ir_append(res.out, ir_from_store(ir, value, ValueFromZero(definition_vtype), location));
                    }
                    res.objects[it.index] = ObjDefMake(identifier, definition_vtype, location, is_constant, value);
                }
            }
        }
    }
    
    res.success = true;
    return res;
}

ObjectDefinitionResult ReadDefinitionList(Arena* arena, Location location, RegisterKind register_kind, IR_Context* ir)
{
    Parser* parser = ParserFromLocation(location);
    
    ObjectDefinitionResult res = {};
    res.out = ir_from_none();
    
    // Empty list
    if (peek_token(parser).kind == TokenKind_None)
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
        
        ObjectDefinitionResult res0 = ReadObjectDefinition(context.arena, parameter_location, false, register_kind, ir);
        if (!res0.success) return {};
        
        foreach(i, res0.objects.count) {
            array_add(&list, res0.objects[i]);
        }
        
        res.out = ir_append(res.out, res0.out);
        
        consume_token(parser);
    }
    
    res.objects = array_from_pooled_array(arena, list);
    res.success = true;
    return res;
}

IR_Group ReadExpressionList(Arena* arena, IR_Context* ir, VType vtype, Array<VType> expected_vtypes, Location location)
{
    Parser* parser = ParserFromLocation(location);
    
    IR_Group out = ir_from_none();
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
        
        Location expression_code = LocationMake(parser->cursor, expression_end_cursor, parser->script_id);
        
        out = ir_append(out, ReadExpression(ir, expression_code, ExpresionContext_from_vtype(expected_vtype, 1)));
        if (!out.success) return ir_failed();
        
        array_add(&list, out.value);
        
        MoveCursor(parser, expression_end_cursor);
        consume_token(parser);
    }
    
    out.value = value_from_return(arena, array_from_pooled_array(context.arena, list));
    
    return out;
}

VType ReadObjectType(Location location)
{
    Parser* parser = ParserFromLocation(location);
    Array<Token> tokens = ConsumeAllTokens(parser);
    
    if (tokens.count == 0)
    {
        report_error(location, "Expecting a type");
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
            report_error(location, "Invalid type format");
            return VType_Nil;
        }
        
        foreach(i, tokens.count)
        {
            TokenKind kind = (i % 2 == 0) ? TokenKind_OpenBracket : TokenKind_CloseBracket;
            if (tokens[i].kind != kind) {
                report_error(location, "Invalid type format");
                return VType_Nil;
            }
        }
        
        array_dimensions = tokens.count / 2;
    }
    
    VType vtype = vtype_from_name(identifier_token.value);
    if (vtype == VType_Nil) {
        report_error(location, "Unknown type");
        return VType_Nil;
    }
    
    vtype = vtype_from_dimension(vtype, array_dimensions);
    if (is_reference) vtype = vtype_from_reference(vtype);
    return vtype;
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
        string_get_codepoint(text, &cursor);
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
        c0 = (cursor < text.size) ? string_get_codepoint(text, &cursor) : 0;
        c1 = (cursor < text.size) ? string_get_codepoint(text, &cursor) : 0;
    }
    
    if (c0 == 0) {
        return token_make_fixed(text, start_cursor, TokenKind_NextLine, 1, script_id);
    }
    
    if (codepoint_is_separator(c0))
    {
        U64 cursor = start_cursor;
        while (cursor < text.size) {
            U64 next_cursor = cursor;
            U32 codepoint = string_get_codepoint(text, &next_cursor);
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
            U32 codepoint = string_get_codepoint(text, &next_cursor);
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
            U32 codepoint = string_get_codepoint(text, &cursor);
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
            U32 codepoint = string_get_codepoint(text, &cursor);
            
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
            U32 codepoint = string_get_codepoint(text, &cursor);
            
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
            U32 codepoint = string_get_codepoint(text, &next_cursor);
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
            U32 codepoint = string_get_codepoint(text, &next_cursor);
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
        
        B32 discard = false;
        if (token.kind == TokenKind_Separator) discard = true;
        if (token.kind == TokenKind_Comment) discard = true;
        if (token.kind == TokenKind_NextLine) discard = true;
        
        total_skip_size += token.skip_size;
        
        if (!discard) break;
    }
    
    token.cursor = start_cursor;
    token.skip_size = total_skip_size;
    
    return token;
}

B32 check_tokens_are_couple(Array<Token> tokens, U32 open_index, U32 close_index, TokenKind open_token, TokenKind close_token)
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

Array<Array<Token>> split_tokens_in_parameters(Arena* arena, Array<Token> tokens)
{
    PooledArray<Array<Token>> params = pooled_array_make<Array<Token>>(context.arena, 8);
    
    U32 cursor = 0;
    U32 last_cursor = 0;
    I32 depth = 0;
    
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
