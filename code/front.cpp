#include "front.h"

internal_fn void FrontRun(FrontContext* front, LaneContext* lane)
{
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
    
    // Identify Pass
    {
        if (LaneNarrow(lane)) {
            LogFlow("Starting Identify Pass");
        }
        
        F64 start_time = TimerNow();
        
        FrontIdentifyDefinitions(lane, front);
        
        if (LaneNarrow(lane)) {
            ProgramInitializeTypesTable(front->program);
            
            F64 ellapsed = TimerNow() - start_time;
            LogFlow("Identify pass finished: %S", StringFromEllapsedTime(ellapsed));
        }
        
        ArenaPopTo(context.arena, 0);
        LaneBarrier(lane);
        
        if (front->reporter->exit_requested) {
            return;
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
    Program* program = ArenaPushStruct<Program>(arena);
    program->arena = arena;
    program->script_dir = StrCopy(arena, PathGetFolder(input->main_script_path));
    program->caller_dir = StrCopy(arena, input->caller_dir);
    
    if (reporter->exit_requested) {
        return program;
    }
    
    FrontContext* front = NULL;
    
    Arena* front_arena = ArenaAlloc(Gb(32), 8);
    front = ArenaPushStruct<FrontContext>(front_arena);
    front->arena = front_arena;
    front->program = program;
    front->reporter = reporter;
    front->input = input;
    front->scripts = pooled_array_make<YovScript>(front_arena, 16);
    front->definition_list = pooled_array_make<CodeDefinition>(front_arena, 64);
    front->global_location_list = pooled_array_make<Location>(front_arena, 32);
    front->global_list = pooled_array_make<Global>(front_arena, 32);
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
        PrintIr(program, fn->identifier, fn->defined.ir);
    }
#endif
    
    return program;
}

YovScript* FrontAddScript(FrontContext* front, String path)
{
    Reporter* reporter = front->reporter;
    
    Assert(OsPathIsAbsolute(path));
    
    RBuffer raw_file;
    if (OsReadEntireFile(front->arena, path, &raw_file).failed) {
        ReportErrorFront(NO_CODE, "File '%S' not found\n", path);
        return NULL;
    }
    
    MutexLockGuard(&front->mutex);
    
    // Check for duplicated
    for (auto it = pooled_array_make_iterator(&front->scripts); it.valid; ++it) {
        YovScript* s = it.value;
        if (StrEquals(s->path, path)) {
            return NULL;
        }
    }
    
    path = StrCopy(front->arena, path);
    String text = StrFromRBuffer(raw_file);
    
    I32 script_id = front->scripts.count;
    
    YovScript* script = array_add(&front->scripts);
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
    String text = YOV_CORE;
    
    MutexLockGuard(&front->mutex);
    
    I32 script_id = front->scripts.count;
    
    YovScript* script = array_add(&front->scripts);
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

internal_fn B32 ExpectAndSkipBraces(Reporter* reporter, Parser* parser, I32 script_id)
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

void FrontReadLocationsAndImports(FrontContext* front, YovScript* script, LaneGroup* lane_group)
{
    if (script == NULL) return;
    
    Reporter* reporter = front->reporter;
    Parser* parser = ParserAlloc(script, { 0, script->text.size });
    
    PooledArray<U64> lines = pooled_array_make<U64>(context.arena, 512);
    array_add<U64>(&lines, 0);
    
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
                    array_add(&lines, i + 1);
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
                array_add(&front->global_location_list, location);
                MutexUnlock(&front->mutex);
                
                AssumeToken(parser, TokenKind_NextSentence);
            }
            else if (op == SentenceKind_FunctionDef || op == SentenceKind_StructDef || op == SentenceKind_EnumDef || op == SentenceKind_ArgDef)
            {
                SkipToken(parser, t0); // Identifier
                
                CodeDefinition def = {};
                def.identifier = t0.value;
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
                    if (!ExpectAndSkipBraces(reporter, parser, script->id)) break;
                    
                    def.enum_or_struct.body_location = LocationMake(definition_start_cursor, parser->cursor, script->id);
                }
                else if (op == SentenceKind_FunctionDef)
                {
                    AssumeToken(parser, TokenKind_Colon);
                    AssumeToken(parser, TokenKind_Colon);
                    AssumeToken(parser, TokenKind_FuncKeyword);
                    
                    def.function.parameters_location = NO_CODE;
                    def.function.returns_location = NO_CODE;
                    def.function.body_location = NO_CODE;
                    
                    // Parameters
                    if (PeekToken(parser).kind == TokenKind_OpenParenthesis)
                    {
                        Location parameters_location = FetchScope(parser, TokenKind_OpenParenthesis, false);
                        
                        if (!LocationIsValid(parameters_location)) {
                            report_common_missing_closing_parenthesis(PeekToken(parser).location);
                            continue;
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
                                continue;
                            }
                            
                            def.function.returns_location = returns_location;
                        }
                        else
                        {
                            Location returns_location = FetchUntil(parser, false, TokenKind_OpenBrace, TokenKind_NextSentence);
                            
                            if (!LocationIsValid(returns_location)) {
                                ReportErrorFront(first.location, "Invalid return definition");
                                continue;
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
                                continue;
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
                        continue;
                    }
                    
                    if (has_type) {
                        def.arg.type_location = to_open_brace;
                    }
                    
                    U64 start_cursor = parser->cursor;
                    
                    if (!ExpectAndSkipBraces(reporter, parser, script->id)) break;
                    
                    def.arg.body_location = LocationMake(start_cursor, parser->cursor, parser->script_id);
                }
                
                def.entire_location = LocationMake(t0.cursor, parser->cursor, script->id);
                
                {
                    MutexLockGuard(&front->mutex);
                    DefinitionType type = def.type;
                    
                    if (type == DefinitionType_Function) front->function_count++;
                    else if (type == DefinitionType_Struct) front->struct_count++;
                    else if (type == DefinitionType_Enum) front->enum_count++;
                    else if (type == DefinitionType_Arg) front->arg_count++;
                    else {
                        InvalidCodepath();
                    }
                    
                    array_add(&front->definition_list, def);
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
                array_add(&lines, i + 1);
            }
        }
    }
    
    script->lines = array_from_pooled_array(front->arena, lines);
}

