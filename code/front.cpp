#include "front.h"

String StringFromDefinitionType(DefinitionType type)
{
    switch (type) {
        case DefinitionType_FunctionHeader: return "FunctionHeader";
        case DefinitionType_GenericFunctionHeader: return "GenericFunctionHeader";
        case DefinitionType_Struct: return "Struct";
        case DefinitionType_Enum: return "Enum";
        case DefinitionType_Arg: return "Arg";
        case DefinitionType_Global: return "Global";

        case DefinitionType_Unknown: break;
    }
    InvalidCodepath();
    return "?";
}

ResolveGenericEntry ResolveGenericEntryCopy(Arena* arena, ResolveGenericEntry src) {
    ResolveGenericEntry dst = {};
    dst.name = StrCopy(arena, src.name);
    dst.type_id = src.type_id;
    return dst;
}

ResolveGenericEntry FindGenericType(String name, Array<ResolveGenericEntry> table)
{
    for (U32 i = 0; i < table.count; i++) {
        if (table[i].name == name) return table[i];
    }
    return {};
}

ObjectDefinition ObjDefMake(String name, U32 type_id, Location location, B32 is_constant) {
    ObjectDefinition d{};
    d.name = name;
    d.type_id = type_id;
    d.is_constant = is_constant;
    d.location = location;
    return d;
}

ObjectDefinition ObjectDefinitionCopy(Arena* arena, ObjectDefinition src)
{
    ObjectDefinition dst = src;
    dst.name = StrCopy(arena, src.name);
    return dst;
}

