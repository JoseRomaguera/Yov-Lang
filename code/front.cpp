#include "front.h"

internal_fn void FrontRun(FrontContext* front, LaneContext* lane)
{
    PROFILE_FUNCTION;
    
    {
        if (LaneNarrow(lane)) {
            LogFlow("Starting Read Locations & Imports Pass");
        }
        
        F64 start_time = TimerNow();
        
        FrontReadAllScripts(lane, front);
        ArenaPopTo(context.arena, 0);
        
        if (front->reporter->exit_requested) {
            return;
        }
        
        if (LaneNarrow(lane)) {
            F64 ellapsed = TimerNow() - start_time;
            LogFlow("Read locations & imports finished: %S", StringFromEllapsedTime(ellapsed));
        }
    }
    
    // Define Pass
    {
        if (LaneNarrow(lane)) {
            LogFlow("Starting Define Pass");
        }
        
        F64 start_time = TimerNow();
        
        FrontDefineDefinitions(lane, front);
        FrontDefineGlobals(lane, front);
        
        ArenaPopTo(context.arena, 0);
        LaneBarrier(lane);
        
        if (LaneNarrow(lane)) {
            F64 ellapsed = TimerNow() - start_time;
            LogFlow("Define pass finished: %S", StringFromEllapsedTime(ellapsed));
        }
        
        if (front->reporter->exit_requested) {
            return;
        }
    }
    
    // Resolve Pass
    {
        if (LaneNarrow(lane)) {
            LogFlow("Starting Resolve Pass");
        }
        
        F64 start_time = TimerNow();
        
        FrontResolveDefinitions(lane, front);
        FrontResolveGlobals(lane, front);
        
        ArenaPopTo(context.arena, 0);
        LaneBarrier(lane);
        
        if (LaneNarrow(lane)) {
            F64 ellapsed = TimerNow() - start_time;
            LogFlow("Resolve pass finished: %S", StringFromEllapsedTime(ellapsed));
        }
        
        if (front->reporter->exit_requested) {
            return;
        }
    }
}

internal_fn void FrontWide(LaneContext* lane)
{
    PROFILE_FUNCTION;
    FrontContext* front = (FrontContext*)lane->group->user_data;
    
    FrontRun(front, lane);
    
    // Resolve reports info
    {
        Reporter* reporter = front->reporter;
        
        RangeU32 range = LaneDistributeUniformWork(lane, reporter->reports.count);
        for (U32 i = range.min; i < range.max; i++)
        {
            Report* report = &reporter->reports[i];
            
            Location location = report->location;
            YovScript* script = FrontGetScript(front, location.script_id);
            if (script == NULL) continue;
            
            report->path = StrCopy(reporter->arena, script->path);
            report->line = LineFromLocation(location, script);
        }
    }
}

Program* ProgramFromInput(Arena* arena, Input* input, Reporter* reporter)
{
    PROFILE_FRAME_MARK;
    PROFILE_FUNCTION;
    
    Program* program = ArenaPushStruct<Program>(arena);
    program->arena = arena;
    program->types = BArrayMake<Type>(program->arena, 256);
    program->definitions = BArrayMake<Definition>(program->arena, 256);
    program->script_dir = StrCopy(arena, PathGetFolder(input->main_script_path));
    program->caller_dir = StrCopy(arena, input->caller_dir);
    
    if (reporter->exit_requested) {
        return program;
    }
    
    FrontContext* front = NULL;
    
    Arena* front_arena = ArenaAlloc(Gb(32), 8, "Arena Front");
    front = ArenaPushStruct<FrontContext>(front_arena);
    front->arena = front_arena;
    front->program = program;
    front->reporter = reporter;
    front->input = input;
    front->scripts = BArrayMake<YovScript>(front_arena, 16);
    front->definitions = BArrayMake<CodeDefinition>(front_arena, 64);
    front->global_location_list = BArrayMake<Location>(front_arena, 32);
    front->global_list = BArrayMake<Global>(front_arena, 32);
    front->global_initialize_group = IRFromNone();
    
    LaneGroup* group = LaneGroupStart(context.arena, FrontWide, front);
    LaneGroupWait(group);
    
    ArenaFree(front_arena);
    ArenaPopTo(context.arena, 0);
    
#if LOG_IR_ENABLED
    PrintIr(program, "Initialize Globals", program->globals_initialize_ir);
    foreach(i, program->definitions.count)
    {
        DefinitionHeader* header = &program->definitions[i].header;
        FunctionDefinition* fn = &program->definitions[i].function;
        if (fn->is_intrinsic) continue;
        PrintIr(program, fn->name, fn->defined.ir);
    }
#endif
    
    return program;
}

internal_fn U32 CountNames(Program* program, String name)
{
    PROFILE_FUNCTION;
    
    U32 count = 0;
    
    foreach(i, program->definitions.count) {
        if (program->definitions[i].header.name == name) count++;
    }
    
    return count;
}

