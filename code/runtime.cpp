#include "runtime.h"

void ExecuteProgram(Program* program, Reporter* reporter, RuntimeSettings settings)
{
    PROFILE_FRAME_MARK;
    
    Runtime* runtime = RuntimeAlloc(program, reporter, settings);
    
    runtime->started_time = OsTimerGet();
    
    RuntimeInitializeGlobals(runtime);
    
    RuntimeStart(runtime, "Main");
    RuntimeStepAll(runtime);
    
    RuntimeFree(runtime);
}

Runtime* RuntimeAlloc(Program* program, Reporter* reporter, RuntimeSettings settings)
{
    Arena* arena = ArenaAlloc(Gb(16), 8, "Arena Runtime");
    Runtime* runtime = ArenaPushStruct<Runtime>(arena);
    runtime->arena = arena;
    runtime->program = program;
    runtime->settings = settings;
    runtime->reporter = reporter;
    runtime->stack = array_make<Scope>(program->arena, 4096);
    
    return runtime;
}

void RuntimeFree(Runtime* runtime)
{
    Reporter* reporter = runtime->reporter;
    ObjectFreeAll(runtime);
    
#if DEV
    if (runtime->gc.object_count > 0) {
        lang_report_unfreed_objects();
    }
    else if (runtime->gc.allocation_count > 0) {
        lang_report_unfreed_dynamic();
    }
    //Assert(yov->reports.count > 0 || runtime->current_scope == runtime->global_scope);
#endif
    
    ArenaFree(runtime->arena);
}

void RuntimeInitializeGlobals(Runtime* runtime)
{
    PROFILE_FUNCTION;
    
    LogFlow("Starting Init Globals");
    F64 start_time = TimerNow();
    
    Program* program = runtime->program;
    
    runtime->globals = array_make<Reference>(runtime->arena, program->globals.count);
    foreach(i, runtime->globals.count) {
        runtime->globals[i] = ref_from_object(null_obj);
    }
    
    if (runtime->reporter->exit_requested) return;
    
    // Yov struct
    if (!TypeIsNil(VType_YovInfo))
    {
        runtime->common_globals.yov = object_alloc(runtime, VType_YovInfo);
        Reference ref = runtime->common_globals.yov;
        RuntimeStoreGlobal(runtime, "yov", ref);
        
        RefSetUIntMember(runtime, ref, "minor", YOV_MINOR_VERSION);
        RefSetUIntMember(runtime, ref, "major", YOV_MAJOR_VERSION);
        RefSetUIntMember(runtime, ref, "revision", YOV_REVISION_VERSION);
        ref_member_set_string(runtime, ref, "version", YOV_VERSION);
        ref_member_set_string(runtime, ref, "path", system_info.executable_path);
    }
    
    // OS struct
    if (!TypeIsNil(VType_OS))
    {
        runtime->common_globals.os = object_alloc(runtime, VType_OS);
        Reference ref = runtime->common_globals.os;
        RuntimeStoreGlobal(runtime, "os", ref);
        
#if OS_WINDOWS
        set_enum_index_member(runtime, ref, "kind", 0);
#else
#error TODO
#endif
    }
    
    // Context struct
    if (!TypeIsNil(VType_Context))
    {
        runtime->common_globals.context = object_alloc(runtime, VType_Context);
        Reference ref = runtime->common_globals.context;
        RuntimeStoreGlobal(runtime, "context", ref);
        
        ref_member_set_string(runtime, ref, "cd", program->script_dir);
        ref_member_set_string(runtime, ref, "script_dir", program->script_dir);
        ref_member_set_string(runtime, ref, "caller_dir", program->caller_dir);
        RefMemberSetUInt(runtime, ref, "seed", OsTimerGet());
        
        // Args
#if 0 // TODO(Jose): 
        {
            Array<ScriptArg> args = yov->script_args;
            Reference array = alloc_array(runtime, VType_String, args.count);
            foreach(i, args.count) {
                ref_set_member(runtime, array, i, alloc_string(runtime, args[i].name));
            }
            
            ref_set_member(runtime, ref, vtype_get_member(VType_Context, "args").index, array);
        }
#endif
        
        // Types
        {
            Reference array = AllocArray(runtime, VType_Type, program->vtypes.count);
            
            foreach(i, program->vtypes.count) {
                Reference element = object_alloc(runtime, VType_Type);
                ref_assign_Type(runtime, element, program->vtypes[i]);
                ref_set_member(runtime, array, i, element);
            }
            
            ref_set_member(runtime, ref, vtype_get_member(VType_Context, "types").index, array);
        }
    }
    
    // Calls struct
    if (!TypeIsNil(VType_CallsContext))
    {
        Reference ref = object_alloc(runtime, VType_CallsContext);
        runtime->common_globals.calls = ref;
        RuntimeStoreGlobal(runtime, "calls", ref);
    }
    
    if (program->globals_initialize_ir.success)
    {
        RuntimePushScope(runtime, -1, 0, program->globals_initialize_ir, {});
        RuntimeStepAll(runtime);
    }
    
    F64 ellapsed = TimerNow() - start_time;
    LogFlow("Init globals finished: %S", StringFromEllapsedTime(ellapsed));
    
    ArenaPopTo(context.arena, 0);
}

void RuntimeStart(Runtime* runtime, String function_name)
{
    PROFILE_FUNCTION;
    
    Program* program = runtime->program;
    Reporter* reporter = runtime->reporter;
    
    if (reporter->exit_requested) return;
    
    LogFlow("Starting Execution");
    LogFlow(SEPARATOR_STRING);
    
    FunctionDefinition* fn = FunctionFromIdentifier(program, function_name);
    
    if (fn == NULL) {
        ReportErrorNoCode("Function '%S' not found", function_name);
        return;
    }
    
    if (fn->returns.count != 0 || fn->parameters.count != 0) {
        ReportErrorNoCode("Invalid entry point '%S', expected a function with no returns and params", function_name);
        return;
    }
    
    RunFunctionCall(runtime, -1, fn, {});
}

void RuntimePushScope(Runtime* runtime, I32 return_index, U32 return_count, IR ir, Array<Value> params)
{
    PROFILE_FUNCTION;
    
    Assert(ir.parameter_count == params.count);
    
    if (runtime->stack_counter >= runtime->stack.count) {
        ReportStackOverflow();
        return;
    }
    
    Program* program = runtime->program;
    Reporter* reporter = runtime->reporter;
    
    Scope* prev_scope = RuntimeGetCurrentScope(runtime);
    
    // Push new scope
    Scope* scope = &runtime->stack[runtime->stack_counter++];
    *scope = {};
    scope->return_index = return_index;
    scope->return_count = return_count;
    scope->ir = ir;
    scope->registers = array_make<Reference>(context.arena, ir.local_registers.count);
    foreach(i, scope->registers.count) {
        I32 register_index = RegIndexFromLocal(program, i);
        RuntimeStore(runtime, scope, register_index, ref_from_object(null_obj));
    }
    
    // Define params
    {
        U32 param_index = 0;
        
        foreach(i, ir.local_registers.count)
        {
            Register reg = ir.local_registers[i];
            if (reg.kind != RegisterKind_Parameter) continue;
            
            I32 register_index = RegIndexFromLocal(program, i);
            Value param = params[param_index++];
            Reference ref = RefFromValue(runtime, prev_scope, param);
            
            if (is_null(RuntimeLoad(runtime, scope, register_index))) {
                RuntimeStore(runtime, scope, register_index, object_alloc(runtime, ref.vtype));
            }
            
            RunCopy(runtime, register_index, ref);
        }
    }
}

void RuntimePopScope(Runtime* runtime)
{
    PROFILE_FUNCTION;
    
    Program* program = runtime->program;
    Reporter* reporter = runtime->reporter;
    
    if (runtime->stack_counter == 0) {
        ReportStackIsBroken();
        return;
    }
    
    Scope* scope = &runtime->stack[--runtime->stack_counter];
    
    Array<Reference> output = array_make<Reference>(context.arena, scope->return_count);
    foreach(i, output.count) {
        output[i] = ref_from_object(null_obj);
    }
    
    // Retrieve return value
    {
        Array<Value> returns = ValuesFromReturn(context.arena, scope->ir.value, false);
        Assert(output.count <= returns.count);
        
        foreach(i, Min(output.count, returns.count)) {
            output[i] = RefFromValue(runtime, scope, returns[i]);
        }
    }
    
    Scope* prev_scope = RuntimeGetCurrentScope(runtime);
    RuntimeStoreReturn(runtime, prev_scope, scope->return_index, output);
    
    foreach(i, scope->registers.count) {
        object_decrement_ref(scope->registers[i].parent);
    }
    
    *scope = {};
}

B32 RuntimeStep(Runtime* runtime)
{
    PROFILE_FUNCTION;
    
    Program* program = runtime->program;
    Scope* scope = RuntimeGetCurrentScope(runtime);
    
    if (scope == NULL) return false;
    if (runtime->reporter->exit_requested) return false;
    
    Unit unit = ScopeGetCurrentUnit(scope);
    RunInstruction(runtime, unit);
    //PrintF("\n->%S\n", StringFromUnit(context.arena, program, 0, 3, 3, unit));
    scope->unit_counter++;
    
    // TODO(Jose): Free garbage memory
    return runtime->stack_counter > 0;
}

B32 RuntimeStepInto(Runtime* runtime)
{
    PROFILE_FUNCTION;
    
    String ref_path = RuntimeGetCurrentFile(runtime);
    U32 ref_line = RuntimeGetCurrentLine(runtime);
    
    while (1)
    {
        if (!RuntimeStep(runtime)) {
            return false;
        }
        
        String path = RuntimeGetCurrentFile(runtime);
        U32 line = RuntimeGetCurrentLine(runtime);
        
        if (ref_line != line || ref_path != path) {
            break;
        }
    }
    
    return true;
}

B32 RuntimeStepOver(Runtime* runtime)
{
    PROFILE_FUNCTION;
    
    U32 ref_line = RuntimeGetCurrentLine(runtime);
    U32 ref_depth = runtime->stack_counter;
    
    while (1)
    {
        if (!RuntimeStep(runtime)) {
            return false;
        }
        
        U32 depth = runtime->stack_counter;
        U32 line = RuntimeGetCurrentLine(runtime);
        
        if (ref_line != line && depth <= ref_depth) {
            break;
        }
    }
    
    return true;
}

B32 RuntimeStepOut(Runtime* runtime)
{
    PROFILE_FUNCTION;
    
    U32 ref_depth = runtime->stack_counter;
    
    while (1)
    {
        if (!RuntimeStep(runtime)) {
            return false;
        }
        
        U32 depth = runtime->stack_counter;
        
        if (depth < ref_depth) {
            break;
        }
    }
    
    return true;
}

void RuntimeStepAll(Runtime* runtime)
{
    PROFILE_FUNCTION;
    while (RuntimeStep(runtime)) {}
}