void FrontReadAllScripts(LaneContext* lane, FrontContext* front)
{
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
    
    if (LaneNarrow(lane, 0)) {
        front->definitions = array_from_pooled_array(front->arena, front->definition_list);
    }
    
    LaneBarrier(lane);
}

internal_fn U32 CountIdentifiers(Program* program, String identifier)
{
    U32 count = 0;
    
    foreach(i, program->definitions.count) {
        if (program->definitions[i].header.identifier == identifier) count++;
    }
    
    return count;
}

void FrontIdentifyDefinitions(LaneContext* lane, FrontContext* front)
{
    Reporter* reporter = front->reporter;
    Program* program = front->program;
    
    if (LaneNarrow(lane))
    {
        U32 definition_count = 0;
        definition_count += front->function_count;
        definition_count += front->struct_count;
        definition_count += front->enum_count;
        definition_count += front->arg_count;
        
        program->definitions = array_make<Definition>(program->arena, definition_count);
        program->function_count = front->function_count;
        program->struct_count = front->struct_count;
        program->enum_count = front->enum_count;
        program->arg_count = front->arg_count;
    }
    
    LaneBarrier(lane);
    
    // Identify
    {
        RangeU32 range = LaneDistributeUniformWork(lane, front->definitions.count);
        
        for (U32 i = range.min; i < range.max; ++i)
        {
            CodeDefinition* code = &front->definitions[i];
            
            code->index = AtomicIncrement32(&front->index_counter) - 1;
            DefinitionIdentify(program, code->index, code->type, code->identifier, code->entire_location);
        }
        
        LaneBarrier(lane);
    }
    
    // Check for duplications
    {
        RangeU32 range = LaneDistributeUniformWork(lane, program->definitions.count);
        
        for (U32 i = range.min; i < range.max; ++i)
        {
            DefinitionHeader* def = &program->definitions[i].header;
            
            U32 count = CountIdentifiers(program, def->identifier);
            
            if (count > 1) {
                ReportErrorFront(def->location, "Duplicated definition '%S'", def->identifier);
                continue;
            }
        }
        
        LaneBarrier(lane);
    }
    
    Assert(front->index_counter == program->definitions.count);
}