Definition* AddDefinition(Program* program, Reporter* reporter, DefinitionType type, String name, B32 is_global, Location location)
{
    PROFILE_FUNCTION;
    
    MutexLockGuard(&program->definitions_mutex);
    
    Definition* full_def = BArrayAdd(&program->definitions);
    DefinitionHeader* def = &full_def->header;
    def->type = type;
    def->name = StrCopy(program->arena, name);
    def->location = location;
    def->stage = DefinitionStage_Identified;
    def->is_global = is_global;
    
    if (type == DefinitionType_Enum) {
        TypeFromEnum(program, &full_def->_enum);
    }
    else if (type == DefinitionType_Struct) {
        TypeFromStruct(program, &full_def->_struct);
    }
    
    if (type == DefinitionType_Arg) program->arg_count++;
    
    // Check if it's duplicated
    {
        U32 count = CountNames(program, def->name);
        
        if (count > 1) {
            ReportErrorFront(def->location, "Duplicated definition '%S'", def->name);
        }
    }
    
    LogType("%S Identify: %S", StringFromDefinitionType(type), identifier);
    return full_def;
}

internal_fn B32 ExpectAndSkipBraces(Parser* parser, Reporter* reporter)
{
    Token open_brace_token = PeekToken(parser);
    
    if (open_brace_token.kind != TokenKind_OpenBrace) {
        report_common_missing_opening_brace(open_brace_token.location);
        return false;
    }
    
    Location location = FetchScope(parser, TokenKind_OpenBrace, true);
    
    if (!LocationIsValid(location)) {
        report_common_missing_closing_brace(open_brace_token.location);
        return false;
    }
    
    return true;
}

B32 ReadCodeDefinition(CodeDefinition* dst, Parser* parser, Reporter* reporter, SentenceKind op)
{
    Token identifier_token = ConsumeToken(parser);
    
    Assert(identifier_token.kind == TokenKind_Identifier);
    
    CodeDefinition def = {};
    defer(*dst = def);
    def.name = identifier_token.value;
    def.entire_location = NO_CODE;
    
    if (op == SentenceKind_FunctionDef) def.type = DefinitionType_Function;
    else if (op == SentenceKind_StructDef) def.type = DefinitionType_Struct;
    else if (op == SentenceKind_EnumDef) def.type = DefinitionType_Enum;
    else if (op == SentenceKind_ArgDef) def.type = DefinitionType_Arg;
    else InvalidCodepath();
    
    
    if (op == SentenceKind_StructDef || op == SentenceKind_EnumDef)
    {
        AssumeToken(parser, TokenKind_Colon);
        AssumeToken(parser, TokenKind_Colon);
        AssumeToken(parser, (op == SentenceKind_StructDef) ? TokenKind_StructKeyword : TokenKind_EnumKeyword);
        
        U64 definition_start_cursor = parser->cursor;
        if (!ExpectAndSkipBraces(parser, reporter)) return false;
        
        def.enum_or_struct.body_location = LocationMake(definition_start_cursor, parser->cursor, parser->script_id);
    }
    else if (op == SentenceKind_FunctionDef)
    {
        AssumeToken(parser, TokenKind_Colon);
        AssumeToken(parser, TokenKind_Colon);
        AssumeToken(parser, TokenKind_FuncKeyword);
        
        def.function.parameters_location = NO_CODE;
        def.function.returns_location = NO_CODE;
        def.function.body_location = NO_CODE;
        def.function.generics_location = NO_CODE;
        
        // Generics
        if (PeekToken(parser).kind == TokenKind_OpenBracket)
        {
            Location generics_location = FetchScope(parser, TokenKind_OpenBracket, false);
            
            if (!LocationIsValid(generics_location)) {
                report_common_missing_closing_bracket(PeekToken(parser).location);
                return false;
            }
            
            def.function.generics_location = generics_location;
        }
        
        // Parameters
        if (PeekToken(parser).kind == TokenKind_OpenParenthesis)
        {
            Location parameters_location = FetchScope(parser, TokenKind_OpenParenthesis, false);
            
            if (!LocationIsValid(parameters_location)) {
                report_common_missing_closing_parenthesis(PeekToken(parser).location);
                return false;
            }
            
            def.function.parameters_location = parameters_location;
        }
        
        // Returns
        if (PeekToken(parser).kind == TokenKind_Arrow)
        {
            AssumeToken(parser, TokenKind_Arrow);
            
            Token first = PeekToken(parser);
            
            def.function.return_is_list = first.kind == TokenKind_OpenParenthesis;
            
            if (def.function.return_is_list)
            {
                Location returns_location = FetchScope(parser, TokenKind_OpenParenthesis, false);
                
                if (!LocationIsValid(returns_location)) {
                    ReportErrorFront(first.location, "Missing parenthesis for return");
                    return false;
                }
                
                def.function.returns_location = returns_location;
            }
            else
            {
                Location returns_location = FetchUntil(parser, false, TokenKind_OpenBrace, TokenKind_NextSentence);
                
                if (!LocationIsValid(returns_location)) {
                    ReportErrorFront(first.location, "Invalid return definition");
                    return false;
                }
                
                def.function.returns_location = returns_location;
            }
        }
        
        // Body
        {
            if (PeekToken(parser).kind == TokenKind_NextSentence) {
                AssumeToken(parser, TokenKind_NextSentence);
            }
            else {
                Location body_location = FetchCode(parser);
                
                if (!LocationIsValid(body_location)) {
                    ReportErrorFront(PeekToken(parser).location, "Expecting the body of the function");
                    return false;
                }
                
                def.function.body_location = body_location;
            }
        }
    }
    else if (op == SentenceKind_ArgDef)
    {
        AssumeToken(parser, TokenKind_Colon);
        AssumeToken(parser, TokenKind_Colon);
        AssumeToken(parser, TokenKind_ArgKeyword);
        
        def.arg.type_location = NO_CODE;
        def.arg.body_location = NO_CODE;
        
        B32 has_type = false;
        
        if (PeekToken(parser).kind == TokenKind_Arrow) {
            AssumeToken(parser, TokenKind_Arrow);
            has_type = true;
        }
        
        Location to_open_brace = FetchUntil(parser, false, TokenKind_OpenBrace);
        
        if (!LocationIsValid(to_open_brace)) {
            ReportErrorFront(PeekToken(parser).location, "Expecting braces for the arg");
            return false;
        }
        
        if (has_type) {
            def.arg.type_location = to_open_brace;
        }
        
        U64 start_cursor = parser->cursor;
        
        if (!ExpectAndSkipBraces(parser, reporter)) return false;
        
        def.arg.body_location = LocationMake(start_cursor, parser->cursor, parser->script_id);
    }
    
    def.entire_location = LocationMake(identifier_token.cursor, parser->cursor, parser->script_id);
    
    return true;
}