void RuntimePrintScriptHelp(Runtime* runtime)
{
    PROFILE_FUNCTION;
    
    Program* program = runtime->program;
    StringBuilder builder = string_builder_make(context.arena);
    
    // Script description
    {
        I32 global_index = GlobalIndexFromIdentifier(program, "script_description");
        
        if (global_index >= 0) {
            Reference ref = RefFromValue(runtime, NULL, ValueFromGlobal(program, global_index));
            String description = StrFromRef(context.arena, runtime, ref, true);
            
            append(&builder, description);
            append(&builder, "\n\n");
        }
    }
    
    Array<String> headers = array_make<String>(context.arena, program->arg_count);
    
    U32 index = 0;
    U32 longest_header = 0;
    foreach(i, program->definitions.count)
    {
        ArgDefinition* arg = &program->definitions[i].arg;
        if (arg->type != DefinitionType_Arg) continue;
        
        B32 show_type = VTypeValid(arg->vtype);
        
        String space = "    ";
        
        String header;
        if (show_type)
        {
            VType type = arg->vtype;
            
            String type_str;
            if (type.kind == VKind_Enum) {
                type_str = "enum";
            }
            else {
                type_str = VTypeGetName(program, arg->vtype);
            }
            header = StrFormat(context.arena, "%S%S -> %S", space, arg->name, type_str);
        }
        else {
            header = StrFormat(context.arena, "%S%S", space, arg->name);
        }
        
        headers[index++] = header;
        
        U32 char_count = StrCalculateCharCount(header);
        longest_header = Max(longest_header, char_count);
    }
    
    U32 chars_to_description = longest_header + 4;
    
    index = 0;
    appendf(&builder, "Script Arguments:\n");
    foreach(i, program->definitions.count)
    {
        ArgDefinition* arg = &program->definitions[i].arg;
        if (arg->type != DefinitionType_Arg) continue;
        
        String header = headers[index++];
        
        append(&builder, header);
        
        if (arg->description.size != 0)
        {
            U32 char_count = StrCalculateCharCount(header);
            for (U32 i = char_count; i < chars_to_description; ++i) {
                append(&builder, " ");
            }
            
            appendf(&builder, "%S", arg->description);
        }
        appendf(&builder, "\n");
    }
    
    String log = string_from_builder(context.arena, &builder);
    PrintF(log);
}

void RuntimeExit(Runtime* runtime, I64 exit_code) {
    ReporterSetExitCode(runtime->reporter, exit_code);
}

void RuntimeReportError(Runtime* runtime, Result result) {
    Reporter* reporter = runtime->reporter;
    ReportErrorRT(result.message);
    RuntimeExit(runtime, result.code);
}

Scope* RuntimeGetCurrentScope(Runtime* runtime)
{
    if (runtime->stack_counter > 0) {
        return &runtime->stack[runtime->stack_counter - 1];
    }
    return NULL;
}

Unit ScopeGetCurrentUnit(Scope* scope)
{
    return scope->ir.instructions[scope->unit_counter];
}

U32 RuntimeGetCurrentLine(Runtime* runtime)
{
    Scope* scope = RuntimeGetCurrentScope(runtime);
    if (scope == NULL) {
        return 0;
    }
    return ScopeGetCurrentUnit(scope).line;
}

String RuntimeGetCurrentFile(Runtime* runtime)
{
    Scope* scope = RuntimeGetCurrentScope(runtime);
    if (scope == NULL) {
        return {};
    }
    return scope->ir.path;
}

String RuntimeGenerateInheritedLangArgs(Runtime* runtime)
{
    StringBuilder builder = string_builder_make(context.arena);
    if (runtime->settings.user_assert) appendf(&builder, "%S ", LANG_ARG_USER_ASSERT);
    if (runtime->settings.no_user) appendf(&builder, "%S ", LANG_ARG_NO_USER);
    return string_from_builder(context.arena, &builder);
}

CallOutput RuntimeCallScript(Runtime* runtime, String script, String args, String lang_args, RedirectStdout redirect_stdout)
{
    String current_dir = RuntimeGetCurrentDirStr(runtime);
    String inherited_lang_args = RuntimeGenerateInheritedLangArgs(runtime);
    
    String lang_exe_path = system_info.executable_path;
    String command = StrFormat(context.arena, "\"%S\" %S %S %S %S", lang_exe_path, inherited_lang_args, lang_args, script, args);
    return OsCall(context.arena, current_dir, command, redirect_stdout);
}

Result RuntimeUserAssertion(Runtime* runtime, String message) {
    if (!runtime->settings.user_assert || RuntimeAskYesNo(runtime, "User Assertion", message)) return RESULT_SUCCESS;
    return ResultMakeFailed("Operation denied by user");
}

B32 RuntimeAskYesNo(Runtime* runtime, String title, String message)
{
    if (runtime->settings.no_user) return true;
    return OsAskYesNo(title, message);
}

Reference RuntimeGetCurrentDirRef(Runtime* runtime) {
    Program* program = runtime->program;
    I32 index = vtype_get_member(VType_Context, "cd").index;
    return ref_get_member(runtime, runtime->common_globals.context, index);
}

String RuntimeGetCurrentDirStr(Runtime* runtime) {
    return get_string(RuntimeGetCurrentDirRef(runtime));
}

String PathAbsoluteToCD(Arena* arena, Runtime* runtime, String path)
{
    String cd = RuntimeGetCurrentDirStr(runtime);
    if (!OsPathIsAbsolute(path)) path = PathResolve(context.arena, PathAppend(context.arena, cd, path));
    return StrCopy(arena, path);
}

RedirectStdout RuntimeGetCallsRedirectStdout(Runtime* runtime)
{
    Reference calls = runtime->common_globals.calls;
    VariableTypeChild info = vtype_get_member(calls.vtype, "redirect_stdout");
    Reference redirect_stdout = ref_get_member(runtime, calls, info.index);
    return (RedirectStdout)get_enum_index(redirect_stdout);
}

Reference RefFromValue(Runtime* runtime, Scope* scope, Value value)
{
    PROFILE_FUNCTION;
    
    Program* program = runtime->program;
    
    if (value.kind == ValueKind_None) return ref_from_object(null_obj);
    if (value.kind == ValueKind_Literal) {
        PROFILE_SCOPE("Literal");
        if (TypeIsInt(value.vtype)) return AllocSInt(runtime, value.literal_sint);
        if (TypeIsUInt(value.vtype)) return AllocUInt(runtime, value.literal_sint);
        if (TypeIsBool(value.vtype)) return AllocBool(runtime, value.literal_bool);
        if (TypeIsFloat(value.vtype)) return AllocFloat(runtime, value.literal_float);
        if (TypeIsString(value.vtype)) return AllocString(runtime, value.literal_string);
        if (TypeIsVoid(value.vtype)) return ref_from_object(null_obj);
        if (TypeEquals(program, value.vtype, VType_Type)) {
            Reference ref = object_alloc(runtime, VType_Type);
            ref_assign_Type(runtime, ref, value.literal_type);
            return ref;
        }
        if (value.vtype.kind == VKind_Enum) {
            return AllocEnum(runtime, value.vtype, value.literal_sint);
        }
        InvalidCodepath();
        return ref_from_object(nil_obj);
    }
    
    if (value.kind == ValueKind_Array)
    {
        PROFILE_SCOPE("Array");
        
        Array<Value> values = value.array.values;
        B32 is_empty = value.array.is_empty;
        
        if (is_empty)
        {
            Array<I64> dimensions = array_make<I64>(context.arena, values.count);
            foreach(i, dimensions.count) {
                dimensions[i] = RefGetUInt(RefFromValue(runtime, scope, values[i]));
            }
            
            VType base_vtype = TypeFromIndex(program, value.vtype.base_index);
            Reference array = AllocArrayMultidimensional(runtime, base_vtype, dimensions);
            return array;
        }
        else
        {
            VType element_vtype = VTypeNext(program, value.vtype);
            Reference array = AllocArray(runtime, element_vtype, values.count);
            
            foreach(i, values.count) {
                ref_set_member(runtime, array, i, RefFromValue(runtime, scope, values[i]));
            }
            return array;
        }
    }
    
    if (value.kind == ValueKind_ZeroInit) {
        PROFILE_SCOPE("ZeroInit");
        return object_alloc(runtime, value.vtype);
    }
    
    if (value.kind == ValueKind_StringComposition)
    {
        PROFILE_SCOPE("StringComposition");
        if (TypeIsString(value.vtype))
        {
            Array<Value> sources = value.string_composition;
            
            StringBuilder builder = string_builder_make(context.arena);
            foreach(i, sources.count)
            {
                Reference source = RefFromValue(runtime, scope, sources[i]);
                if (is_unknown(source)) return ref_from_object(nil_obj);
                append(&builder, StrFromRef(context.arena, runtime, source));
            }
            
            return AllocString(runtime, string_from_builder(context.arena, &builder));
        }
    }
    
    if (value.kind == ValueKind_MultipleReturn) {
        return RefFromValue(runtime, scope, value.multiple_return[0]);
    }
    
    if (value.kind == ValueKind_LValue || value.kind == ValueKind_Register)
    {
        PROFILE_SCOPE("Register");
        
        Reference ref = RuntimeLoad(runtime, scope, ValueGetRegister(value));
        
        I32 op = value.reg.reference_op;
        
        while (op > 0) {
            ref = AllocReference(runtime, ref);
            op--;
        }
        
        while (op < 0)
        {
            if (is_null(ref)) {
                InvalidCodepath();
                return ref_from_object(nil_obj);
            }
            
            ref = RefDereference(runtime, ref);
            op++;
        }
        
        return ref;
    }
    
    InvalidCodepath();
    return ref_from_object(null_obj);
}

void RunInstruction(Runtime* runtime, Unit unit)
{
    PROFILE_FUNCTION;
    Program* program = runtime->program;
    Reporter* reporter = runtime->reporter;
    
    Scope* scope = RuntimeGetCurrentScope(runtime);
    Reference src0 = RefFromValue(runtime, scope, unit.src0);
    Reference src1 = RefFromValue(runtime, scope, unit.src1);
    
    LogTrace("RUN: %S", StringFromUnit(context.arena, program, 0, 0, 0, unit));
    
    I32 dst_index = unit.dst_index;
    
    switch(unit.kind)
    {
        case UnitKind_Copy:
        {
            RunCopy(runtime, dst_index, src0);
            return;
        }
        
        case UnitKind_Store:
        {
            RunStore(runtime, dst_index, src0);
            return;
        }
        
        case UnitKind_FunctionCall:
        {
            FunctionDefinition* fn = unit.function_call.fn;
            Array<Value> parameters = unit.function_call.parameters;
            RunFunctionCall(runtime, dst_index, fn, parameters);
            return;
        }
        
        case UnitKind_Return:
        {
            RunReturn(runtime);
            return;
        }
        
        case UnitKind_Jump:
        {
            I32 condition = unit.jump.condition;
            I32 offset = unit.jump.offset;
            RunJump(runtime, src0, condition, offset);
            return;
        }
        
        case UnitKind_Child:
        {
            B32 is_member = unit.child.child_is_member;
            RunChild(runtime, dst_index, src0, src1, is_member);
            return;
        }
        
        case UnitKind_ResultEval:
        {
            Reference src = RefFromValue(runtime, RuntimeGetCurrentScope(runtime), unit.src0);
            
            Assert(TypeEquals(program, src.vtype, VType_Result));
            
            if (TypeEquals(program, src.vtype, VType_Result)) {
                Result result = Result_from_ref(runtime, src);
                if (result.failed) {
                    RuntimeReportError(runtime, result);
                }
            }
            return;
        }
        
        case UnitKind_Add: {
            RunAdd(runtime, dst_index, unit.op_dst_type, src0, src1);
            return;
        }
        case UnitKind_Sub: {
            RunSub(runtime, dst_index, unit.op_dst_type, src0, src1);
            return;
        }
        case UnitKind_Mul: {
            RunMul(runtime, dst_index, unit.op_dst_type, src0, src1);
            return;
        }
        case UnitKind_Div: {
            RunDiv(runtime, dst_index, unit.op_dst_type, src0, src1);
            return;
        }
        case UnitKind_Mod: {
            RunMod(runtime, dst_index, unit.op_dst_type, src0, src1);
            return;
        }
        
        case UnitKind_Eql: {
            RunEql(runtime, dst_index, unit.op_dst_type, src0, src1);
            return;
        }
        case UnitKind_Neq: {
            RunNeq(runtime, dst_index, unit.op_dst_type, src0, src1);
            return;
        }
        case UnitKind_Gtr: {
            RunGtr(runtime, dst_index, unit.op_dst_type, src0, src1);
            return;
        }
        case UnitKind_Lss: {
            RunLss(runtime, dst_index, unit.op_dst_type, src0, src1);
            return;
        }
        case UnitKind_Geq: {
            RunGeq(runtime, dst_index, unit.op_dst_type, src0, src1);
            return;
        }
        case UnitKind_Leq: {
            RunLeq(runtime, dst_index, unit.op_dst_type, src0, src1);
            return;
        }
        
        case UnitKind_Or: {
            RunOr(runtime, dst_index, unit.op_dst_type, src0, src1);
            return;
        }
        case UnitKind_And: {
            RunAnd(runtime, dst_index, unit.op_dst_type, src0, src1);
            return;
        }
        case UnitKind_Not: {
            RunNot(runtime, dst_index, unit.op_dst_type, src0);
            return;
        }
        
        case UnitKind_Is: {
            RunIs(runtime, dst_index, src0, src1);
            return;
        }
        
        case UnitKind_Neg: {
            RunNeg(runtime, dst_index, unit.op_dst_type, src0);
            return;
        }
        
        case UnitKind_Cast: {
            RunCast(runtime, dst_index, unit.op_dst_type, src0);
            return;
        }
        
        case UnitKind_BitCast: {
            RunBitCast(runtime, dst_index, unit.op_dst_type, src0);
            return;
        }
        
        case UnitKind_Error:
        case UnitKind_Empty:
        break;
    }
    
    InvalidCodepath();
}