void FrontDefineDefinitions(LaneContext* lane, FrontContext* front)
{
    RangeU32 range = LaneDistributeUniformWork(lane, front->definitions.count);
    
    for (U32 i = range.min; i < range.max; ++i)
    {
        CodeDefinition* code = &front->definitions[i];
        
        if (code->type == DefinitionType_Enum) {
            FrontDefineEnum(front, code);
        }
        else if (code->type == DefinitionType_Struct) {
            FrontDefineStruct(front, code);
        }
        else if (code->type == DefinitionType_Function) {
            FrontDefineFunction(front, code);
            
            FunctionDefinition* def = FunctionFromIndex(front->program, code->index);
            Assert(def->stage == DefinitionStage_Defined);
        }
        else {
            FrontDefineArg(front, code);
        }
    }
    
    LaneBarrier(lane);
}


internal_fn void DefineLangGlobal(FrontContext* front, String name, String type)
{
    Global global = {};
    global.identifier = name;
    global.vtype = TypeFromName(front->program, type);
    global.is_constant = true;
    
    MutexLock(&front->mutex);
    array_add(&front->global_list, global);
    MutexUnlock(&front->mutex);
}

void FrontDefineGlobals(LaneContext* lane, FrontContext* front)
{
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
            
            Array<Global> globals = array_make<Global>(context.arena, res.objects.count);
            
            foreach(i, globals.count)
            {
                ObjectDefinition def = res.objects[i];
                
                Global global = {};
                global.identifier = StrCopy(program->arena, def.name);
                global.vtype = def.vtype;
                global.is_constant = def.is_constant;
                
                Assert(VTypeValid(def.vtype));
                
                globals[i] = global;
            }
            
            MutexLock(&front->mutex);
            foreach(i, globals.count)
                array_add(&front->global_list, globals[i]);
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
            global.identifier = StrCopy(program->arena, def.identifier);
            global.vtype = def.vtype;
            global.is_constant = true;
            
            Assert(VTypeValid(def.vtype));
            
            array_add(&front->global_list, global);
        }
    }
    LaneBarrier(lane);
    
    if (LaneNarrow(lane)) {
        program->globals = array_from_pooled_array(program->arena, front->global_list);
    }
    LaneBarrier(lane);
}

void FrontResolveGlobals(LaneContext* lane, FrontContext* front)
{
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
                        if (TypeIsBool(def->vtype)) {
                            value = ValueFromBool(true);
                        }
                    }
                    else
                    {
                        value = ValueFromStringExpression(program->arena, script_arg->value, def->vtype);
                    }
                    
                    if (value.kind == ValueKind_None) {
                        report_arg_wrong_value(def->name, script_arg->value);
                        continue;
                    }
                }
                
                I32 global_index = GlobalIndexFromIdentifier(program, def->identifier);
                
                if (global_index >= 0)
                {
                    front->global_initialize_group = IRAppend(front->global_initialize_group, IRFromStore(ir_context, ValueFromGlobal(program, global_index), value, def->location));
                    front->number_of_registers_for_global_initialize = Max(front->number_of_registers_for_global_initialize, ir_context->local_registers.count);
                }
            }
        }
        
        if (front->global_initialize_group.success)
        {
            Array<Register> registers = array_make<Register>(context.arena, front->number_of_registers_for_global_initialize);
            
            foreach(i, registers.count)
            {
                Register reg = {};
                reg.kind = RegisterKind_Local;
                reg.is_constant = false;
                reg.vtype = VType_Any;
                
                registers[i] = reg;
            };
            
            program->globals_initialize_ir = MakeIR(program->arena, program, registers, front->global_initialize_group, NULL);
        }
    }
    
    LaneBarrier(lane);
}

