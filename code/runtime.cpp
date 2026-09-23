#include "runtime.h"

internal_fn Object _MakeObject(Type* type) {
    Object obj{};
    obj.type = type;
    return obj;
}

read_only Object _nil_obj = _MakeObject(nil_type);
read_only Object _null_obj = _MakeObject(void_type);

Object* nil_obj = &_nil_obj;
Object* null_obj = &_null_obj;

void ExecuteProgram(RBuffer binary, Input* input, Reporter* reporter, RuntimeSettings settings)
{
    PROFILE_FRAME_MARK;
    
    Runtime* runtime = RuntimeAlloc(binary, input, reporter, settings);
    
    RuntimeInitializeGlobals(runtime);
    
    RuntimeStart(runtime);
    RuntimeStepAll(runtime);
    
    RuntimeFree(runtime);
}

struct RuntimeScript {
    String path;
};

internal_fn RuntimeScript ReadRuntimeScript(Arena* arena, Deserializer* s)
{
    RuntimeScript dst = {};
    U32 version = ReadVersionU32(s, 0, 0);
    dst.path = ReadString(arena, s);
    return dst;
}

internal_fn FunctionBody ReadFunctionBody(Arena* arena, Deserializer* s)
{
    FunctionBody dst = {};

    U32 version = ReadVersionU32(s, 0, 0);

    dst.header_index = ReadU32(s);
    dst.ir = ReadIR(arena, s);
    return dst;
}

internal_fn FunctionHeader ReadFunctionHeader(Arena* arena, Deserializer* s)
{
    U32 version = ReadVersionU32(s, 0, 0);
    
    FunctionHeader dst = {};

    dst.name = ReadString(arena, s);
    dst.parameters = ReadArrayArena<ObjectDefinition>(arena, s, ReadObjectDefinition);
    dst.returns = ReadArrayArena<ObjectDefinition>(arena, s, ReadObjectDefinition);
    
    Location location = ReadLocation(s);
    return dst;
}

internal_fn StructDefinition ReadStructDefinition(Arena* arena, Deserializer* s)
{
    U32 version = ReadVersionU32(s, 0, 0);

    StructDefinition dst = {};
    dst.name = ReadString(arena, s);

    dst.generic_names = ReadArrayArena<String>(arena, s, ReadString);
    dst.generic_types = ReadArray<U32>(arena, s, ReadU32);
    dst.has_generics = ReadB8(s);
    dst.names = ReadArrayArena<String>(arena, s, ReadString);
    dst.types = ReadArray<U32>(arena, s, ReadU32);

    Location location = ReadLocation(s);
    return dst;
}

internal_fn EnumDefinition ReadEnumDefinition(Arena* arena, Deserializer* s)
{
    U32 version = ReadVersionU32(s, 0, 0);

    EnumDefinition dst = {};
    
    dst.name = ReadString(arena, s);
    dst.names = ReadArrayArena<String>(arena, s, ReadString);
    dst.values = ReadArray<I64>(arena, s, ReadI64);
    
    Location location = ReadLocation(s);
    return dst;
}

internal_fn ArgDefinition ReadArgDefinition(Arena* arena, Deserializer* s)
{
    U32 version = ReadVersionU32(s, 0, 0);

    ArgDefinition dst = {};
    
    dst.global_index = ReadI32(s);
    dst.name = ReadString(arena, s);
    dst.required = ReadB8(s);
    
    Location location = ReadLocation(s);
    return dst;
}

internal_fn void CalculateStructMemoryLayout(Runtime* runtime, U32 index)
{
    TypeSystem* tsys = runtime->tsys;

    StructDefinition* def = &runtime->structs[index];
    if (def->has_generics) return;

    U32 member_count = def->types.count;

    if (def->offsets.count == member_count) return;
    Assert(def->size == 0);

    def->offsets = ArrayAlloc<U32>(runtime->arena, member_count);
    def->needs_internal_release = false;

    foreach(i, member_count)
    {
        Type* type = TypeGet(def->types[i]);

        if (type->kind == VKind_Struct) {
            CalculateStructMemoryLayout(runtime, type->definition_index);
        }

        def->offsets[i] = def->size;
        def->size += (U32)TypeGetSize(runtime, type);
        def->needs_internal_release |= TypeNeedsInternalRelease(runtime, type);
    }
}

internal_fn B32 RuntimeReadBinary(Runtime* runtime, RBuffer binary)
{
    Arena* temp_arena = context.arena;

    ArenaCapture(temp_arena);
    Deserializer* s = DeserializerAlloc(temp_arena, binary);

    Arena* arena = runtime->arena;

    U32 version = ReadVersionU32(s, 0, 0);
    if (s->failed) return false;

    String main_script_path = ReadStringView(s);

    Array<RuntimeScript> scripts = ReadArrayArena<RuntimeScript>(temp_arena, s, ReadRuntimeScript);

    // Read type table
    {
        U32 count = ReadU32(s);
        for (U32 i = 0; i < count; i++) {
            ReadType(runtime->tsys, s);
        }
    }

    runtime->global_objects = ReadArrayArena<ObjectDefinition>(arena, s, ReadObjectDefinition);

    runtime->functions = ReadArrayArena<FunctionBody>(arena, s, ReadFunctionBody);
    runtime->function_headers = ReadArrayArena<FunctionHeader>(arena, s, ReadFunctionHeader);
    runtime->structs = ReadArrayArena<StructDefinition>(arena, s, ReadStructDefinition);
    runtime->enums = ReadArrayArena<EnumDefinition>(arena, s, ReadEnumDefinition);
    runtime->args = ReadArrayArena<ArgDefinition>(arena, s, ReadArgDefinition);
    // TODO(Jose): ReadArgs
    // TODO(Jose): ReadGlobals

    // Solve struct sizes & offsets
    foreach(i, runtime->structs.count) {
        CalculateStructMemoryLayout(runtime, i);
    }

    runtime->global_registers = ArrayAlloc<Reference>(runtime->arena, runtime->global_objects.count);
    foreach(i, runtime->global_objects.count) {
        runtime->global_registers[i] = ref_from_object(null_obj);
    }
    
    return !s->failed;
}

Runtime* RuntimeAlloc(RBuffer binary, Input* input, Reporter* reporter, RuntimeSettings settings)
{
    Arena* arena = ArenaAlloc(Gb(16), 8, "Arena Runtime");

    Runtime* runtime = ArenaPushStruct<Runtime>(arena);
    runtime->arena = arena;
    runtime->settings = RuntimeSettingsCopy(arena, settings);
    runtime->input = input;
    runtime->reporter = reporter;
    runtime->stack = ArrayAlloc<Scope>(arena, 4096);
    runtime->tsys = TypeSystemAlloc(arena);

    if (!RuntimeReadBinary(runtime, binary)) {
        ReportErrorRT("Program binary is not valid");
    }

    runtime->started_time = OsTimerGet();
    
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
    
}

void RuntimeStart(Runtime* runtime)
{
    PROFILE_FUNCTION;

    TypeSystem* tsys = runtime->tsys;
    Reporter* reporter = runtime->reporter;
    
    if (reporter->exit_requested) return;
    
    LogFlow("Starting Execution");
    LogFlow(SEPARATOR_STRING);

    if (runtime->functions.count == 0) {
        ReportErrorNoCode("Entry point not found");
        return;
    }
    
    FunctionBody* fn = &runtime->functions[0];
    FunctionHeader* header = &runtime->function_headers[fn->header_index];
    
    if (header->returns.count != 0 || header->parameters.count != 0) {
        ReportErrorNoCode("Invalid entry point, expected a function with no returns and params");
        return;
    }

    if (fn->ir.instructions.count > 0)
        RunFunction(runtime, -1, fn, header, {});
}