void RunStore(Runtime* runtime, I32 dst_index, Reference src)
{
    PROFILE_FUNCTION;
    if (is_unknown(src)) {
        InvalidCodepath();
        return;
    }
    RuntimeStore(runtime, NULL, dst_index, src);
}

void RunCopy(Runtime* runtime, I32 dst_index, Reference src)
{
    PROFILE_FUNCTION;
    
    Program* program = runtime->program;
    Reporter* reporter = runtime->reporter;
    
    Reference dst = RuntimeLoad(runtime, RuntimeGetCurrentScope(runtime), dst_index);
    
    if (is_unknown(dst) || is_unknown(src)) {
        InvalidCodepath();
        return;
    }
    
    VType src_vtype = src.vtype;
    
    if (is_null(dst) || is_null(src)) {
        ReportNullRef();
        return;
    }
    
    if (TypeIsReference(dst.vtype) && TypeEquals(program, VTypeNext(program, dst.vtype), src_vtype)) {
        dst = RefDereference(runtime, dst);
        
        if (is_null(dst)) {
            ReportNullRef();
            return;
        }
    }
    
    RefCopy(runtime, dst, src);
}


void RunFunctionCall(Runtime* runtime, I32 dst_index, FunctionDefinition* fn, Array<Value> parameters)
{
    PROFILE_FUNCTION;
    
    Program* program = runtime->program;
    Reporter* reporter = runtime->reporter;
    
    if (fn->is_intrinsic)
    {
        Array<Reference> returns = array_make<Reference>(context.arena, fn->returns.count);
        
        if (fn->intrinsic.fn == NULL) {
            report_intrinsic_not_resolved(fn->identifier);
            return;
        }
        
        Array<Reference> params = array_make<Reference>(context.arena, parameters.count);
        foreach(i, params.count) {
            params[i] = RefFromValue(runtime, RuntimeGetCurrentScope(runtime), parameters[i]);
        }
        
        fn->intrinsic.fn(runtime, params, returns);
        
        foreach(i, returns.count) {
            Assert(is_valid(returns[i]));
        }
        
        RuntimeStoreReturn(runtime, NULL, dst_index, returns);
    }
    else
    {
        RuntimePushScope(runtime, dst_index, fn->returns.count, fn->defined.ir, parameters);
    }
}