internal_fn void ReadLocationsAndImports(FrontContext* front, FrontScript* script, LaneGroup* lane_group)
{
    PROFILE_FUNCTION;
    if (script == NULL) return;
    
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
            FrontScript* new_script = FrontAddScript(front, import_path);
            
            if (new_script != NULL && lane_group != NULL) {
                LaneTaskAdd(lane_group, 1);
            }
        }
        else
        {
            ReadDefinition(front, parser, true);
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

internal_fn void IdentifyPass(LaneContext* lane, FrontContext* front)
{
    PROFILE_FUNCTION;
    LaneGroup* group = lane->group;
    
    if (LaneNarrow(lane, 0)) {
        FrontAddScript(front, front->main_script_path);
        FrontAddCoreScript(front);
    }
    
    LaneTaskStart(lane, front->scripts.count);
    
    while (LaneDynamicTaskIsBusy(group))
    {
        U32 script_id;
        if (LaneTaskFetch(group, &script_id))
        {
            FrontScript* script = FrontGetScript(front, script_id);
            ReadLocationsAndImports(front, script, group);
            LaneDynamicTaskFinish(group);
        }
        else {
            OsThreadYield();
        }
    }
}

internal_fn void DependencyPass(LaneContext* lane, FrontContext* front)
{
    PROFILE_FUNCTION;
    
    Reporter* reporter = front->reporter;

    U32 task_index = 0;

    LaneTaskStart(lane, front->definitions.count);
    while (LaneTaskFetch(lane->group, &task_index))
    {
        FrontDefinition* def = &front->definitions[task_index];
        if (def->type == DefinitionType_GenericFunctionHeader) continue;
        ResolveDefinition(front, def);
    }

    LaneBarrier(lane);

    LaneTaskStart(lane, front->definitions.count);
    while (LaneTaskFetch(lane->group, &task_index))
    {
        FrontDefinition* def = &front->definitions[task_index];
        if (def->type != DefinitionType_GenericFunctionHeader) continue;
        ResolveDefinition(front, def);
    }
    
    LaneBarrier(lane);
}

internal_fn IR_Group IRGenerateScriptHelp(IR_Context* ir)
{
    PROFILE_FUNCTION;

    FrontContext* front = ir->front;
    Reporter* reporter = front->reporter;

    IR_Group group = IRFromNone(ValueNone());

    BArray<Value> values = BArrayMake<Value>(context.arena, 32);

    // Script description
    {
        I32 global_index = FrontGlobalIndexFromName(front, "script_description");

        if (global_index >= 0)
        {
            group = IRAppend(group, IRFromSymbol(ir, "script_description", NO_CODE));
            if (!group.success) return IRFailed();

            BArrayAdd(&values, group.value);
            BArrayAdd(&values, ValueFromString(context.arena, "\n\n"));
        }
    }
    
    Array<String> headers = ArrayAlloc<String>(context.arena, front->_args.count);
    
    U32 index = 0;
    U32 longest_header = 0;
    foreach(i, headers.count)
    {
        ArgDefinition* arg = ArgFromIndex(front, i);
        ObjectDefinition* obj = &front->global_objects[arg->global_index];
        Type* type = TypeFromID(front->tsys, obj->type_id);
    
        B32 show_type = TypeIsValid(type);
        
        String space = "    ";
        
        String header;
        if (show_type)
        {
            String type_str;
            if (type->kind == VKind_Enum) {
                type_str = "enum";
            }
            else {
                type_str = type->name;
            }
            header = StrFormat(context.arena, "%S-%S -> %S", space, arg->arg_name, type_str);
        }
        else {
            header = StrFormat(context.arena, "%S-%S", space, arg->arg_name);
        }
        
        headers[index++] = header;
        
        U32 char_count = StrCalculateCharCount(header);
        longest_header = Max(longest_header, char_count);
    }
    
    U32 chars_to_description = longest_header + 4;
    
    index = 0;

    BArrayAdd(&values, ValueFromString(context.arena, "Script Arguments:\n"));

    foreach(i, headers.count)
    {
        ArgDefinition* arg = ArgFromIndex(front, i);
        ObjectDefinition* obj = &front->global_objects[arg->global_index];
        Type* type = TypeFromID(front->tsys, obj->type_id);
        
        String header = headers[index++];
        
        BArrayAdd(&values, ValueFromString(context.arena, header));

        // Parse description
        if (LocationIsValid(arg->description_location))
        {
            Parser* parser = ParserFromLocation(front, arg->description_location);
            IR_Group out = ReadExpression(ir, parser, ExpresionContext_from_type(string_type, 1));
            if (!out.success) continue;

            U32 char_count = StrCalculateCharCount(header);
            for (U32 i = char_count; i < chars_to_description; ++i) {
                BArrayAdd(&values, ValueFromString(context.arena, " "));
            }

            group = IRAppend(group, out);
            BArrayAdd(&values, out.value);
        }

        BArrayAdd(&values, ValueFromString(context.arena, "\n"));
    }

    Value value = ValueFromStringArray(ir->arena, ArrayFromBArray(context.arena, values));
    group.value = value;
    return group;
}

internal_fn IR_Group IRInitializeScriptHelp(IR_Context* ir)
{
    IR_Group group = IRFromNone();

    group = IRAppend(group, IRFromSymbol(ir, "__YovScriptHelp", NO_CODE));
    if (!group.success) return IRFailed();

    Value dst = group.value;

    group = IRAppend(group, IRGenerateScriptHelp(ir));
    if (!group.success) return IRFailed();

    Value src = group.value;

    return IRAppend(group, IRFromStore(ir, dst, src, NO_CODE));
}

internal_fn void IRPass(LaneContext* lane, FrontContext* front)
{
    PROFILE_FUNCTION;
    
    TypeSystem* tsys = front->tsys;
    Reporter* reporter = front->reporter;

    if (LaneNarrow(lane))
    {
        FunctionHeader* header = FunctionHeaderFromName(front, "__YovEntryPoint");

        if (header == NULL) {
            ReportErrorFront(NO_CODE, "Function '__YovEntryPoint' is not defined");
        }
        else
        {
            IR_Context* ir = IrContextAlloc(front, {});

            IR_Group group = IRFromNone();

            // User defined globals
            foreach_BArray(it, &front->_globals)
            {
                GlobalDefinition* def = it.value;

                Parser* parser = ParserFromLocation(front, def->location);

                ObjectDefinitionResult res = ReadObjectDefinitionWithIr(context.arena, ir, parser, false, RegisterKind_Global);
                
                if (res.success) {
                    group = IRAppend(group, res.out);
                }
            }

            // Init arg globals
            foreach_BArray(it, &front->_args)
            {
                ArgDefinition* def = it.value;

                B32 has_default = LocationIsValid(def->default_value_location);
                if (!has_default && def->required) continue;

                ObjectDefinition* obj = &front->global_objects[def->global_index];

                Value dst = ValueFromGlobal(obj->type_id, def->global_index);
                Value src = ValueFromZero(TypeGet(obj->type_id));
                
                if (has_default)
                {
                    Parser* parser = ParserFromLocation(front, def->default_value_location);

                    IR_Group default_value_out = ReadExpression(ir, parser, ExpresionContext_from_type(TypeGet(obj->type_id), 1));
                    if (!default_value_out.success) continue;

                    src = default_value_out.value;

                    group = IRAppend(group, default_value_out);
                }
                
                group = IRAppend(group, IRFromStore(ir, dst, src, def->default_value_location));
            }

            // Script Help
            group = IRAppend(group, IRInitializeScriptHelp(ir));

            // Runtime globals
            group = IRAppend(group, IRFromFunctionCallName(ir, "__YovSetupRuntime", {}, ExpresionContext_from_void(), NO_CODE));

            // Main
            group = IRAppend(group, IRFromFunctionCallName(ir, "Main", {}, ExpresionContext_from_void(), NO_CODE));

            FunctionBody* entry_point = AddBody(front, header->index, MakeIR(front->arena, ArrayFromBArray(context.arena, ir->local_registers), group, NULL));
        }
    }

    LaneGroup* group = lane->group;

    LaneTaskStart(lane, front->definitions.count);

    while (LaneDynamicTaskIsBusy(group))
    {
        U32 def_index;
        if (LaneTaskFetch(lane->group, &def_index))
        {
            FrontDefinition* def = &front->definitions[def_index];
            GenerateIR(front, def);

            LaneTaskSetTotal(group, front->definitions.count);
            LaneDynamicTaskFinish(group);
        }
        else {
            OsThreadYield();
        }
    }    

    LaneBarrier(lane);
}

internal_fn void FrontRun(FrontContext* front, LaneContext* lane)
{
    PROFILE_FUNCTION;
    
    // Identify Pass
    {
        if (LaneNarrow(lane)) {
            LogFlow("Starting Identify");
        }

        F64 start_time = TimerNow();

        IdentifyPass(lane, front);
        LaneBarrier(lane);
        ArenaPopTo(context.arena, 0);
    
        if (LaneNarrow(lane)) {
            F64 ellapsed = TimerNow() - start_time;
            LogFlow("Identify pass finished: %S", StringFromEllapsedTime(ellapsed));
        }

        if (front->reporter->exit_requested) {
            return;
        }
    }
    
    // Dependency Pass
    {
        if (LaneNarrow(lane)) {
            LogFlow("Starting Dependency Pass");
        }
        
        F64 start_time = TimerNow();
        
        DependencyPass(lane, front);
        LaneBarrier(lane);
        ArenaPopTo(context.arena, 0);
        
        if (LaneNarrow(lane)) {
            F64 ellapsed = TimerNow() - start_time;
            LogFlow("Dependency Pass finished: %S", StringFromEllapsedTime(ellapsed));
        }
        
        if (front->reporter->exit_requested) {
            return;
        }
    }
    
    // IR Pass
    {
        if (LaneNarrow(lane)) {
            LogFlow("Starting IR Pass");
        }
        
        F64 start_time = TimerNow();

        IRPass(lane, front);
        LaneBarrier(lane);
        ArenaPopTo(context.arena, 0);
        
        if (LaneNarrow(lane)) {
            F64 ellapsed = TimerNow() - start_time;
            LogFlow("IR Pass finished: %S", StringFromEllapsedTime(ellapsed));
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
            FrontScript* script = FrontGetScript(front, location.script_id);
            if (script == NULL) continue;
            
            report->path = StrCopy(reporter->arena, script->path);
            report->line = LineFromLocation(location, script);
        }
    }
}

void WriteFrontScript(Serializer* s, FrontScript src)
{
    WriteU32(s, 0); // VERSION
    WriteString(s, src.path);
}

void WriteFunctionBody(Serializer* s, FunctionBody src)
{
    WriteU32(s, 0); // VERSION
    
    WriteU32(s, src.function_header_index);
    WriteIR(s, src.ir);
}

void WriteFunctionHeader(Serializer* s, FunctionHeader src)
{
    WriteU32(s, 0); // VERSION
    
    WriteString(s, src.name);
    WriteArray(s, src.parameters, WriteObjectDefinition);
    WriteArray(s, src.returns, WriteObjectDefinition);

    WriteLocation(s, src.location);
}

void WriteStructDefinition(Serializer* s, StructDefinition src)
{
    WriteU32(s, 0); // VERSION
    
    WriteString(s, src.name);

    WriteArray(s, src.generic_names, WriteString);
    WriteArray(s, src.generic_types, WriteU32);
    WriteB8(s, src.has_generics);
    WriteArray(s, src.names, WriteString);
    WriteArray(s, src.types, WriteU32);

    WriteLocation(s, src.location);
}

void WriteEnumDefinition(Serializer* s, EnumDefinition src)
{
    WriteU32(s, 0); // VERSION
    
    WriteString(s, src.name);
    WriteArray(s, src.names, WriteString);
    WriteArray(s, src.values, WriteI64);
    
    WriteLocation(s, src.location);
}

void WriteGlobalDefinition(Serializer* s, GlobalDefinition src)
{
    WriteU32(s, 0); // VERSION
    
    WriteB8(s, src.is_constant);
    
    WriteLocation(s, src.location);
}

void WriteArgDefinition(Serializer* s, ArgDefinition src)
{
    WriteU32(s, 0); // VERSION
    
    WriteI32(s, src.global_index);
    WriteString(s, src.arg_name);
    WriteB8(s, src.required);

    WriteLocation(s, src.location);
}

RBuffer BinaryFromFrontContext(Arena* arena, FrontContext* src)
{
    Serializer* s = SerializerAlloc(context.arena);

    WriteU32(s, 0); // VERSION

    WriteString(s, src->main_script_path);

    WriteBArray(s, src->scripts, WriteFrontScript);
    
    {
        MutexLockGuard(&src->tsys->types_mutex);
        WriteU32(s, src->tsys->types.count);
        foreach_BArray(it, &src->tsys->types) WriteType(src->tsys, s, *it.value);
    }

    WriteBArray(s, src->global_objects, WriteObjectDefinition);

    WriteBArray(s, src->_functions, WriteFunctionBody);
    WriteBArray(s, src->_function_headers, WriteFunctionHeader);
    WriteBArray(s, src->_structs, WriteStructDefinition);
    WriteBArray(s, src->_enums, WriteEnumDefinition);
    WriteBArray(s, src->_args, WriteArgDefinition);
    WriteBArray(s, src->_globals, WriteGlobalDefinition);
    
    return RBufferFromSerializer(arena, s);
}

RBuffer YovCompile(Arena* arena, Reporter* reporter, String path)
{
    PROFILE_FRAME_MARK;
    PROFILE_FUNCTION;
    
    Arena* front_arena = ArenaAlloc(Gb(32), 8, "Arena Front");

    defer (
        ArenaFree(front_arena);
        ArenaPopTo(context.arena, 0);
    );

    FrontContext* front = NULL;
    front = ArenaPushStruct<FrontContext>(front_arena);
    front->arena = front_arena;
    front->reporter = reporter;
    front->tsys = TypeSystemAlloc(front_arena);

    front->main_script_path = path;

    front->scripts = BArrayMake<FrontScript>(front_arena, 16);
    front->definitions = BArrayMake<FrontDefinition>(front_arena, 64);
    front->global_objects = BArrayMake<ObjectDefinition>(front_arena, 32);

    front->_functions = BArrayMake<FunctionBody>(front_arena, 32);
    front->_function_headers = BArrayMake<FunctionHeader>(front_arena, 32);
    front->_generic_function_headers = BArrayMake<GenericFunctionHeader>(front_arena, 32);
    front->_structs = BArrayMake<StructDefinition>(front_arena, 32);
    front->_enums = BArrayMake<EnumDefinition>(front_arena, 32);
    front->_globals = BArrayMake<GlobalDefinition>(front_arena, 32);
    front->_args = BArrayMake<ArgDefinition>(front_arena, 32);

    U32 lane_count = DEV_SINGLE_THREAD ? 1 : U32_MAX;
    
    LaneGroup* group = LaneGroupStart(context.arena, FrontWide, front, lane_count);
    LaneGroupWait(group);

#if LOG_IR_ENABLED
    foreach(i, front->_functions.count)
    {
        FunctionBody* fn = &front->_functions[i];
        FunctionHeader* header = &front->_function_headers[fn->function_header_index];
        PrintIr(front, header->name, fn->ir);
    }
#endif

    return BinaryFromFrontContext(arena, front);
}

FrontDefinition* ReadDefinition(FrontContext* front, Parser* parser, B32 is_global)
{
    Reporter* reporter = front->reporter;

    U64 start_cursor = parser->cursor;

    Token identifier_token = ConsumeToken(parser);
    Token colon0 = ConsumeToken(parser);

    DefinitionType type = DefinitionType_Unknown;

    B32 has_generics = false;
    Array<String> generics = {};

    if (PeekToken(parser).kind != TokenKind_Colon) {
        type = DefinitionType_Global;
    }
    else {
        AssumeToken(parser, TokenKind_Colon);

        TokenKind key = ConsumeToken(parser).kind;
        Token next = PeekToken(parser);

        has_generics = next.kind == TokenKind_OpenBracket;

        if (key == TokenKind_FuncKeyword) {
            if (has_generics) type = DefinitionType_GenericFunctionHeader;
            else type = DefinitionType_FunctionHeader;
        }
        else if (key == TokenKind_StructKeyword) type = DefinitionType_Struct;
        else if (key == TokenKind_EnumKeyword) type = DefinitionType_Enum;
        else if (key == TokenKind_ArgKeyword) type = DefinitionType_Arg;
        else type = DefinitionType_Global;
    }

    if (has_generics && (type != DefinitionType_Struct && type != DefinitionType_GenericFunctionHeader)) {
        ReportErrorFront(LocationFromToken(identifier_token), "Generics not allowed for this definition");
        return NULL;
    }

    if (has_generics)
    {
        Location generics_location = FetchScope(parser, TokenKind_OpenBracket, false);
        if (!LocationIsValid(generics_location)) {
            report_common_missing_closing_bracket(LocationFromToken(identifier_token));
            return NULL;
        }

        generics = ReadGenerics(context.arena, ParserSub(parser, generics_location), reporter);
        if (reporter->exit_requested) return NULL;
    }

    Location location = NO_CODE;

    while (1)
    {
        Token t = PeekToken(parser);

        if (t.kind == TokenKind_None) {
            report_expecting_semicolon(LocationFromToken(identifier_token));
            return NULL;
        }

        if (t.kind == TokenKind_NextSentence) {
            location = LocationMake(start_cursor, parser->cursor, parser->script_id);
            SkipToken(parser, t);
            break;
        }

        if (t.kind == TokenKind_OpenBrace) {
            Location brace_location = FetchScope(parser, TokenKind_OpenBrace, true);
            if (!LocationIsValid(brace_location)) {
                report_common_missing_closing_brace(t.location);
                return NULL;
            }

            location = LocationMake(start_cursor, parser->cursor, parser->script_id);
            break;
        }

        SkipToken(parser, t);
    }

    if (identifier_token.kind != TokenKind_Identifier) {
        ReportErrorFront(identifier_token.location, "Expecting an identifier for the definition");
        return NULL;
    }
    
    if (colon0.kind != TokenKind_Colon) {
        ReportErrorFront(identifier_token.location, "Invalid definition format");
        return NULL;
    }

    if (type == DefinitionType_Unknown) {
        ReportErrorFront(identifier_token.location, "Unknown definition");
        return NULL;
    }

    String name = identifier_token.value;

    return AddDefinition(front, type, name, generics, is_global, location);
}


internal_fn B32 ResolveEnum(FrontContext* front, EnumDefinition* def)
{
    PROFILE_FUNCTION;
    
    Reporter* reporter = front->reporter;
    Parser* parser = ParserFromLocation(front, def->location);
    Location starting_location = PeekToken(parser).location;
    
    AssumeToken(parser, TokenKind_Identifier);
    AssumeToken(parser, TokenKind_Colon);
    AssumeToken(parser, TokenKind_Colon);
    AssumeToken(parser, TokenKind_EnumKeyword);

    Token open_brace_token = ConsumeToken(parser);
    if (open_brace_token.kind != TokenKind_OpenBrace) {
        ReportErrorFront(open_brace_token.location, "Expecting open brace");
        return false;
    }
    
    BArray<String> names = BArrayMake<String>(context.arena, 16);
    BArray<I64> values = BArrayMake<I64>(context.arena, 16);
    U32 index = 0;

    IR_Context* ir_context = IrContextAlloc(front, {});
    
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
        
        Token comma_token = ConsumeToken(parser);
        if (comma_token.kind != TokenKind_Comma && comma_token.kind != TokenKind_CloseBrace) {
            report_enumdef_expecting_comma_separated_identifier(comma_token.location);
            return false;
        }

        I64 value = index;
        
        if (LocationIsValid(expression_location))
        {
            if (!LocationIsValid(expression_location)) continue;
            
            IR ir = IrFromValue(context.arena, ValueFromInt(value));
            
            ExpresionContext expr_context = ExpresionContext_from_type(int_type, 1);
            IR_Group group = ReadExpression(ir_context, ParserSub(parser, expression_location), expr_context);
            ir = MakeIR(context.arena, ArrayFromBArray(context.arena, ir_context->local_registers), group, NULL);
            if (!ir.valid) return false;
            
            if (ir.output_value.kind != ValueKind_Literal || !TypeIsAnyInt(TypeFromID(front->tsys, ir.output_value.type_id))) {
                ReportErrorFront(expression_location, "Enum value expects an Int literal");
                return false;
            }
            
            value = ir.output_value.literal_sint;
        }

        BArrayAdd(&names, name_token.value);
        BArrayAdd(&values, value);

        index++;

        if (comma_token.kind == TokenKind_CloseBrace) break;
    }
    
    def->names = StrArrayCopy(front->arena, ArrayFromBArray(context.arena, names));
    def->values = ArrayCopy(front->arena, ArrayFromBArray(context.arena, values));
    
    foreach(i, def->names.count) {
        LogType("Enum Resolve: %S.%S = %i", def->identifier, def->names[i], (I32)def->values[i]);
    }

    return true;
}