void RuntimePushScope(Runtime* runtime, I32 return_index, U32 return_count, IR ir, Array<Value> params)
{
    PROFILE_FUNCTION;

    U32 parameter_count = 0;
    foreach(i, ir.local_registers.count) {
        if (ir.local_registers[i].kind == RegisterKind_Parameter) parameter_count++;
    }
    Assert(parameter_count == params.count);
    
    if (runtime->stack_counter >= runtime->stack.count) {
        ReportStackOverflow();
        return;
    }
    

    Reporter* reporter = runtime->reporter;
    
    Scope* prev_scope = RuntimeGetCurrentScope(runtime);
    
    // Push new scope
    Scope* scope = &runtime->stack[runtime->stack_counter++];
    *scope = {};
    scope->return_index = return_index;
    scope->return_count = return_count;
    scope->ir = ir;
    scope->registers = ArrayAlloc<Reference>(context.arena, ir.local_registers.count);
    foreach(i, scope->registers.count) {
        I32 register_index = RegIndexFromLocal(i);
        RuntimeStore(runtime, scope, register_index, ref_from_object(null_obj));
    }
    
    // Define params
    {
        U32 param_index = 0;
        
        foreach(i, ir.local_registers.count)
        {
            Register reg = ir.local_registers[i];
            if (reg.kind != RegisterKind_Parameter) continue;
            
            I32 register_index = RegIndexFromLocal(i);
            Value param = params[param_index++];
            Reference ref = RefFromValue(runtime, prev_scope, param);
            
            if (is_null(RuntimeLoad(runtime, scope, register_index))) {
                RuntimeStore(runtime, scope, register_index, object_alloc(runtime, ref.type));
            }
            
            RunCopy(runtime, register_index, ref);
        }
    }
}