void FrontResolveDefinitions(LaneContext* lane, FrontContext* front)
{
    Program* program = front->program;
    
    while (front->resolve_count < front->definitions.count)
    {
        RangeU32 range = LaneDistributeUniformWork(lane, front->definitions.count);
        
        for (U32 i = range.min; i < range.max; ++i)
        {
            CodeDefinition* code = &front->definitions[i];
            
            B32 resolved = false;
            
            if (code->type == DefinitionType_Enum) {
                resolved = true;
                EnumDefinition* def = EnumFromIndex(program, code->index);
                FrontResolveEnum(front, def);
            }
            else if (code->type == DefinitionType_Struct) {
                StructDefinition* def = StructFromIndex(program, code->index);
                resolved = FrontResolveStruct(front, def);
            }
            else if (code->type == DefinitionType_Function) {
                resolved = true;
                FunctionDefinition* def = FunctionFromIndex(program, code->index);
                FrontResolveFunction(front, def, code);
            }
            else {
                resolved = true;
                FrontResolveArg(front, code);
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
                    front->resolve_count = front->definitions.count;
                }
                else {
                    front->last_resolve_count = front->resolve_count;
                    front->resolve_count = 0;
                }
            }
            
            front->resolve_iterations++;
        }
        
        LaneBarrier(lane);
    }
    
    if (LaneNarrow(lane)) {
        LogFlow("Resove iterations: %u", front->resolve_iterations);
    }
    
    LaneBarrier(lane);
}

void FrontDefineEnum(FrontContext* front, CodeDefinition* code)
{
    Program* program = front->program;
    Reporter* reporter = front->reporter;
    
    EnumDefinition* def = EnumFromIndex(program, code->index);
    if (def == NULL) {
        InvalidCodepath();
        return;
    }
    
    Parser* parser = ParserFromLocation(front, code->enum_or_struct.body_location);
    
    Location starting_location = PeekToken(parser).location;
    
    AssumeToken(parser, TokenKind_OpenBrace);
    
    PooledArray<String> names = pooled_array_make<String>(context.arena, 16);
    PooledArray<Location> expression_locations = pooled_array_make<Location>(context.arena, 16);
    
    while (true)
    {
        Token name_token = ConsumeToken(parser);
        if (name_token.kind == TokenKind_CloseBrace) break;
        if (name_token.kind != TokenKind_Identifier) {
            report_enumdef_expecting_comma_separated_identifier(name_token.location);
            return;
        }
        
        Token assignment_token = PeekToken(parser);
        
        Location expression_location = NO_CODE;
        
        if (assignment_token.kind == TokenKind_Assignment && assignment_token.assignment_binary_operator == BinaryOperator_None) {
            SkipToken(parser, assignment_token);
            
            expression_location = FetchUntil(parser, false, TokenKind_CloseBrace, TokenKind_Comma);
            
            if (!LocationIsValid(expression_location)) {
                ReportErrorFront(assignment_token.location, "Expecting expression for enum value");
                return;
            }
        }
        
        array_add(&names, name_token.value);
        array_add(&expression_locations, expression_location);
        
        Token comma_token = ConsumeToken(parser);
        if (comma_token.kind == TokenKind_CloseBrace) break;
        if (comma_token.kind != TokenKind_Comma) {
            report_enumdef_expecting_comma_separated_identifier(comma_token.location);
            return;
        }
    }
    
    EnumDefine(program, def, array_from_pooled_array(context.arena, names), array_from_pooled_array(context.arena, expression_locations));
}