internal_fn Array<ResolveGenericEntry> ResolveGenericsTableFromNames(Arena* arena, TypeSystem* tsys, Array<String> generics)
{
    Array<ResolveGenericEntry> table = ArrayAlloc<ResolveGenericEntry>(context.arena, generics.count);
    foreach(i, generics.count) {
        table[i].name = generics[i];
        table[i].type_id = TypeFromGeneric(tsys, generics[i])->id;
    }
    return table;
}

internal_fn B32 ResolveStruct(FrontContext* front, StructDefinition* def)
{
    PROFILE_FUNCTION;
    
    TypeSystem* tsys = front->tsys;
    Reporter* reporter = front->reporter;
    Parser* parser = ParserFromLocation(front, def->location);
    Location starting_location = PeekToken(parser).location;
    
    AssumeToken(parser, TokenKind_Identifier);
    AssumeToken(parser, TokenKind_Colon);
    AssumeToken(parser, TokenKind_Colon);
    AssumeToken(parser, TokenKind_StructKeyword);

    // Skip generics
    if (PeekToken(parser).kind == TokenKind_OpenBracket) {
        FetchScope(parser, TokenKind_OpenBracket, true);
    }

    Array<ResolveGenericEntry> resolve_generics = ResolveGenericsTableFromNames(context.arena, tsys, def->generic_names);

    Token open_brace_token = ConsumeToken(parser);
    if (open_brace_token.kind != TokenKind_OpenBrace) {
        ReportErrorFront(open_brace_token.location, "Expecting open brace");
        return false;
    }
    
    BArray<ObjectDefinition> members = BArrayMake<ObjectDefinition>(context.arena, 16);
    
    while (PeekToken(parser).kind != TokenKind_CloseBrace)
    {
        Location member_location = FetchUntil(parser, false, TokenKind_NextSentence);
        if (!LocationIsValid(member_location)) {
            report_expecting_semicolon(LocationFromParser(parser, parser->cursor));
            return false;
        }

        ObjectDefinitionResult read_result = ReadObjectDefinition(context.arena, ParserSub(parser, member_location), front, false, resolve_generics, RegisterKind_Local);
        if (!read_result.success) return false;
        
        foreach(i, read_result.objects.count) {
            ObjectDefinition member = read_result.objects[i];
            BArrayAdd(&members, member);
        }
        
        AssumeToken(parser, TokenKind_NextSentence);
    }
    
    ConsumeToken(parser);
    
    Type* struct_type = TypeFromStruct(front->tsys, def->index);
    Assert(TypeIsStruct(struct_type));
    
    B32 valid = true;
    
    foreach_BArray(it, &members)
    {
        ObjectDefinition* def = it.value;
        
        Type* type = TypeFromID(tsys, def->type_id);
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
    
    Array<String> names = ArrayAlloc<String>(front->arena, members.count);
    Array<U32> types = ArrayAlloc<U32>(front->arena, members.count);
    
    foreach(i, members.count)
    {
        ObjectDefinition member = members[i];

        Type* type = TypeFromID(tsys, member.type_id);
        
        names[i] = StrCopy(front->arena, member.name);
        types[i] = member.type_id;
        
        if (!TypeIsValid(type)) {
            InvalidCodepath();
            return false;
        }
    }
    
    Assert(names.count == types.count);
    
    def->names = names;
    def->types = types;

    foreach(i, members.count) {
        LogType("Struct Resolve: %S -> %S: %S", def->identifier, def->names[i], VTypeGetName(program, def->types[i]));
    }

    return true;
}

internal_fn Array<ObjectDefinition> ReadFunctionParameters(Arena* arena, Parser* parser, FrontContext* front, Array<String> generics)
{
    PROFILE_FUNCTION;

    Reporter* reporter = front->reporter;
    TypeSystem* tsys = front->tsys;

    Location location = NO_CODE;

    if (PeekToken(parser).kind != TokenKind_OpenParenthesis)
        return {};
    
    location = FetchScope(parser, TokenKind_OpenParenthesis, false);
    
    if (!LocationIsValid(location)) {
        report_common_missing_closing_parenthesis(PeekToken(parser).location);
        return {};
    }

    Array<ResolveGenericEntry> resolve_generics = ResolveGenericsTableFromNames(context.arena, tsys, generics);

    ObjectDefinitionResult res = ReadObjectDefinitionList(arena, ParserSub(parser, location), front, resolve_generics, RegisterKind_Parameter);
    if (!res.success) 
        return {};

    return res.objects;
}

internal_fn Array<ObjectDefinition> ReadFunctionReturns(Arena* arena, Parser* parser, FrontContext* front, Array<String> generics)
{
    PROFILE_FUNCTION;

    Reporter* reporter = front->reporter;
    TypeSystem* tsys = front->tsys;

    Location location = NO_CODE;
    B32 is_list = false;

    if (PeekToken(parser).kind != TokenKind_Arrow) return {};

    AssumeToken(parser, TokenKind_Arrow);
    
    Token first = PeekToken(parser);
    
    is_list = first.kind == TokenKind_OpenParenthesis;
    
    if (is_list)
    {
        location = FetchScope(parser, TokenKind_OpenParenthesis, false);
        
        if (!LocationIsValid(location)) {
            ReportErrorFront(first.location, "Missing parenthesis for return");
            return {};
        }
    }
    else
    {
        location = FetchUntil(parser, false, TokenKind_OpenBrace);

        if (!LocationIsValid(location)) {
            location = LocationMake(parser->cursor, parser->range.max, parser->script_id);
            MoveCursor(parser, parser->range.max);
        }
    }

    Array<ResolveGenericEntry> resolve_generics = ResolveGenericsTableFromNames(context.arena, tsys, generics);

    if (is_list) {
        ObjectDefinitionResult res = ReadObjectDefinitionList(context.arena, ParserSub(parser, location), front, resolve_generics, RegisterKind_Return);
        if (!res.success)  return {};
        return res.objects;
    }
    else {
        IR_Context* dummy_ir = IrContextAlloc(front, resolve_generics);
        Type* type = ReadObjectType(dummy_ir, ParserSub(parser, location));
        if (type == nil_type) return {};
        
        Array<ObjectDefinition> returns = ArrayAlloc<ObjectDefinition>(context.arena, 1);
        returns[0] = ObjDefMake("return", type->id, location, false);
        return returns;
    }
}

internal_fn B32 ResolveFunctionHeader(FrontContext* front, FunctionHeaderBase* def)
{
    PROFILE_FUNCTION;
    
    TypeSystem* tsys = front->tsys;
    Reporter* reporter = front->reporter;
    Parser* parser = ParserFromLocation(front, def->location);
    Location starting_location = PeekToken(parser).location;
    
    AssumeToken(parser, TokenKind_Identifier);
    AssumeToken(parser, TokenKind_Colon);
    AssumeToken(parser, TokenKind_Colon);
    AssumeToken(parser, TokenKind_FuncKeyword);

    def->body_location = NO_CODE;
    
    Array<ObjectDefinition> parameters = ReadFunctionParameters(context.arena, parser, front, {});
    if (reporter->exit_requested) return false;

    Array<ObjectDefinition> returns = ReadFunctionReturns(context.arena, parser, front, {});
    if (reporter->exit_requested) return false;
    
    // Body
    if (PeekToken(parser).kind != TokenKind_None)
    {
        def->body_location = FetchCode(parser);
        
        if (!LocationIsValid(def->body_location)) {
            ReportErrorFront(PeekToken(parser).location, "Expecting the body of the function");
            return false;
        }
    }

    def->parameters = ArrayCopyRecursive(front->arena, parameters, ObjectDefinitionCopy);
    def->returns = ArrayCopyRecursive(front->arena, returns, ObjectDefinitionCopy);
    
    if (LOG_TYPE_ENABLED) {
        StringBuilder builder = string_builder_make(context.arena);
        appendf(&builder, "Function Define: %S (", def->name);
        
        foreach(i, parameters.count) {
            Type* type = TypeFromID(tsys, parameters[i].type_id);
            appendf(&builder, "%S: %S", parameters[i].name, type->name);
            if (i + 1 < parameters.count)
                append(&builder, ", ");
        }
        
        append(&builder, ") -> (");
        
        foreach(i, returns.count) {
            Type* type = TypeFromID(tsys, returns[i].type_id);
            appendf(&builder, "%S: %S", returns[i].name, type->name);
            if (i + 1 < returns.count)
                append(&builder, ", ");
        }
        
        append(&builder, ")");
        
        LogType(string_from_builder(context.arena, &builder));
    }

    return true;
}

internal_fn B32 ResolveGenericFunctionHeader(FrontContext* front, GenericFunctionHeader* def)
{
    PROFILE_FUNCTION;
    
    TypeSystem* tsys = front->tsys;
    Reporter* reporter = front->reporter;
    Parser* parser = ParserFromLocation(front, def->location);
    Location starting_location = PeekToken(parser).location;
    
    AssumeToken(parser, TokenKind_Identifier);
    AssumeToken(parser, TokenKind_Colon);
    AssumeToken(parser, TokenKind_Colon);
    AssumeToken(parser, TokenKind_FuncKeyword);

    def->body_location = NO_CODE;

    // Skip generics
    if (PeekToken(parser).kind == TokenKind_OpenBracket) {
        FetchScope(parser, TokenKind_OpenBracket, true);
    }
    
    Array<ObjectDefinition> parameters = ReadFunctionParameters(context.arena, parser, front, def->generics);
    if (reporter->exit_requested) return false;

    Array<ObjectDefinition> returns = ReadFunctionReturns(context.arena, parser, front, def->generics);
    if (reporter->exit_requested) return false;
    
    // Body
    if (PeekToken(parser).kind != TokenKind_None)
    {
        def->body_location = FetchCode(parser);
        
        if (!LocationIsValid(def->body_location)) {
            ReportErrorFront(PeekToken(parser).location, "Expecting the body of the function");
            return false;
        }
    }
    
    def->parameters = ArrayCopyRecursive(front->arena, parameters, ObjectDefinitionCopy);
    def->returns = ArrayCopyRecursive(front->arena, returns, ObjectDefinitionCopy);
    
    if (LOG_TYPE_ENABLED) {
        StringBuilder builder = string_builder_make(context.arena);
        appendf(&builder, "Generic Function Define: %S[", def->name);
        
        foreach(i, def->generics.count) {
            appendf(&builder, "%S", def->generics[i]);
            if (i + 1 < def->generics.count)
                append(&builder, ",");
        }
        
        append(&builder, "]");
        
        LogType(string_from_builder(context.arena, &builder));
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

internal_fn B32 ResolveArg(FrontContext* front, ArgDefinition* def)
{
    PROFILE_FUNCTION;
    
    TypeSystem* tsys = front->tsys;
    Reporter* reporter = front->reporter;
    Parser* parser = ParserFromLocation(front, def->location);
    Location starting_location = PeekToken(parser).location;

    Token name_token = ConsumeToken(parser);
    
    AssumeToken(parser, TokenKind_Colon);
    AssumeToken(parser, TokenKind_Colon);
    AssumeToken(parser, TokenKind_ArgKeyword);

    Location type_location = NO_CODE;
    Location body_location = NO_CODE;
    
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
        type_location = to_open_brace;
    }

    U64 start_cursor = parser->cursor;
        
    body_location = FetchCode(parser);

    if (!LocationIsValid(body_location)) {
        ReportErrorFront(starting_location, "Missing body for argument '%S'", name_token.value);
        return false;
    }

    Type* type = bool_type;
    
    if (LocationIsValid(type_location))
    {
        IR_Context* dummy_ir = IrContextAlloc(front, {});
        type = ReadObjectType(dummy_ir, ParserSub(parser, type_location));
        
        if (type == nil_type) return false;
        
        if (!TypeIsValid(type)) {
            ReportErrorFront(type_location, "Invalid type '%S' for an argument", type->name);
            return false;
        }
    }

    def->global_index = AddGlobal(front, name_token.value, type->id, true, def->location);
    if (def->global_index < 0) return false;

    ObjectDefinition* global = &front->global_objects[def->global_index];
    
    String arg_name = global->name;
    Location description_location = NO_CODE;
    B32 required = false;
    Location default_value_location = NO_CODE;
    
    parser = ParserFromLocation(front, body_location);
    
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
        
        if (identifier == "description") {
            description_location = expression_location;
        }
        else if (identifier == "default") {
            default_value_location = expression_location;
        }
        else
        {
            Array<Token> tokens = ConsumeAllTokens(ParserFromLocation(front, expression_location));
            
            if (tokens.count != 1) {
                ReportErrorFront(expression_location, "Expecting a single value");
                return false;
            }

            Token token = tokens[0];

            if (identifier == "name") {
                if (token.kind != TokenKind_Identifier) {
                    ReportErrorFront(expression_location, "Expecting an identifier");
                    return false;
                }
                arg_name = token.value;
            }
            else if (identifier == "required") {
                if (token.kind != TokenKind_BoolLiteral) {
                    ReportErrorFront(expression_location, "Expecting a bool literal");
                    return false;
                }
                required = token.value == "true";
            }
            else {
                ReportErrorFront(identifier_token.location, "Unknown property '%S'", identifier);
                return false;
            }
        }
    }
    
    if (!ValidateArgName(reporter, arg_name, def->location)) return false;
    
    def->arg_name = StrCopy(front->arena, arg_name);
    def->required = required;
    def->description_location = description_location;
    def->default_value_location = default_value_location;
    
    LogType("Arg Resolve: %S", def->identifier);
    return true;
}

internal_fn B32 ResolveGlobal(FrontContext* front, GlobalDefinition* def)
{
    TypeSystem* tsys = front->tsys;
    Reporter* reporter = front->reporter;
    Parser* parser = ParserFromLocation(front, def->location);
    Location starting_location = PeekToken(parser).location;
    
    ObjectDefinitionResult res = ReadObjectDefinition(context.arena, ParserFromLocation(front, def->location), front, false, {}, RegisterKind_Global);
    if (!res.success) return false;

    B32 success = true;

    foreach(i, res.objects.count)
    {
        ObjectDefinition obj = res.objects[i];

        I32 global = AddGlobal(front, obj.name, obj.type_id, obj.is_constant, obj.location);
        if (global < 0) {
            success = false;
        }
    }

    return success;
}

B32 ResolveDefinition(FrontContext* front, FrontDefinition* def)
{
    if (def == NULL) return false;

    switch (def->type)
    {
        case DefinitionType_Enum: return ResolveEnum(front, &front->_enums[def->index]);
        case DefinitionType_Struct: return ResolveStruct(front, &front->_structs[def->index]);
        case DefinitionType_FunctionHeader: return ResolveFunctionHeader(front, &front->_function_headers[def->index]);
        case DefinitionType_GenericFunctionHeader: return ResolveGenericFunctionHeader(front, &front->_generic_function_headers[def->index]);
        case DefinitionType_Arg: return ResolveArg(front, &front->_args[def->index]);
        case DefinitionType_Global: return ResolveGlobal(front, &front->_globals[def->index]);

        case DefinitionType_Unknown: break;
    }

    return false;
}

internal_fn FunctionBody* ReadFunctionBody(FrontContext* front, U32 function_header_index)
{
    PROFILE_FUNCTION;

    FunctionHeader* header = FunctionHeaderFromIndex(front, function_header_index);
    Reporter* reporter = front->reporter;
    TypeSystem* tsys = front->tsys;

    if (!LocationIsValid(header->body_location)) return NULL;

    LogFlow("Generating IR for function '%S'", header->name);
    
    IR_Context* ir = IrContextAlloc(front, header->resolve_generics);
    IR_Group out = IRFromNone();
    
    // Define parameters
    foreach(i, header->parameters.count)
    {
        ObjectDefinition obj = header->parameters[i];
        Type* type = TypeFromID(tsys, obj.type_id);
        Assert(!TypeHasGenerics(front, type));
        out = IRAppend(out, IRFromDefineObject(ir, RegisterKind_Parameter, obj.name, type, false, obj.location));
    }
    
    // Define returns
    foreach(i, header->returns.count)
    {
        ObjectDefinition obj = header->returns[i];
        Type* type = TypeFromID(tsys, obj.type_id);
        Assert(!TypeHasGenerics(front, type));
        out = IRAppend(out, IRFromDefineObject(ir, RegisterKind_Return, obj.name, type, false, obj.location));
        out = IRAppend(out, IRFromStore(ir, out.value, ValueFromZero(type), obj.location));
    }
    
    Parser* parser = ParserFromLocation(front, header->body_location);
    out = IRAppend(out, ReadCode(ir, parser));
    
    FrontScript* script = parser->script;
    IR res = MakeIR(front->arena, ArrayFromBArray(context.arena, ir->local_registers), out, script);
    
    // Check for returns
    if (res.valid)
    {
        Array<Unit> units = res.instructions;
        
        // TODO(Jose): Check for infinite loops
        
        if (!IRValidateReturnPath(units)) {
            report_function_no_return(header->body_location, header->name);
        }
    }
    
    return AddBody(front, function_header_index, res);
}

B32 GenerateIR(FrontContext* front, FrontDefinition* def)
{
     if (def->type == DefinitionType_FunctionHeader)
     {
        FunctionHeader* header = FunctionHeaderFromIndex(front, def->index);
        if (LocationIsValid(header->body_location)) {
            FunctionBody* body = ReadFunctionBody(front, def->index);
            return body != NULL;
        }

        return true;
    }

    return true;
}

TypeChild TypeGetMember(FrontContext* front, Type* type, String member)
{
    TypeSystem* tsys = front->tsys;

    if (type->kind == VKind_Struct) {
        StructDefinition* def = StructFromIndex(front, type->definition_index);
        Assert(def->names.count != 0 && def->types.count != 0);
        foreach(i, def->names.count) {
            if (def->names[i] == member) {
                return TypeChildMake(TypeGet(def->types[i]), def->names[i], i, false);
            }
        }
    }
    
    return TypeChildMake(nil_type, "", -1, false);
}

TypeChild TypeGetChild(FrontContext* front, Type* type, String name)
{
    TypeChild info = TypeGetMember(front, type, name);
    if (info.index >= 0) return info;
    return TypeGetProperty(type, name);
}

B32 TypeHasGenerics(FrontContext* front, Type* type)
{
    if (type->kind == VKind_Generic) return true;

    if (type->kind == VKind_Reference) {
        return TypeHasGenerics(front, TypeGetBase(front->tsys, type));
    }

    if (type->kind == VKind_Array) {
        return TypeHasGenerics(front, TypeGetNext(front->tsys, type));
    }

    if (type->kind == VKind_Struct)
    {
        StructDefinition* def = StructFromIndex(front, type->definition_index);
        
        foreach(i, def->types.count) {
            Type* member = TypeFromID(front->tsys, def->types[i]);
            if (TypeHasGenerics(front, member)) return true;
        }
        return false;
    }

    return false;
}

internal_fn U32 CountName(FrontContext* front, String name)
{
    PROFILE_FUNCTION;
    
    U32 count = 0;

    foreach_BArray(it, &front->definitions) {
        if (it.value->name == name) count++;
    }

    foreach_BArray(it, &front->global_objects) {
        if (it.value->name == name) count++;
    }
    
    return count;
}

FrontDefinition* AddDefinition(FrontContext* front, DefinitionType type, String name, Array<String> generics, B32 is_global, Location location)
{
    PROFILE_FUNCTION;

    Reporter* reporter = front->reporter;

    if (type == DefinitionType_Global || type == DefinitionType_Arg) {
        name = {};
    }
    else {
        if (name.size == 0) {
            InvalidCodepath();
            return NULL;
        }

        name = StrCopy(front->arena, name);
    }
    
    RWMutexLockGuard_Write(&front->definitions_mutex);

    // Check duplicated names
    if (name.size != 0)
    {
        U32 count = CountName(front, name);

        if (count != 0) {
            ReportErrorFront(location, "Duplicated definition '%S'", name);
        }
    }

    U32 index = U32_MAX;

    if (type == DefinitionType_FunctionHeader) {
        index = front->_function_headers.count;
        FunctionHeader* def = BArrayAdd(&front->_function_headers);
        def->name = name;
        def->location = location;
        def->index = index;
        def->generic_index = -1;
    }
    else if (type == DefinitionType_GenericFunctionHeader) {
        index = front->_generic_function_headers.count;
        GenericFunctionHeader* def = BArrayAdd(&front->_generic_function_headers);
        def->name = name;
        def->location = location;
        def->index = index;
        def->generics = StrArrayCopy(front->arena, generics);
    }
    else if (type == DefinitionType_Struct) {
        index = front->_structs.count;
        StructDefinition* def = BArrayAdd(&front->_structs);
        def->name = name;
        def->location = location;
        def->index = index;
        def->generic_index = -1;
        def->generic_names = StrArrayCopy(front->arena, generics);
        def->has_generics = def->generic_names.count > 0;

        TypeAddStruct(front->tsys, name, index);
    }
    else if (type == DefinitionType_Enum) {
        index = front->_enums.count;
        EnumDefinition* def = BArrayAdd(&front->_enums);
        def->name = name;
        def->location = location;
        def->index = index;

        TypeAddEnum(front->tsys, name, index);
    }
    else if (type == DefinitionType_Arg) {
        index = front->_args.count;
        ArgDefinition* def = BArrayAdd(&front->_args);
        def->location = location;
    }
    else if (type == DefinitionType_Global) {
        index = front->_globals.count;
        GlobalDefinition* def = BArrayAdd(&front->_globals);
        def->location = location;
    }
    
    FrontDefinition* def = BArrayAdd(&front->definitions);
    def->type = type;
    def->name = name;
    def->location = location;
    def->index = index;
    def->is_global = is_global;
    
    LogType("%S Identify: %S", StringFromDefinitionType(type), identifier);
    return def;
}

I32 AddGlobal(FrontContext* front, String name, U32 type_id, B32 is_constant, Location location)
{
    PROFILE_FUNCTION;
    Reporter* reporter = front->reporter;

    RWMutexLockGuard_Write(&front->definitions_mutex);

    // Check duplicated names
    {
        U32 count = CountName(front, name);

        if (count != 0) {
            ReportErrorFront(location, "Duplicated definition '%S'", name);
        }
    }

    U32 index = front->global_objects.count;
    ObjectDefinition* global = BArrayAdd(&front->global_objects);
    global->name = StrCopy(front->arena, name);
    global->type_id = type_id;
    global->is_constant = is_constant;
    global->location = location;

    Assert(TypeIsValid(TypeFromID(front->tsys, global->type_id)));

    return index;
}

FunctionBody* AddBody(FrontContext* front, U32 header_index, IR ir)
{
    RWMutexLockGuard_Write(&front->definitions_mutex);
    FunctionBody* body = BArrayAdd(&front->_functions);
    body->function_header_index = header_index;
    body->ir = ir;
    
    LogType("Function Body: %S", header->name);

    return body;
}

internal_fn Type* ResolveGenericType(FrontContext* front, Type* type, Array<ResolveGenericEntry> resolve_generics)
{
    TypeSystem* tsys = front->tsys;

    if (!TypeHasGenerics(front, type)) return type;

    if (type->kind == VKind_Reference) {
        Type* subtype = ResolveGenericType(front, TypeGetNext(tsys, type), resolve_generics);
        return TypeFromReference(tsys, subtype);
    }

    if (type->kind == VKind_Array) {
        Type* subtype = ResolveGenericType(front, TypeGetNext(tsys, type), resolve_generics);
        return TypeFromArray(tsys, subtype, 1);
    }

    if (type->kind == VKind_Struct)
    {
        StructDefinition* unresolved_def = StructFromIndex(front, type->definition_index);

        if (unresolved_def->generic_index < 0) {
            InvalidCodepath();
            return nil_type;
        }

        StructDefinition* base_def = StructFromIndex(front, unresolved_def->generic_index);
        
        if (base_def->generic_names.count == 0 || unresolved_def->generic_types.count != base_def->generic_names.count) {
            InvalidCodepath();
            return nil_type;
        }

        Array<ResolveGenericEntry> forward_generics = ArrayAlloc<ResolveGenericEntry>(context.arena, base_def->generic_names.count);

        foreach(i, forward_generics.count) {
            forward_generics[i].name = base_def->generic_names[i];
            forward_generics[i].type_id = ResolveGenericType(front, TypeGet(unresolved_def->generic_types[i]), resolve_generics)->id;
        }

        StructDefinition* new_def = ResolveStructWithGenerics(front, base_def, forward_generics);
        if (new_def == NULL) return nil_type;

        return TypeFromStruct(tsys, new_def->index);
    }

    if (type->kind == VKind_Generic) {
        type = TypeGet(FindGenericType(type->name, resolve_generics).type_id);
        return type;
    }

    InvalidCodepath();
    return type;
}

internal_fn Array<ObjectDefinition> ResolveGenericObjects(Arena* arena, FrontContext* front, Array<ObjectDefinition> src, Array<ResolveGenericEntry> table)
{
    TypeSystem* tsys = front->tsys;
    Array<ObjectDefinition> dst = ArrayCopyRecursive<ObjectDefinition>(arena, src, ObjectDefinitionCopy);

    for (U32 i = 0; i < dst.count; i++)
    {
        Type* type = TypeGet(dst[i].type_id);
        dst[i].type_id = ResolveGenericType(front, type, table)->id;
    }

    return dst;
}

FunctionHeader* ResolveFunctionHeaderWithGenerics(FrontContext* front, GenericFunctionHeader* unresolved, Array<ResolveGenericEntry> resolve_generics)
{
    if (LOG_FLOW_ENABLED) {
        StringBuilder builder = string_builder_make(context.arena);
        appendf(&builder, "Resolving Generic Header(%S): ", unresolved->name);
        foreach(i, resolve_generics.count) {
            append(&builder, resolve_generics[i].name);
            if (i < resolve_generics.count - 1) append(&builder, ", ");
        }
        LogFlow("%S", string_from_builder(context.arena, &builder));
    }

    
    RWMutexLockGuard_Write(&front->definitions_mutex);

    // Check for already defined header
    {
        foreach_BArray(it, &front->_function_headers) {
            FunctionHeader* fn = it.value;
            if (fn->generic_index == unresolved->index)
            {
                if (fn->resolve_generics.count != resolve_generics.count) {
                    InvalidCodepath();
                    continue;
                }

                B32 match = true;
                foreach(i, fn->resolve_generics.count) {
                    match &= fn->resolve_generics[i].type_id == resolve_generics[i].type_id;
                }

                if (match) {
                    return fn;
                }
            }
        }
    }

    FrontDefinition* unresolved_def = FrontDefinitionFromIndex(front, DefinitionType_GenericFunctionHeader, unresolved->index);

    U32 index = front->_function_headers.count;
    FunctionHeader* fn = BArrayAdd(&front->_function_headers);

    fn->parameters = ResolveGenericObjects(front->arena, front, unresolved->parameters, resolve_generics);
    fn->returns = ResolveGenericObjects(front->arena, front, unresolved->returns, resolve_generics);
    
    fn->name = unresolved->name;
    fn->location = unresolved->location;
    fn->body_location = unresolved->body_location;
    fn->index = index;
    fn->generic_index = unresolved->index;
    fn->resolve_generics = ArrayCopyRecursive(front->arena, resolve_generics, ResolveGenericEntryCopy);

    
    FrontDefinition* def = BArrayAdd(&front->definitions);
    def->type = DefinitionType_FunctionHeader;
    def->name = fn->name;
    def->location = fn->location;
    def->index = index;
    def->is_global = unresolved_def->is_global;
    
    LogType("Resolve Generic Function %S", def->name);
    return fn;
}

StructDefinition* ResolveStructWithGenerics(FrontContext* front, StructDefinition* unresolved, Array<ResolveGenericEntry> resolve_generics)
{
    TypeSystem* tsys = front->tsys;

    Assert(unresolved->generic_index < 0);
    Assert(resolve_generics.count == unresolved->generic_names.count);
    Assert(unresolved->generic_types.count == 0);

    String type_name = {};
    {
        StringBuilder builder = string_builder_make(context.arena);
        append(&builder, unresolved->name);
        append(&builder, "[");
        foreach(i, resolve_generics.count) {
            append(&builder, TypeFromID(front->tsys, resolve_generics[i].type_id)->name);
            if (i < resolve_generics.count - 1)
                append(&builder, ",");
        }
        append(&builder, "]");

        type_name = string_from_builder(context.arena, &builder);
    }

    LogFlow("Resolving Generic Struct: %S", type_name);

    RWMutexLockGuard_Write(&front->definitions_mutex);

    // Check for already defined header
    {
        Type* type = TypeFromName(front->tsys, type_name);
        if (type->kind == VKind_Struct) {
            return StructFromIndex(front, type->definition_index);
        }
    }

    FrontDefinition* unresolved_def = FrontDefinitionFromIndex(front, DefinitionType_Struct, unresolved->index);

    U32 index = front->_structs.count;
    StructDefinition* s = BArrayAdd(&front->_structs);
    s->name = StrCopy(front->arena, type_name);
    s->location = unresolved->location;
    s->generic_names = unresolved->generic_names;
    s->index = index;
    s->names = unresolved->names;
    s->generic_index = unresolved->index;
    
    s->types = ArrayAlloc<U32>(front->arena, unresolved->types.count);
    foreach(i, s->types.count) {
        Type* type = ResolveGenericType(front, TypeFromID(front->tsys, unresolved->types[i]), resolve_generics);
        s->types[i] = type->id;
    }
    
    s->has_generics = false;
    s->generic_types = ArrayAlloc<U32>(front->arena, resolve_generics.count);
    foreach(i, resolve_generics.count) {
        Type* type = TypeGet(resolve_generics[i].type_id);
        s->generic_types[i] = type->id;
        s->has_generics |= TypeHasGenerics(front, type);
    }
    
    TypeAddStruct(front->tsys, s->name, index);
    
    FrontDefinition* def = BArrayAdd(&front->definitions);
    def->type = DefinitionType_Struct;
    def->name = s->name;
    def->location = s->location;
    def->index = index;
    def->is_global = unresolved_def->is_global;
    
    LogType("Resolved Generic Struct %S", def->name);
    return s;
}

internal_fn B32 AddResolveGeneric(BArray<ResolveGenericEntry>* table, String name, Type* type)
{
    foreach_BArray(it, table) {
        if (it.value->name == name) {
            return it.value->type_id == type->id;
        }
    }

    ResolveGenericEntry entry = {};
    entry.name = name;
    entry.type_id = type->id;

    BArrayAdd(table, entry);
    return true;
}

internal_fn B32 AddResolveGenerics(FrontContext* front, BArray<ResolveGenericEntry>* table, Type* generic_type, Type* type, U32 param_index, Location location)
{
    Reporter* reporter = front->reporter;
    TypeSystem* tsys = front->tsys;

    while (1)
    {
        if (generic_type->kind == VKind_Generic)
        {
            if (!AddResolveGeneric(table, generic_type->name, type)) {
                ReportErrorFront(location, "Invalid argument resolve for generic '%S'", generic_type->name);
                return false;
            }

            return true;
        }

        if (generic_type->kind == VKind_Struct && type->kind == VKind_Struct)
        {
            StructDefinition* generic_def = StructFromIndex(front, generic_type->definition_index);
            StructDefinition* def = StructFromIndex(front, type->definition_index);

            I32 base_struct_index = generic_def->generic_index;
            if (base_struct_index < 0) base_struct_index = generic_def->index;

            B32 valid_struct = !def->has_generics;
            valid_struct &= base_struct_index == def->generic_index;

            if (valid_struct)
            {
                Assert(generic_def->generic_names.count == def->generic_types.count);

                foreach(i, def->generic_types.count)
                {
                    Type* generic_subtype = TypeFromGeneric(tsys, generic_def->generic_names[i]);
                    if (i < generic_def->generic_types.count) {
                        generic_subtype = TypeGet(generic_def->generic_types[i]);
                    }

                    Type* subtype = TypeGet(def->generic_types[i]);

                    if (!AddResolveGenerics(front, table, generic_subtype, subtype, param_index, location)) {
                        return false;
                    }
                }

                return true;
            }
        }

        type = TypeGetNext(tsys, type);
        generic_type = TypeGetNext(tsys, generic_type);

        if (type == nil_type || generic_type == nil_type)
        {
            ReportErrorFront(location, "Can't resolve generic for parameter %u", param_index + 1);
            return false;
        }
    }

    return true;
}

Array<ResolveGenericEntry> GuessResolveGenericsFromArguments(Arena* arena, IR_Context* ir, GenericFunctionHeader* fn, Array<Value> args, Location location)
{
    FrontContext* front = ir->front;
    TypeSystem* tsys = front->tsys;
    Reporter* reporter = front->reporter;

    Assert(fn->parameters.count == args.count);

    BArray<ResolveGenericEntry> table = BArrayMake<ResolveGenericEntry>(context.arena, 8);

    for (U32 i = 0; i < fn->parameters.count; i++)
    {
        Type* generic_type = TypeGet(fn->parameters[i].type_id);
        Type* type = TypeGet(args[i].type_id);

        if (!TypeHasGenerics(ir->front, generic_type)) continue;

        if (!AddResolveGenerics(front, &table, generic_type, type, i, location)) {
            return {};
        }
    }

    return ArrayFromBArray(arena, table);
}

FunctionHeader* ResolveFunctionHeaderGenericsFromArguments(IR_Context* ir, GenericFunctionHeader* unresolved_fn, Array<Value> args, Location location)
{
    FrontContext* front = ir->front;
    Reporter* reporter = front->reporter;

    Array<ResolveGenericEntry> forward_generics = GuessResolveGenericsFromArguments(context.arena, ir, unresolved_fn, args, location);
    if (reporter->exit_requested) return NULL;

    return ResolveFunctionHeaderWithGenerics(front, unresolved_fn, forward_generics);
}

FrontScript* FrontAddScript(FrontContext* front, String path)
{
    PROFILE_FUNCTION;
    Reporter* reporter = front->reporter;
    
    Assert(OsPathIsAbsolute(path));
    
    RBuffer raw_file;
    if (OsReadEntireFile(front->arena, path, &raw_file).failed) {
        ReportErrorFront(NO_CODE, "File '%S' not found\n", path);
        return NULL;
    }
    
    RWMutexLockGuard_Write(&front->definitions_mutex);
    
    // Check for duplicated
    foreach_BArray(it, &front->scripts) {
        FrontScript* s = it.value;
        if (StrEquals(s->path, path)) {
            return NULL;
        }
    }
    
    path = StrCopy(front->arena, path);
    String text = StrFromRBuffer(raw_file);
    
    I32 script_id = front->scripts.count;
    
    FrontScript* script = BArrayAdd(&front->scripts);
    script->id = script_id;
    script->path = path;
    script->name = PathGetLastElement(path);
    script->dir = PathResolve(front->arena, PathAppend(context.arena, path, ".."));
    script->text = text;
    return script;
}

#include "autogenerated/core.h"

FrontScript* FrontAddCoreScript(FrontContext* front)
{
    PROFILE_FUNCTION;
    String text = YOV_CORE;
    
    RWMutexLockGuard_Write(&front->definitions_mutex);
    
    I32 script_id = front->scripts.count;
    
    FrontScript* script = BArrayAdd(&front->scripts);
    script->id = script_id;
    script->path = "core.yov";
    script->name = "core.yov";
    script->dir = "./";
    script->text = text;
    return script;
}

FrontScript* FrontGetScript(FrontContext* front, I32 script_id)
{
    if (script_id < 0) return NULL;
    
    if (script_id >= front->scripts.count) {
        Assert(0);
        return NULL;
    }
    
    return &front->scripts[script_id];
}

U32 LineFromLocation(Location location, FrontScript* script)
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

FrontDefinition* FrontDefinitionFromName(FrontContext* front, String name)
{
    foreach_BArray(it, &front->definitions) {
        if (it.value->name == name) return it.value;
    }
    return NULL;
}

FrontDefinition* FrontDefinitionFromIndex(FrontContext* front, DefinitionType type, U32 index)
{
    foreach_BArray(it, &front->definitions) {
        if (it.value->type == type && it.value->index == index) {
            return it.value;
        }
    }
    return NULL;
}

I32 FrontGlobalIndexFromName(FrontContext* front, String name)
{
    foreach_BArray(it, &front->global_objects) {
        if (it.value->name == name) return it.index;
    }
    return -1;
}

ObjectDefinition* FrontGlobalRegisterFromRegIndex(FrontContext* front, U32 register_index)
{
    U32 global_index = GlobalFromRegIndex(register_index);
    if (global_index >= front->global_objects.count) return NULL;
    return &front->global_objects[global_index];
}

GenericFunctionHeader* GenericFunctionHeaderFromIndex(FrontContext* front, U32 index)
{
    RWMutexLockGuard_Read(&front->definitions_mutex);
    return &front->_generic_function_headers[index];
}

FunctionHeader* FunctionHeaderFromName(FrontContext* front, String name)
{
    RWMutexLockGuard_Read(&front->definitions_mutex);
    foreach_BArray(it, &front->_function_headers) {
        if (it.value->name == name) return it.value;
    }
    return NULL;
}

FunctionHeader* FunctionHeaderFromIndex(FrontContext* front, U32 index)
{
    RWMutexLockGuard_Read(&front->definitions_mutex);
    return &front->_function_headers[index];
}

StructDefinition* StructFromName(FrontContext* front, String name)
{
    RWMutexLockGuard_Read(&front->definitions_mutex);
    foreach_BArray(it, &front->_structs) {
        if (it.value->name == name) return it.value;
    }
    return NULL;
}

StructDefinition* StructFromIndex(FrontContext* front, U32 index)
{
    RWMutexLockGuard_Read(&front->definitions_mutex);
    return &front->_structs[index];
}

EnumDefinition* EnumFromIndex(FrontContext* front, U32 index)
{
    RWMutexLockGuard_Read(&front->definitions_mutex);
    return &front->_enums[index];
}

ArgDefinition* ArgFromIndex(FrontContext* front, U32 index)
{
    RWMutexLockGuard_Read(&front->definitions_mutex);
    return &front->_args[index];
}

Parser* ParserFromLocation(FrontContext* front, Location location)
{
    FrontScript* script = FrontGetScript(front, location.script_id);
    if (script == NULL) {
        InvalidCodepath();
        return ParserAlloc(NULL, {});
    }
    
    return ParserAlloc(script, location.range);
}

#if DEV

internal_fn String StringFromRegister(Arena* arena, FrontContext* front, I32 index)
{
    ObjectDefinition* global = FrontGlobalRegisterFromRegIndex(front, index);
    if (global != NULL) return global->name;
    
    I32 local_index = LocalFromRegIndex(index);
    if (local_index < 0) return "rE";
    return StrFormat(arena, "r%i", local_index);
}

internal_fn String StrFromValue(Arena* arena, FrontContext* front, Value value, B32 raw = false)
{
    TypeSystem* tsys = front->tsys;

    Type* type = TypeGet(value.type_id);

    if (value.kind == ValueKind_None) return "E";
    
    if (value.kind == ValueKind_Literal) {
        if (type == int_type) return StrFromI64(arena, value.literal_sint);
        if (type == uint_type) return StrFromU64(arena, value.literal_uint);
        if (type == bool_type) return value.literal_bool ? "true" : "false";
        if (type == float_type) return StrFromF64(arena, value.literal_float, 4);
        if (type == string_type) {
            if (raw) return value.literal_string;
            String escape = escape_string_from_raw_string(context.arena, value.literal_string);
            return StrFormat(arena, "\"%S\"", escape);
        }
        if (type == void_type) return "null";
        if (type == type_type) return TypeGet(value.literal_type_id)->name;
        if (TypeIsEnum(type)) {
            EnumDefinition* enum_def = EnumFromIndex(front, type->definition_index);
            I32 index = (I32)value.literal_sint;
            if (index < 0 || index >= enum_def->names.count) return "?";
            return enum_def->names[index];
        }
        InvalidCodepath();
        return "";
    }
    
    if (value.kind == ValueKind_Array)
    {
        Array<Value> values = value.array.values;
        if (values.count == 0) return "{ }";
        
        StringBuilder builder = string_builder_make(context.arena);
        
        append(&builder, "[ ");
        foreach(i, values.count) {
            append(&builder, StrFromValue(context.arena, front, values[i], false));
            if (i < values.count - 1) append(&builder, ", ");
        }
        append(&builder, " ]");
        
        return string_from_builder(arena, &builder);
    }
    
    if (value.kind == ValueKind_StringComposition)
    {
        Assert(type == string_type);
        Array<Value> values = value.string_composition;
        StringBuilder builder = string_builder_make(context.arena);
        foreach(i, values.count) {
            String src = StrFromValue(context.arena, front, values[i]);
            appendf(&builder, "%S", src);
            if (i < values.count - 1) append(&builder, " + ");
        }
        return string_from_builder(arena, &builder);
    }
    
    if (value.kind == ValueKind_MultipleReturn)
    {
        Array<Value> values = value.multiple_return;
        StringBuilder builder = string_builder_make(context.arena);
        append(&builder, "(");
        foreach(i, values.count) {
            String src = StrFromValue(context.arena, front, values[i]);
            appendf(&builder, "%S", src);
            if (i < values.count - 1) append(&builder, ", ");
        }
        append(&builder, ")");
        return string_from_builder(arena, &builder);
    }
    
    if (value.kind == ValueKind_ZeroInit) {
        return StrFormat(arena, "%S()", type->name);
    }
    
    if (value.kind == ValueKind_Register || value.kind == ValueKind_LValue)
    {
        String ref_op = "";
        
        if (value.reg.reference_op != 0)
        {
            I32 op = value.reg.reference_op;
            
            while (op > 0) {
                ref_op = StrFormat(context.arena, "&%S", ref_op);
                op--;
            }
            
            while (op < 0) {
                ref_op = StrFormat(context.arena, "*%S", ref_op);
                op++;
            }
        }
        
        return StrFormat(arena, "%S%S", ref_op, StringFromRegister(context.arena, front, value.reg.index));
    }
    
    InvalidCodepath();
    return "";
}

internal_fn String StringFromBinaryOperation(Arena* arena, String dst, String left, String right, OperatorKind op_kind)
{
    String op = StringFromOperatorKind(op_kind);
    return StrFormat(arena, "%S = %S %S %S", dst, left, op, right);
}

internal_fn String StringFromUnaryOperation(Arena* arena, String dst, String src, OperatorKind op_kind)
{
    String op = StringFromOperatorKind(op_kind);
    return StrFormat(arena, "%S = %S%S", dst, op, src);
}

internal_fn String StringFromCast(Arena* arena, String dst, String src, PrimitiveType type)
{
    String type_str = StringFromPrimitive(type);
    return StrFormat(arena, "%S = (%S)%S", dst, type_str, src);
}

internal_fn TypeChild TypeGetChildAt(FrontContext* front, Type* type, U32 index, B32 is_property)
{
    TypeSystem* tsys = front->tsys;

    if (is_property) {
        return TypeGetPropertyAt(type, index);
    }

    if (type->kind == VKind_Struct) {
        StructDefinition* def = StructFromIndex(front, type->definition_index);
        if (index < def->types.count)
            return TypeChildMake(TypeGet(def->types[index]), def->names[index], index, false);
    }

    Type* next = TypeGetNext(tsys, type);

    if (next != nil_type) {
        return TypeChildMake(next, {}, index, false);
    }
    
    return TypeChildMake(nil_type, "", -1, false);
}

internal_fn String StringFromUnitInfo(Arena* arena, FrontContext* front, Unit unit)
{
    TypeSystem* tsys = front->tsys;

    String dst = StringFromRegister(context.arena, front, unit.dst_index);
    String src0 = StrFromValue(context.arena, front, unit.src0);
    String src1 = StrFromValue(context.arena, front, unit.src1);
    
    switch(unit.kind)
    {
        case UnitKind_Error: return {};
        
        case UnitKind_Copy:
        case UnitKind_Store:
        return StrFormat(arena, "%S = %S", dst, src0);
        
        case UnitKind_FunctionCall:
        {
            FunctionHeader* header = FunctionHeaderFromIndex(front, unit.function_call.header_index);

            StringBuilder builder = string_builder_make(context.arena);
            
            if (unit.dst_index >= 0)
            {
                foreach(i, header->returns.count)
                {
                    appendf(&builder, StringFromRegister(context.arena, front, unit.dst_index + i));
                    if (i + 1 < header->returns.count) {
                        appendf(&builder, ", ");
                    }
                }
                
                appendf(&builder, " = ");
            }
            
            String name = header->name;
            Array<Value> params = unit.function_call.parameters;
            
            appendf(&builder, "%S(", name);
            
            foreach(i, params.count) {
                String param = StrFromValue(context.arena, front, params[i]);
                appendf(&builder, "%S", param);
                if (i < params.count - 1) append(&builder, ", ");
            }
            append(&builder, ")");
            return string_from_builder(arena, &builder);
        }
        
        case UnitKind_Return: return "";
        
        case UnitKind_Jump:
        {
            StringBuilder builder = string_builder_make(context.arena);
            String condition = src0;
            if (unit.jump.condition > 0) appendf(&builder, "%S ", condition);
            else if (unit.jump.condition < 0) appendf(&builder, "!%S ", condition);
            appendf(&builder, "%i", unit.jump.offset);
            return string_from_builder(arena, &builder);
        }
        
        case UnitKind_Child:
        {
            Value src = unit.src0;
            Value index = unit.src1;
            
            B32 is_property = unit.child.child_is_property;
            B32 is_literal_int = index.kind == ValueKind_Literal && index.type_id == uint_type->id;
            
            String op = {};
            
            if (is_literal_int)
            {
                U32 i = (U32)index.literal_uint;

                TypeChild child = TypeGetChildAt(front, TypeGet(src.type_id), i, is_property);

                if (child.index < 0) {
                    op = "?";
                }
                else {
                    op = StrFormat(context.arena, ".%S", child.name);
                }
            }
            
            if (op.size == 0) {
                op = StrFormat(context.arena, "[%S]", src1);
            }
            
            return StrFormat(arena, "%S = %S%S", dst, src0, op);
        }
        
        case UnitKind_ResultEval: return src0;
        
        case UnitKind_Add: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_Addition);
        case UnitKind_Sub: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_Substraction);
        case UnitKind_Mul: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_Multiplication);
        case UnitKind_Div: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_Division);
        case UnitKind_Mod: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_Modulo);
        
        case UnitKind_Eql: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_Equals);
        case UnitKind_Neq: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_NotEquals);
        case UnitKind_Gtr: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_GreaterThan);
        case UnitKind_Lss: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_LessThan);
        case UnitKind_Geq: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_GreaterEqualsThan);
        case UnitKind_Leq: return StringFromBinaryOperation(arena, dst, src0, src1, OperatorKind_LessEqualsThan);
        
        case UnitKind_Or: return StringFromUnaryOperation(arena, dst, src0, OperatorKind_LogicalOr);
        case UnitKind_And: return StringFromUnaryOperation(arena, dst, src0, OperatorKind_LogicalAnd);
        case UnitKind_Not: return StringFromUnaryOperation(arena, dst, src0, OperatorKind_LogicalNot);
        
        case UnitKind_Neg: return StringFromUnaryOperation(arena, dst, src0, OperatorKind_Substraction);
        
        case UnitKind_Cast:
        case UnitKind_BitCast:
        return StringFromCast(arena, dst, src0, unit.op_dst_type);
        
        case UnitKind_Is: return StrFormat(arena, "%S = %S is %S", dst, src0, src1);
        
        case UnitKind_Empty:
        case UnitKind_count:
        return {};
    }
    
    
    InvalidCodepath();
    return {};
}