void RuntimePopScope(Runtime* runtime)
{
    PROFILE_FUNCTION;
    

    Reporter* reporter = runtime->reporter;
    
    if (runtime->stack_counter == 0) {
        ReportStackIsBroken();
        return;
    }
    
    Scope* scope = &runtime->stack[--runtime->stack_counter];
    
    Array<Reference> output = ArrayAlloc<Reference>(context.arena, scope->return_count);
    foreach(i, output.count) {
        output[i] = ref_from_object(null_obj);
    }
    
    // Retrieve return value
    {
        Array<Value> returns = ValuesFromReturn(context.arena, scope->ir.output_value, false);
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
    return "TODO";  // TODO(Jose): scope->ir.path;
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
    TypeSystem* tsys = runtime->tsys;
    I32 index = TypeGetMember(runtime, Type_Context, "cd").index;
    return RefGetMember(runtime, runtime->common_globals.context, index);
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
    TypeChild info = TypeGetMember(runtime, calls.type, "redirect_stdout");
    Reference redirect_stdout = RefGetMember(runtime, calls, info.index);
    return (RedirectStdout)get_enum_index(redirect_stdout);
}

Reference RefFromValue(Runtime* runtime, Scope* scope, Value value)
{
    PROFILE_FUNCTION;
    
    TypeSystem* tsys = runtime->tsys;
    Type* type = TypeGet(value.type_id);
    
    if (value.kind == ValueKind_None) return ref_from_object(null_obj);
    if (value.kind == ValueKind_Literal) {
        PROFILE_SCOPE("Literal");
        if (type == int_type) return AllocSInt(runtime, value.literal_sint);
        if (type == uint_type) return AllocUInt(runtime, value.literal_sint);
        if (type == bool_type) return AllocBool(runtime, value.literal_bool);
        if (type == float_type) return AllocFloat(runtime, value.literal_float);
        if (type == string_type) return AllocString(runtime, value.literal_string);
        if (type == void_type) return ref_from_object(null_obj);
        if (type == type_type) {
            Reference ref = object_alloc(runtime, type_type);
            RefSetType(runtime, ref, value.literal_type_id);
            return ref;
        }
        if (type->kind == VKind_Enum) {
            return AllocEnum(runtime, type, value.literal_sint);
        }
        InvalidCodepath();
        return ref_from_object(nil_obj);
    }
    
    if (value.kind == ValueKind_Array)
    {
        PROFILE_SCOPE("Array");
        
        Array<Value> values = value.array.values;
        
        Type* element_type = TypeGetNext(tsys, type);
        Reference array = AllocArray(runtime, element_type, values.count);
        
        foreach(i, values.count) {
            ref_set_member(runtime, array, i, RefFromValue(runtime, scope, values[i]));
        }
        return array;
    }
    
    if (value.kind == ValueKind_ZeroInit) {
        PROFILE_SCOPE("ZeroInit");
        return object_alloc(runtime, type);
    }
    
    if (value.kind == ValueKind_StringComposition)
    {
        PROFILE_SCOPE("StringComposition");
        if (type == string_type)
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

Value ValueFromStringExpression(Arena* arena, Runtime* runtime, String str, Type* type)
{
    if (str.size <= 0) return ValueNone();
    // TODO(Jose): if (StrEquals(str, "null")) return value_null();
    
    if (type == bool_type) {
        if (str == "true" || str == "1") return ValueFromBool(true);
        if (str == "false" || str == "0") return ValueFromBool(false);
    }
    
    if (type == int_type) {
        I64 value;
        if (!I64FromString(&value, str)) return ValueNone();
        return ValueFromInt(value);
    }
    
    if (type == uint_type) {
        U64 value;
        if (!U64FromString(&value, str)) return ValueNone();
        return ValueFromUInt(value);
    }
    
    if (type == float_type) {
        F64 value;
        if (!F64FromString(&value, str)) return ValueNone();
        return ValueFromFloat(value);
    }
    
    if (type == string_type) {
        return ValueFromString(arena, str);
    }
    
    if (TypeIsEnum(type))
    {
        U64 start_name = 0;
        if (str[0] == '.') {
            start_name = 1;
        }
        else if (StrStarts(str, StrFormat(context.arena, "%S.", type->name))) {
            start_name = type->name.size + 1;
        }
        
        String enum_name = StrSub(str, start_name, str.size - start_name);
        EnumDefinition* enum_def = &runtime->enums[type->definition_index];
        
        foreach(i, enum_def->names.count) {
            if (StrEquals(enum_def->names[i], enum_name)) return ValueFromEnum(type, i);
        }

        String enum_name_lower = StrToLower(context.arena, enum_name);
        
        foreach(i, enum_def->names.count) {
            ArenaCapture(context.arena);
            String v = StrToLower(context.arena, enum_def->names[i]);
            if (StrEquals(v, enum_name_lower)) return ValueFromEnum(type, i);
        }
        return ValueNone();
    }
    
    return ValueNone();
}

U64 TypeGetSize(Runtime* runtime, Type* type)
{
    if (type->kind == VKind_Primitive)
    {
        switch (type->primitive)
        {
        case PrimitiveType_Bool: return sizeof(B32);
        case PrimitiveType_Float: return sizeof(F64);
        case PrimitiveType_Int: return sizeof(I64);
        case PrimitiveType_UInt: return sizeof(U64);
        case PrimitiveType_String: return sizeof(ObjectData_String);
        case PrimitiveType_Type: return sizeof(U32);
        }
    }

    if (type->kind == VKind_Struct)
    {
        StructDefinition* struct_def = &runtime->structs[type->definition_index];
        return struct_def->size;
    }

    if (type->kind == VKind_Enum) {
        return sizeof(I64);
    }

    if (type->kind == VKind_Reference) {
        return sizeof(ObjectData_Ref);
    }

    if (type->kind == VKind_Array) {
        return sizeof(ObjectData_Array);
    }

    InvalidCodepath();
    return 0;
}

B32 TypeNeedsInternalRelease(Runtime* runtime, Type* type)
{
    if (TypeIsStruct(type)) {
        StructDefinition* struct_def = &runtime->structs[type->definition_index];
        return struct_def->needs_internal_release;
    }
    
    return TypeIsArray(type) || type == string_type;
}

TypeChild TypeGetMember(Runtime* runtime, Type* type, String member)
{
    TypeSystem* tsys = runtime->tsys;

    if (type->kind == VKind_Struct) {
        StructDefinition* def = &runtime->structs[type->definition_index];
        foreach(i, def->names.count) {
            if (def->names[i] == member) {
                return TypeChildMake(TypeGet(def->types[i]), def->names[i], i, false);
            }
        }
    }
    
    return TypeChildMake(nil_type, "", -1, false);
}

I32 GlobalIndexFromName(Runtime* runtime, String name)
{
    foreach(i, runtime->global_objects.count) {
        ObjectDefinition* global = &runtime->global_objects[i];
        if (global->name == name) return i;
    }
    return -1;
}

FunctionBody* FunctionBodyFromCall(Runtime* runtime, FunctionHeader* header)
{
    U32 header_index = (U32)(header - runtime->function_headers.data);

    foreach(i, runtime->functions.count) {
        if (runtime->functions[i].header_index == header_index) return &runtime->functions[i];
    }
    return NULL;
}

void RunInstruction(Runtime* runtime, Unit unit)
{
    PROFILE_FUNCTION;

    Reporter* reporter = runtime->reporter;
    TypeSystem* tsys = runtime->tsys;
    
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
            FunctionHeader* fn = &runtime->function_headers[unit.function_call.header_index];
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
            B32 is_property = unit.child.child_is_property;
            RunChild(runtime, dst_index, src0, src1, is_property);
            return;
        }
        
        case UnitKind_ResultEval:
        {
            Reference src = src0;
            
            Assert(src.type == Type_Result);
            
            if (src.type == Type_Result) {
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
        case UnitKind_count:
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
    
    TypeSystem* tsys = runtime->tsys;
    Reporter* reporter = runtime->reporter;
    
    Reference dst = RuntimeLoad(runtime, RuntimeGetCurrentScope(runtime), dst_index);
    
    if (is_unknown(dst) || is_unknown(src)) {
        InvalidCodepath();
        return;
    }
    
    Type* src_type = src.type;
    
    if (is_null(dst) || is_null(src)) {
        ReportNullRef();
        return;
    }
    
    if (TypeIsReference(dst.type) && TypeGetNext(tsys, dst.type) == src_type) {
        dst = RefDereference(runtime, dst);
        
        if (is_null(dst)) {
            ReportNullRef();
            return;
        }
    }
    
    RefCopy(runtime, dst, src);
}


void RunFunctionCall(Runtime* runtime, I32 dst_index, FunctionHeader* fn, Array<Value> parameters)
{
    PROFILE_FUNCTION;
    

    Reporter* reporter = runtime->reporter;

    FunctionBody* body = FunctionBodyFromCall(runtime, fn);

    if (body != NULL) {
        RunFunction(runtime, dst_index, body, fn, parameters);
        return;
    }

    IntrinsicFunction* intrinsic = IntrinsicFromName(fn->name);
    
    if (intrinsic != NULL) {
        RunIntrinsic(runtime, dst_index, intrinsic, fn, parameters);
        return;
    }
    
    report_intrinsic_not_resolved(fn->name);
}

void RunFunction(Runtime* runtime, I32 dst_index, FunctionBody* fn, FunctionHeader* header, Array<Value> parameters)
{
    RuntimePushScope(runtime, dst_index, header->returns.count, fn->ir, parameters);
}

void RunIntrinsic(Runtime* runtime, I32 dst_index, IntrinsicFunction* intrinsic, FunctionHeader* header, Array<Value> parameters)
{

    Reporter* reporter = runtime->reporter;

    Array<Reference> returns = ArrayAlloc<Reference>(context.arena, header->returns.count);
    
    Array<Reference> params = ArrayAlloc<Reference>(context.arena, parameters.count);
    foreach(i, params.count) {
        params[i] = RefFromValue(runtime, RuntimeGetCurrentScope(runtime), parameters[i]);
    }
    
    intrinsic(runtime, header, params, returns);
    
    foreach(i, returns.count) {
        Assert(is_valid(returns[i]));
    }
    
    RuntimeStoreReturn(runtime, NULL, dst_index, returns);
}

internal_fn Reference _RunChild(Runtime* runtime, Reference src, Reference index, B32 is_property)
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
    U32 child_count = RefGetChildCount(runtime, src, is_property);
    
    if (child_index >= child_count) {
        ReportErrorRT("Out of bounds");
        return ref_from_object(null_obj);
    }
    
    return RefGetChild(runtime, src, (U32)child_index, is_property);
}

void RunChild(Runtime* runtime, I32 dst_index, Reference src, Reference index, B32 is_property)
{
    PROFILE_FUNCTION;
    Reference child = _RunChild(runtime, src, index, is_property);
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
        case PrimitiveType_Type:
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
        case PrimitiveType_Type:
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
        case PrimitiveType_Type:
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
        case PrimitiveType_Type:
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
        case PrimitiveType_Type:
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
    
    Type* type = TypeFromPrimitive(ptype);
    
    switch (left.type->primitive)
    {
        case PrimitiveType_Int:  return AllocBool(runtime, RefGetInt(left) == RefGetInt(right));
        case PrimitiveType_UInt:  return AllocBool(runtime, RefGetUInt(left) == RefGetUInt(right));
        case PrimitiveType_Float:  return AllocBool(runtime, RefGetFloat(left) == RefGetFloat(right));
        case PrimitiveType_Bool:  return AllocBool(runtime, RefGetBool(left) == RefGetBool(right));
        case PrimitiveType_Type:  return AllocBool(runtime, RefGetType(runtime, left) == RefGetType(runtime, right));
        
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
    
    Type* type = TypeFromPrimitive(ptype);
    
    switch (left.type->primitive)
    {
        case PrimitiveType_Int:  return AllocBool(runtime, RefGetInt(left) != RefGetInt(right));
        case PrimitiveType_UInt:  return AllocBool(runtime, RefGetUInt(left) != RefGetUInt(right));
        case PrimitiveType_Float:  return AllocBool(runtime, RefGetFloat(left) != RefGetFloat(right));
        case PrimitiveType_Bool:  return AllocBool(runtime, RefGetBool(left) != RefGetBool(right));
        case PrimitiveType_Type:  return AllocBool(runtime, RefGetType(runtime, left) != RefGetType(runtime, right));
        
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
    
    Type* type = TypeFromPrimitive(ptype);
    
    switch (left.type->primitive)
    {
        case PrimitiveType_Int:  return AllocBool(runtime, RefGetInt(left) > RefGetInt(right));
        case PrimitiveType_UInt:  return AllocBool(runtime, RefGetUInt(left) > RefGetUInt(right));
        case PrimitiveType_Float:  return AllocBool(runtime, RefGetFloat(left) > RefGetFloat(right));
        case PrimitiveType_Bool:  return AllocBool(runtime, RefGetBool(left) > RefGetBool(right));
        
        case PrimitiveType_String:
        case PrimitiveType_Type:
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
    
    Type* type = TypeFromPrimitive(ptype);
    
    switch (left.type->primitive)
    {
        case PrimitiveType_Int:  return AllocBool(runtime, RefGetInt(left) < RefGetInt(right));
        case PrimitiveType_UInt:  return AllocBool(runtime, RefGetUInt(left) < RefGetUInt(right));
        case PrimitiveType_Float:  return AllocBool(runtime, RefGetFloat(left) < RefGetFloat(right));
        case PrimitiveType_Bool:  return AllocBool(runtime, RefGetBool(left) < RefGetBool(right));
        
        case PrimitiveType_String:
        case PrimitiveType_Type:
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
    
    Type* type = TypeFromPrimitive(ptype);
    
    switch (left.type->primitive)
    {
        case PrimitiveType_Int:  return AllocBool(runtime, RefGetInt(left) >= RefGetInt(right));
        case PrimitiveType_UInt:  return AllocBool(runtime, RefGetUInt(left) >= RefGetUInt(right));
        case PrimitiveType_Float:  return AllocBool(runtime, RefGetFloat(left) >= RefGetFloat(right));
        case PrimitiveType_Bool:  return AllocBool(runtime, RefGetBool(left) >= RefGetBool(right));
        
        case PrimitiveType_String:
        case PrimitiveType_Type:
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
    
    Type* type = TypeFromPrimitive(ptype);
    
    switch (left.type->primitive)
    {
        case PrimitiveType_Int:  return AllocBool(runtime, RefGetInt(left) <= RefGetInt(right));
        case PrimitiveType_UInt:  return AllocBool(runtime, RefGetUInt(left) <= RefGetUInt(right));
        case PrimitiveType_Float:  return AllocBool(runtime, RefGetFloat(left) <= RefGetFloat(right));
        case PrimitiveType_Bool:  return AllocBool(runtime, RefGetBool(left) <= RefGetBool(right));
        
        case PrimitiveType_String:
        case PrimitiveType_Type:
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
    
    Type* type = TypeFromPrimitive(ptype);
    
    switch (left.type->primitive)
    {
        case PrimitiveType_Int:  return AllocBool(runtime, RefGetInt(left) || RefGetInt(right));
        case PrimitiveType_UInt:  return AllocBool(runtime, RefGetUInt(left) || RefGetUInt(right));
        case PrimitiveType_Float:  return AllocBool(runtime, RefGetFloat(left) || RefGetFloat(right));
        case PrimitiveType_Bool:  return AllocBool(runtime, RefGetBool(left) || RefGetBool(right));
        
        case PrimitiveType_String:
        case PrimitiveType_Type:
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
    
    Type* type = TypeFromPrimitive(ptype);
    
    switch (left.type->primitive)
    {
        case PrimitiveType_Int:  return AllocBool(runtime, RefGetInt(left) && RefGetInt(right));
        case PrimitiveType_UInt:  return AllocBool(runtime, RefGetUInt(left) && RefGetUInt(right));
        case PrimitiveType_Float:  return AllocBool(runtime, RefGetFloat(left) && RefGetFloat(right));
        case PrimitiveType_Bool:  return AllocBool(runtime, RefGetBool(left) && RefGetBool(right));
        
        case PrimitiveType_String:
        case PrimitiveType_Type:
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
    
    Type* type = TypeFromPrimitive(ptype);
    
    switch (src.type->primitive)
    {
        case PrimitiveType_Int:  return AllocSInt(runtime, -RefGetInt(src));
        case PrimitiveType_UInt:  return AllocSInt(runtime, -(I64)RefGetUInt(src));
        case PrimitiveType_Float:  return AllocFloat(runtime, -RefGetFloat(src));
        
        case PrimitiveType_Bool:
        case PrimitiveType_String:
        case PrimitiveType_Type:
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
    
    Type* type = TypeFromPrimitive(ptype);
    
    switch (src.type->primitive)
    {
        case PrimitiveType_Int:  return AllocBool(runtime, !RefGetInt(src));
        case PrimitiveType_UInt:  return AllocBool(runtime, !RefGetUInt(src));
        case PrimitiveType_Float:  return AllocBool(runtime, !RefGetFloat(src));
        case PrimitiveType_Bool:  return AllocBool(runtime, !RefGetBool(src));
        
        case PrimitiveType_String:
        case PrimitiveType_Type:
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
case PrimitiveType_Type: break; \
} } while (0);

internal_fn Reference _RunCast(Runtime* runtime, PrimitiveType ptype, Reference src)
{
    if (is_null(src)) {
        ReportNullRef();
        return ref_from_object(null_obj);
    }
    
    Type* dst_type = TypeFromPrimitive(ptype);
    
    switch (src.type->primitive)
    {
        case PrimitiveType_Int: _Cast(I64, SInt);
        case PrimitiveType_UInt: _Cast(U64, UInt);
        case PrimitiveType_Bool: _Cast(B32, Bool);
        case PrimitiveType_Float: _Cast(F64, Float);
        
        case PrimitiveType_String:
        case PrimitiveType_Type:
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
    
    if (ptype == PrimitiveType_String || src.type->primitive == PrimitiveType_String)
    {
        InvalidCodepath();
        return ref_from_object(null_obj);
    }

    if (ptype == PrimitiveType_Type || src.type->primitive == PrimitiveType_Type)
    {
        InvalidCodepath();
        return ref_from_object(null_obj);
    }
    
    Type* dst_type = TypeFromPrimitive(ptype);
    Type* src_type = src.type;
    
    Reference dst = object_alloc(runtime, dst_type);
    
    U64 dst_size = TypeGetSize(runtime, dst_type);
    U64 src_size = TypeGetSize(runtime, src_type);
    
    void* dst_address = dst.address;
    void* src_address = src.address;
    
    MemoryZero(dst_address, dst_size);
    U64 copy_size = Min(src_size, dst_size);
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
    

    TypeSystem* tsys = runtime->tsys;
    
    if (right.type != type_type) {
        ReportErrorRT("Right value is not a Type");
        return AllocBool(runtime, false);
    }
    
    Type* type = RefGetType(runtime, right);
    
    return AllocBool(runtime, left.type == type);
}

void RunIs(Runtime* runtime, I32 dst_index, Reference left, Reference right)
{
    PROFILE_FUNCTION;
    Reference result = _RunIs(runtime, left, right);
    RuntimeStore(runtime, NULL, dst_index, result);
}

#if 0// TODO(Jose): 

Reference RunBinaryOperation(Runtime* runtime, Type* dst_type, Reference left, Reference right)
{
    
    Type* left_type = left.type;
    Type* right_type = right.type;
    
    B32 can_reuse_left = dst.address == left.address;
    
    if (is_reference(left) && TypeEquals(program, left.type, right.type))
    {
        void* v0 = RefDereference(runtime, left).address;
        void* v1 = RefDereference(runtime, right).address;
        
        if (op == OperatorKind_Equals) return AllocBool(runtime, v0 == v1);
        if (op == OperatorKind_NotEquals) return AllocBool(runtime, v0 != v1);
    }
    
    if (TypeEquals(program, left_type, Type_Type) && TypeEquals(program, right_type, Type_Type))
    {
        I32 index = type_get_member(Type_Type, "name").index;
        
        String left_name = get_string(RefGetMember(runtime, left, index));
        String right_name = get_string(RefGetMember(runtime, right, index));
        
        Type* left = TypeFromName(program, left_name);
        Type* right = TypeFromName(program, right_name);
        
        if (op == OperatorKind_Equals) {
            return AllocBool(runtime, TypeEquals(program, left, right));
        }
        else if (op == OperatorKind_NotEquals) {
            return AllocBool(runtime, !TypeEquals(program, left, right));
        }
    }
    
    if (TypeEquals(program, right_type, Type_Type))
    {
        I32 index = type_get_member(Type_Type, "name").index;
        
        String name = get_string(RefGetMember(runtime, right, index));
        
        Type* type = TypeFromName(program, name);
        
        if (op == OperatorKind_Is) {
            return AllocBool(runtime, TypeEquals(program, type, left.type));
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
    
    if (TypeIsArray(left_type) && TypeIsArray(right_type) && TypeEquals(program, TypeGetNext(program, left_type), TypeGetNext(program, right_type)))
    {
        Type* element_type = TypeGetNext(program, left_type);
        
        I32 left_count = get_array(left)->count;
        I32 right_count = get_array(right)->count;
        
        if (op == OperatorKind_Addition) {
            Reference array = AllocArray(runtime, element_type, left_count + right_count);
            for (U32 i = 0; i < left_count; ++i) {
                Reference src = RefGetChild(runtime, left, i, true);
                ref_set_member(runtime, array, i, src);
            }
            for (U32 i = 0; i < right_count; ++i) {
                Reference src = RefGetChild(runtime, right, i, true);
                ref_set_member(runtime, array, left_count + i, src);
            }
            return array;
        }
    }
    
    if ((left_type->kind == VKind_Array && right_type->kind != VKind_Array) || (left_type->kind != VKind_Array && right_type->kind == VKind_Array))
    {
        Type* array_type = (left_type->kind == VKind_Array) ? left_type : right_type;
        Type* element_type = (left_type->kind == VKind_Array) ? right_type : left_type;
        
        if (!TypeEquals(program, TypeGetNext(program, array_type), element_type)) {
            ReportInvalidOp();
            return ref_from_object(nil_obj);
        }
        
        Reference array_src = (left_type->kind == VKind_Array) ? left : right;
        Reference element = (left_type->kind == VKind_Array) ? right : left;
        
        I32 array_src_count = get_array(array_src)->count;
        Reference array = AllocArray(runtime, element_type, array_src_count + 1);
        
        I32 array_offset = (left_type->kind == VKind_Array) ? 0 : 1;
        
        for (I32 i = 0; i < array_src_count; ++i) {
            Reference src = RefGetChild(runtime, array_src, i, true);
            ref_set_member(runtime, array, i + array_offset, src);
        }
        
        I32 element_offset = (left_type->kind == VKind_Array) ? array_src_count : 0;
        ref_set_member(runtime, array, element_offset, element);
        return array;
    }
    
    ReportInvalidOp();
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

Reference RunUnaryOperation(Runtime* runtime, Type* dst_type, Reference src, UnaryOperation op)
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
    
    if (scope == NULL) scope = RuntimeGetCurrentScope(runtime);
    
    I32 local_index = LocalFromRegIndex(register_index);
    
    Reference* reg;
    
    if (local_index >= 0) {
        reg = &scope->registers[local_index];
    }
    else {
        U32 global_index = GlobalFromRegIndex(register_index);
        reg = &runtime->global_registers[global_index];
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
    
    I32 global_index = GlobalIndexFromName(runtime, identifier);
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
    
    if (scope == NULL) scope = RuntimeGetCurrentScope(runtime);
    I32 local_index = LocalFromRegIndex(register_index);
    
    if (local_index >= 0) {
        return scope->registers[local_index];
    }
    else {
        U32 global_index = GlobalFromRegIndex(register_index);
        return runtime->global_registers[global_index];
    }
}

Reference RuntimeLoadGlobal(Runtime* runtime, String identifier)
{
    PROFILE_FUNCTION;
    
    I32 global_index = GlobalIndexFromName(runtime, identifier);
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
    
    Type* type = ref.type;
    
    if (type == string_type) {
        if (raw) return get_string(ref);
        return StrFormat(arena, "\"%S\"", get_string(ref));
    }
    if (type == int_type) { return StrFromI64(arena, RefGetSInt(ref)); }
    if (type == uint_type) { return StrFromU64(arena, RefGetUInt(ref)); }
    if (type == float_type) { return StrFromF64(arena, RefGetFloat(ref), 4); }
    if (type == bool_type) { return RefGetBool(ref) ? "true" : "false"; }
    if (type == void_type) { return "void"; }
    if (type == nil_type) { return "nil"; }
    if (type == type_type) { return RefGetType(runtime, ref)->name; }
    
    if (type->kind == VKind_Array)
    {
        StringBuilder builder = string_builder_make(context.arena);
        
        append(&builder, "{ ");
        
        ObjectData_Array* array = RefGetArray(ref);
        
        foreach(i, array->count) {
            Reference element = RefGetMember(runtime, ref, i);
            append(&builder, StrFromRef(context.arena, runtime, element, false));
            if (i < array->count - 1) append(&builder, ", ");
        }
        
        append(&builder, " }");
        
        return string_from_builder(arena, &builder);
    }
    
    if (type->kind == VKind_Enum)
    {
        EnumDefinition* enum_def = &runtime->enums[type->definition_index];
        I64 index = get_enum_index(ref);
        if (index < 0 || index >= enum_def->names.count) return "?";
        String name = enum_def->names[(U32)index];
        if (!raw) name = StrFormat(arena, "\"%S\"", name);
        return name;
    }
    
    if (type->kind == VKind_Struct)
    {
        StructDefinition* struct_def = &runtime->structs[type->definition_index];

        StringBuilder builder = string_builder_make(context.arena);
        
        append(&builder, "{ ");
        
        foreach(i, struct_def->types.count)
        {
            String member_name = struct_def->names[i];
            
            Reference member = RefGetMember(runtime, ref, i);
            appendf(&builder, "%S = %S", member_name, StrFromRef(context.arena, runtime, member, false));
            if (i < struct_def->types.count - 1) append(&builder, ", ");
        }
        
        append(&builder, " }");
        
        return string_from_builder(arena, &builder);
    }
    
    if (type->kind == VKind_Reference) {
        ref = RefDereference(runtime, ref);
        return StrFromRef(arena, runtime, ref, raw);
    }
    
    InvalidCodepath();
    return "?";
}

Reference ref_from_object(Object* object)
{
    Reference ref = {};
    ref.parent = object;
    ref.type = object->type;
    ref.address = object + 1;
    return ref;
}

Reference ref_from_address(Object* parent, Type* type, void* address)
{
    Reference member = {};
    member.parent = parent;
    member.type = type;
    member.address = address;
    return member;
}

void ref_set_member(Runtime* runtime, Reference ref, U32 index, Reference member)
{
    Reference dst = RefGetMember(runtime, ref, index);
    RefCopy(runtime, dst, member);
}

Reference RefGetChild(Runtime* runtime, Reference ref, U32 index, B32 is_property)
{
    if (!is_property) {
        Reference child = RefGetMember(runtime, ref, index);
        // TODO(Jose): What about memory requirements
        return child;
    }
    else return RefGetProperty(runtime, ref, index);
}

Reference RefGetMember(Runtime* runtime, Reference ref, U32 index)
{
    TypeSystem* tsys = runtime->tsys;
    
    if (is_unknown(ref) || is_null(ref)) {
        InvalidCodepath();
        return ref_from_object(nil_obj);
    }
    
    Type* type = ref.type;
    
    if (type->kind == VKind_Array)
    {
        ObjectData_Array* array = RefGetArray(ref);
        
        if (index >= array->count) {
            InvalidCodepath();
            return ref_from_object(nil_obj);
        }
        
        Type* element_type = TypeGetNext(tsys, ref.type);
        
        U64 offset = TypeGetSize(runtime, element_type) * index;
        return ref_from_address(ref.parent, element_type, array->data + offset);
    }
    
    if (type->kind == VKind_Struct)
    {
        StructDefinition* struct_def = &runtime->structs[type->definition_index];

        Array<U32> types = struct_def->types;
        
        if (index >= types.count) {
            InvalidCodepath();
            return ref_from_object(nil_obj);
        }
        
        U8* data = (U8*)ref.address;
        U32 offset = struct_def->offsets[index];
        
        return ref_from_address(ref.parent, TypeGet(types[index]), data + offset);
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

Reference RefGetProperty(Runtime* runtime, Reference ref, U32 index)
{
    if (is_unknown(ref) || is_null(ref)) {
        InvalidCodepath();
        return ref_from_object(nil_obj);
    }
    
    Type* type = ref.type;
    
    if (type == string_type)
    {
        if (index == 0) return AllocUInt(runtime, get_string(ref).size);
    }

    if (type == type_type) {
        if (index == 0) {
            Type* type = RefGetType(runtime, ref);
            return AllocString(runtime, type->name);
        }
    }
    
    if (type->kind == VKind_Array)
    {
        if (index == 0) return AllocUInt(runtime, RefGetArray(ref)->count);
    }
    
    if (type->kind == VKind_Enum)
    {
        EnumDefinition* enum_def = &runtime->enums[type->definition_index];

        I64 v = get_enum_index(ref);
        if (index == 0) return AllocSInt(runtime, v);
        if (index == 1) {
            if (v < 0 || v >= enum_def->values.count) return AllocSInt(runtime, -1);
            return AllocSInt(runtime, enum_def->values[v]);
        }
        if (index == 2) {
            if (v < 0 || v >= enum_def->names.count) return AllocString(runtime, "?");
            return AllocString(runtime, enum_def->names[v]);
        }
    }
    
    InvalidCodepath();
    return ref_from_object(nil_obj);
}

U32 RefGetChildCount(Runtime* runtime, Reference ref, B32 is_property)
{
    PROFILE_FUNCTION;
    if (!is_property) return RefGetMemberCount(runtime, ref);
    else return RefGetPropertyCount(runtime, ref);
}

U32 RefGetPropertyCount(Runtime* runtime, Reference ref) {
    return TypeGetProperties(ref.type).count;
}

U32 RefGetMemberCount(Runtime* runtime, Reference ref)
{


    if (ref.type->kind == VKind_Array) {
        return RefGetArray(ref)->count;
    }
    else if (ref.type->kind == VKind_Struct) {
        StructDefinition* struct_def = &runtime->structs[ref.type->definition_index];
        return struct_def->types.count;
    }
    return 0;
}

Reference AllocSInt(Runtime* runtime, I64 value)
{
    Reference ref = object_alloc(runtime, int_type);
    RefSetSInt(ref, value);
    return ref;
}

Reference AllocUInt(Runtime* runtime, U64 value)
{
    Reference ref = object_alloc(runtime, uint_type);
    RefSetUInt(ref, value);
    return ref;
}

Reference AllocFloat(Runtime* runtime, F64 value)
{
    Reference ref = object_alloc(runtime, float_type);
    RefSetFloat(ref, value);
    return ref;
}

Reference AllocBool(Runtime* runtime, B32 value)
{
    Reference ref = object_alloc(runtime, bool_type);
    RefSetBool(ref, value);
    return ref;
}

Reference AllocString(Runtime* runtime, String value)
{
    Reference ref = object_alloc(runtime, string_type);
    ref_string_set(runtime, ref, value);
    return ref;
}

Reference AllocArray(Runtime* runtime, Type* element_type, U32 count)
{
    PROFILE_FUNCTION;
    Type* type = TypeFromArray(runtime->tsys, element_type, 1);
    
    if (type->kind != VKind_Array) {
        InvalidCodepath();
        return ref_from_object(nil_obj);
    }
    
    Reference ref = object_alloc(runtime, type);
    RefArrayPrepare(runtime, ref, count);
    
    ObjectData_Array* array = RefGetArray(ref);
    array->count = array->capacity;
    return ref;
}

Reference AllocArrayMultidimensional(Runtime* runtime, Type* base_type, Array<I64> dimensions)
{
    TypeSystem* tsys = runtime->tsys;
    if (dimensions.count <= 0) {
        InvalidCodepath();
        return ref_from_object(nil_obj);
    }
    
    Type* type = TypeFromArray(runtime->tsys, base_type, dimensions.count);
    Type* element_type = TypeGetNext(tsys, type);
    
    if (dimensions.count == 1)
    {
        return AllocArray(runtime, element_type, (U32)dimensions[0]);
    }
    else
    {
        U32 count = (U32)dimensions[0];
        Reference ref = AllocArray(runtime, TypeGetNext(tsys, type), count);
        
        foreach(i, count) {
            Reference element_src = AllocArrayMultidimensional(runtime, base_type, ArraySub(dimensions, 1, dimensions.count - 1));
            Reference element_dst = RefGetChild(runtime, ref, i, true);
            RefCopy(runtime, element_dst, element_src);
        }
        
        return ref;
    }
}

Reference AllocEnum(Runtime* runtime, Type* type, I64 index)
{
    Reference ref = object_alloc(runtime, type);
    set_enum_index(ref, index);
    return ref;
}

Reference AllocReference(Runtime* runtime, Reference ref)
{
    Reference res = object_alloc(runtime, TypeFromReference(runtime->tsys, ref.type));
    set_reference(runtime, res, ref);
    return res;
}

B32 is_valid(Reference ref) {
    return !is_unknown(ref);
}
B32 is_unknown(Reference ref) {
    if (ref.parent == NULL) return true;
    if (ref.type == nil_type) return true;
    return ref.parent->type == nil_type;
}

B32 is_const(Reference ref) {
    // TODO(Jose): return value.kind == ValueKind_LValue && value.lvalue.ref->constant;
    return false;
}

B32 is_null(Reference ref) {
    if (is_unknown(ref)) return true;
    return ref.type == void_type && ref.parent->type == void_type;
}

B32 RefIsAnyInt(Reference ref) { return is_valid(ref) && TypeIsAnyInt(ref.type); }
B32 RefIsInt(Reference ref) { return is_valid(ref) && ref.type == int_type; }
B32 RefIsUInt(Reference ref) { return is_valid(ref) && ref.type == uint_type; }
B32 RefIsBool(Reference ref) { return is_valid(ref) && ref.type == bool_type; }
B32 RefIsFloat(Reference ref) { return is_valid(ref) && ref.type == float_type; }
B32 is_string(Reference ref) { return is_valid(ref) && ref.type == string_type; }

B32 RefIsArray(Reference ref) {
    if (is_unknown(ref)) return false;
    return TypeIsArray(ref.type);
}

B32 is_enum(Reference ref) {
    if (is_unknown(ref)) return false;
    return TypeIsEnum(ref.type);
}

B32 RefIsReference(Reference ref) {
    if (is_unknown(ref)) return false;
    return TypeIsReference(ref.type);
}

B32 RefIsType(TypeSystem* tsys, Reference ref) {
    if (is_unknown(ref)) return false;
    return ref.type == type_type;
}

I64 RefGetSInt(Reference ref)
{
    if (!RefIsInt(ref)) {
        InvalidCodepath();
        return 0;
    }
    
    I64* data = (I64*)ref.address;
    return *data;
}

U64 RefGetUInt(Reference ref)
{
    if (!RefIsUInt(ref)) {
        InvalidCodepath();
        return 0;
    }
    
    U64* data = (U64*)ref.address;
    return *data;
}

I64 RefGetInt(Reference ref)
{
    if (ref.type == int_type) return RefGetSInt(ref);
    return (I64)RefGetUInt(ref);
}

B32 RefGetBool(Reference ref)
{
    if (!RefIsBool(ref)) {
        InvalidCodepath();
        return 0;
    }
    
    B32* data = (B32*)ref.address;
    return *data;
}

F64 RefGetFloat(Reference ref)
{
    if (!RefIsFloat(ref)) {
        InvalidCodepath();
        return 0;
    }
    
    F64* data = (F64*)ref.address;
    return *data;
}

I64 get_enum_index(Reference ref) {
    if (!is_enum(ref)) {
        InvalidCodepath();
        return 0;
    }
    
    I64* data = (I64*)ref.address;
    return *data;
}

String get_string(Reference ref)
{
    if (!is_string(ref)) {
        InvalidCodepath();
        return 0;
    }
    
    ObjectData_String* data = (ObjectData_String*)ref.address;
    return StrMake(data->chars, data->size);
}

ObjectData_Array* RefGetArray(Reference ref)
{
    if (!RefIsArray(ref)) {
        InvalidCodepath();
        return {};
    }
    
    ObjectData_Array* array = (ObjectData_Array*)ref.address;
    return array;
}

Reference RefDereference(Runtime* runtime, Reference ref)
{
    if (!RefIsReference(ref)) {
        InvalidCodepath();
        return {};
    }

    TypeSystem* tsys = runtime->tsys;
    
    ObjectData_Ref* data = (ObjectData_Ref*)ref.address;
    Assert(TypeGetSize(runtime, ref.type) == sizeof(ObjectData_Ref));
    
    if (data->parent == null_obj || data->parent == NULL) {
        return ref_from_object(null_obj);
    }
    
    Reference deref = {};
    deref.parent = data->parent;
    deref.address = data->address;
    deref.type = TypeGetNext(tsys, ref.type);
    return deref;
}

Type* RefGetType(Runtime* runtime, Reference ref)
{
    if (!RefIsType(runtime->tsys, ref)) {
        InvalidCodepath();
        return {};
    }
    
    U32* data = (U32*)ref.address;
    return TypeFromID(runtime->tsys, *data);
}

I64 get_int_member(Runtime* runtime, Reference ref, String member)
{
    I32 index = TypeGetMember(runtime, ref.type, member).index;
    Reference member_ref = RefGetMember(runtime, ref, index);
    return RefGetSInt(member_ref);
}

B32 get_bool_member(Runtime* runtime, Reference ref, String member)
{
    I32 index = TypeGetMember(runtime, ref.type, member).index;
    Reference member_ref = RefGetMember(runtime, ref, index);
    return RefGetBool(member_ref);
}

String get_string_member(Runtime* runtime, Reference ref, String member)
{
    I32 index = TypeGetMember(runtime, ref.type, member).index;
    Reference member_ref = RefGetMember(runtime, ref, index);
    return get_string(member_ref);
}

void RefSetSInt(Reference ref, I64 v)
{
    if (!RefIsInt(ref)) {
        InvalidCodepath();
        return;
    }
    
    I64* data = (I64*)ref.address;
    *data = v;
}

void RefSetUInt(Reference ref, U64 v)
{
    if (!RefIsUInt(ref)) {
        InvalidCodepath();
        return;
    }
    
    U64* data = (U64*)ref.address;
    *data = v;
}

void RefSetFloat(Reference ref, F64 v)
{
    if (!RefIsFloat(ref)) {
        InvalidCodepath();
        return;
    }
    
    F64* data = (F64*)ref.address;
    *data = v;
}

void RefSetBool(Reference ref, B32 v)
{
    if (!RefIsBool(ref)) {
        InvalidCodepath();
        return;
    }
    
    B32* data = (B32*)ref.address;
    *data = v;
}

void set_enum_index(Reference ref, I64 v)
{
    if (!is_enum(ref)) {
        InvalidCodepath();
        return;
    }
    
    I64* data = (I64*)ref.address;
    *data = v;
}

void RefSetType(Runtime* runtime, Reference ref, U32 type_id)
{
    if (!RefIsType(runtime->tsys, ref)) {
        InvalidCodepath();
        return;
    }

    U32* data = (U32*)ref.address;
    *data = type_id;
}

ObjectData_String* ref_string_get_data(Runtime* runtime, Reference ref)
{
    if (!is_string(ref)) {
        InvalidCodepath();
        return NULL;
    }
    
    ObjectData_String* data = (ObjectData_String*)ref.address;
    Assert(TypeGetSize(runtime, ref.type) == sizeof(ObjectData_String));
    
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
    ObjectData_Array* array = RefGetArray(ref);
    Type* element_type = TypeGetNext(runtime->tsys, ref.type);
    
    if (TypeNeedsInternalRelease(runtime, element_type))
    {
        U64 element_size = TypeGetSize(runtime, element_type);
        
        U8* it = array->data;
        U8* end = array->data + element_size * array->count;
        
        while (it < end)
        {
            Reference member = ref_from_address(ref.parent, element_type, it);
            ref_release_internal(runtime, member, true);
            it += element_size;
        }
    }
    
    object_dynamic_free(runtime, array->data);
    *array = {};
}

void RefArrayPrepare(Runtime* runtime, Reference ref, U32 capacity)
{
    TypeSystem* tsys = runtime->tsys;

    ObjectData_Array* array = RefGetArray(ref);
    
    if (array->capacity < capacity)
    {
        Type* element_type = TypeGetNext(tsys, ref.type);
        U64 element_size = TypeGetSize(runtime, element_type);
        
        void* last_data = array->data;
        
        array->capacity = capacity;
        array->data = (U8*)object_dynamic_allocate(runtime, element_size * array->capacity);
        
        MemoryCopy(array->data, last_data, element_size * array->count);
        
        if (last_data) object_dynamic_free(runtime, last_data);
    }
}

void set_reference(Runtime* runtime, Reference ref, Reference src)
{
    TypeSystem* tsys = runtime->tsys;
    
    if (!RefIsReference(ref) || (!is_null(src) && TypeGetNext(tsys, ref.type) != src.type)) {
        InvalidCodepath();
        return;
    }
    
    ObjectData_Ref* data = (ObjectData_Ref*)ref.address;
    Assert(TypeGetSize(runtime, ref.type) == sizeof(ObjectData_Ref));
    
    object_decrement_ref(data->parent);
    data->parent = src.parent;
    data->address = src.address;
    object_increment_ref(data->parent);
}

void RefSetSIntMember(Runtime* runtime, Reference ref, String member, I64 v)
{
    I32 index = TypeGetMember(runtime, ref.type, member).index;
    Reference member_ref = RefGetMember(runtime, ref, index);
    if (is_unknown(member_ref)) return;
    else RefSetSInt(member_ref, v);
}

void RefSetUIntMember(Runtime* runtime, Reference ref, String member, U64 v)
{
    I32 index = TypeGetMember(runtime, ref.type, member).index;
    Reference member_ref = RefGetMember(runtime, ref, index);
    if (is_unknown(member_ref)) return;
    else RefSetUInt(member_ref, v);
}

void ref_member_set_bool(Runtime* runtime, Reference ref, String member, B32 v)
{
    I32 index = TypeGetMember(runtime, ref.type, member).index;
    Reference member_ref = RefGetMember(runtime, ref, index);
    if (is_unknown(member_ref)) return;
    else RefSetBool(member_ref, v);
}

void set_enum_index_member(Runtime* runtime, Reference ref, String member, I64 v)
{
    I32 index = TypeGetMember(runtime, ref.type, member).index;
    Reference member_ref = RefGetMember(runtime, ref, index);
    if (is_unknown(member_ref)) return;
    if (is_null(member_ref)) {
        TypeChild info = TypeGetMember(runtime, ref.type, member);
        ref_set_member(runtime, ref, index, AllocEnum(runtime, info.type, v));
    }
    else set_enum_index(member_ref, v);
}

void ref_member_set_string(Runtime* runtime, Reference ref, String member, String v)
{
    I32 index = TypeGetMember(runtime, ref.type, member).index;
    Reference member_ref = RefGetMember(runtime, ref, index);
    if (is_unknown(member_ref)) return;
    if (is_null(member_ref)) ref_set_member(runtime, ref, index, AllocString(runtime, v));
    else ref_string_set(runtime, member_ref, v);
}

void RefMemberSetUInt(Runtime* runtime, Reference ref, String member, U64 v)
{
    I32 index = TypeGetMember(runtime, ref.type, member).index;
    Reference member_ref = RefGetMember(runtime, ref, index);
    if (is_unknown(member_ref)) return;
    else RefSetUInt(member_ref, v);
}

void ref_assign_Result(Runtime* runtime, Reference ref, Result res)
{
    TypeSystem* tsys = runtime->tsys;
    Assert(ref.type == Type_Result);
    ref_member_set_string(runtime, ref, "message", res.message);
    RefSetSIntMember(runtime, ref, "code", res.code);
    ref_member_set_bool(runtime, ref, "failed", res.failed);
}

void ref_assign_CallOutput(Runtime* runtime, Reference ref, CallOutput res)
{
    TypeSystem* tsys = runtime->tsys;
    Assert(ref.type == Type_CallOutput);
    ref_member_set_string(runtime, ref, "stdout", res.stdout);
}

void ref_assign_FileInfo(Runtime* runtime, Reference ref, FileInfo info)
{
    TypeSystem* tsys = runtime->tsys;
    Assert(ref.type == Type_FileInfo);
    ref_member_set_string(runtime, ref, "path", info.path);
    ref_member_set_bool(runtime, ref, "is_directory", info.is_directory);
}

void ref_assign_FunctionHeader(Runtime* runtime, Reference ref, FunctionHeader* fn)
{
    TypeSystem* tsys = runtime->tsys;
    ref_member_set_string(runtime, ref, "name", fn->name);
    
    Reference parameters = AllocArray(runtime, Type_ObjectDefinition, fn->parameters.count);
    foreach(i, fn->parameters.count) {
        Reference param = RefGetMember(runtime, parameters, i);
        ref_assign_ObjectDefinition(runtime, param, fn->parameters[i]);
    }
    
    Reference returns = AllocArray(runtime, Type_ObjectDefinition, fn->returns.count);
    foreach(i, fn->returns.count) {
        Reference ret = RefGetMember(runtime, returns, i);
        ref_assign_ObjectDefinition(runtime, ret, fn->returns[i]);
    }
    
    ref_set_member(runtime, ref, TypeGetMember(runtime, ref.type, "parameters").index, parameters);
    ref_set_member(runtime, ref, TypeGetMember(runtime, ref.type, "returns").index, returns);
}

void ref_assign_StructDefinition(Runtime* runtime, Reference ref, Type* type)
{
    if (type->kind != VKind_Struct) return;

    TypeSystem* tsys = runtime->tsys;

    StructDefinition* struct_def = &runtime->structs[type->definition_index];

    ref_member_set_string(runtime, ref, "identifier", type->name);
    
    Reference members = AllocArray(runtime, Type_ObjectDefinition, struct_def->names.count);
    foreach(i, struct_def->names.count) {
        Reference mem = RefGetMember(runtime, members, i);
        String name = struct_def->names[i];
        Type* mem_type = TypeGet(struct_def->types[i]);
        ref_assign_ObjectDefinition(runtime, mem, ObjDefMake(name, mem_type->id, NO_CODE, false));
    }
    
    ref_set_member(runtime, ref, TypeGetMember(runtime, ref.type, "members").index, members);
}

void ref_assign_EnumDefinition(Runtime* runtime, Reference ref, Type* type)
{
    ref_member_set_string(runtime, ref, "identifier", type->name);

    EnumDefinition* enum_def = &runtime->enums[type->definition_index];
    
    Reference elements = AllocArray(runtime, string_type, enum_def->names.count);
    Reference values = AllocArray(runtime, int_type, enum_def->names.count);
    foreach(i, enum_def->names.count) {
        Reference element = RefGetMember(runtime, elements, i);
        Reference value = RefGetMember(runtime, values, i);
        ref_string_set(runtime, element, enum_def->names[i]);
        RefSetSInt(value, enum_def->values[i]);
    }
    
    ref_set_member(runtime, ref, TypeGetMember(runtime, ref.type, "elements").index, elements);
    ref_set_member(runtime, ref, TypeGetMember(runtime, ref.type, "values").index, values);
}

void ref_assign_ObjectDefinition(Runtime* runtime, Reference ref, ObjectDefinition def)
{
    TypeSystem* tsys = runtime->tsys;

    ref_member_set_string(runtime, ref, "identifier", def.name);
    ref_member_set_bool(runtime, ref, "is_constant", def.is_constant);
    
    TypeChild type_info = TypeGetMember(runtime, ref.type, "type");
    Reference type = RefGetMember(runtime, ref, type_info.index);
    RefSetType(runtime, type, def.type_id);
}

Reference ref_from_Result(Runtime* runtime, Result res)
{
    TypeSystem* tsys = runtime->tsys;
    Reference ref = object_alloc(runtime, Type_Result);
    ref_assign_Result(runtime, ref, res);
    return ref;
}

Result Result_from_ref(Runtime* runtime, Reference ref)
{
    TypeSystem* tsys = runtime->tsys;
    
    if (ref.type != Type_Result) {
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

Reference object_alloc(Runtime* runtime, Type* type)
{
    PROFILE_FUNCTION;

    Assert(TypeIsValid(type));
    
    U32 ID = object_generate_id(runtime);
    
    LogMemory("Alloc obj(%u): %S", ID, VTypeGetName(program, type));
    
    Assert(TypeGetSize(runtime, type) > 0);
    U32 type_size = sizeof(Object) + TypeGetSize(runtime, type);
    
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
    obj->type = type;
    obj->ref_count = 0;
    
    return ref_from_object(obj);
}

void object_free(Runtime* runtime, Object* obj, B32 release_internal_refs)
{

    Assert(obj->ref_count == 0);
    
    LogMemory("Free obj(%u): %S", obj->ID, VTypeGetName(program, obj->type));
    
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
    if (obj == NULL || obj->type == nil_type || obj->type == void_type) return;
    obj->ref_count++;
}

void object_decrement_ref(Object* obj)
{
    if (obj == NULL || obj->type == nil_type || obj->type == void_type) return;
    obj->ref_count--;
    Assert(obj->ref_count >= 0);
}

void ref_release_internal(Runtime* runtime, Reference ref, B32 release_refs)
{
    PROFILE_FUNCTION;

    TypeSystem* tsys = runtime->tsys;
    
    Type* type = ref.type;
    
    if (!TypeNeedsInternalRelease(runtime, type)) return;
    
    if (type->kind == VKind_Array)
    {
        ObjectData_Array* array = RefGetArray(ref);
        Type* element_type = TypeGetNext(tsys, type);
        
        if (TypeNeedsInternalRelease(runtime, element_type))
        {
            U64 element_size = TypeGetSize(runtime, element_type);
            
            U8* it = array->data;
            U8* end = array->data + element_size * array->count;
            
            while (it < end)
            {
                Reference member = ref_from_address(ref.parent, element_type, it);
                ref_release_internal(runtime, member, release_refs);
                it += element_size;
            }
        }
        
        object_dynamic_free(runtime, array->data);
        *array = {};
    }
    else if (type->kind == VKind_Struct)
    {
        StructDefinition* struct_def = &runtime->structs[type->definition_index];
        Array<U32> types = struct_def->types;
        
        foreach(i, types.count)
        {
            U8* data = (U8*)ref.address;
            U32 offset = struct_def->offsets[i];
            
            Reference member = ref_from_address(ref.parent, TypeGet(types[i]), data + offset);
            ref_release_internal(runtime, member, release_refs);
        }
    }
    else if (type->kind == VKind_Reference && release_refs) {
        Reference deref = RefDereference(runtime, ref);
        object_decrement_ref(deref.parent);
    }
    else if (type == string_type) {
        ref_string_clear(runtime, ref);
    }
}

void RefCopy(Runtime* runtime, Reference dst, Reference src)
{
    PROFILE_FUNCTION;
    
    TypeSystem* tsys = runtime->tsys;
    
    if (is_unknown(dst) || is_null(dst)) {
        InvalidCodepath();
        return;
    }
    
    if (is_unknown(src) || is_null(src)) {
        InvalidCodepath();
        return;
    }
    
    if (src.type != dst.type) {
        InvalidCodepath();
        return;
    }
    
    Type* type = dst.type;
    
    if (type->kind == VKind_Struct)
    {
        StructDefinition* struct_def = &runtime->structs[type->definition_index];

        foreach(i, struct_def->types.count) {
            Reference dst_mem = RefGetMember(runtime, dst, i);
            Reference src_mem = RefGetMember(runtime, src, i);
            RefCopy(runtime, dst_mem, src_mem);
        }
    }
    else if (type->kind == VKind_Primitive || type->kind == VKind_Enum)
    {
        if (type == string_type)
        {
            ref_string_set(runtime, dst, get_string(src));
        }
        else {
            I64* v0 = (I64*)dst.address;
            I64* v1 = (I64*)src.address;
            MemoryCopy(dst.address, src.address, TypeGetSize(runtime, type));
        }
    }
    else if (type->kind == VKind_Array)
    {
        ref_release_internal(runtime, dst, true);
        
        ObjectData_Array* dst_array = RefGetArray(dst);
        ObjectData_Array* src_array = RefGetArray(src);
        
        U64 element_size = TypeGetSize(runtime, TypeGetNext(tsys, type));
        dst_array->capacity = src_array->count;
        dst_array->data = (U8*)object_dynamic_allocate(runtime, dst_array->capacity * element_size);
        dst_array->count = src_array->count;
        
        foreach(i, dst_array->count) {
            Reference dst_element = RefGetMember(runtime, dst, i);
            Reference src_element = RefGetMember(runtime, src, i);
            RefCopy(runtime, dst_element, src_element);
        }
    }
    else if (type->kind == VKind_Reference)
    {
        Reference dst_deref = RefDereference(runtime, dst);
        Reference src_deref = RefDereference(runtime, src);
        
        object_decrement_ref(dst_deref.parent);
        object_increment_ref(src_deref.parent);
        MemoryCopy(dst.address, src.address, TypeGetSize(runtime, type));
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
    
    Reference dst = object_alloc(runtime, src.type);
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
