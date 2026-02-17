#include "runtime.h"

//- CORE

void Intrinsic_typeof(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    Program* program = runtime->program;
    
    Reference ref = params[0];
    VType vtype = ref.vtype;
    
    if (TypeIsNil(vtype)) {
        InvalidCodepath();
        vtype = VType_Void;
    }
    
    Reference type = object_alloc(runtime, VType_Type);
    ref_assign_Type(runtime, type, vtype);
    returns[0] = type;
}

void Intrinsic_print(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String str = StrFromRef(context.arena, runtime, params[0]);
    OsPrint(PrintLevel_UserCode, str);
}

void Intrinsic_println(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String str = StrFromRef(context.arena, runtime, params[0]);
    PrintEx(PrintLevel_UserCode, "%S\n", str);
}

void Intrinsic_exit(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    I64 exit_code = get_int(params[0]);
    RuntimeExit(runtime, (I32)exit_code);
}

void Intrinsic_set_cd(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
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

void Intrinsic_assert(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    B32 result = get_bool(params[0]);
    
    Result res = RESULT_SUCCESS;
    if (!result) {
        res = ResultMakeFailed(StrFormat(runtime->arena, "Assertion failed at '%S:%u'", RuntimeGetCurrentFile(runtime), RuntimeGetCurrentLine(runtime)));
    }
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_failed(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String message = get_string(params[0]);
    I64 exit_code = get_int(params[1]);
    
    Result res = ResultMakeFailed(message, exit_code);
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_sleep(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    I64 millis = get_int(params[0]);
    OsThreadSleep(millis);
}

void Intrinsic_env(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String name = get_string(params[0]);
    String value;
    Result res = OsEnvGet(context.arena, &value, name);
    
    returns[0] = alloc_string(runtime, value);
    returns[1] = ref_from_Result(runtime, res);
}

void Intrinsic_env_path(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String name = get_string(params[0]);
    String value;
    Result res = OsEnvGet(context.arena, &value, name);
    
    if (!res.failed) {
        value = PathResolve(context.arena, value);
    }
    
    returns[0] = alloc_string(runtime, value);
    returns[1] = ref_from_Result(runtime, res);
}

void Intrinsic_env_path_array(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String name = get_string(params[0]);
    String value;
    Result res = OsEnvGet(context.arena, &value, name);
    
    Reference array;
    
    if (!res.failed)
    {
        Array<String> values = StrSplit(context.arena, value, ";");
        array = alloc_array(runtime, VType_String, values.count);
        
        foreach(i, values.count) {
            Reference element = ref_get_member(runtime, array, i);
            String path = values[i];
            path = PathResolve(context.arena, path);
            ref_string_set(runtime, element, path);
        }
    }
    else {
        array = alloc_array(runtime, VType_String, 0);
    }
    
    returns[0] = array;
    returns[1] = ref_from_Result(runtime, res);
}

//- CONSOLE 

void Intrinsic_console_write(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String str = get_string(params[0]);
    OsPrint(PrintLevel_UserCode, str);
}

void Intrinsic_console_clear(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    OsConsoleClear();
}

//- EXTERNAL CALLS

internal_fn void ReturnFromExternalCall(Runtime* runtime, CallOutput res, Array<Reference> returns)
{
    Program* program = runtime->program;
    Reference call_result = object_alloc(runtime, VType_CallOutput);
    ref_assign_CallOutput(runtime, call_result, res);
    
    returns[0] = call_result;
    returns[1] = ref_from_Result(runtime, res.result);
}

void Intrinsic_call(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
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

void Intrinsic_call_exe(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
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

void Intrinsic_call_script(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
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

void Intrinsic_path_resolve(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String res = PathResolve(context.arena, get_string(params[0]));
    returns[0] = alloc_string(runtime, res);
}

void Intrinsic_str_get_codepoint(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String str = get_string(params[0]);
    U64 cursor = get_int(params[1]);
    
    U32 codepoint = StrGetCodepoint(str, &cursor);
    
    returns[0] = alloc_int(runtime, codepoint);
    returns[1] = alloc_int(runtime, cursor);
}

void Intrinsic_str_split(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String str = get_string(params[0]);
    String separator = get_string(params[1]);
    
    Array<String> result = StrSplit(context.arena, str, separator);
    
    Reference array = alloc_array(runtime, VType_String, result.count);
    foreach(i, result.count) {
        ref_set_member(runtime, array, i, alloc_string(runtime, result[i]));
    }
    
    returns[0] = array;
}

struct JsonProperty {
    String name;
    String value;
};

internal_fn U64 json_skip(String json, U64 cursor)
{
    if (cursor != 0) {
        while (cursor < json.size && json[cursor] != ',') cursor++;
    }
    if (cursor >= json.size) return cursor;
    while (cursor < json.size && json[cursor] != '\"') cursor++;
    return cursor;
}

internal_fn JsonProperty json_get_property(String json, U64 cursor)
{
    while (cursor < json.size && json[cursor] != ':') cursor++;
    if (cursor >= json.size) return {};
    
    U64 separator = cursor;// TODO(Jose): 
}

internal_fn B32 json_access(String* dst, String json, String searching_name)
{
    *dst = {};
    
    U64 cursor = 0;
    
    while (cursor < json.size)
    {
        if (json[cursor] == '\"')
        {
            U64 name_begin = cursor + 1;
            U64 name_end = name_begin;
            
            while (name_end < json.size && json[name_end] != '\"')
                name_end++;
            
            String name = StrSub(json, name_begin, name_end - name_begin);
            
            if (StrEquals(name, searching_name)) {
                // TODO(Jose): 
                return true;
            }
        }
        
        cursor = json_skip(json, cursor);
    }
    
    return false;
}

void Intrinsic_json_route(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String json = get_string(params[0]);
    String route = get_string(params[1]);
    
    Array<String> names = StrSplit(context.arena, route, "/");
    
    B32 success = true;
    foreach(i, names.count)
    {
        String last_json = json;
        json = {};
        if (!json_access(&json, last_json, names[i])) {
            success = false;
            break;
        }
    }
    
    Result res = RESULT_SUCCESS;
    if (!success) res = ResultMakeFailed("Json route not found");
    
    returns[0] = alloc_string(runtime, json);
    returns[1] = ref_from_Result(runtime, res);
}

//- YOV

void Intrinsic_yov_require(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    I64 major = get_int(params[0]);
    I64 minor = get_int(params[1]);
    
    Result res = RESULT_SUCCESS;
    
    B32 valid = major == YOV_MAJOR_VERSION && minor == YOV_MINOR_VERSION;
    if (!valid) {
        res = ResultMakeFailed(StrFormat(context.arena, "Require version: Yov v%u.%u", major, minor));
    }
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_yov_require_min(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    I64 major = get_int(params[0]);
    I64 minor = get_int(params[1]);
    
    Result res = RESULT_SUCCESS;
    
    B32 valid = true;
    if (major > YOV_MAJOR_VERSION) valid = false;
    else if (major == YOV_MAJOR_VERSION && minor > YOV_MINOR_VERSION) valid = false;
    if (!valid) {
        res = ResultMakeFailed(StrFormat(context.arena, "Require minimum version: Yov v%u.%u", major, minor));
    }
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_yov_require_max(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    I64 major = get_int(params[0]);
    I64 minor = get_int(params[1]);
    
    Result res = RESULT_SUCCESS;
    
    B32 valid = false;
    if (major > YOV_MAJOR_VERSION) valid = true;
    else if (major == YOV_MAJOR_VERSION && minor >= YOV_MINOR_VERSION) valid = true;
    if (!valid) {
        res = ResultMakeFailed(StrFormat(context.arena, "Require maximum version: Yov v%u.%u", major, minor));
    }
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_yov_parse(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
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
    
    Reference out = object_alloc(runtime, VType_YovParseOutput);
    ref_assign_YovParseOutput(runtime, out, temp_yov);
    
    yov = temp_yov;
    
    yov_shutdown();
    yov = last_yov;
    
    returns[0] = out;
#endif
}

//- MISC

void Intrinsic_ask_yesno(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String content = get_string(params[0]);
    B32 result = RuntimeAskYesNo(runtime, "Ask", content);
    returns[0] = alloc_bool(runtime, result);
}

void Intrinsic_exists(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String path = get_string(params[0]);
    B32 result = OsPathExists(path);
    returns[0] = alloc_bool(runtime, result);
}

void Intrinsic_create_directory(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String path = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    B32 recursive = get_bool(params[1]);
    
    Result res = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Create directory:\n%S", path));
    
    if (!res.failed) {
        res = OsCreateDirectory(path, recursive);
    }
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_delete_directory(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String path = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    
    Result res = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Delete directory:\n%S", path));
    
    if (!res.failed) {
        res = OsDeleteDirectory(path);
    }
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_copy_directory(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String dst = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    String src = PathAbsoluteToCD(context.arena, runtime, get_string(params[1]));
    
    Result res = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Copy directory\n'%S'\nto\n'%S'", src, dst));
    
    if (!res.failed) {
        res = OsCopyDirectory(dst, src);
    }
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_move_directory(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String dst = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    String src = PathAbsoluteToCD(context.arena, runtime, get_string(params[1]));
    
    Result res = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Move directory\n'%S'\nto\n'%S'", src, dst));
    
    if (!res.failed) {
        res = OsMoveDirectory(dst, src);
    }
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_copy_file(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String dst = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    String src = PathAbsoluteToCD(context.arena, runtime, get_string(params[1]));
    CopyMode copy_mode = get_enum_CopyMode(params[2]);
    
    Result res = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Copy file\n'%S'\nto\n'%S'", src, dst));
    
    if (!res.failed) res = OsCopyFile(dst, src, copy_mode == CopyMode_Override);
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_move_file(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String dst = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    String src = PathAbsoluteToCD(context.arena, runtime, get_string(params[1]));
    
    Result res = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Move file\n'%S'\nto\n'%S'", src, dst));
    
    if (!res.failed) res = OsMoveFile(dst, src);
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_delete_file(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String path = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    
    Result res = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Delete file:\n'%S'", path));
    
    if (!res.failed) res = OsDeleteFile(path);
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_write_entire_file(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String path = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    String content = get_string(params[1]);
    // TODO(Jose): B32 append = get_bool(params[2]);
    
    Result res = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Write entire file:\n'%S'", path));
    if (!res.failed) res = OsWriteEntireFile(path, { content.data, content.size });
    
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_read_entire_file(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    String path = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    
    Result res = RuntimeUserAssertion(runtime, StrFormat(context.arena, "Read entire file:\n'%S'", path));
    RBuffer content{};
    if (!res.failed) res = OsReadEntireFile(context.arena, path, &content);
    String content_str = StrMake((char*)content.data, content.size);
    
    returns[0] = alloc_string(runtime, content_str);
    returns[1] = ref_from_Result(runtime, res);
}

void Intrinsic_file_get_info(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    Program* program = runtime->program;
    String path = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    
    FileInfo info;
    Result res = OsFileGetInfo(context.arena, path, &info);
    
    Reference ret = object_alloc(runtime, VType_FileInfo);
    if (!res.failed) ref_assign_FileInfo(runtime, ret, info);
    
    returns[0] = ret;
    returns[1] = ref_from_Result(runtime, res);
}

void Intrinsic_dir_get_files_info(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    Program* program = runtime->program;
    String path = PathAbsoluteToCD(context.arena, runtime, get_string(params[0]));
    
    Array<FileInfo> infos;
    Result res = OsDirGetFilesInfo(context.arena, path, &infos);
    
    Reference ret = alloc_array(runtime, VType_FileInfo, infos.count);
    if (!res.failed) {
        foreach(i, infos.count) {
            Reference element = ref_get_member(runtime, ret, i);
            ref_assign_FileInfo(runtime, element, infos[i]);
        }
    }
    
    returns[0] = ret;
    returns[1] = ref_from_Result(runtime, res);
}

//- MSVC

void Intrinsic_msvc_import_env_x64(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
{
    Result res = MSVCImportEnv(MSVC_Env_x64);
    returns[0] = ref_from_Result(runtime, res);
}

void Intrinsic_msvc_import_env_x86(Runtime* runtime, Array<Reference> params, Array<Reference> returns)
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
    { Intrinsic_typeof, "typeof" },
    { Intrinsic_print, "print" },
    { Intrinsic_println, "println" },
    { Intrinsic_exit, "exit" },
    { Intrinsic_set_cd, "set_cd" },
    { Intrinsic_assert, "assert" },
    { Intrinsic_failed, "failed" },
    { Intrinsic_sleep, "sleep" },
    { Intrinsic_env, "env" },
    { Intrinsic_env_path, "env_path" },
    { Intrinsic_env_path_array, "env_path_array" },
    { Intrinsic_console_write, "console_write" },
    { Intrinsic_console_clear, "console_clear" },
    { Intrinsic_call, "call" },
    { Intrinsic_call_exe, "call_exe" },
    { Intrinsic_call_script, "call_script" },
    { Intrinsic_path_resolve, "path_resolve" },
    { Intrinsic_str_get_codepoint, "str_get_codepoint" },
    { Intrinsic_str_split, "str_split" },
    { Intrinsic_json_route, "json_route" },
    { Intrinsic_yov_require, "yov_require" },
    { Intrinsic_yov_require_min, "yov_require_min" },
    { Intrinsic_yov_require_max, "yov_require_max" },
    { Intrinsic_yov_parse, "yov_parse" },
    { Intrinsic_ask_yesno, "ask_yesno" },
    { Intrinsic_exists, "exists" },
    { Intrinsic_create_directory, "create_directory" },
    { Intrinsic_delete_directory, "delete_directory" },
    { Intrinsic_copy_directory, "copy_directory" },
    { Intrinsic_move_directory, "move_directory" },
    { Intrinsic_copy_file, "copy_file" },
    { Intrinsic_move_file, "move_file" },
    { Intrinsic_delete_file, "delete_file" },
    { Intrinsic_write_entire_file, "write_entire_file" },
    { Intrinsic_read_entire_file, "read_entire_file" },
    { Intrinsic_file_get_info, "file_get_info" },
    { Intrinsic_dir_get_files_info, "dir_get_files_info" },
    { Intrinsic_msvc_import_env_x64, "msvc_import_env_x64" },
    { Intrinsic_msvc_import_env_x86, "msvc_import_env_x86" },
    
};

IntrinsicFunction* IntrinsicFromIdentifier(String identifier)
{
    foreach(i, countof(intrinsics)) {
        if (intrinsics[i].identifier == identifier) return intrinsics[i].fn;
    }
    return NULL;
}