internal_fn String StringFromUnit(Arena* arena, FrontContext* front, U32 index, U32 index_digits, U32 line_digits, Unit unit)
{
    PROFILE_FUNCTION;
    
    StringBuilder builder = string_builder_make(context.arena);
    
    String index_str = StrFormat(context.arena, "%u", index);
    String line_str = StrFormat(context.arena, "%u", unit.line);
    
    for (U32 i = (U32)index_str.size; i < index_digits; ++i) append(&builder, "0");
    append(&builder, index_str);
    append(&builder, " (");
    for (U32 i = (U32)line_str.size; i < line_digits; ++i) append(&builder, "0");
    append(&builder, line_str);
    append(&builder, ") ");
    
    String name = StringFromUnitKind(context.arena, unit.kind);
    
    append(&builder, name);
    for (U32 i = (U32)name.size; i < 7; ++i) append(&builder, " ");
    
    String info = StringFromUnitInfo(context.arena, front, unit);
    append(&builder, info);
    
    return string_from_builder(arena, &builder);
}

internal_fn void PrintUnits(FrontContext* front, Array<Unit> units)
{
    U32 max_index = units.count;
    U32 max_line = 0;
    
#if DEV_LOCATION_INFO
    foreach(i, units.count) {
        max_line = Max(units[i].location.info.line, max_line);
    }
#else
    max_line = 10000;
#endif
    
    // TODO(Jose): Use log
    U32 index_digits = 0;
    U32 aux = max_index;
    while (aux != 0) {
        aux /= 10;
        index_digits++;
    }
    
    U32 line_digits = 0;
    aux = max_line;
    while (aux != 0) {
        aux /= 10;
        line_digits++;
    }
    
    foreach(i, units.count) {
        PrintEx(PrintLevel_DevLog, "%S\n", StringFromUnit(context.arena, front, i, index_digits, line_digits, units[i]));
    }
}

void PrintIr(FrontContext* front, String name, IR ir)
{
    TypeSystem* tsys = front->tsys;

    PrintEx(PrintLevel_DevLog, "[IR] %S:\n", name);
    PrintUnits(front, ir.instructions);
    
    if (ir.local_registers.count > 0)
        PrintEx(PrintLevel_DevLog, "----- REGISTERS -----\n");
    
    foreach(i, ir.local_registers.count)
    {
        Register reg = ir.local_registers[i];
        
        B32 is_param = reg.kind == RegisterKind_Parameter;
        B32 is_return = reg.kind == RegisterKind_Return;
        
        if (is_param) PrintEx(PrintLevel_DevLog, "[param] ");
        else if (is_return) PrintEx(PrintLevel_DevLog, "[return] ");
        Assert(is_param + is_return <= 1);
        
        PrintEx(PrintLevel_DevLog, "%S: %S", StringFromRegister(context.arena, front, RegIndexFromLocal(i)), TypeGet(reg.type_id)->name);
        PrintEx(PrintLevel_DevLog, "\n");
    }
    
    PrintEx(PrintLevel_DevLog, SEPARATOR_STRING "\n");
}

#endif