B32 ReadEnumDefinition(Parser* parser, Program* program, Reporter* reporter, CodeDefinition* code)
{
    PROFILE_FUNCTION;
    
    EnumDefinition* def = &code->definition->_enum;
    
    parser = ParserSub(parser, code->enum_or_struct.body_location);
    
    Location starting_location = PeekToken(parser).location;
    
    AssumeToken(parser, TokenKind_OpenBrace);
    
    BArray<String> names = BArrayMake<String>(context.arena, 16);
    BArray<Location> expression_locations = BArrayMake<Location>(context.arena, 16);
    
    while (true)
    {
        Token name_token = ConsumeToken(parser);
        if (name_token.kind == TokenKind_CloseBrace) break;
        if (name_token.kind != TokenKind_Identifier) {
            report_enumdef_expecting_comma_separated_identifier(name_token.location);
            return false;
        }
        
        Token assignment_token = PeekToken(parser);
        
        Location expression_location = NO_CODE;
        
        if (assignment_token.kind == TokenKind_Assignment && assignment_token.assignment_operator == OperatorKind_None) {
            SkipToken(parser, assignment_token);
            
            expression_location = FetchUntil(parser, false, TokenKind_CloseBrace, TokenKind_Comma);
            
            if (!LocationIsValid(expression_location)) {
                ReportErrorFront(assignment_token.location, "Expecting expression for enum value");
                return false;
            }
        }
        
        BArrayAdd(&names, name_token.value);
        BArrayAdd(&expression_locations, expression_location);
        
        Token comma_token = ConsumeToken(parser);
        if (comma_token.kind == TokenKind_CloseBrace) break;
        if (comma_token.kind != TokenKind_Comma) {
            report_enumdef_expecting_comma_separated_identifier(comma_token.location);
            return false;
        }
    }
    
    EnumDefine(program, def, ArrayFromBArray(context.arena, names), ArrayFromBArray(context.arena, expression_locations));
    return true;
}

B32 ReadStructDefinition(Parser* parser, Program* program, Reporter* reporter, CodeDefinition* code)
{
    PROFILE_FUNCTION;
    
    StructDefinition* def = &code->definition->_struct;
    
    parser = ParserSub(parser, code->enum_or_struct.body_location);
    
    Location starting_location = PeekToken(parser).location;
    AssumeToken(parser, TokenKind_OpenBrace);
    
    U32 member_index = 0;
    
    BArray<ObjectDefinition> members = BArrayMake<ObjectDefinition>(context.arena, 16);
    
    while (PeekToken(parser).kind != TokenKind_CloseBrace)
    {
        Location member_location = FetchUntil(parser, false, TokenKind_NextSentence);
        if (!LocationIsValid(member_location)) {
            report_expecting_semicolon(LocationFromParser(parser, parser->cursor));
            return false;
        }
        
        ObjectDefinitionResult read_result = ReadObjectDefinition(context.arena, ParserSub(parser, member_location), reporter, program, false, RegisterKind_Local);
        if (!read_result.success) return false;
        
        foreach(i, read_result.objects.count) {
            ObjectDefinition member = read_result.objects[i];
            BArrayAdd(&members, member);
            
            //out = IRAppend(out, ir_from_child(ir_context, ret, value_from_int(member_index), true, member.vtype, member.location));
            //Value dst = out.value;
            //out = IRAppend(out, ir_from_assignment(ir_context, true, dst, member.value, BinaryOperator_None, member.location));
            
            member_index++;
        }
        
        AssumeToken(parser, TokenKind_NextSentence);
    }
    
    ConsumeToken(parser);
    
    Type* struct_type = TypeFromStruct(program, def);
    Assert(TypeIsStruct(struct_type));
    
    B32 valid = true;
    
    foreach_BArray(it, &members)
    {
        ObjectDefinition* def = it.value;
        
        Type* type = def->type;
        if (type == nil_type) {
            valid = false;
            continue;
        }
        
        if (type == any_type) {
            ReportErrorFront(def->location, "Any is not a valid member for a struct");
            valid = false;
            continue;
        }
        
        if (type == struct_type) {
            report_struct_recursive(def->location);
            valid = false;
            continue;
        }
    }
    
    if (!valid) return false;
    
    StructDefine(program, def, ArrayFromBArray(context.arena, members));
    return true;
}