void FrontDefineStruct(FrontContext* front, CodeDefinition* code)
{
    Program* program = front->program;
    Reporter* reporter = front->reporter;
    
    StructDefinition* def = StructFromIndex(program, code->index);
    if (def == NULL) {
        InvalidCodepath();
        return;
    }
    
    Parser* parser = ParserFromLocation(front, code->enum_or_struct.body_location);
    
    Location starting_location = PeekToken(parser).location;
    AssumeToken(parser, TokenKind_OpenBrace);
    
    U32 member_index = 0;
    
    PooledArray<ObjectDefinition> members = pooled_array_make<ObjectDefinition>(context.arena, 16);
    
    while (PeekToken(parser).kind != TokenKind_CloseBrace)
    {
        Location member_location = FetchUntil(parser, false, TokenKind_NextSentence);
        if (!LocationIsValid(member_location)) {
            report_expecting_semicolon(LocationFromParser(parser, parser->cursor));
            return;
        }
        
        ObjectDefinitionResult read_result = ReadObjectDefinition(context.arena, ParserFromLocation(front, member_location), reporter, program, false, RegisterKind_Local);
        if (!read_result.success) return;
        
        foreach(i, read_result.objects.count) {
            ObjectDefinition member = read_result.objects[i];
            array_add(&members, member);
            
            //out = IRAppend(out, ir_from_child(ir_context, ret, value_from_int(member_index), true, member.vtype, member.location));
            //Value dst = out.value;
            //out = IRAppend(out, ir_from_assignment(ir_context, true, dst, member.value, BinaryOperator_None, member.location));
            
            member_index++;
        }
        
        AssumeToken(parser, TokenKind_NextSentence);
    }
    
    ConsumeToken(parser);
    
    VType struct_vtype = TypeFromName(program, def->identifier);
    Assert(struct_vtype.kind == VKind_Struct);
    
    B32 valid = true;
    
    for(auto it = pooled_array_make_iterator(&members); it.valid; ++it)
    {
        ObjectDefinition* def = it.value;
        
        VType vtype = def->vtype;
        if (TypeIsNil(vtype)) {
            valid = false;
            continue;
        }
        
        if (TypeIsAny(vtype)) {
            ReportErrorFront(def->location, "Any is not a valid member for a struct");
            valid = false;
            continue;
        }
        
        if (TypeEquals(program, vtype, struct_vtype)) {
            report_struct_recursive(def->location);
            valid = false;
            continue;
        }
    }
    
    if (!valid) return;
    
    StructDefine(program, def, array_from_pooled_array(context.arena, members));
}

void FrontDefineFunction(FrontContext* front, CodeDefinition* code)
{
    Program* program = front->program;
    Reporter* reporter = front->reporter;
    
    FunctionDefinition* def = FunctionFromIndex(program, code->index);
    if (def == NULL) {
        InvalidCodepath();
        return;
    }
    
    Array<ObjectDefinition> parameters = {};
    Array<ObjectDefinition> returns = {};
    
    if (LocationIsValid(code->function.parameters_location)) {
        ObjectDefinitionResult res = ReadDefinitionList(context.arena, ParserFromLocation(front, code->function.parameters_location), reporter, program, RegisterKind_Parameter);
        if (!res.success) 
            return;
        parameters = res.objects;
    }
    
    if (LocationIsValid(code->function.returns_location)) {
        if (code->function.return_is_list) {
            ObjectDefinitionResult res = ReadDefinitionList(context.arena, ParserFromLocation(front, code->function.returns_location), reporter, program, RegisterKind_Return);
            if (!res.success) 
                return;
            returns = res.objects;
        }
        else {
            VType vtype = ReadObjectType(ParserFromLocation(front, code->function.returns_location), reporter, program);
            
            if (!TypeIsNil(vtype)) {
                returns = array_make<ObjectDefinition>(context.arena, 1);
                returns[0] = ObjDefMake("return", vtype, code->function.returns_location, false, ValueFromZero(vtype));
            }
        }
        
        if (returns.count == 0) 
            return;
    }
    
    FunctionDefine(program, def, parameters, returns);
}

