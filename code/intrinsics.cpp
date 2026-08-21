#include "runtime.h"

//- CORE

void Intrinsic_SetupRuntime(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    PROFILE_FUNCTION;

    TypeSystem* tsys = runtime->tsys;
    Reporter* reporter = runtime->reporter;
    
    LogFlow("Starting Init Globals");
    F64 start_time = TimerNow();
    
    // Yov struct
    if (Type_YovInfo != nil_type)
    {
        runtime->common_globals.yov = RuntimeLoadGlobal(runtime, "yov");
        Reference ref = runtime->common_globals.yov;
        
        RefSetUIntMember(runtime, ref, "minor", YOV_MINOR_VERSION);
        RefSetUIntMember(runtime, ref, "major", YOV_MAJOR_VERSION);
        RefSetUIntMember(runtime, ref, "revision", YOV_REVISION_VERSION);
        ref_member_set_string(runtime, ref, "version", YOV_VERSION);
        ref_member_set_string(runtime, ref, "path", system_info.executable_path);
    }
    
    // OS struct
    if (Type_OS != nil_type)
    {
        runtime->common_globals.os = RuntimeLoadGlobal(runtime, "os");
        Reference ref = runtime->common_globals.os;
        
#if OS_WINDOWS
        set_enum_index_member(runtime, ref, "kind", 0);
#else
#error TODO
#endif
    }
    
    // Context struct
    if (Type_Context != nil_type)
    {
        runtime->common_globals.context = RuntimeLoadGlobal(runtime, "context");
        Reference ref = runtime->common_globals.context;
        
        ref_member_set_string(runtime, ref, "cd", runtime->settings.origin_dir);
        ref_member_set_string(runtime, ref, "origin_dir", runtime->settings.origin_dir);
        ref_member_set_string(runtime, ref, "caller_dir", runtime->settings.caller_dir);
        RefMemberSetUInt(runtime, ref, "seed", OsTimerGet());
        
        // Args
        {
            Array<String> args = runtime->input->script_args;
            Reference array = AllocArray(runtime, string_type, args.count);
            foreach(i, args.count) {
                ref_set_member(runtime, array, i, AllocString(runtime, args[i]));
            }
            
            ref_set_member(runtime, ref, TypeGetMember(runtime, Type_Context, "args").index, array);
        }
        
        // Types
        {
            U32 first_type_id = any_type->id + 1;
            U32 last_type_id = TypeGetLastID(runtime->tsys);

            U32 type_count = last_type_id - first_type_id;
            Reference array = AllocArray(runtime, type_type, type_count);

            foreach(i, type_count) {
                U32 id = i + first_type_id;
                Reference element = object_alloc(runtime, type_type);
                RefSetType(runtime, element, id);
                ref_set_member(runtime, array, i, element);
            }
            
            ref_set_member(runtime, ref, TypeGetMember(runtime, Type_Context, "types").index, array);
        }
    }
    
    // Calls struct
    if (Type_CallsContext != nil_type)
    {
        Reference ref = RuntimeLoadGlobal(runtime, "calls");
        runtime->common_globals.calls = ref;
    }
    
    // User args
    {
        Input* input = runtime->input;
        Array<B8> defined_flags = ArrayAlloc<B8>(context.arena, runtime->args.count);
        
        B32 show_script_help = false;

        foreach(i, input->script_args.count)
        {
            String arg_name = input->script_args[i];

            if (!StrStarts(arg_name, "-")) {
                ReportErrorNoCode("An argument name starts with '-'");
                continue;
            }

            arg_name = StrSub(arg_name, 1, arg_name.size - 1);

            if (arg_name == "help") {
                show_script_help = true;
                continue;
            }

            String arg_value = ((i + 1) < input->script_args.count) ? input->script_args[i + 1] : "";

            I32 arg_index = -1;
            foreach(i, runtime->args.count) {
                if (runtime->args[i].name == arg_name) {
                    arg_index = i;
                    break;
                }
            }

            if (arg_index < 0) {
                ReportErrorNoCode("Unknown argument '%S'", arg_name);
                show_script_help = true;
                continue;
            }

            if (defined_flags[arg_index]) {
                ReportErrorNoCode("Argument already defined '%S'", arg_name);
                continue;
            }
            defined_flags[arg_index] = true;

            ArgDefinition* def = &runtime->args[arg_index];
            ObjectDefinition obj_def = runtime->global_objects[def->global_index];
            Type* value_type = TypeGet(obj_def.type_id);

            Value value = ValueNone();

            if (value_type == bool_type) {
                value = ValueFromBool(true);
            }
            else {
                i++;
                value = ValueFromStringExpression(context.arena, runtime, arg_value, value_type);

                if (value.kind == ValueKind_None) {
                    report_arg_wrong_value(def->name, arg_value);
                    continue;
                }
            }

            Reference ref = RefFromValue(runtime, NULL, value);
            RuntimeStore(runtime, NULL, RegIndexFromGlobal(def->global_index), ref);
        }

        if (show_script_help) {
            I32 global_index = GlobalIndexFromName(runtime, "__YovScriptHelp");
        
            if (global_index >= 0)
            {
                ObjectDefinition* obj = &runtime->global_objects[global_index];
                Reference ref = RefFromValue(runtime, NULL, ValueFromGlobal(obj->type_id, global_index));
                String help_str = StrFromRef(context.arena, runtime, ref, true);

                reporter->exit_requested = true;
                ReportEx(reporter, ReportLevel_Info, NO_CODE, 0, {}, help_str);
            }
        }
    }
    
    F64 ellapsed = TimerNow() - start_time;
    LogFlow("Init globals finished: %S", StringFromEllapsedTime(ellapsed));
    
    ArenaPopTo(context.arena, 0);
}