B32 ReadFunctionDefinition(Parser* parser, Program* program, Reporter* reporter, CodeDefinition* code)
{
    PROFILE_FUNCTION;
    
    FunctionDefinition* def = &code->definition->function;
    
    Array<ObjectDefinition> parameters = {};
    Array<ObjectDefinition> returns = {};
    
    if (LocationIsValid(code->function.parameters_location)) {
        ObjectDefinitionResult res = ReadDefinitionList(context.arena, ParserSub(parser, code->function.parameters_location), reporter, program, RegisterKind_Parameter);
        if (!res.success) 
            return false;
        parameters = res.objects;
    }
    
    if (LocationIsValid(code->function.returns_location)) {
        if (code->function.return_is_list) {
            ObjectDefinitionResult res = ReadDefinitionList(context.arena, ParserSub(parser, code->function.returns_location), reporter, program, RegisterKind_Return);
            if (!res.success) 
                return false;
            returns = res.objects;
        }
        else {
            Type* type = ReadObjectType(ParserSub(parser, code->function.returns_location), reporter, program);
            
            if (type != nil_type) {
                returns = ArrayAlloc<ObjectDefinition>(context.arena, 1);
                returns[0] = ObjDefMake("return", type, code->function.returns_location, false, ValueFromZero(type));
            }
        }
        
        if (returns.count == 0) 
            return false;
    }
    
    FunctionDefine(program, def, parameters, returns);
    return true;
}

B32 ReadArgDefinition(Parser* parser, Program* program, Reporter* reporter, CodeDefinition* code)
{
    PROFILE_FUNCTION;
    
    ArgDefinition* def = &code->definition->arg;
    
    Type* type = bool_type;
    
    if (LocationIsValid(code->arg.type_location))
    {
        type = ReadObjectType(ParserSub(parser, code->arg.type_location), reporter, program);
        
        if (type == nil_type) return false;
        
        if (!TypeIsValid(type)) {
            ReportErrorFront(code->arg.type_location, "Invalid type '%S' for an argument", type->name);
            return false;
        }
    }
    
    ArgDefine(program, def, type);
    return true;
}

B32 ResolveEnumDefinition(Parser* parser, Program* program, Reporter* reporter, CodeDefinition* code)
{
    PROFILE_FUNCTION;
    
    EnumDefinition* def = &code->definition->_enum;
    
    if (def->stage == DefinitionStage_Ready) {
        return true;
    }
    
    if (def->stage != DefinitionStage_Defined) {
        InvalidCodepath();
        return false;
    }
    
    IR_Context* ir_context = IrContextAlloc(program, reporter);
    
    Array<I64> values = ArrayAlloc<I64>(context.arena, def->names.count);
    
    for (U32 i = 0; i < values.count; i++)
    {
        values[i] = i;
        
        if (i < def->expression_locations.count)
        {
            Location expression_location = def->expression_locations[i];
            
            if (!LocationIsValid(expression_location)) continue;
            
            IR ir = IrFromValue(context.arena, program, ValueFromInt(values[i]));
            
            ExpresionContext expr_context = ExpresionContext_from_type(int_type, 1);
            IR_Group group = ReadExpression(ir_context, ParserSub(parser, expression_location), expr_context);
            ir = MakeIR(context.arena, program, ArrayFromBArray(context.arena, ir_context->local_registers), group, NULL);
            if (!ir.success) return false;
            
            if (ir.value.kind != ValueKind_Literal || !TypeIsAnyInt(ir.value.type)) {
                ReportErrorFront(expression_location, "Enum value expects an Int literal");
                return false;
            }
            
            values[i] = ir.value.literal_sint;
        }
    }
    
    EnumResolve(program, def, values);
    return true;
}

B32 ResolveStructDefinition(Parser* parser, Program* program, Reporter* reporter, CodeDefinition* code)
{
    PROFILE_FUNCTION;
    
    StructDefinition* def = &code->definition->_struct;
    
    if (def->stage == DefinitionStage_Ready) {
        return true;
    }
    
    if (def->stage != DefinitionStage_Defined) {
        InvalidCodepath();
        return false;
    }
    
    // Check for dependencies
    foreach(i, def->types.count)
    {
        Type* type = def->types[i];
        
        if (!TypeIsSizeReady(type)) {
            return false;
        }
    }
    
    StructResolve(program, def);
    return true;
}