void FrontDefineArg(FrontContext* front, CodeDefinition* code)
{
    Program* program = front->program;
    Reporter* reporter = front->reporter;
    
    ArgDefinition* def = ArgFromIndex(program, code->index);
    if (def == NULL) {
        InvalidCodepath();
        return;
    }
    
    VType vtype = VType_Bool;
    
    if (LocationIsValid(code->arg.type_location))
    {
        vtype = ReadObjectType(ParserFromLocation(front, code->arg.type_location), reporter, program);
        
        if (TypeIsNil(vtype)) return;
        
        if (!VTypeValid(vtype)) {
            ReportErrorFront(code->arg.type_location, "Invalid type '%S' for an argument", VTypeGetName(program, vtype));
            return;
        }
    }
    
    ArgDefine(program, def, vtype);
}

void FrontResolveEnum(FrontContext* front, EnumDefinition* def)
{
    Reporter* reporter = front->reporter;
    Program* program = front->program;
    
    if (def->stage == DefinitionStage_Ready) {
        return;
    }
    
    if (def->stage != DefinitionStage_Defined) {
        InvalidCodepath();
        return;
    }
    
    IR_Context* ir_context = IrContextAlloc(program, reporter);
    
    Array<I64> values = array_make<I64>(context.arena, def->names.count);
    
    for (U32 i = 0; i < values.count; i++)
    {
        values[i] = i;
        
        if (i < def->expression_locations.count)
        {
            Location expression_location = def->expression_locations[i];
            
            if (!LocationIsValid(expression_location)) continue;
            
            IR ir = IrFromValue(context.arena, program, ValueFromInt(values[i]));
            
            ExpresionContext expr_context = ExpresionContext_from_vtype(VType_Int, 1);
            IR_Group group = ReadExpression(ir_context, ParserFromLocation(front, expression_location), expr_context);
            ir = MakeIR(context.arena, program, array_from_pooled_array(context.arena, ir_context->local_registers), group, NULL);
            if (!ir.success) return;
            
            if (ir.value.kind != ValueKind_Literal || !TypeIsInt(ir.value.vtype)) {
                ReportErrorFront(expression_location, "Enum value expects an Int literal");
                return;
            }
            
            values[i] = ir.value.literal_int;
        }
    }
    
    EnumResolve(program, def, values);
}

B32 FrontResolveStruct(FrontContext* front, StructDefinition* def)
{
    Program* program = front->program;
    
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
    
    StructResolve(program, def);
    return true;
}

void FrontResolveFunction(FrontContext* front, FunctionDefinition* def, CodeDefinition* code)
{
    Program* program = front->program;
    Reporter* reporter = front->reporter;
    
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
            report_intrinsic_not_resolved(def->identifier);
            return;
        }
        
        FunctionResolveIntrinsic(program, def, fn);
    }
    else
    {
        Location block_location = code->function.body_location;
        
        IR_Context* ir = IrContextAlloc(program, reporter);
        IR_Group out = IRFromNone();
        
        if (LocationIsValid(code->function.parameters_location)) {
            ObjectDefinitionResult params = ReadDefinitionListWithIr(context.arena, ParserFromLocation(front, code->function.parameters_location), ir, RegisterKind_Parameter);
            if (!params.success) return;
            
            out = IRAppend(out, params.out);
        }
        
        // Define returns
        foreach(i, def->returns.count)
        {
            ObjectDefinition obj = def->returns[i];
            out = IRAppend(out, IRFromDefineObject(ir, RegisterKind_Return, obj.name, obj.vtype, false, obj.location));
            out = IRAppend(out, IRFromStore(ir, out.value, ValueFromZero(obj.vtype), obj.location));
        }
        
        out = IRAppend(out, ReadCode(ir, ParserFromLocation(front, block_location)));
        
        YovScript* script = FrontGetScript(front, code->entire_location.script_id);
        IR res = MakeIR(front->program->arena, front->program, array_from_pooled_array(context.arena, ir->local_registers), out, script);
        
        // Check for returns
        if (res.success)
        {
            Array<Unit> units = res.instructions;
            
            // TODO(Jose): Check for infinite loops
            
            if (!IRValidateReturnPath(units)) {
                report_function_no_return(block_location, def->identifier);
            }
        }
        
        FunctionResolve(program, def, res);
    }
}