internal_fn Reference _RunChild(Runtime* runtime, Reference src, Reference index, B32 is_member)
{
    if (is_null(src)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    
    if (!RefIsAnyInt(index)) {
        ReportErrorRT("Expecting an integer");
        return ref_from_object(null_obj);
    }
    
    U64 child_index = RefIsUInt(index) ? RefGetUInt(index) : RefGetInt(index);
    U32 child_count = RefGetChildCount(runtime, src, is_member);
    
    if (child_index >= child_count) {
        ReportErrorRT("Out of bounds");
        return ref_from_object(null_obj);
    }
    
    return ref_get_child(runtime, src, (U32)child_index, is_member);
}

void RunChild(Runtime* runtime, I32 dst_index, Reference src, Reference index, B32 is_member)
{
    PROFILE_FUNCTION;
    Reference child = _RunChild(runtime, src, index, is_member);
    RuntimeStore(runtime, NULL, dst_index, child);
}

void RunReturn(Runtime* runtime)
{
    PROFILE_FUNCTION;
    RuntimePopScope(runtime);
}

void RunJump(Runtime* runtime, Reference ref, I32 condition, I32 offset)
{
    PROFILE_FUNCTION;
    Program* program = runtime->program;
    Reporter* reporter = runtime->reporter;
    
    B32 jump = true;
    
    if (condition != 0)
    {
        if (is_unknown(ref)) return;
        
        if (!RefIsBool(ref)) {
            ReportErrorRT("Expected a boolean expression");
            return;
        }
        
        jump = RefGetBool(ref);
        if (condition < 0) jump = !jump;
    }
    
    if (jump) {
        Scope* scope = RuntimeGetCurrentScope(runtime);
        if (scope == NULL) {
            InvalidCodepath();
            return;
        }
        
        scope->unit_counter += offset;
    }
}

internal_fn Reference _RunAdd(Runtime* runtime, PrimitiveType type, Reference left, Reference right)
{
    if (is_null(left)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    if (is_null(right)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    
    switch (type)
    {
        case PrimitiveType_Int:  return AllocSInt(runtime, RefGetInt(left) + RefGetInt(right));
        case PrimitiveType_UInt:  return AllocUInt(runtime, RefGetUInt(left) + RefGetUInt(right));
        case PrimitiveType_Float:  return AllocFloat(runtime, RefGetFloat(left) + RefGetFloat(right));
        
        case PrimitiveType_Bool:
        case PrimitiveType_String:
        break;
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

void RunAdd(Runtime* runtime, I32 dst_index, PrimitiveType type, Reference left, Reference right)
{
    PROFILE_FUNCTION;
    Reference result = _RunAdd(runtime, type, left, right);
    RuntimeStore(runtime, NULL, dst_index, result);
}

internal_fn Reference _RunSub(Runtime* runtime, PrimitiveType type, Reference left, Reference right)
{
    if (is_null(left)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    if (is_null(right)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    
    switch (type)
    {
        case PrimitiveType_Int:  return AllocSInt(runtime, RefGetInt(left) - RefGetInt(right));
        case PrimitiveType_UInt:  return AllocUInt(runtime, RefGetUInt(left) - RefGetUInt(right));
        case PrimitiveType_Float:  return AllocFloat(runtime, RefGetFloat(left) - RefGetFloat(right));
        
        case PrimitiveType_Bool:
        case PrimitiveType_String:
        break;
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

void RunSub(Runtime* runtime, I32 dst_index, PrimitiveType type, Reference left, Reference right)
{
    PROFILE_FUNCTION;
    Reference result = _RunSub(runtime, type, left, right);
    RuntimeStore(runtime, NULL, dst_index, result);
}

internal_fn Reference _RunMul(Runtime* runtime, PrimitiveType type, Reference left, Reference right)
{
    if (is_null(left)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    if (is_null(right)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    
    switch (type)
    {
        case PrimitiveType_Int:  return AllocSInt(runtime, RefGetInt(left) * RefGetInt(right));
        case PrimitiveType_UInt:  return AllocUInt(runtime, RefGetUInt(left) * RefGetUInt(right));
        case PrimitiveType_Float:  return AllocFloat(runtime, RefGetFloat(left) * RefGetFloat(right));
        
        case PrimitiveType_Bool:
        case PrimitiveType_String:
        break;
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

void RunMul(Runtime* runtime, I32 dst_index, PrimitiveType type, Reference left, Reference right)
{
    PROFILE_FUNCTION;
    Reference result = _RunMul(runtime, type, left, right);
    RuntimeStore(runtime, NULL, dst_index, result);
}

internal_fn Reference _RunDiv(Runtime* runtime, PrimitiveType type, Reference left, Reference right)
{
    if (is_null(left)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    if (is_null(right)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    
    if (type == PrimitiveType_Int)
    {
        I64 divisor = RefGetSInt(right);
        if (divisor == 0) {
            ReportZeroDivision();
            return ref_from_object(null_obj);
        }
    }
    
    if (type == PrimitiveType_UInt)
    {
        U64 divisor = RefGetUInt(right);
        if (divisor == 0) {
            ReportZeroDivision();
            return ref_from_object(null_obj);
        }
    }
    
    
    switch (type)
    {
        case PrimitiveType_Int:  return AllocSInt(runtime, RefGetInt(left) / RefGetInt(right));
        case PrimitiveType_UInt:  return AllocUInt(runtime, RefGetUInt(left) / RefGetUInt(right));
        case PrimitiveType_Float:  return AllocFloat(runtime, RefGetFloat(left) / RefGetFloat(right));
        
        case PrimitiveType_Bool:
        case PrimitiveType_String:
        break;
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

void RunDiv(Runtime* runtime, I32 dst_index, PrimitiveType type, Reference left, Reference right)
{
    PROFILE_FUNCTION;
    Reference result = _RunDiv(runtime, type, left, right);
    RuntimeStore(runtime, NULL, dst_index, result);
}

internal_fn Reference _RunMod(Runtime* runtime, PrimitiveType type, Reference left, Reference right)
{
    if (is_null(left)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    if (is_null(right)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    
    if (type == PrimitiveType_Int)
    {
        I64 divisor = RefGetSInt(right);
        if (divisor == 0) {
            ReportZeroDivision();
            return ref_from_object(null_obj);
        }
    }
    
    if (type == PrimitiveType_UInt)
    {
        U64 divisor = RefGetUInt(right);
        if (divisor == 0) {
            ReportZeroDivision();
            return ref_from_object(null_obj);
        }
    }
    
    
    switch (type)
    {
        case PrimitiveType_Int:  return AllocSInt(runtime, RefGetInt(left) % RefGetInt(right));
        case PrimitiveType_UInt:  return AllocUInt(runtime, RefGetUInt(left) % RefGetUInt(right));
        
        case PrimitiveType_Float:
        case PrimitiveType_Bool:
        case PrimitiveType_String:
        break;
    }
    
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

void RunMod(Runtime* runtime, I32 dst_index, PrimitiveType type, Reference left, Reference right)
{
    PROFILE_FUNCTION;
    Reference result = _RunMod(runtime, type, left, right);
    RuntimeStore(runtime, NULL, dst_index, result);
}

internal_fn Reference _RunEql(Runtime* runtime, PrimitiveType ptype, Reference left, Reference right)
{
    if (is_null(left)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    if (is_null(right)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    
    VType type = TypeFromPrimitive(runtime->program, ptype);
    
    switch (left.vtype.primitive_type)
    {
        case PrimitiveType_Int:  return AllocBool(runtime, RefGetInt(left) == RefGetInt(right));
        case PrimitiveType_UInt:  return AllocBool(runtime, RefGetUInt(left) == RefGetUInt(right));
        case PrimitiveType_Float:  return AllocBool(runtime, RefGetFloat(left) == RefGetFloat(right));
        case PrimitiveType_Bool:  return AllocBool(runtime, RefGetBool(left) == RefGetBool(right));
        
        case PrimitiveType_String:
        break;
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

void RunEql(Runtime* runtime, I32 dst_index, PrimitiveType type, Reference left, Reference right)
{
    PROFILE_FUNCTION;
    Reference result = _RunEql(runtime, type, left, right);
    RuntimeStore(runtime, NULL, dst_index, result);
}

internal_fn Reference _RunNeq(Runtime* runtime, PrimitiveType ptype, Reference left, Reference right)
{
    if (is_null(left)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    if (is_null(right)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    
    VType type = TypeFromPrimitive(runtime->program, ptype);
    
    switch (left.vtype.primitive_type)
    {
        case PrimitiveType_Int:  return AllocBool(runtime, RefGetInt(left) != RefGetInt(right));
        case PrimitiveType_UInt:  return AllocBool(runtime, RefGetUInt(left) != RefGetUInt(right));
        case PrimitiveType_Float:  return AllocBool(runtime, RefGetFloat(left) != RefGetFloat(right));
        case PrimitiveType_Bool:  return AllocBool(runtime, RefGetBool(left) != RefGetBool(right));
        
        case PrimitiveType_String:
        break;
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

void RunNeq(Runtime* runtime, I32 dst_index, PrimitiveType type, Reference left, Reference right)
{
    PROFILE_FUNCTION;
    Reference result = _RunNeq(runtime, type, left, right);
    RuntimeStore(runtime, NULL, dst_index, result);
}

internal_fn Reference _RunGtr(Runtime* runtime, PrimitiveType ptype, Reference left, Reference right)
{
    if (is_null(left)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    if (is_null(right)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    
    VType type = TypeFromPrimitive(runtime->program, ptype);
    
    switch (left.vtype.primitive_type)
    {
        case PrimitiveType_Int:  return AllocBool(runtime, RefGetInt(left) > RefGetInt(right));
        case PrimitiveType_UInt:  return AllocBool(runtime, RefGetUInt(left) > RefGetUInt(right));
        case PrimitiveType_Float:  return AllocBool(runtime, RefGetFloat(left) > RefGetFloat(right));
        case PrimitiveType_Bool:  return AllocBool(runtime, RefGetBool(left) > RefGetBool(right));
        
        case PrimitiveType_String:
        break;
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

void RunGtr(Runtime* runtime, I32 dst_index, PrimitiveType type, Reference left, Reference right)
{
    PROFILE_FUNCTION;
    Reference result = _RunGtr(runtime, type, left, right);
    RuntimeStore(runtime, NULL, dst_index, result);
}

internal_fn Reference _RunLss(Runtime* runtime, PrimitiveType ptype, Reference left, Reference right)
{
    if (is_null(left)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    if (is_null(right)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    
    VType type = TypeFromPrimitive(runtime->program, ptype);
    
    switch (left.vtype.primitive_type)
    {
        case PrimitiveType_Int:  return AllocBool(runtime, RefGetInt(left) < RefGetInt(right));
        case PrimitiveType_UInt:  return AllocBool(runtime, RefGetUInt(left) < RefGetUInt(right));
        case PrimitiveType_Float:  return AllocBool(runtime, RefGetFloat(left) < RefGetFloat(right));
        case PrimitiveType_Bool:  return AllocBool(runtime, RefGetBool(left) < RefGetBool(right));
        
        case PrimitiveType_String:
        break;
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

void RunLss(Runtime* runtime, I32 dst_index, PrimitiveType type, Reference left, Reference right)
{
    PROFILE_FUNCTION;
    Reference result = _RunLss(runtime, type, left, right);
    RuntimeStore(runtime, NULL, dst_index, result);
}

internal_fn Reference _RunGeq(Runtime* runtime, PrimitiveType ptype, Reference left, Reference right)
{
    if (is_null(left)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    if (is_null(right)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    
    VType type = TypeFromPrimitive(runtime->program, ptype);
    
    switch (left.vtype.primitive_type)
    {
        case PrimitiveType_Int:  return AllocBool(runtime, RefGetInt(left) >= RefGetInt(right));
        case PrimitiveType_UInt:  return AllocBool(runtime, RefGetUInt(left) >= RefGetUInt(right));
        case PrimitiveType_Float:  return AllocBool(runtime, RefGetFloat(left) >= RefGetFloat(right));
        case PrimitiveType_Bool:  return AllocBool(runtime, RefGetBool(left) >= RefGetBool(right));
        
        case PrimitiveType_String:
        break;
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

void RunGeq(Runtime* runtime, I32 dst_index, PrimitiveType type, Reference left, Reference right)
{
    PROFILE_FUNCTION;
    Reference result = _RunGeq(runtime, type, left, right);
    RuntimeStore(runtime, NULL, dst_index, result);
}

internal_fn Reference _RunLeq(Runtime* runtime, PrimitiveType ptype, Reference left, Reference right)
{
    if (is_null(left)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    if (is_null(right)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    
    VType type = TypeFromPrimitive(runtime->program, ptype);
    
    switch (left.vtype.primitive_type)
    {
        case PrimitiveType_Int:  return AllocBool(runtime, RefGetInt(left) <= RefGetInt(right));
        case PrimitiveType_UInt:  return AllocBool(runtime, RefGetUInt(left) <= RefGetUInt(right));
        case PrimitiveType_Float:  return AllocBool(runtime, RefGetFloat(left) <= RefGetFloat(right));
        case PrimitiveType_Bool:  return AllocBool(runtime, RefGetBool(left) <= RefGetBool(right));
        
        case PrimitiveType_String:
        break;
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

void RunLeq(Runtime* runtime, I32 dst_index, PrimitiveType type, Reference left, Reference right)
{
    PROFILE_FUNCTION;
    Reference result = _RunLeq(runtime, type, left, right);
    RuntimeStore(runtime, NULL, dst_index, result);
}

internal_fn Reference _RunOr(Runtime* runtime, PrimitiveType ptype, Reference left, Reference right)
{
    if (is_null(left)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    if (is_null(right)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    
    VType type = TypeFromPrimitive(runtime->program, ptype);
    
    switch (left.vtype.primitive_type)
    {
        case PrimitiveType_Int:  return AllocBool(runtime, RefGetInt(left) || RefGetInt(right));
        case PrimitiveType_UInt:  return AllocBool(runtime, RefGetUInt(left) || RefGetUInt(right));
        case PrimitiveType_Float:  return AllocBool(runtime, RefGetFloat(left) || RefGetFloat(right));
        case PrimitiveType_Bool:  return AllocBool(runtime, RefGetBool(left) || RefGetBool(right));
        
        case PrimitiveType_String:
        break;
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

void RunOr(Runtime* runtime, I32 dst_index, PrimitiveType type, Reference left, Reference right)
{
    PROFILE_FUNCTION;
    Reference result = _RunOr(runtime, type, left, right);
    RuntimeStore(runtime, NULL, dst_index, result);
}

internal_fn Reference _RunAnd(Runtime* runtime, PrimitiveType ptype, Reference left, Reference right)
{
    if (is_null(left)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    if (is_null(right)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    
    VType type = TypeFromPrimitive(runtime->program, ptype);
    
    switch (left.vtype.primitive_type)
    {
        case PrimitiveType_Int:  return AllocBool(runtime, RefGetInt(left) && RefGetInt(right));
        case PrimitiveType_UInt:  return AllocBool(runtime, RefGetUInt(left) && RefGetUInt(right));
        case PrimitiveType_Float:  return AllocBool(runtime, RefGetFloat(left) && RefGetFloat(right));
        case PrimitiveType_Bool:  return AllocBool(runtime, RefGetBool(left) && RefGetBool(right));
        
        case PrimitiveType_String:
        break;
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

void RunAnd(Runtime* runtime, I32 dst_index, PrimitiveType type, Reference left, Reference right)
{
    PROFILE_FUNCTION;
    Reference result = _RunAnd(runtime, type, left, right);
    RuntimeStore(runtime, NULL, dst_index, result);
}

internal_fn Reference _RunNeg(Runtime* runtime, PrimitiveType ptype, Reference src)
{
    if (is_null(src)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    
    VType type = TypeFromPrimitive(runtime->program, ptype);
    
    switch (src.vtype.primitive_type)
    {
        case PrimitiveType_Int:  return AllocSInt(runtime, -RefGetInt(src));
        case PrimitiveType_UInt:  return AllocSInt(runtime, -(I64)RefGetUInt(src));
        case PrimitiveType_Float:  return AllocFloat(runtime, -RefGetFloat(src));
        
        case PrimitiveType_Bool:
        case PrimitiveType_String:
        break;
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

void RunNeg(Runtime* runtime, I32 dst_index, PrimitiveType type, Reference src)
{
    PROFILE_FUNCTION;
    Reference result = _RunNeg(runtime, type, src);
    RuntimeStore(runtime, NULL, dst_index, result);
}

internal_fn Reference _RunNot(Runtime* runtime, PrimitiveType ptype, Reference src)
{
    if (is_null(src)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    
    VType type = TypeFromPrimitive(runtime->program, ptype);
    
    switch (src.vtype.primitive_type)
    {
        case PrimitiveType_Int:  return AllocBool(runtime, !RefGetInt(src));
        case PrimitiveType_UInt:  return AllocBool(runtime, !RefGetUInt(src));
        case PrimitiveType_Float:  return AllocBool(runtime, !RefGetFloat(src));
        case PrimitiveType_Bool:  return AllocBool(runtime, !RefGetBool(src));
        
        case PrimitiveType_String:
        break;
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

void RunNot(Runtime* runtime, I32 dst_index, PrimitiveType type, Reference src)
{
    PROFILE_FUNCTION;
    Reference result = _RunNot(runtime, type, src);
    RuntimeStore(runtime, NULL, dst_index, result);
}

#define _Cast(_c_type, _type) do { \
_c_type v = RefGet##_type(src); \
switch(ptype) { \
case PrimitiveType_Int: return AllocSInt(runtime, (I64)v); \
case PrimitiveType_UInt: return AllocUInt(runtime, (U64)v); \
case PrimitiveType_Bool: return AllocBool(runtime, (B32)!!(v)); \
case PrimitiveType_Float: return AllocFloat(runtime, (F64)v); \
case PrimitiveType_String: break; \
} } while (0);

internal_fn Reference _RunCast(Runtime* runtime, PrimitiveType ptype, Reference src)
{
    if (is_null(src)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    
    VType dst_type = TypeFromPrimitive(runtime->program, ptype);
    
    switch (src.vtype.primitive_type)
    {
        case PrimitiveType_Int: _Cast(I64, SInt);
        case PrimitiveType_UInt: _Cast(U64, UInt);
        case PrimitiveType_Bool: _Cast(B32, Bool);
        case PrimitiveType_Float: _Cast(F64, Float);
        
        case PrimitiveType_String:
        break;
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

void RunCast(Runtime* runtime, I32 dst_index, PrimitiveType type, Reference src)
{
    PROFILE_FUNCTION;
    Reference result = _RunCast(runtime, type, src);
    RuntimeStore(runtime, NULL, dst_index, result);
}

internal_fn Reference _RunBitCast(Runtime* runtime, PrimitiveType ptype, Reference src)
{
    if (is_null(src)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    
    if (ptype == PrimitiveType_String || src.vtype.primitive_type == PrimitiveType_String)
    {
        InvalidCodepath();
        return ref_from_object(null_obj);
    }
    
    VType dst_type = TypeFromPrimitive(runtime->program, ptype);
    VType src_type = src.vtype;
    
    Reference dst = object_alloc(runtime, dst_type);
    
    U32 dst_size = VTypeGetSize(dst_type);
    U32 src_size = VTypeGetSize(src_type);
    
    void* dst_address = dst.address;
    void* src_address = src.address;
    
    MemoryZero(dst_address, dst_size);
    U32 copy_size = Min(src_size, dst_size);
    MemoryCopy(dst_address, src_address, copy_size);
    
    return dst;
}

void RunBitCast(Runtime* runtime, I32 dst_index, PrimitiveType type, Reference src)
{
    PROFILE_FUNCTION;
    Reference result = _RunBitCast(runtime, type, src);
    RuntimeStore(runtime, NULL, dst_index, result);
}

Reference _RunIs(Runtime* runtime, Reference left, Reference right)
{
    if (is_null(right)) {
        ReportNullRef();
        return AllocBool(runtime, false);
    }
    
    Program* program = runtime->program;
    
    if (!TypeEquals(program, right.vtype, VType_Type)) {
        ReportErrorRT("Right value is not a Type");
        return AllocBool(runtime, false);
    }
    
    VType type = RefGetType(runtime, right);
    
    return AllocBool(runtime, TypeEquals(program, left.vtype, type));
}

void RunIs(Runtime* runtime, I32 dst_index, Reference left, Reference right)
{
    PROFILE_FUNCTION;
    Reference result = _RunIs(runtime, left, right);
    RuntimeStore(runtime, NULL, dst_index, result);
}

#if 0// TODO(Jose): 

Reference RunBinaryOperation(Runtime* runtime, VType dst_type, Reference left, Reference right)
{
    
    VType left_vtype = left.vtype;
    VType right_vtype = right.vtype;
    
    B32 can_reuse_left = dst.address == left.address;
    
    if (is_reference(left) && TypeEquals(program, left.vtype, right.vtype))
    {
        void* v0 = RefDereference(runtime, left).address;
        void* v1 = RefDereference(runtime, right).address;
        
        if (op == OperatorKind_Equals) return AllocBool(runtime, v0 == v1);
        if (op == OperatorKind_NotEquals) return AllocBool(runtime, v0 != v1);
    }
    
    if (TypeEquals(program, left_vtype, VType_Type) && TypeEquals(program, right_vtype, VType_Type))
    {
        I32 index = vtype_get_member(VType_Type, "name").index;
        
        String left_name = get_string(ref_get_member(runtime, left, index));
        String right_name = get_string(ref_get_member(runtime, right, index));
        
        VType left = TypeFromName(program, left_name);
        VType right = TypeFromName(program, right_name);
        
        if (op == OperatorKind_Equals) {
            return AllocBool(runtime, TypeEquals(program, left, right));
        }
        else if (op == OperatorKind_NotEquals) {
            return AllocBool(runtime, !TypeEquals(program, left, right));
        }
    }
    
    if (TypeEquals(program, right_vtype, VType_Type))
    {
        I32 index = vtype_get_member(VType_Type, "name").index;
        
        String name = get_string(ref_get_member(runtime, right, index));
        
        VType type = TypeFromName(program, name);
        
        if (op == OperatorKind_Is) {
            return AllocBool(runtime, TypeEquals(program, type, left.vtype));
        }
    }
    
    if ((is_string(left) && RefIsI64(right)) || (RefIsI64(left) && is_string(right)))
    {
        if (op == OperatorKind_Addition)
        {
            Reference string_ref = is_string(left) ? left : right;
            Reference codepoint_ref = RefIsI64(left) ? left : right;
            
            String codepoint_str = StringFromCodepoint(context.arena, (U32)RefGetI64(codepoint_ref));
            
            if (can_reuse_left && is_string(dst))
            {
                ref_string_append(runtime, dst, codepoint_str);
                return dst;
            }
            else {
                String left_str = is_string(left) ? get_string(left) : codepoint_str;
                String right_str = is_string(right) ? get_string(right) : codepoint_str;
                
                String str = StrFormat(context.arena, "%S%S", left_str, right_str);
                return AllocString(runtime, str);
            }
        }
    }
    
    if (is_enum(left) && is_enum(right)) {
        if (op == OperatorKind_Equals) {
            return AllocBool(runtime, get_enum_index(left) == get_enum_index(right));
        }
        else if (op == OperatorKind_NotEquals) {
            return AllocBool(runtime, get_enum_index(left) != get_enum_index(right));
        }
    }
    
    if (TypeIsArray(left_vtype) && TypeIsArray(right_vtype) && TypeEquals(program, VTypeNext(program, left_vtype), VTypeNext(program, right_vtype)))
    {
        VType element_vtype = VTypeNext(program, left_vtype);
        
        I32 left_count = get_array(left)->count;
        I32 right_count = get_array(right)->count;
        
        if (op == OperatorKind_Addition) {
            Reference array = AllocArray(runtime, element_vtype, left_count + right_count);
            for (U32 i = 0; i < left_count; ++i) {
                Reference src = ref_get_child(runtime, left, i, true);
                ref_set_member(runtime, array, i, src);
            }
            for (U32 i = 0; i < right_count; ++i) {
                Reference src = ref_get_child(runtime, right, i, true);
                ref_set_member(runtime, array, left_count + i, src);
            }
            return array;
        }
    }
    
    if ((left_vtype.kind == VKind_Array && right_vtype.kind != VKind_Array) || (left_vtype.kind != VKind_Array && right_vtype.kind == VKind_Array))
    {
        VType array_type = (left_vtype.kind == VKind_Array) ? left_vtype : right_vtype;
        VType element_type = (left_vtype.kind == VKind_Array) ? right_vtype : left_vtype;
        
        if (!TypeEquals(program, VTypeNext(program, array_type), element_type)) {
            ReportInvalidOp();
            return ref_from_object(nil_obj);
        }
        
        Reference array_src = (left_vtype.kind == VKind_Array) ? left : right;
        Reference element = (left_vtype.kind == VKind_Array) ? right : left;
        
        I32 array_src_count = get_array(array_src)->count;
        Reference array = AllocArray(runtime, element_type, array_src_count + 1);
        
        I32 array_offset = (left_vtype.kind == VKind_Array) ? 0 : 1;
        
        for (I32 i = 0; i < array_src_count; ++i) {
            Reference src = ref_get_child(runtime, array_src, i, true);
            ref_set_member(runtime, array, i + array_offset, src);
        }
        
        I32 element_offset = (left_vtype.kind == VKind_Array) ? array_src_count : 0;
        ref_set_member(runtime, array, element_offset, element);
        return array;
    }
    
    ReportInvalidOp();
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

Reference RunUnaryOperation(Runtime* runtime, VType dst_type, Reference src, UnaryOperation op)
{
    switch (op)
    {
        case UnaryOperation_Neg: return RunNeg(runtime, dst_type, src);
        case UnaryOperation_Not: return RunNot(runtime, dst_type, src);
        case UnaryOperation_Cast: return RunCast(runtime, dst_type, src);
        
        case UnaryOperation_None:
        case UnaryOperation_Count:
        InvalidCodepath();
        return ref_from_object(nil_obj);
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}
#endif

//- SCOPE

void RuntimeStore(Runtime* runtime, Scope* scope, I32 register_index, Reference ref)
{
    PROFILE_FUNCTION;
    
    Program* program = runtime->program;
    
    if (scope == NULL) scope = RuntimeGetCurrentScope(runtime);
    
    I32 local_index = LocalFromRegIndex(program, register_index);
    
    Reference* reg;
    
    if (local_index >= 0) {
        reg = &scope->registers[local_index];
    }
    else {
        reg = &runtime->globals[register_index];
    }
    
    if (is_unknown(ref)) {
        InvalidCodepath();
    }
    
    object_decrement_ref(reg->parent);
    *reg = ref;
    object_increment_ref(reg->parent);
}

void RuntimeStoreGlobal(Runtime* runtime, String identifier, Reference ref)
{
    PROFILE_FUNCTION;
    
    Program* program = runtime->program;
    
    I32 global_index = GlobalIndexFromIdentifier(program, identifier);
    if (global_index < 0) {
        InvalidCodepath();
        return;
    }
    
    U32 register_index = RegIndexFromGlobal(global_index);
    RuntimeStore(runtime, NULL, register_index, ref);
}

void RuntimeStoreReturn(Runtime* runtime, Scope* scope, I32 dst_index, Array<Reference> refs)
{
    PROFILE_FUNCTION;
    
    if (dst_index < 0) return;
    
    foreach(i, refs.count) {
        I32 reg = dst_index + i;
        RuntimeStore(runtime, scope, reg, refs[i]);
    }
}

Reference RuntimeLoad(Runtime* runtime, Scope* scope, I32 register_index)
{
    PROFILE_FUNCTION;
    
    Program* program = runtime->program;
    
    if (scope == NULL) scope = RuntimeGetCurrentScope(runtime);
    I32 local_index = LocalFromRegIndex(program, register_index);
    
    if (local_index >= 0) {
        return scope->registers[local_index];
    }
    else {
        return runtime->globals[register_index];
    }
}

Reference RuntimeLoadGlobal(Runtime* runtime, String identifier)
{
    PROFILE_FUNCTION;
    
    Program* program = runtime->program;
    
    I32 global_index = GlobalIndexFromIdentifier(program, identifier);
    if (global_index < 0) {
        return ref_from_object(null_obj);
    }
    return RuntimeLoad(runtime, NULL, RegIndexFromGlobal(global_index));
}

//- OBJECT 

String StrFromObject(Arena* arena, Runtime* runtime, Object* object, B32 raw) {
    return StrFromRef(arena, runtime, ref_from_object(object), raw);
}

String StrFromRef(Arena* arena, Runtime* runtime, Reference ref, B32 raw)
{
    PROFILE_FUNCTION;
    
    if (is_null(ref)) {
        return "null";
    }
    
    VType vtype = ref.vtype;
    
    if (TypeIsString(vtype)) {
        if (raw) return get_string(ref);
        return StrFormat(arena, "\"%S\"", get_string(ref));
    }
    if (TypeIsInt(vtype)) { return StrFromI64(arena, RefGetSInt(ref)); }
    if (TypeIsUInt(vtype)) { return StrFromU64(arena, RefGetUInt(ref)); }
    if (TypeIsFloat(vtype)) { return StrFromF64(arena, RefGetFloat(ref), 4); }
    if (TypeIsBool(vtype)) { return RefGetBool(ref) ? "true" : "false"; }
    if (TypeIsVoid(vtype)) { return "void"; }
    if (TypeIsNil(vtype)) { return "nil"; }
    
    if (vtype.kind == VKind_Array)
    {
        StringBuilder builder = string_builder_make(context.arena);
        
        append(&builder, "{ ");
        
        ObjectData_Array* array = RefGetArray(ref);
        
        foreach(i, array->count) {
            Reference element = ref_get_member(runtime, ref, i);
            append(&builder, StrFromRef(context.arena, runtime, element, false));
            if (i < array->count - 1) append(&builder, ", ");
        }
        
        append(&builder, " }");
        
        return string_from_builder(arena, &builder);
    }
    
    if (vtype.kind == VKind_Enum)
    {
        I64 index = get_enum_index(ref);
        if (index < 0 || index >= vtype._enum->names.count) return "?";
        String name = vtype._enum->names[(U32)index];
        if (!raw) name = StrFormat(arena, "\"%S\"", name);
        return name;
    }
    
    if (vtype.kind == VKind_Struct)
    {
        StringBuilder builder = string_builder_make(context.arena);
        
        append(&builder, "{ ");
        
        foreach(i, vtype._struct->vtypes.count)
        {
            String member_name = vtype._struct->names[i];
            
            Reference member = ref_get_member(runtime, ref, i);
            appendf(&builder, "%S = %S", member_name, StrFromRef(context.arena, runtime, member, false));
            if (i < vtype._struct->vtypes.count - 1) append(&builder, ", ");
        }
        
        append(&builder, " }");
        
        return string_from_builder(arena, &builder);
    }
    
    if (vtype.kind == VKind_Reference) {
        U64 address = (U64)ref.address;
        return StrFormat(arena, "%l", address);
    }
    
    InvalidCodepath();
    return "?";
}

Reference ref_from_object(Object* object)
{
    Reference ref = {};
    ref.parent = object;
    ref.vtype = object->vtype;
    ref.address = object + 1;
    return ref;
}

Reference ref_from_address(Object* parent, VType vtype, void* address)
{
    Reference member = {};
    member.parent = parent;
    member.vtype = vtype;
    member.address = address;
    return member;
}

void ref_set_member(Runtime* runtime, Reference ref, U32 index, Reference member)
{
    Reference dst = ref_get_member(runtime, ref, index);
    RefCopy(runtime, dst, member);
}

Reference ref_get_child(Runtime* runtime, Reference ref, U32 index, B32 is_member)
{
    if (is_member) {
        Reference child = ref_get_member(runtime, ref, index);
        // TODO(Jose): What about memory requirements
        return child;
    }
    else return ref_get_property(runtime, ref, index);
}

Reference ref_get_member(Runtime* runtime, Reference ref, U32 index)
{
    Program* program = runtime->program;
    
    if (is_unknown(ref) || is_null(ref)) {
        InvalidCodepath();
        return ref_from_object(nil_obj);
    }
    
    VType vtype = ref.vtype;
    
    if (vtype.kind == VKind_Array)
    {
        ObjectData_Array* array = RefGetArray(ref);
        
        if (index >= array->count) {
            InvalidCodepath();
            return ref_from_object(nil_obj);
        }
        
        VType element_vtype = VTypeNext(program, ref.vtype);
        
        U32 offset = VTypeGetSize(element_vtype) * index;
        return ref_from_address(ref.parent, element_vtype, array->data + offset);
    }
    
    if (vtype.kind == VKind_Struct)
    {
        Array<VType> vtypes = vtype._struct->vtypes;
        
        if (index >= vtypes.count) {
            InvalidCodepath();
            return ref_from_object(nil_obj);
        }
        
        U8* data = (U8*)ref.address;
        U32 offset = vtype._struct->offsets[index];
        
        return ref_from_address(ref.parent, vtypes[index], data + offset);
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

Reference ref_get_property(Runtime* runtime, Reference ref, U32 index)
{
    if (is_unknown(ref) || is_null(ref)) {
        InvalidCodepath();
        return ref_from_object(nil_obj);
    }
    
    VType vtype = ref.vtype;
    
    if (TypeIsString(vtype))
    {
        if (index == 0) return AllocUInt(runtime, get_string(ref).size);
    }
    
    if (vtype.kind == VKind_Array)
    {
        if (index == 0) return AllocUInt(runtime, RefGetArray(ref)->count);
    }
    
    if (vtype.kind == VKind_Enum)
    {
        I64 v = get_enum_index(ref);
        if (index == 0) return AllocSInt(runtime, v);
        if (index == 1) {
            if (v < 0 || v >= vtype._enum->values.count) return AllocSInt(runtime, -1);
            return AllocSInt(runtime, vtype._enum->values[v]);
        }
        if (index == 2) {
            if (v < 0 || v >= vtype._enum->names.count) return AllocString(runtime, "?");
            return AllocString(runtime, vtype._enum->names[v]);
        }
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

U32 RefGetChildCount(Runtime* runtime, Reference ref, B32 is_member)
{
    PROFILE_FUNCTION;
    if (is_member) return RefGetMemberCount(ref);
    else return RefGetPropertyCount(runtime, ref);
}

U32 RefGetPropertyCount(Runtime* runtime, Reference ref) {
    return VTypeGetProperties(runtime->program, ref.vtype).count;
}

U32 RefGetMemberCount(Reference ref)
{
    if (ref.vtype.kind == VKind_Array) {
        return RefGetArray(ref)->count;
    }
    else if (ref.vtype.kind == VKind_Struct) {
        return ref.vtype._struct->vtypes.count;
    }
    return 0;
}

Reference AllocSInt(Runtime* runtime, I64 value)
{
    Reference ref = object_alloc(runtime, VType_Int);
    RefSetSInt(ref, value);
    return ref;
}

Reference AllocUInt(Runtime* runtime, U64 value)
{
    Reference ref = object_alloc(runtime, VType_UInt);
    RefSetUInt(ref, value);
    return ref;
}

Reference AllocFloat(Runtime* runtime, F64 value)
{
    Reference ref = object_alloc(runtime, VType_Float);
    RefSetFloat(ref, value);
    return ref;
}

Reference AllocBool(Runtime* runtime, B32 value)
{
    Reference ref = object_alloc(runtime, VType_Bool);
    RefSetBool(ref, value);
    return ref;
}

Reference AllocString(Runtime* runtime, String value)
{
    Reference ref = object_alloc(runtime, VType_String);
    ref_string_set(runtime, ref, value);
    return ref;
}

Reference AllocArray(Runtime* runtime, VType element_vtype, U32 count)
{
    PROFILE_FUNCTION;
    VType vtype = vtype_from_dimension(element_vtype, 1);
    
    if (vtype.kind != VKind_Array) {
        InvalidCodepath();
        return ref_from_object(nil_obj);
    }
    
    Reference ref = object_alloc(runtime, vtype);
    RefArrayPrepare(runtime, ref, count);
    
    ObjectData_Array* array = RefGetArray(ref);
    array->count = array->capacity;
    return ref;
}

Reference AllocArrayMultidimensional(Runtime* runtime, VType base_vtype, Array<I64> dimensions)
{
    Program* program = runtime->program;
    
    if (dimensions.count <= 0) {
        InvalidCodepath();
        return ref_from_object(nil_obj);
    }
    
    VType vtype = vtype_from_dimension(base_vtype, dimensions.count);
    VType element_vtype = VTypeNext(program, vtype);
    
    if (dimensions.count == 1)
    {
        return AllocArray(runtime, element_vtype, (U32)dimensions[0]);
    }
    else
    {
        U32 count = (U32)dimensions[0];
        Reference ref = AllocArray(runtime, VTypeNext(program, vtype), count);
        
        foreach(i, count) {
            Reference element_src = AllocArrayMultidimensional(runtime, base_vtype, array_subarray(dimensions, 1, dimensions.count - 1));
            Reference element_dst = ref_get_child(runtime, ref, i, true);
            RefCopy(runtime, element_dst, element_src);
        }
        
        return ref;
    }
}

Reference AllocEnum(Runtime* runtime, VType vtype, I64 index)
{
    Reference ref = object_alloc(runtime, vtype);
    set_enum_index(ref, index);
    return ref;
}

Reference AllocReference(Runtime* runtime, Reference ref)
{
    Reference res = object_alloc(runtime, vtype_from_reference(ref.vtype));
    set_reference(runtime, res, ref);
    return res;
}

B32 is_valid(Reference ref) {
    return !is_unknown(ref);
}
B32 is_unknown(Reference ref) {
    if (ref.parent == NULL) return true;
    if (TypeIsNil(ref.vtype)) return true;
    return TypeIsNil(ref.parent->vtype);
}

B32 is_const(Reference ref) {
    // TODO(Jose): return value.kind == ValueKind_LValue && value.lvalue.ref->constant;
    return false;
}

B32 is_null(Reference ref) {
    if (is_unknown(ref)) return true;
    return TypeIsVoid(ref.vtype) && TypeIsVoid(ref.parent->vtype);
}

B32 RefIsAnyInt(Reference ref) { return is_valid(ref) && TypeIsAnyInt(ref.vtype); }
B32 RefIsInt(Reference ref) { return is_valid(ref) && TypeIsInt(ref.vtype); }
B32 RefIsUInt(Reference ref) { return is_valid(ref) && TypeIsUInt(ref.vtype); }
B32 RefIsBool(Reference ref) { return is_valid(ref) && TypeIsBool(ref.vtype); }
B32 RefIsFloat(Reference ref) { return is_valid(ref) && TypeIsFloat(ref.vtype); }
B32 is_string(Reference ref) { return is_valid(ref) && TypeIsString(ref.vtype); }

B32 RefIsArray(Reference ref) {
    if (is_unknown(ref)) return false;
    return TypeIsArray(ref.vtype);
}

B32 is_enum(Reference ref) {
    if (is_unknown(ref)) return false;
    return TypeIsEnum(ref.vtype);
}

B32 is_reference(Reference ref) {
    if (is_unknown(ref)) return false;
    return TypeIsReference(ref.vtype);
}

B32 RefIsType(Program* program, Reference ref) {
    if (is_unknown(ref)) return false;
    return TypeEquals(program, ref.vtype, VType_Type);
}

I64 RefGetSInt(Reference ref)
{
    if (!RefIsInt(ref)) {
        InvalidCodepath();
        return 0;
    }
    
    I64* data = (I64*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(I64));
    return *data;
}

U64 RefGetUInt(Reference ref)
{
    if (!RefIsUInt(ref)) {
        InvalidCodepath();
        return 0;
    }
    
    U64* data = (U64*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(U64));
    return *data;
}

I64 RefGetInt(Reference ref)
{
    if (TypeIsInt(ref.vtype)) return RefGetSInt(ref);
    return (I64)RefGetUInt(ref);
}

B32 RefGetBool(Reference ref)
{
    if (!RefIsBool(ref)) {
        InvalidCodepath();
        return 0;
    }
    
    B32* data = (B32*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(B32));
    return *data;
}

F64 RefGetFloat(Reference ref)
{
    if (!RefIsFloat(ref)) {
        InvalidCodepath();
        return 0;
    }
    
    F64* data = (F64*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(F64));
    return *data;
}

I64 get_enum_index(Reference ref) {
    if (!is_enum(ref)) {
        InvalidCodepath();
        return 0;
    }
    
    I64* data = (I64*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(I64));
    return *data;
}

String get_string(Reference ref)
{
    if (!is_string(ref)) {
        InvalidCodepath();
        return 0;
    }
    
    ObjectData_String* data = (ObjectData_String*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(ObjectData_String));
    return StrMake(data->chars, data->size);
}

ObjectData_Array* RefGetArray(Reference ref)
{
    if (!RefIsArray(ref)) {
        InvalidCodepath();
        return {};
    }
    
    ObjectData_Array* array = (ObjectData_Array*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(ObjectData_Array));
    return array;
}

Reference RefDereference(Runtime* runtime, Reference ref)
{
    if (!is_reference(ref)) {
        InvalidCodepath();
        return {};
    }
    
    ObjectData_Ref* data = (ObjectData_Ref*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(ObjectData_Ref));
    
    if (data->parent == null_obj || data->parent == NULL) {
        return ref_from_object(null_obj);
    }
    
    Reference deref = {};
    deref.parent = data->parent;
    deref.address = data->address;
    deref.vtype = VTypeNext(runtime->program, ref.vtype);
    return deref;
}

VType RefGetType(Runtime* runtime, Reference ref)
{
    if (!RefIsType(runtime->program, ref)) {
        InvalidCodepath();
        return {};
    }
    
    String name = get_string_member(runtime, ref, "name");
    return TypeFromName(runtime->program, name);
}

I64 get_int_member(Runtime* runtime, Reference ref, String member)
{
    I32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(runtime, ref, index);
    return RefGetSInt(member_ref);
}

B32 get_bool_member(Runtime* runtime, Reference ref, String member)
{
    I32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(runtime, ref, index);
    return RefGetBool(member_ref);
}

String get_string_member(Runtime* runtime, Reference ref, String member)
{
    I32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(runtime, ref, index);
    return get_string(member_ref);
}

void RefSetSInt(Reference ref, I64 v)
{
    if (!RefIsInt(ref)) {
        InvalidCodepath();
        return;
    }
    
    I64* data = (I64*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(I64));
    *data = v;
}

void RefSetUInt(Reference ref, U64 v)
{
    if (!RefIsUInt(ref)) {
        InvalidCodepath();
        return;
    }
    
    U64* data = (U64*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(U64));
    *data = v;
}

void RefSetFloat(Reference ref, F64 v)
{
    if (!RefIsFloat(ref)) {
        InvalidCodepath();
        return;
    }
    
    F64* data = (F64*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(F64));
    *data = v;
}

void RefSetBool(Reference ref, B32 v)
{
    if (!RefIsBool(ref)) {
        InvalidCodepath();
        return;
    }
    
    B32* data = (B32*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(B32));
    *data = v;
}

void set_enum_index(Reference ref, I64 v)
{
    if (!is_enum(ref)) {
        InvalidCodepath();
        return;
    }
    
    I64* data = (I64*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(I64));
    *data = v;
}

ObjectData_String* ref_string_get_data(Runtime* runtime, Reference ref)
{
    if (!is_string(ref)) {
        InvalidCodepath();
        return NULL;
    }
    
    ObjectData_String* data = (ObjectData_String*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(ObjectData_String));
    
    return data;
}

void ref_string_prepare(Runtime* runtime, Reference ref, U64 new_size, B32 can_discard)
{
    ObjectData_String* data = ref_string_get_data(runtime, ref);
    if (data == NULL) return;
    
    if (data->capacity > 0 && new_size <= 0)
    {
        object_dynamic_free(runtime, data->chars);
        *data = {};
        return;
    }
    
    if (new_size <= data->capacity) return;
    
    data->capacity = Max(new_size, data->capacity * 2);
    
    char* old_chars = data->chars;
    char* new_chars = (char*)object_dynamic_allocate(runtime, data->capacity);
    
    if (!can_discard) MemoryCopy(new_chars, old_chars, data->size);
    
    object_dynamic_free(runtime, old_chars);
    data->chars = new_chars;
}

void ref_string_clear(Runtime* runtime, Reference ref)
{
    ObjectData_String* data = ref_string_get_data(runtime, ref);
    if (data == NULL) return;
    object_dynamic_free(runtime, data->chars);
    *data = {};
}

void ref_string_set(Runtime* runtime, Reference ref, String v)
{
    ObjectData_String* data = ref_string_get_data(runtime, ref);
    if (data == NULL) return;
    
    ref_string_prepare(runtime, ref, v.size, true);
    
    MemoryCopy(data->chars, v.data, v.size);
    data->size = v.size;
}

void ref_string_append(Runtime* runtime, Reference ref, String v)
{
    ObjectData_String* data = ref_string_get_data(runtime, ref);
    if (data == NULL) return;
    
    U64 new_size = data->size + v.size;
    ref_string_prepare(runtime, ref, new_size, false);
    
    MemoryCopy(data->chars + data->size, v.data, v.size);
    data->size = new_size;
}

void RefArrayFree(Runtime* runtime, Reference ref, U32 capacity)
{
    Program* program = runtime->program;
    ObjectData_Array* array = RefGetArray(ref);
    VType element_vtype = VTypeNext(program, ref.vtype);
    
    if (VTypeNeedsInternalRelease(program, element_vtype))
    {
        U32 element_size = VTypeGetSize(element_vtype);
        
        U8* it = array->data;
        U8* end = array->data + element_size * array->count;
        
        while (it < end)
        {
            Reference member = ref_from_address(ref.parent, element_vtype, it);
            ref_release_internal(runtime, member, true);
            it += element_size;
        }
    }
    
    object_dynamic_free(runtime, array->data);
    *array = {};
}

void RefArrayPrepare(Runtime* runtime, Reference ref, U32 capacity)
{
    Program* program = runtime->program;
    ObjectData_Array* array = RefGetArray(ref);
    
    if (array->capacity < capacity)
    {
        VType element_vtype = VTypeNext(program, ref.vtype);
        U32 element_size = VTypeGetSize(element_vtype);
        
        void* last_data = array->data;
        
        array->capacity = capacity;
        array->data = (U8*)object_dynamic_allocate(runtime, element_size * array->capacity);
        
        MemoryCopy(array->data, last_data, element_size * array->count);
        
        if (last_data) object_dynamic_free(runtime, last_data);
    }
}

void set_reference(Runtime* runtime, Reference ref, Reference src)
{
    Program* program = runtime->program;
    
    if (!is_reference(ref) || (!is_null(src) && !TypeEquals(program, VTypeNext(program, ref.vtype), src.vtype))) {
        InvalidCodepath();
        return;
    }
    
    ObjectData_Ref* data = (ObjectData_Ref*)ref.address;
    Assert(VTypeGetSize(ref.vtype) == sizeof(ObjectData_Ref));
    
    object_decrement_ref(data->parent);
    data->parent = src.parent;
    data->address = src.address;
    object_increment_ref(data->parent);
}

void RefSetSIntMember(Runtime* runtime, Reference ref, String member, I64 v)
{
    I32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(runtime, ref, index);
    if (is_unknown(member_ref)) return;
    else RefSetSInt(member_ref, v);
}

void RefSetUIntMember(Runtime* runtime, Reference ref, String member, U64 v)
{
    I32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(runtime, ref, index);
    if (is_unknown(member_ref)) return;
    else RefSetUInt(member_ref, v);
}

void ref_member_set_bool(Runtime* runtime, Reference ref, String member, B32 v)
{
    I32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(runtime, ref, index);
    if (is_unknown(member_ref)) return;
    else RefSetBool(member_ref, v);
}

void set_enum_index_member(Runtime* runtime, Reference ref, String member, I64 v)
{
    I32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(runtime, ref, index);
    if (is_unknown(member_ref)) return;
    if (is_null(member_ref)) {
        VariableTypeChild info = vtype_get_member(ref.vtype, member);
        ref_set_member(runtime, ref, index, AllocEnum(runtime, info.vtype, v));
    }
    else set_enum_index(member_ref, v);
}

void ref_member_set_string(Runtime* runtime, Reference ref, String member, String v)
{
    I32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(runtime, ref, index);
    if (is_unknown(member_ref)) return;
    if (is_null(member_ref)) ref_set_member(runtime, ref, index, AllocString(runtime, v));
    else ref_string_set(runtime, member_ref, v);
}

void RefMemberSetUInt(Runtime* runtime, Reference ref, String member, U64 v)
{
    I32 index = vtype_get_member(ref.vtype, member).index;
    Reference member_ref = ref_get_member(runtime, ref, index);
    if (is_unknown(member_ref)) return;
    else RefSetUInt(member_ref, v);
}

void ref_assign_Result(Runtime* runtime, Reference ref, Result res)
{
    Program* program = runtime->program;
    Assert(TypeEquals(program, ref.vtype, VType_Result));
    ref_member_set_string(runtime, ref, "message", res.message);
    RefSetSIntMember(runtime, ref, "code", res.code);
    ref_member_set_bool(runtime, ref, "failed", res.failed);
}

void ref_assign_CallOutput(Runtime* runtime, Reference ref, CallOutput res)
{
    Program* program = runtime->program;
    Assert(TypeEquals(program, ref.vtype, VType_CallOutput));
    ref_member_set_string(runtime, ref, "stdout", res.stdout);
}

void ref_assign_FileInfo(Runtime* runtime, Reference ref, FileInfo info)
{
    Program* program = runtime->program;
    Assert(TypeEquals(program, ref.vtype, VType_FileInfo));
    ref_member_set_string(runtime, ref, "path", info.path);
    ref_member_set_bool(runtime, ref, "is_directory", info.is_directory);
}

void ref_assign_FunctionDefinition(Runtime* runtime, Reference ref, FunctionDefinition* fn)
{
    Program* program = runtime->program;
    ref_member_set_string(runtime, ref, "identifier", fn->identifier);
    
    Reference parameters = AllocArray(runtime, VType_ObjectDefinition, fn->parameters.count);
    foreach(i, fn->parameters.count) {
        Reference param = ref_get_member(runtime, parameters, i);
        ref_assign_ObjectDefinition(runtime, param, fn->parameters[i]);
    }
    
    Reference returns = AllocArray(runtime, VType_ObjectDefinition, fn->returns.count);
    foreach(i, fn->returns.count) {
        Reference ret = ref_get_member(runtime, returns, i);
        ref_assign_ObjectDefinition(runtime, ret, fn->returns[i]);
    }
    
    ref_set_member(runtime, ref, TypeGetChild(program, ref.vtype, "parameters").index, parameters);
    ref_set_member(runtime, ref, TypeGetChild(program, ref.vtype, "returns").index, returns);
}

void ref_assign_StructDefinition(Runtime* runtime, Reference ref, VType vtype)
{
    Program* program = runtime->program;
    ref_member_set_string(runtime, ref, "identifier", VTypeGetName(program, vtype));
    
    Reference members = AllocArray(runtime, VType_ObjectDefinition, vtype._struct->names.count);
    foreach(i, vtype._struct->names.count) {
        Reference mem = ref_get_member(runtime, members, i);
        String name = vtype._struct->names[i];
        VType mem_vtype = vtype._struct->vtypes[i];
        ref_assign_ObjectDefinition(runtime, mem, ObjDefMake(name, mem_vtype, NO_CODE, false, ValueFromZero(mem_vtype)));
    }
    
    ref_set_member(runtime, ref, TypeGetChild(program, ref.vtype, "members").index, members);
}

void ref_assign_EnumDefinition(Runtime* runtime, Reference ref, VType vtype)
{
    Program* program = runtime->program;
    ref_member_set_string(runtime, ref, "identifier", VTypeGetName(program, vtype));
    
    Reference elements = AllocArray(runtime, VType_String, vtype._enum->names.count);
    Reference values = AllocArray(runtime, VType_Int, vtype._enum->names.count);
    foreach(i, vtype._enum->names.count) {
        Reference element = ref_get_member(runtime, elements, i);
        Reference value = ref_get_member(runtime, values, i);
        ref_string_set(runtime, element, vtype._enum->names[i]);
        RefSetSInt(value, vtype._enum->values[i]);
    }
    
    ref_set_member(runtime, ref, TypeGetChild(program, ref.vtype, "elements").index, elements);
    ref_set_member(runtime, ref, TypeGetChild(program, ref.vtype, "values").index, values);
}

void ref_assign_ObjectDefinition(Runtime* runtime, Reference ref, ObjectDefinition def)
{
    Program* program = runtime->program;
    ref_member_set_string(runtime, ref, "identifier", def.name);
    ref_member_set_bool(runtime, ref, "is_constant", def.is_constant);
    
    VariableTypeChild type_info = vtype_get_member(ref.vtype, "type");
    Reference type = ref_get_member(runtime, ref, type_info.index);
    ref_assign_Type(runtime, type, def.vtype);
}

void ref_assign_Type(Runtime* runtime, Reference ref, VType vtype)
{
    Program* program = runtime->program;
    Assert(TypeEquals(program, ref.vtype, VType_Type));
    ref_member_set_string(runtime, ref, "name", VTypeGetName(program, vtype));
}

Reference ref_from_Result(Runtime* runtime, Result res)
{
    Program* program = runtime->program;
    Reference ref = object_alloc(runtime, VType_Result);
    ref_assign_Result(runtime, ref, res);
    return ref;
}

Result Result_from_ref(Runtime* runtime, Reference ref)
{
    Program* program = runtime->program;
    
    if (!TypeEquals(program, ref.vtype, VType_Result)) {
        InvalidCodepath();
        return {};
    }
    
    Result res;
    res.message = get_string_member(runtime, ref, "message");
    res.code = (I32)get_int_member(runtime, ref, "code");
    res.failed = get_bool_member(runtime, ref, "failed");
    return res;
}

U32 object_generate_id(Runtime* runtime) {
    return ++runtime->object_id_counter;
}

Reference object_alloc(Runtime* runtime, VType vtype)
{
    PROFILE_FUNCTION;
    Program* program = runtime->program;
    Assert(VTypeValid(vtype));
    
    U32 ID = object_generate_id(runtime);
    
    LogMemory("Alloc obj(%u): %S", ID, VTypeGetName(program, vtype));
    
    Assert(VTypeGetSize(vtype) > 0);
    U32 type_size = sizeof(Object) + VTypeGetSize(vtype);
    
    Object* obj = NULL;
    
    obj = (Object*)object_dynamic_allocate(runtime, type_size);
    
    B32 use_gc = true;
    
    if (use_gc) {
        obj->next = runtime->gc.object_list;
        if (runtime->gc.object_list) runtime->gc.object_list->prev = obj;
        runtime->gc.object_list = obj;
        runtime->gc.object_count++;
    }
    
    obj->ID = ID;
    obj->vtype = vtype;
    obj->ref_count = 0;
    
    return ref_from_object(obj);
}

void object_free(Runtime* runtime, Object* obj, B32 release_internal_refs)
{
    Program* program = runtime->program;
    Assert(obj->ref_count == 0);
    
    LogMemory("Free obj(%u): %S", obj->ID, VTypeGetName(program, obj->vtype));
    
    B32 use_gc = true;
    
    if (use_gc)
    {
        if (obj == runtime->gc.object_list)
        {
            Assert(obj->prev == NULL);
            runtime->gc.object_list = obj->next;
            if (runtime->gc.object_list != NULL) runtime->gc.object_list->prev = NULL;
        }
        else
        {
            if (obj->next != NULL) obj->next->prev = obj->prev;
            obj->prev->next = obj->next;
        }
        runtime->gc.object_count--;
    }
    
    ref_release_internal(runtime, ref_from_object(obj), release_internal_refs);
    
    *obj = {};
    
    if (use_gc) {
        object_dynamic_free(runtime, obj);
    }
}

void object_increment_ref(Object* obj)
{
    if (obj == NULL || TypeIsNil(obj->vtype) || TypeIsVoid(obj->vtype)) return;
    obj->ref_count++;
}

void object_decrement_ref(Object* obj)
{
    if (obj == NULL || TypeIsNil(obj->vtype) || TypeIsVoid(obj->vtype)) return;
    obj->ref_count--;
    Assert(obj->ref_count >= 0);
}

void ref_release_internal(Runtime* runtime, Reference ref, B32 release_refs)
{
    PROFILE_FUNCTION;
    
    Program* program = runtime->program;
    VType vtype = ref.vtype;
    
    if (!VTypeNeedsInternalRelease(program, vtype)) return;
    
    if (vtype.kind == VKind_Array)
    {
        ObjectData_Array* array = RefGetArray(ref);
        VType element_vtype = VTypeNext(program, vtype);
        
        if (VTypeNeedsInternalRelease(program, element_vtype))
        {
            U32 element_size = VTypeGetSize(element_vtype);
            
            U8* it = array->data;
            U8* end = array->data + element_size * array->count;
            
            while (it < end)
            {
                Reference member = ref_from_address(ref.parent, element_vtype, it);
                ref_release_internal(runtime, member, release_refs);
                it += element_size;
            }
        }
        
        object_dynamic_free(runtime, array->data);
        *array = {};
    }
    else if (vtype.kind == VKind_Struct)
    {
        Array<VType> vtypes = vtype._struct->vtypes;
        
        foreach(i, vtypes.count)
        {
            U8* data = (U8*)ref.address;
            U32 offset = vtype._struct->offsets[i];
            
            Reference member = ref_from_address(ref.parent, vtypes[i], data + offset);
            ref_release_internal(runtime, member, release_refs);
        }
    }
    else if (vtype.kind == VKind_Reference && release_refs) {
        Reference deref = RefDereference(runtime, ref);
        object_decrement_ref(deref.parent);
    }
    else if (TypeIsString(vtype)) {
        ref_string_clear(runtime, ref);
    }
}

void RefCopy(Runtime* runtime, Reference dst, Reference src)
{
    PROFILE_FUNCTION;
    
    Program* program = runtime->program;
    
    if (is_unknown(dst) || is_null(dst)) {
        InvalidCodepath();
        return;
    }
    
    if (is_unknown(src) || is_null(src)) {
        InvalidCodepath();
        return;
    }
    
    if (!TypeEquals(program, src.vtype, dst.vtype)) {
        InvalidCodepath();
        return;
    }
    
    VType vtype = dst.vtype;
    
    if (vtype.kind == VKind_Struct)
    {
        foreach(i, vtype._struct->vtypes.count) {
            Reference dst_mem = ref_get_member(runtime, dst, i);
            Reference src_mem = ref_get_member(runtime, src, i);
            RefCopy(runtime, dst_mem, src_mem);
        }
    }
    else if (vtype.kind == VKind_Primitive || vtype.kind == VKind_Enum)
    {
        if (TypeIsString(vtype))
        {
            ref_string_set(runtime, dst, get_string(src));
        }
        else {
            I64* v0 = (I64*)dst.address;
            I64* v1 = (I64*)src.address;
            MemoryCopy(dst.address, src.address, VTypeGetSize(vtype));
        }
    }
    else if (vtype.kind == VKind_Array)
    {
        ref_release_internal(runtime, dst, true);
        
        ObjectData_Array* dst_array = RefGetArray(dst);
        ObjectData_Array* src_array = RefGetArray(src);
        
        U32 element_size = VTypeGetSize(VTypeNext(program, vtype));
        dst_array->capacity = src_array->count;
        dst_array->data = (U8*)object_dynamic_allocate(runtime, dst_array->capacity * element_size);
        dst_array->count = src_array->count;
        
        foreach(i, dst_array->count) {
            Reference dst_element = ref_get_member(runtime, dst, i);
            Reference src_element = ref_get_member(runtime, src, i);
            RefCopy(runtime, dst_element, src_element);
        }
    }
    else if (vtype.kind == VKind_Reference)
    {
        Reference dst_deref = RefDereference(runtime, dst);
        Reference src_deref = RefDereference(runtime, src);
        
        object_decrement_ref(dst_deref.parent);
        object_increment_ref(src_deref.parent);
        MemoryCopy(dst.address, src.address, VTypeGetSize(vtype));
    }
    else {
        InvalidCodepath();
    }
}

Reference ref_alloc_and_copy(Runtime* runtime, Reference src)
{
    if (is_unknown(src)) {
        InvalidCodepath();
        return ref_from_object(nil_obj);
    }
    
    if (is_null(src)) return ref_from_object(null_obj);
    
    Reference dst = object_alloc(runtime, src.vtype);
    RefCopy(runtime, dst, src);
    return dst;
}

void* object_dynamic_allocate(Runtime* runtime, U64 size)
{
    PROFILE_FUNCTION;
    
    if (size == 0) return NULL;
    
    B32 use_gc = true;
    
    if (use_gc) {
        return gc_allocate(runtime, size);
    }
    else {
#if 0
        Arena* arena = NULL;
        if (memory == ObjectMemory_Temp) arena = runtime->temp_arena;
        else if (memory == ObjectMemory_Static) arena = yov->static_arena;
        else {
            InvalidCodepath();
            return NULL;
        }
        
        return ArenaPush(arena, size);
#endif
    }
    
    return NULL;
}

void object_dynamic_free(Runtime* runtime, void* ptr)
{
    PROFILE_FUNCTION;
    
    if (ptr == NULL) return;
    
    B32 use_gc = true;
    
    if (use_gc) {
        gc_free(runtime, ptr);
    }
    else {
        //Assert(memory != ObjectMemory_Static);
    }
}

void object_free_unused_memory(Runtime* runtime)
{
    //arena_pop_to(runtime->temp_arena, 0);
    gc_free_unused(runtime);
}

void ObjectFreeAll(Runtime* runtime)
{
    PROFILE_FUNCTION;
    
    Object* obj = runtime->gc.object_list;
    
    while (obj != NULL)
    {
        Object* next = obj->next;
        obj->ref_count = 0;
        object_free(runtime, obj, false);
        obj = next;
    }
}

void* gc_allocate(Runtime* runtime, U64 size)
{
    PROFILE_FUNCTION;
    runtime->gc.allocation_count++;
    return OsHeapAllocate(size);
}

void gc_free(Runtime* runtime, void* ptr)
{
    PROFILE_FUNCTION;
    Assert(runtime->gc.allocation_count > 0);
    runtime->gc.allocation_count--;
    OsHeapFree(ptr);
}

void gc_free_unused(Runtime* runtime)
{
    PROFILE_FUNCTION;
    U32 free_count;
    do {
        Object* obj = runtime->gc.object_list;
        free_count = 0;
        
        while (obj != NULL)
        {
            Object* next = obj->next;
            if (obj->ref_count == 0) {
                object_free(runtime, obj, true);
                free_count++;
            }
            obj = next;
        }
    }
    while (free_count != 0);
}

void LogMemoryUsage(Runtime* runtime)
{
    LogMemory(SEPARATOR_STRING);
    
    Object* obj = runtime->gc.object_list;
    while (obj != NULL)
    {
        Object* next = obj->next;
        LogMemory("Obj %u: %u refs", obj->ID, obj->ref_count);
        obj = next;
    }
    
    LogMemory("Object Count: %u", runtime->gc.object_count);
    LogMemory("Alloc Count: %u", runtime->gc.allocation_count);
    LogMemory(SEPARATOR_STRING);
}