B32 ResolveFunctionDefinition(Parser* parser, Program* program, Reporter* reporter, CodeDefinition* code)
{
    PROFILE_FUNCTION;
    
    FunctionDefinition* def = &code->definition->function;
    
    if (def->stage == DefinitionStage_Ready) {
        return true;
    }
    
    if (def->stage != DefinitionStage_Defined) {
        InvalidCodepath();
        return false;
    }
    
    B32 is_intrinsic = !LocationIsValid(code->function.body_location);
    
    if (is_intrinsic)
    {
        IntrinsicFunction* fn = IntrinsicFromName(def->name);
        
        if (fn == NULL) {
            report_intrinsic_not_resolved(def->name);
            return false;
        }
        
        FunctionResolveIntrinsic(program, def, fn);
    }
    else
    {
        Location block_location = code->function.body_location;
        
        IR_Context* ir = IrContextAlloc(program, reporter);
        IR_Group out = IRFromNone();
        
        if (LocationIsValid(code->function.parameters_location)) {
            ObjectDefinitionResult params = ReadDefinitionListWithIr(context.arena, ParserSub(parser, code->function.parameters_location), ir, RegisterKind_Parameter);
            if (!params.success) return false;
            
            out = IRAppend(out, params.out);
        }
        
        // Define returns
        foreach(i, def->returns.count)
        {
            ObjectDefinition obj = def->returns[i];
            out = IRAppend(out, IRFromDefineObject(ir, RegisterKind_Return, obj.name, obj.type, false, obj.location));
            out = IRAppend(out, IRFromStore(ir, out.value, ValueFromZero(obj.type), obj.location));
        }
        
        out = IRAppend(out, ReadCode(ir, ParserSub(parser, block_location)));
        
        YovScript* script = parser->script;
        IR res = MakeIR(program->arena, program, ArrayFromBArray(context.arena, ir->local_registers), out, script);
        
        // Check for returns
        if (res.success)
        {
            Array<Unit> units = res.instructions;
            
            // TODO(Jose): Check for infinite loops
            
            if (!IRValidateReturnPath(units)) {
                report_function_no_return(block_location, def->name);
            }
        }
        
        FunctionResolve(program, def, res);
    }
    
    return true;
}