internal_fn B32 ValidateArgName(Reporter* reporter, String name, Location location)
{
    B32 valid_chars = true;
    
    U64 cursor = 0;
    while (cursor < name.size) {
        U32 codepoint = StrGetCodepoint(name, &cursor);
        
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

void FrontResolveArg(FrontContext* front, CodeDefinition* code)
{
    Program* program = front->program;
    Reporter* reporter = front->reporter;
    ArgDefinition* def = ArgFromIndex(program, code->index);
    
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
    
    Parser* parser = ParserFromLocation(front, code->arg.body_location);
    
    AssumeToken(parser, TokenKind_OpenBrace);
    
    while (true)
    {
        Token identifier_token = ConsumeToken(parser);
        
        if (identifier_token.kind == TokenKind_CloseBrace) break;
        if (identifier_token.kind == TokenKind_None) {
            ReportErrorFront(identifier_token.location, "Missing close brace for arg definition");
            return;
        }
        
        if (identifier_token.kind != TokenKind_Identifier) {
            ReportErrorFront(identifier_token.location, "Expecting property list");
            return;
        }
        
        Token assignment_token = ConsumeToken(parser);
        if (assignment_token.kind != TokenKind_Assignment || assignment_token.assignment_binary_operator != BinaryOperator_None)
        {
            ReportErrorFront(assignment_token.location, "Expecting a property assignment");
            return;
        }
        
        Location expression_location = FetchUntil(parser, false, TokenKind_NextSentence);
        if (!LocationIsValid(expression_location)) {
            ReportErrorFront(identifier_token.location, "Missing semicolon");
            return;
        }
        
        AssumeToken(parser, TokenKind_NextSentence);
        
        String identifier = identifier_token.value;
        
        ExpresionContext expr_context = ExpresionContext_from_inference(1);
        
        if (identifier == "name") expr_context = ExpresionContext_from_vtype(VType_String, 1);
        else if (identifier == "description") expr_context = ExpresionContext_from_vtype(VType_String, 1);
        else if (identifier == "required") expr_context = ExpresionContext_from_vtype(VType_Bool, 1);
        else if (identifier == "default") expr_context = ExpresionContext_from_vtype(def->vtype, 1);
        else {
            ReportErrorFront(identifier_token.location, "Unknown property '%S'", identifier);
            return;
        }
        
        IR_Context* ir_context = IrContextAlloc(program, reporter);
        IR_Group group = ReadExpression(ir_context, ParserFromLocation(front, expression_location), expr_context);
        
        IR ir = MakeIR(context.arena, program, array_from_pooled_array(context.arena, ir_context->local_registers), group, NULL);
        if (!ir.success) {
            return;
        }
        
        Value value = ir.value;
        
        if (!TypeEquals(program, value.vtype, expr_context.vtype)) {
            report_type_missmatch_assign(expression_location, value.vtype, expr_context.vtype);
            return;
        }
        
        if (!ValueIsCompiletime(value)) {
            ReportErrorFront(expression_location, "Expecting a compile-time value");
            return;
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
    
    if (!ValidateArgName(reporter, name, code->entire_location)) return;
    
    ArgResolve(program, def, name, description, required, default_value);
}