void Intrinsic_Typeof(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    TypeSystem* tsys = runtime->tsys;
    
    Reference ref = params[0];
    Type* type = ref.type;
    
    if (type == nil_type) {
        InvalidCodepath();
        type = void_type;
    }
    
    Reference res = object_alloc(runtime, type_type);
    RefSetType(runtime, res, type->id);
    returns[0] = res;
}

void Intrinsic_Print(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String str = StrFromRef(context.arena, runtime, params[0]);
    PrintEx(PrintLevel_UserCode, str);
}

void Intrinsic_PrintLn(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String str = StrFromRef(context.arena, runtime, params[0]);
    PrintEx(PrintLevel_UserCode, "%S\n", str);
}

void Intrinsic_Exit(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    I64 exit_code = RefGetSInt(params[0]);
    RuntimeExit(runtime, (I32)exit_code);
}

void Intrinsic_SetCD(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    Reference ref = RuntimeGetCurrentDirRef(runtime);
    String path = get_string(params[0]);
    
    if (!OsPathIsAbsolute(path)) {
        path = PathResolve(context.arena, PathAppend(context.arena, get_string(ref), path));
    }
    
    Result res;
    
    if (OsPathExists(path)) {
        res = RESULT_SUCCESS;
        ref_string_set(runtime, ref, path);
    }
    else {
        res = ResultMakeFailed("Path does not exists");
    }
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_Assert(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    B32 result = RefGetBool(params[0]);
    
    Result res = RESULT_SUCCESS;
    if (!result) {
        res = ResultMakeFailed(StrFormat(runtime->arena, "Assertion failed at '%S:%u'", RuntimeGetCurrentFile(runtime), RuntimeGetCurrentLine(runtime)));
    }
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_Failed(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String message = get_string(params[0]);
    I64 exit_code = RefGetSInt(params[1]);
    
    Result res = ResultMakeFailed(message, exit_code);
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_SleepMs(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    U64 millis = RefGetUInt(params[0]);
    OsThreadSleep(millis);
}

void Intrinsic_Sleep(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    F64 sec = RefGetFloat(params[0]);
    
    U64 start = OsTimerGet();
    
    U64 nanos = (U64)(sec * 1000000000.0);
    
    U64 millis = nanos / 1000000;
    if (millis >= 2) {
        OsThreadSleep(millis - 1);
    }
    
    U64 seconds_in_timer_freq = (U64)(sec * (F64)system_info.timer_frequency);
    
    U64 end = start + seconds_in_timer_freq;
    
    while (OsTimerGet() < end) {
        OsThreadYield();
    }
}

void Intrinsic_Env(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String name = get_string(params[0]);
    String value;
    Result res = OsEnvGet(context.arena, &value, name);
    
    returns[0] = AllocString(runtime, value);
    returns[1] = ref_from_Result(runtime, res);
}

void Intrinsic_EnvPath(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String name = get_string(params[0]);
    String value;
    Result res = OsEnvGet(context.arena, &value, name);
    
    if (!res.failed) {
        value = PathResolve(context.arena, value);
    }
    
    returns[0] = AllocString(runtime, value);
    returns[1] = ref_from_Result(runtime, res);
}

void Intrinsic_EnvPathArray(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String name = get_string(params[0]);
    String value;
    Result res = OsEnvGet(context.arena, &value, name);
    
    Reference array;
    
    if (!res.failed)
    {
        Array<String> values = StrSplit(context.arena, value, ";");
        array = AllocArray(runtime, string_type, values.count);
        
        foreach(i, values.count) {
            Reference element = RefGetMember(runtime, array, i);
            String path = values[i];
            path = PathResolve(context.arena, path);
            ref_string_set(runtime, element, path);
        }
    }
    else {
        array = AllocArray(runtime, string_type, 0);
    }
    
    returns[0] = array;
    returns[1] = ref_from_Result(runtime, res);
}

void Intrinsic_ArrayAppendBack(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    TypeSystem* tsys = runtime->tsys;

    Assert(TypeIsReference(params[0].type) && TypeIsArray(TypeGetNext(tsys, params[0].type)));
    Assert(TypeIsArray(params[1].type));
    
    Reference dst = RefDereference(runtime, params[0]);
    Reference src = params[1];
    
    ObjectData_Array* dst_array = RefGetArray(dst);
    ObjectData_Array* src_array = RefGetArray(src);
    
    RefArrayPrepare(runtime, dst, dst_array->count + src_array->count);
    
    for (U32 i = 0; i < src_array->count; i++)
    {
        U32 dst_index = dst_array->count++;
        ref_set_member(runtime, dst, dst_index, RefGetMember(runtime, src, i));
    }
}

void Intrinsic_ArrayAppendElementBack(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    TypeSystem* tsys = runtime->tsys;

    Type* array_type = TypeGetNext(tsys, params[0].type);
    Type* element_type = TypeGetNext(tsys, array_type);
    Assert(TypeIsReference(params[0].type) && TypeIsArray(array_type));
    Assert(element_type == params[1].type);
    
    Reference dst = RefDereference(runtime, params[0]);
    Reference src = params[1];
    
    ObjectData_Array* dst_array = RefGetArray(dst);
    
    RefArrayPrepare(runtime, dst, dst_array->count + 1);
    
    U32 dst_index = dst_array->count++;
    ref_set_member(runtime, dst, dst_index, src);
}

void Intrinsic_ArrayRemove(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    TypeSystem* tsys = runtime->tsys;

    if (!TypeIsReference(params[0].type) || !TypeIsArray(TypeGetNext(tsys, params[0].type))) {
        ReportErrorRT("First parameter is not an array reference");
        return;
    }
    
    Type* array_type = TypeGetNext(tsys, params[0].type);
    Type* element_type = TypeGetNext(tsys, array_type);
    
    Reference dst = RefDereference(runtime, params[0]);
    U64 index = RefGetUInt(params[1]);
    
    ObjectData_Array* dst_array = RefGetArray(dst);
    
    if (index >= dst_array->count) {
        ReportErrorRT("Array out of bounds");
        return;
    }
    
    U64 element_size = TypeGetSize(runtime, element_type);
    
    ref_release_internal(runtime, RefGetMember(runtime, dst, (U32)index), true);
    
    dst_array->count--;
    for (U32 i = (U32)index; i < dst_array->count; i++)
    {
        U8* e0 = dst_array->data + (i + 0) * element_size;
        U8* e1 = dst_array->data + (i + 1) * element_size;
        MemoryCopy(e0, e1, element_size);
    }
}

void Intrinsic_ArrayUnorderedRemove(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    TypeSystem* tsys = runtime->tsys;

    if (!TypeIsReference(params[0].type) || !TypeIsArray(TypeGetNext(tsys, params[0].type))) {
        ReportErrorRT("First parameter is not an array reference");
        return;
    }
    
    Type* array_type = TypeGetNext(tsys, params[0].type);
    Type* element_type = TypeGetNext(tsys, array_type);
    
    Reference dst = RefDereference(runtime, params[0]);
    U64 index = RefGetUInt(params[1]);
    
    ObjectData_Array* dst_array = RefGetArray(dst);
    
    if (index >= dst_array->count) {
        ReportErrorRT("Array out of bounds");
        return;
    }
    
    U64 element_size = TypeGetSize(runtime, element_type);
    
    ref_release_internal(runtime, RefGetMember(runtime, dst, (U32)index), true);
    
    U32 last_index = dst_array->count - 1;
    
    U8* e0 = dst_array->data + index * element_size;
    U8* e1 = dst_array->data + last_index * element_size;
    if (e0 != e1) {
        MemoryCopy(e0, e1, element_size);
    }
    
    dst_array->count--;
}

void Intrinsic_ArrayMakeEmpty(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    Type* base_type = RefGetType(runtime, params[0]);
    Reference arr_ref = params[1];
    ObjectData_Array* arr = RefGetArray(arr_ref);
    
    Array<I64> dimensions = ArrayAlloc<I64>(context.arena, arr->count);
    foreach(i, dimensions.count) {
        dimensions[i] = RefGetUInt(RefGetMember(runtime, arr_ref, i));
    }
    
    returns[0] = AllocArrayMultidimensional(runtime, base_type, dimensions);
}

void Intrinsic_ListMakeEmpty(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    InvalidCodepath();
}

//- CONSOLE 

void Intrinsic_ConsoleConfigure(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    B32 raw_reads = RefGetBool(params[0]);
    
    OsConsoleConfigure(raw_reads);
}

void Intrinsic_ConsoleWrite(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String str = get_string(params[0]);
    OsConsoleWrite(str);
}

void Intrinsic_ConsoleFlush(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    OsConsoleFlush();
}

void Intrinsic_ConsoleRead(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String str = OsConsoleRead(context.arena);
    returns[0] = AllocString(runtime, str);
}

//- EXTERNAL CALLS

internal_fn void ReturnFromExternalCall(Runtime* runtime, CallOutput res, Array<Reference> returns)
{
    TypeSystem* tsys = runtime->tsys;
    Reference call_result = object_alloc(runtime, Type_CallOutput);
    ref_assign_CallOutput(runtime, call_result, res);
    
    returns[0] = call_result;
    returns[1] = ref_from_Result(runtime, res.result);
}

void Intrinsic_Call(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    CallOutput res = {};
    
    String command_line = get_string(params[0]);
    
    res.result = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Call:\n%S", command_line));
    
    RedirectStdout redirect_stdout = RuntimeGetCallsRedirectStdout(runtime);
    
    if (!res.result.failed) {
        res = OsCall(context.arena, RuntimeGetCurrentDirStr(runtime), command_line, redirect_stdout);
    }
    
    ReturnFromExternalCall(runtime, res, returns);
}

void Intrinsic_CallExe(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String exe_name = get_string(params[0]);
    String args = get_string(params[1]);
    
    CallOutput res{};
    
    res.result = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Call Exe:\n%S %S", exe_name, args));
    
    RedirectStdout redirect_stdout = RuntimeGetCallsRedirectStdout(runtime);
    
    if (!res.result.failed) {
        res = OsCallExe(context.arena, RuntimeGetCurrentDirStr(runtime), exe_name, args, redirect_stdout);
    }
    
    ReturnFromExternalCall(runtime, res, returns);
}

void Intrinsic_CallScript(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String script_name = get_string(params[0]);
    String args = get_string(params[1]);
    String lang_args = get_string(params[2]);
    
    CallOutput res{};
    
    res.result = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Call Script:\n%S %S %S", lang_args, script_name, args));
    
    RedirectStdout redirect_stdout = RuntimeGetCallsRedirectStdout(runtime);
    
    if (!res.result.failed) {
        res = RuntimeCallScript(runtime, script_name, args, lang_args, redirect_stdout);
    }
    
    ReturnFromExternalCall(runtime, res, returns);
}

//- UTILS

void Intrinsic_StrAppend(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String str0 = get_string(params[0]);
    String str1 = get_string(params[1]);
    
    String res = StrAlloc(context.arena, str0.size + str1.size);
    MemoryCopy(res.data, str0.data, str0.size);
    MemoryCopy(res.data + str0.size, str1.data, str1.size);
    
    returns[0] = AllocString(runtime, res);
}

void Intrinsic_StrEquals(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String str0 = get_string(params[0]);
    String str1 = get_string(params[1]);
    
    B32 res = StrEquals(str0, str1);
    returns[0] = AllocBool(runtime, res);
}

void Intrinsic_StrSplit(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String str = get_string(params[0]);
    String separator = get_string(params[1]);
    
    Array<String> result = StrSplit(context.arena, str, separator);
    
    Reference array = AllocArray(runtime, string_type, result.count);
    foreach(i, result.count) {
        ref_set_member(runtime, array, i, AllocString(runtime, result[i]));
    }
    
    returns[0] = array;
}

void Intrinsic_StrGetCodepoint(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String str = get_string(params[0]);
    U64 cursor = RefGetUInt(params[1]);
    
    U32 codepoint = StrGetCodepoint(str, &cursor);
    
    returns[0] = AllocUInt(runtime, codepoint);
    returns[1] = AllocUInt(runtime, cursor);
}

void Intrinsic_StrFromCodepoint(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    U32 codepoint = (U32)RefGetUInt(params[0]);
    String str = StringFromCodepoint(context.arena, codepoint);
    
    returns[0] = AllocString(runtime, str);
}


void Intrinsic_PathAppend(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String str0 = get_string(params[0]);
    String str1 = get_string(params[1]);
    
    String res = PathAppend(context.arena, str0, str1);
    res = PathResolve(context.arena, res);
    
    returns[0] = AllocString(runtime, res);
}

void Intrinsic_PathResolve(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String res = PathResolve(context.arena, get_string(params[0]));
    returns[0] = AllocString(runtime, res);
}

internal_fn U64 RuntimeRandom(Runtime* runtime)
{
    TypeSystem* tsys = runtime->tsys;
    
    Reference seed_ref = RefGetMember(runtime, runtime->common_globals.context, TypeGetMember(runtime, Type_Context, "seed").index);
    U64 seed = RefGetUInt(seed_ref);
    
    seed += 0x9E3779B97F4A7C15;
    
    U64 z = seed;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EB;
    U64 result = z ^ (z >> 31);
    
    RefSetUInt(seed_ref, seed);
    return result;
}

void Intrinsic_Random(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    returns[0] = AllocUInt(runtime, RuntimeRandom(runtime));
}

void Intrinsic_RandomRange(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    I64 min = RefGetInt(params[0]);
    I64 max = RefGetInt(params[1]);
    
    if (max < min) {
        Swap(min, max);
    }
    
    I64 range = max - min;
    I64 result = min;
    U64 random = RuntimeRandom(runtime);
    
    if (range <= 0)
    {
        random %= (U64)range;
        result = random + min;
    }
    
    returns[0] = AllocSInt(runtime, result);
}

void Intrinsic_Random01(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    U64 random = RuntimeRandom(runtime);
    U64 bits = (random >> 12) | 0x3FF0000000000000;
    F64 result = *(F64*)&bits; 
    returns[0] = AllocFloat(runtime, result - 1.0);
}

void Intrinsic_TimeTicks(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    returns[0] = AllocUInt(runtime, OsTimerGet());
}

void Intrinsic_TimeElapsed(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    U64 time = OsTimerGet() - runtime->started_time;
    F64 elapsed = time / (F64)system_info.timer_frequency;
    returns[0] = AllocFloat(runtime, elapsed);
}

//- YOV

void Intrinsic_YovRequire(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    U64 major = RefGetUInt(params[0]);
    U64 minor = RefGetUInt(params[1]);
    
    Result res = RESULT_SUCCESS;
    
    B32 valid = major == YOV_MAJOR_VERSION && minor == YOV_MINOR_VERSION;
    if (!valid) {
        res = ResultMakeFailed(StrFormat(context.arena, "Require version: Yov v%u.%u", major, minor));
    }
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_YovRequireMin(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    U64 major = RefGetUInt(params[0]);
    U64 minor = RefGetUInt(params[1]);
    
    Result res = RESULT_SUCCESS;
    
    B32 valid = true;
    if (major > YOV_MAJOR_VERSION) valid = false;
    else if (major == YOV_MAJOR_VERSION && minor > YOV_MINOR_VERSION) valid = false;
    if (!valid) {
        res = ResultMakeFailed(StrFormat(context.arena, "Require minimum version: Yov v%u.%u", major, minor));
    }
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_YovRequireMax(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    U64 major = RefGetUInt(params[0]);
    U64 minor = RefGetUInt(params[1]);
    
    Result res = RESULT_SUCCESS;
    
    B32 valid = false;
    if (major > YOV_MAJOR_VERSION) valid = true;
    else if (major == YOV_MAJOR_VERSION && minor >= YOV_MINOR_VERSION) valid = true;
    if (!valid) {
        res = ResultMakeFailed(StrFormat(context.arena, "Require maximum version: Yov v%u.%u", major, minor));
    }
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_YovParse(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
#if 0 // TODO(Jose): 
    SCRATCH();
    String path = get_string(params[0]);
    
    path = path_absolute_to_cd(context.arena, inter, path);
    
    B32 file_is_core = string_ends(path, "code/core.yov");
    
    Yov* last_yov = yov;
    
    YovSettings settings = {};
    settings.analyze_only = true;
    settings.no_user = true;
    settings.ignore_core = !!file_is_core;
    
    yov_initialize();
    yov_config(path, settings, {});
    
    yov_run();
    
    Yov* temp_yov = yov;
    yov = last_yov;
    
    Reference out = object_alloc(runtime, Type_YovParseOutput);
    ref_assign_YovParseOutput(runtime, out, temp_yov);
    
    yov = temp_yov;
    
    yov_shutdown();
    yov = last_yov;
    
    returns[0] = out;
#endif
}

//- MISC

void Intrinsic_AskYesNo(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String content = get_string(params[0]);
    B32 result = RuntimeAskYesNo(runtime, "Ask", content);
    returns[0] = AllocBool(runtime, result);
}

void Intrinsic_Exists(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String path = get_string(params[0]);
    B32 result = OsPathExists(path);
    returns[0] = AllocBool(runtime, result);
}

void Intrinsic_DirCreate(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String path = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    B32 recursive = RefGetBool(params[1]);
    
    Result res = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Create directory:\n%S", path));
    
    if (!res.failed) {
        res = OsCreateDirectory(path, recursive);
    }
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_DirDelete(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String path = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    
    Result res = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Delete directory:\n%S", path));
    
    if (!res.failed) {
        res = OsDeleteDirectory(path);
    }
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_DirCopy(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String dst = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    String src = PathAbsoluteToCD(context.arena, runtime, get_string(params[1]));
    
    Result res = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Copy directory\n'%S'\nto\n'%S'", src, dst));
    
    if (!res.failed) {
        res = OsCopyDirectory(dst, src);
    }
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_DirMove(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String dst = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    String src = PathAbsoluteToCD(context.arena, runtime, get_string(params[1]));
    
    Result res = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Move directory\n'%S'\nto\n'%S'", src, dst));
    
    if (!res.failed) {
        res = OsMoveDirectory(dst, src);
    }
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_FileCopy(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String dst = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    String src = PathAbsoluteToCD(context.arena, runtime, get_string(params[1]));
    CopyMode copy_mode = get_enum_CopyMode(params[2]);
    
    Result res = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Copy file\n'%S'\nto\n'%S'", src, dst));
    
    if (!res.failed) res = OsCopyFile(dst, src, copy_mode == CopyMode_Override);
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_FileMove(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String dst = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    String src = PathAbsoluteToCD(context.arena, runtime, get_string(params[1]));
    
    Result res = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Move file\n'%S'\nto\n'%S'", src, dst));
    
    if (!res.failed) res = OsMoveFile(dst, src);
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_FileDelete(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String path = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    
    Result res = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Delete file:\n'%S'", path));
    
    if (!res.failed) res = OsDeleteFile(path);
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_FileGetInfo(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    TypeSystem* tsys = runtime->tsys;
    String path = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    
    FileInfo info;
    Result res = OsFileGetInfo(context.arena, path, &info);
    
    Reference ret = object_alloc(runtime, Type_FileInfo);
    if (!res.failed) ref_assign_FileInfo(runtime, ret, info);
    
    returns[0] = ret;
    returns[1] = ref_from_Result(runtime, res);
}

void Intrinsic_DirGetInfo(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    TypeSystem* tsys = runtime->tsys;
    String path = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    
    Array<FileInfo> infos;
    Result res = OsDirGetFilesInfo(context.arena, path, &infos);
    
    Reference ret = AllocArray(runtime, Type_FileInfo, infos.count);
    if (!res.failed) {
        foreach(i, infos.count) {
            Reference element = RefGetMember(runtime, ret, i);
            ref_assign_FileInfo(runtime, element, infos[i]);
        }
    }
    
    returns[0] = ret;
    returns[1] = ref_from_Result(runtime, res);
}

void Intrinsic_WriteEntireFile(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String path = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    String content = get_string(params[1]);
    // TODO(Jose): B32 append = get_bool(params[2]);
    
    Result res = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Write entire file:\n'%S'", path));
    if (!res.failed) res = OsWriteEntireFile(path, RBufferFromStr(content));
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_ReadEntireFile(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String path = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    
    Result res = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Read entire file:\n'%S'", path));
    RBuffer content{};
    if (!res.failed) res = OsReadEntireFile(context.arena, path, &content);
    String content_str = StrMake((char*)content.data, content.size);
    
    returns[0] = AllocString(runtime, content_str);
    returns[1] = ref_from_Result(runtime, res);
}

//- MSVC

void Intrinsic_MsvcImportEnvX64(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    Result res = MSVCImportEnv(MSVC_Env_x64);
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_MsvcImportEnvX86(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    Result res = MSVCImportEnv(MSVC_Env_x86);
    returns[0] = ref_from_Result(runtime, res);
}

//- REGISTRY 

struct IntrinsicRegistry {
    IntrinsicFunction* fn;
    String identifier;
};

IntrinsicRegistry intrinsics[] = {
    { Intrinsic_SetupRuntime, "__YovSetupRuntime" },
    
    { Intrinsic_Typeof, "Typeof" },
    { Intrinsic_Print, "Print" },
    { Intrinsic_PrintLn, "PrintLn" },
    { Intrinsic_Exit, "Exit" },
    { Intrinsic_SetCD, "SetCD" },
    { Intrinsic_Assert, "Assert" },
    { Intrinsic_Failed, "Failed" },
    { Intrinsic_SleepMs, "SleepMs" },
    { Intrinsic_Sleep, "Sleep" },
    { Intrinsic_Env, "Env" },
    { Intrinsic_EnvPath, "EnvPath" },
    { Intrinsic_EnvPathArray, "EnvPathArray" },
    
    { Intrinsic_ArrayAppendBack, "ArrayAppendBack" },
    // TODO(Jose): { Intrinsic_ArrayAppendFront, "ArrayAppendFront" },
    { Intrinsic_ArrayAppendElementBack, "ArrayAppendElementBack" },
    // TODO(Jose): { Intrinsic_ArrayAppendElementFront, "ArrayAppendElementFront" },
    { Intrinsic_ArrayRemove, "ArrayRemove" },
    { Intrinsic_ArrayUnorderedRemove, "ArrayUnorderedRemove" },
    { Intrinsic_ArrayMakeEmpty, "ArrayMakeEmpty" },
    { Intrinsic_ListMakeEmpty, "ListMakeEmpty" },
    
    { Intrinsic_ConsoleConfigure, "ConsoleConfigure" },
    { Intrinsic_ConsoleWrite, "ConsoleWrite" },
    { Intrinsic_ConsoleFlush, "ConsoleFlush" },
    { Intrinsic_ConsoleRead, "ConsoleRead" },
    { Intrinsic_Call, "Call" },
    { Intrinsic_CallExe, "CallExe" },
    { Intrinsic_CallScript, "CallScript" },
    
    { Intrinsic_StrAppend, "TimerOsClock" },
    
    { Intrinsic_StrAppend, "StrAppend" },
    { Intrinsic_StrEquals, "StrEquals" },
    { Intrinsic_StrSplit, "StrSplit" },
    { Intrinsic_StrGetCodepoint, "StrGetCodepoint" },
    { Intrinsic_StrFromCodepoint, "StrFromCodepoint" },
    
    { Intrinsic_PathAppend, "PathAppend" },
    { Intrinsic_PathResolve, "PathResolve" },
    
    { Intrinsic_TimeTicks, "TimeTicks" },
    { Intrinsic_TimeElapsed, "TimeElapsed" },
    
    { Intrinsic_Random, "Random" },
    { Intrinsic_RandomRange, "RandomRange" },
    { Intrinsic_Random01, "Random01" },
    
    { Intrinsic_YovRequire, "YovRequire" },
    { Intrinsic_YovRequireMin, "YovRequireMin" },
    { Intrinsic_YovRequireMax, "YovRequireMax" },
    { Intrinsic_YovParse, "YovParse" },
    { Intrinsic_AskYesNo, "AskYesNo" },
    
    { Intrinsic_Exists, "Exists" },
    { Intrinsic_DirCreate, "DirCreate" },
    { Intrinsic_DirDelete, "DirDelete" },
    { Intrinsic_DirCopy, "DirCopy" },
    { Intrinsic_DirMove, "DirMove" },
    { Intrinsic_FileCopy, "FileCopy" },
    { Intrinsic_FileMove, "FileMove" },
    { Intrinsic_FileDelete, "FileDelete" },
    { Intrinsic_FileGetInfo, "FileGetInfo" },
    { Intrinsic_DirGetInfo, "DirGetInfo" },
    
    { Intrinsic_WriteEntireFile, "WriteEntireFile" },
    { Intrinsic_ReadEntireFile, "ReadEntireFile" },
    
    { Intrinsic_MsvcImportEnvX64, "MsvcImportEnvX64" },
    { Intrinsic_MsvcImportEnvX86, "MsvcImportEnvX86" },
    
};

IntrinsicFunction* IntrinsicFromName(String identifier)
{
    foreach(i, countof(intrinsics)) {
        if (intrinsics[i].identifier == identifier) return intrinsics[i].fn;
    }
    return NULL;
}