internal_fn B32 ValidateArgName(Reporter* reporter, String name, Location location)
{
    B32 valid_chars = true;
    
    U64 cursor = 0;
    while (cursor < name.size) {
        U32 codepoint = StrGetCodepoint(name, &cursor);
        
        B32 valid = false;
        if (CodepointIsText(codepoint)) valid = true;
        if (CodepointIsNumber(codepoint)) valid = true;
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

B32 ResolveArgDefinition(Parser* parser, Program* program, Reporter* reporter, CodeDefinition* code)
{
    PROFILE_FUNCTION;
    
    ArgDefinition* def = &code->definition->arg;
    
    if (def->stage == DefinitionStage_Ready) {
        return true;
    }
    
    String name = StrFormat(context.arena, "-%S", code->name);
    String description = {};
    B32 required = false;
    Value default_value = ValueFromZero(def->value_type);
    
    parser = ParserSub(parser, code->arg.body_location);
    
    AssumeToken(parser, TokenKind_OpenBrace);
    
    while (true)
    {
        Token identifier_token = ConsumeToken(parser);
        
        if (identifier_token.kind == TokenKind_CloseBrace) break;
        if (identifier_token.kind == TokenKind_None) {
            ReportErrorFront(identifier_token.location, "Missing close brace for arg definition");
            return false;
        }
        
        if (identifier_token.kind != TokenKind_Identifier) {
            ReportErrorFront(identifier_token.location, "Expecting property list");
            return false;
        }
        
        Token assignment_token = ConsumeToken(parser);
        if (assignment_token.kind != TokenKind_Assignment || assignment_token.assignment_operator != OperatorKind_None)
        {
            ReportErrorFront(assignment_token.location, "Expecting a property assignment");
            return false;
        }
        
        Location expression_location = FetchUntil(parser, false, TokenKind_NextSentence);
        if (!LocationIsValid(expression_location)) {
            ReportErrorFront(identifier_token.location, "Missing semicolon");
            return false;
        }
        
        AssumeToken(parser, TokenKind_NextSentence);
        
        String identifier = identifier_token.value;
        
        ExpresionContext expr_context = ExpresionContext_from_inference(1);
        
        if (identifier == "name") expr_context = ExpresionContext_from_type(string_type, 1);
        else if (identifier == "description") expr_context = ExpresionContext_from_type(string_type, 1);
        else if (identifier == "required") expr_context = ExpresionContext_from_type(bool_type, 1);
        else if (identifier == "default") expr_context = ExpresionContext_from_type(def->value_type, 1);
        else {
            ReportErrorFront(identifier_token.location, "Unknown property '%S'", identifier);
            return false;
        }
        
        IR_Context* ir_context = IrContextAlloc(program, reporter);
        IR_Group group = ReadExpression(ir_context, ParserSub(parser, expression_location), expr_context);
        
        IR ir = MakeIR(context.arena, program, ArrayFromBArray(context.arena, ir_context->local_registers), group, NULL);
        if (!ir.success) {
            return false;
        }
        
        Value value = ir.value;
        
        if (value.type != expr_context.type) {
            report_type_missmatch_assign(expression_location, value.type, expr_context.type);
            return false;
        }
        
        if (!ValueIsCompiletime(value)) {
            ReportErrorFront(expression_location, "Expecting a compile-time value");
            return false;
        }
        
        if (identifier == "name") {
            name = StringFromCompiletime(context.arena, program, value);
        }
        else if (identifier == "description") {
            description = StringFromCompiletime(context.arena, program, value);
        }
        else if (identifier == "required") {
            required = B32FromCompiletime(value);
        }
        else if (identifier == "default") {
            default_value = value;
        }
    }
    
    if (!ValidateArgName(reporter, name, code->entire_location)) return false;
    
    ArgResolve(program, def, name, description, required, default_value);
    return true;
}

YovScript* FrontAddScript(FrontContext* front, String path)
{
    PROFILE_FUNCTION;
    Reporter* reporter = front->reporter;
    
    Assert(OsPathIsAbsolute(path));
    
    RBuffer raw_file;
    if (OsReadEntireFile(front->arena, path, &raw_file).failed) {
        ReportErrorFront(NO_CODE, "File '%S' not found\n", path);
        return NULL;
    }
    
    MutexLockGuard(&front->mutex);
    
    // Check for duplicated
    foreach_BArray(it, &front->scripts) {
        YovScript* s = it.value;
        if (StrEquals(s->path, path)) {
            return NULL;
        }
    }
    
    path = StrCopy(front->arena, path);
    String text = StrFromRBuffer(raw_file);
    
    I32 script_id = front->scripts.count;
    
    YovScript* script = BArrayAdd(&front->scripts);
    script->id = script_id;
    script->path = path;
    script->name = PathGetLastElement(path);
    script->dir = PathResolve(front->arena, PathAppend(context.arena, path, ".."));
    script->text = text;
    return script;
}

#include "autogenerated/core.h"

YovScript* FrontAddCoreScript(FrontContext* front)
{
    PROFILE_FUNCTION;
    String text = YOV_CORE;
    
    MutexLockGuard(&front->mutex);
    
    I32 script_id = front->scripts.count;
    
    YovScript* script = BArrayAdd(&front->scripts);
    script->id = script_id;
    script->path = "core.yov";
    script->name = "core.yov";
    script->dir = "./";
    script->text = text;
    return script;
}

YovScript* FrontGetScript(FrontContext* front, I32 script_id)
{
    if (script_id < 0) return NULL;
    
    if (script_id >= front->scripts.count) {
        Assert(0);
        return NULL;
    }
    
    return &front->scripts[script_id];
}

U32 LineFromLocation(Location location, YovScript* script)
{
    if (script == NULL || !LocationIsValid(location)) return 0;
    
    Array<U64> lines = script->lines;
    if (lines.count == 0) return 0;
    
    U64 cursor = location.range.min;
    
    U32 low = 0;
    U32 high = lines.count - 1;
    
    while (low <= high)
    {
        U32 mid = low + (high - low) / 2;
        
        if (lines[mid] <= cursor)
        {
            if (mid + 1 >= lines.count || lines[mid + 1] > cursor) {
                return mid + 1;
            }
            
            low = mid + 1;
        }
        else
        {
            high = mid - 1;
        }
    }
    
    InvalidCodepath();
    return 0;
}

Parser* ParserFromLocation(FrontContext* front, Location location)
{
    YovScript* script = FrontGetScript(front, location.script_id);
    if (script == NULL) {
        InvalidCodepath();
        return ParserAlloc(NULL, {});
    }
    
    return ParserAlloc(script, location.range);
}

void FrontReadLocationsAndImports(FrontContext* front, YovScript* script, LaneGroup* lane_group)
{
    PROFILE_FUNCTION;
    if (script == NULL) return;
    
    Program* program = front->program;
    Reporter* reporter = front->reporter;
    Parser* parser = ParserAlloc(script, { 0, script->text.size });
    
    BArray<U64> lines = BArrayMake<U64>(context.arena, 512);
    BArrayAdd<U64>(&lines, 0);
    
    U64 last_line_check_cursor = 0;
    
    while (true)
    {
        Token t0 = PeekToken(parser);
        
        // Check for new lines
        {
            U64 end_cursor = t0.cursor + t0.skip_size;
            
            for (U64 i = last_line_check_cursor; i < end_cursor; i++)
            {
                if (script->text[i] == '\n') {
                    BArrayAdd(&lines, i + 1);
                }
            }
            
            last_line_check_cursor = end_cursor;
        }
        
        if (t0.kind == TokenKind_None) break;
        
        Location start_location = t0.location;
        
        if (t0.kind == TokenKind_ImportKeyword)
        {
            SkipToken(parser, t0);
            Token literal_token = ConsumeToken(parser);
            Token semicolon_token = ConsumeToken(parser);
            
            if (literal_token.kind != TokenKind_StringLiteral) {
                report_expecting_string_literal(start_location);
                break;
            }
            
            if (semicolon_token.kind != TokenKind_NextSentence) {
                report_expecting_semicolon(start_location);
                break;
            }
            
            String import_path = PathResolveImport(context.arena, script->dir, literal_token.value);
            YovScript* new_script = FrontAddScript(front, import_path);
            
            if (new_script != NULL && lane_group != NULL) {
                LaneTaskAdd(lane_group, 1);
            }
        }
        else
        {
            SentenceKind op = GuessSentenceKind(parser);
            
            if (op == SentenceKind_ObjectDef)
            {
                Location next_sentence = FetchUntil(parser, false, TokenKind_NextSentence);
                
                if (!LocationIsValid(next_sentence)) {
                    ReportErrorFront(t0.location, "Missing semicolon");
                    continue;
                }
                
                Location location = LocationMake(t0.cursor, parser->cursor, script->id);
                
                MutexLock(&front->mutex);
                BArrayAdd(&front->global_location_list, location);
                MutexUnlock(&front->mutex);
                
                AssumeToken(parser, TokenKind_NextSentence);
            }
            else if (op == SentenceKind_FunctionDef || op == SentenceKind_StructDef || op == SentenceKind_EnumDef || op == SentenceKind_ArgDef)
            {
                CodeDefinition def;
                if (!ReadCodeDefinition(&def, parser, reporter, op)) {
                    break;
                }
                
                {
                    MutexLockGuard(&front->mutex);
                    String identifier = def.name;
                    def.definition = AddDefinition(program, reporter, def.type, identifier, true, def.entire_location);
                    BArrayAdd(&front->definitions, def);
                }
            }
            else
            {
                ReportErrorFront(start_location, "Unsupported operation");
                // TODO(Jose): Skip tokens without breaking
                break;
            }
        }
    }
    
    // Check for new lines
    {
        U64 end_cursor = script->text.size;
        
        for (U64 i = last_line_check_cursor; i < end_cursor; i++)
        {
            if (script->text[i] == '\n') {
                BArrayAdd(&lines, i + 1);
            }
        }
    }
    
    script->lines = ArrayFromBArray(front->arena, lines);
}

void FrontReadAllScripts(LaneContext* lane, FrontContext* front)
{
    PROFILE_FUNCTION;
    LaneGroup* group = lane->group;
    
    if (LaneNarrow(lane, 0)) {
        FrontAddScript(front, front->input->main_script_path);
        FrontAddCoreScript(front);
    }
    
    LaneTaskStart(lane, front->scripts.count);
    
    while (LaneDynamicTaskIsBusy(group))
    {
        U32 script_id;
        if (LaneTaskFetch(group, &script_id))
        {
            YovScript* script = FrontGetScript(front, script_id);
            FrontReadLocationsAndImports(front, script, group);
            LaneDynamicTaskFinish(group);
        }
        else {
            OsThreadYield();
        }
    }
    
    LaneBarrier(lane);
}

void FrontDefineDefinitions(LaneContext* lane, FrontContext* front)
{
    PROFILE_FUNCTION;
    
    Program* program = front->program;
    Reporter* reporter = front->reporter;
    
    RangeU32 range = LaneDistributeUniformWork(lane, front->definitions.count);
    
    for (U32 i = range.min; i < range.max; ++i)
    {
        CodeDefinition* code = &front->definitions[i];
        
        Parser* parser = ParserFromLocation(front, code->entire_location);
        
        if (code->type == DefinitionType_Enum) {
            ReadEnumDefinition(parser, program, reporter, code);
        }
        else if (code->type == DefinitionType_Struct) {
            ReadStructDefinition(parser, program, reporter, code);
        }
        else if (code->type == DefinitionType_Function) {
            ReadFunctionDefinition(parser, program, reporter, code);
        }
        else {
            ReadArgDefinition(parser, program, reporter, code);
        }
    }
    
    LaneBarrier(lane);
}


internal_fn void DefineLangGlobal(FrontContext* front, String name, String type)
{
    PROFILE_FUNCTION;
    
    Global global = {};
    global.identifier = name;
    global.type = TypeFromName(front->program, type);
    global.is_constant = true;
    
    MutexLock(&front->mutex);
    BArrayAdd(&front->global_list, global);
    MutexUnlock(&front->mutex);
}

void FrontDefineGlobals(LaneContext* lane, FrontContext* front)
{
    PROFILE_FUNCTION;
    
    Program* program = front->program;
    Reporter* reporter = front->reporter;
    
    if (LaneNarrow(lane))
    {
        DefineLangGlobal(front, "yov", "YovInfo");
        DefineLangGlobal(front, "os", "OS");
        DefineLangGlobal(front, "context", "Context");
        DefineLangGlobal(front, "calls", "CallsContext");
    }
    
    RangeU32 code_range = LaneDistributeUniformWork(lane, front->global_location_list.count);
    
    // Define code globals
    {
        for (U32 i = code_range.min; i < code_range.max; ++i)
        {
            Location location = front->global_location_list[i];
            
            ObjectDefinitionResult res = ReadObjectDefinition(context.arena, ParserFromLocation(front, location), reporter, program, false, RegisterKind_Global);
            if (!res.success) continue;
            
            Array<Global> globals = ArrayAlloc<Global>(context.arena, res.objects.count);
            
            foreach(i, globals.count)
            {
                ObjectDefinition def = res.objects[i];
                
                Global global = {};
                global.identifier = StrCopy(program->arena, def.name);
                global.type = def.type;
                global.is_constant = def.is_constant;
                
                Assert(TypeIsValid(def.type));
                
                globals[i] = global;
            }
            
            MutexLock(&front->mutex);
            foreach(i, globals.count)
                BArrayAdd(&front->global_list, globals[i]);
            MutexUnlock(&front->mutex);
        }
        
        LaneBarrier(lane);
    }
    
    // Define args globals
    if (LaneNarrow(lane))
    {
        foreach(i, program->definitions.count)
        {
            ArgDefinition def = program->definitions[i].arg;
            if (def.type != DefinitionType_Arg) continue;
            
            
            Global global = {};
            global.identifier = StrCopy(program->arena, def.name);
            global.type = def.value_type;
            global.is_constant = true;
            
            Assert(TypeIsValid(def.value_type));
            
            BArrayAdd(&front->global_list, global);
        }
    }
    LaneBarrier(lane);
    
    if (LaneNarrow(lane)) {
        program->globals = ArrayFromBArray(program->arena, front->global_list);
    }
    LaneBarrier(lane);
}

void FrontResolveGlobals(LaneContext* lane, FrontContext* front)
{
    PROFILE_FUNCTION;
    
    Reporter* reporter = front->reporter;
    Program* program = front->program;
    
    // Code globals
    {
        RangeU32 code_range = LaneDistributeUniformWork(lane, front->global_location_list.count);
        
        for (U32 i = code_range.min; i < code_range.max; ++i)
        {
            Location location = front->global_location_list[i];
            
            IR_Context* ir_context = IrContextAlloc(program, reporter);
            
            ObjectDefinitionResult res = ReadObjectDefinitionWithIr(context.arena, ParserFromLocation(front, location), ir_context, false, RegisterKind_Global);
            if (!res.success) continue;
            
            MutexLock(&front->mutex);
            front->global_initialize_group = IRAppend(front->global_initialize_group, res.out);
            front->number_of_registers_for_global_initialize = Max(front->number_of_registers_for_global_initialize, ir_context->local_registers.count);
            MutexUnlock(&front->mutex);
        }
    }
    LaneBarrier(lane);
    
    
    if (LaneNarrow(lane))
    {
        // Args
        {
            IR_Context* ir_context = IrContextAlloc(program, reporter);
            
            foreach(i, program->definitions.count)
            {
                ArgDefinition* def = &program->definitions[i].arg;
                if (def->type != DefinitionType_Arg) continue;
                
                Value value = def->default_value;
                
                ScriptArg* script_arg = InputFindScriptArg(front->input, def->name);
                
                if (script_arg == NULL) {
                    if (def->required) {
                        report_arg_is_required(def->location, def->name);
                        continue;
                    }
                }
                else
                {
                    value = ValueNone();
                    
                    if (script_arg->value.size <= 0)
                    {
                        if (def->value_type == bool_type) {
                            value = ValueFromBool(true);
                        }
                    }
                    else
                    {
                        value = ValueFromStringExpression(program->arena, script_arg->value, def->value_type);
                    }
                    
                    if (value.kind == ValueKind_None) {
                        report_arg_wrong_value(def->name, script_arg->value);
                        continue;
                    }
                }
                
                I32 global_index = GlobalIndexFromIdentifier(program, def->name);
                
                if (global_index >= 0)
                {
                    front->global_initialize_group = IRAppend(front->global_initialize_group, IRFromStore(ir_context, ValueFromGlobal(program, global_index), value, def->location));
                    front->number_of_registers_for_global_initialize = Max(front->number_of_registers_for_global_initialize, ir_context->local_registers.count);
                }
            }
        }
        
        if (front->global_initialize_group.success)
        {
            Array<Register> registers = ArrayAlloc<Register>(context.arena, front->number_of_registers_for_global_initialize);
            
            foreach(i, registers.count)
            {
                Register reg = {};
                reg.kind = RegisterKind_Local;
                reg.is_constant = false;
                reg.type = any_type;
                
                registers[i] = reg;
            };
            
            program->globals_initialize_ir = MakeIR(program->arena, program, registers, front->global_initialize_group, NULL);
        }
    }
    
    LaneBarrier(lane);
}

void FrontResolveDefinitions(LaneContext* lane, FrontContext* front)
{
    PROFILE_FUNCTION;
    
    Program* program = front->program;
    Reporter* reporter = front->reporter;
    
    LaneBarrier(lane);
    
    while (front->resolve_count < front->definitions.count && !reporter->exit_requested)
    {
        LaneBarrier(lane);
    
        RangeU32 range = LaneDistributeUniformWork(lane, front->definitions.count);
        
        for (U32 i = range.min; i < range.max; ++i)
        {
            CodeDefinition* code = &front->definitions[i];
            
            Parser* parser = ParserFromLocation(front, code->entire_location);
            
            B32 resolved = false;
            
            if (code->type == DefinitionType_Enum) {
                resolved = ResolveEnumDefinition(parser, program, reporter, code);
            }
            else if (code->type == DefinitionType_Struct) {
                resolved = ResolveStructDefinition(parser, program, reporter, code);
            }
            else if (code->type == DefinitionType_Function) {
                resolved = ResolveFunctionDefinition(parser, program, reporter, code);
            }
            else {
                resolved = ResolveArgDefinition(parser, program, reporter, code);
            }
            
            if (resolved) {
                AtomicIncrement32(&front->resolve_count);
            }
        }
        
        LaneBarrier(lane);
        
        if (LaneNarrow(lane))
        {
            if (front->resolve_count < front->definitions.count)
            {
                if (front->last_resolve_count == front->resolve_count) {
                    InvalidCodepath();
                    AtomicStore32(&front->resolve_count, front->definitions.count);
                }
                else {
                    front->last_resolve_count = front->resolve_count;
                    AtomicStore32(&front->resolve_count, 0);
                }
            }
            
            front->resolve_iterations++;
        }
        
        LaneBarrier(lane);
        MemoryBarrierAcquire();
    }
    
    if (LaneNarrow(lane)) {
        LogFlow("Resove iterations: %u", front->resolve_iterations);
    }
    
    LaneBarrier(lane);
}
