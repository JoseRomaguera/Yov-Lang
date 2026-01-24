#include "inc.h"

//- CORE

void Intrinsic_typeof(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    Reference ref = params[0];
    VType vtype = ref.vtype;
    
    if (vtype == VType_Nil) {
        InvalidCodepath();
        vtype = VType_Void;
    }
    
    Reference type = object_alloc(inter, VType_Type);
    ref_assign_Type(inter, type, vtype);
    returns[0] = type;
}

void Intrinsic_print(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String str = string_from_ref(context.arena, params[0]);
    OsPrint(PrintLevel_UserCode, str);
}

void Intrinsic_println(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String str = string_from_ref(context.arena, params[0]);
    PrintEx(PrintLevel_UserCode, "%S\n", str);
}

void Intrinsic_exit(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    I64 exit_code = get_int(params[0]);
    interpreter_exit(inter, (I32)exit_code);
}

void Intrinsic_set_cd(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    Reference ref = get_cd(inter);
    String path = get_string(params[0]);
    
    if (!os_path_is_absolute(path)) {
        path = path_resolve(context.arena, path_append(context.arena, get_string(ref), path));
    }
    
    Result res;
    
    if (OsPathExists(path)) {
        res = RESULT_SUCCESS;
        ref_string_set(inter, ref, path);
    }
    else {
        res = ResultMakeFailed("Path does not exists");
    }
    
    returns[0] = ref_from_Result(inter, res);
}

void Intrinsic_assert(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    B32 result = get_bool(params[0]);
    
    Result res = RESULT_SUCCESS;
    if (!result) {
        String line_sample = yov_get_line_sample(context.arena, location);
        res = ResultMakeFailed(StrFormat(yov->arena, "Assertion failed: %S", line_sample));
    }
    
    returns[0] = ref_from_Result(inter, res);
}

void Intrinsic_failed(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String message = get_string(params[0]);
    I64 exit_code = get_int(params[1]);
    
    Result res = ResultMakeFailed(message, exit_code);
    returns[0] = ref_from_Result(inter, res);
}

void Intrinsic_sleep(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    I64 millis = get_int(params[0]);
    os_thread_sleep(millis);
}

void Intrinsic_env(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String name = get_string(params[0]);
    String value;
    Result res = os_env_get(context.arena, &value, name);
    
    returns[0] = alloc_string(inter, value);
    returns[1] = ref_from_Result(inter, res);
}

void Intrinsic_env_path(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String name = get_string(params[0]);
    String value;
    Result res = os_env_get(context.arena, &value, name);
    
    if (!res.failed) {
        value = path_resolve(context.arena, value);
    }
    
    returns[0] = alloc_string(inter, value);
    returns[1] = ref_from_Result(inter, res);
}

void Intrinsic_env_path_array(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String name = get_string(params[0]);
    String value;
    Result res = os_env_get(context.arena, &value, name);
    
    Reference array;
    
    if (!res.failed)
    {
        Array<String> values = string_split(context.arena, value, ";");
        array = alloc_array(inter, VType_String, values.count);
        
        foreach(i, values.count) {
            Reference element = ref_get_member(inter, array, i);
            String path = values[i];
            path = path_resolve(context.arena, path);
            ref_string_set(inter, element, path);
        }
    }
    else {
        array = alloc_array(inter, VType_String, 0);
    }
    
    returns[0] = array;
    returns[1] = ref_from_Result(inter, res);
}

//- CONSOLE 

void Intrinsic_console_write(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String str = get_string(params[0]);
    OsPrint(PrintLevel_UserCode, str);
}

void Intrinsic_console_clear(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    OsConsoleClear();
}

//- EXTERNAL CALLS

internal_fn void return_from_external_call(Interpreter* inter, CallOutput res, Array<Reference> returns)
{
    Reference call_result = object_alloc(inter, VType_CallOutput);
    ref_assign_CallOutput(inter, call_result, res);
    
    returns[0] = call_result;
    returns[1] = ref_from_Result(inter, res.result);
}

void Intrinsic_call(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    CallOutput res = {};
    
    String command_line = get_string(params[0]);
    
    res.result = user_assertion(inter, StrFormat(context.arena, "Call:\n%S", command_line));
    
    RedirectStdout redirect_stdout = get_calls_redirect_stdout(inter);
    
    if (!res.result.failed) {
        res = os_call(context.arena, get_cd_value(inter), command_line, redirect_stdout);
    }
    
    return_from_external_call(inter, res, returns);
}

void Intrinsic_call_exe(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String exe_name = get_string(params[0]);
    String args = get_string(params[1]);
    
    CallOutput res{};
    
    res.result = user_assertion(inter, StrFormat(context.arena, "Call Exe:\n%S %S", exe_name, args));
    
    RedirectStdout redirect_stdout = get_calls_redirect_stdout(inter);
    
    if (!res.result.failed) {
        res = os_call_exe(context.arena, get_cd_value(inter), exe_name, args, redirect_stdout);
    }
    
    return_from_external_call(inter, res, returns);
}

void Intrinsic_call_script(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String script_name = get_string(params[0]);
    String args = get_string(params[1]);
    String yov_args = get_string(params[2]);
    
    CallOutput res{};
    
    res.result = user_assertion(inter, StrFormat(context.arena, "Call Script:\n%S %S %S", yov_args, script_name, args));
    
    RedirectStdout redirect_stdout = get_calls_redirect_stdout(inter);
    
    if (!res.result.failed) {
        res = os_call_script(context.arena, get_cd_value(inter), script_name, args, yov_args, redirect_stdout);
    }
    
    return_from_external_call(inter, res, returns);
}

//- UTILS

void Intrinsic_path_resolve(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String res = path_resolve(context.arena, get_string(params[0]));
    returns[0] = alloc_string(inter, res);
}

void Intrinsic_str_get_codepoint(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String str = get_string(params[0]);
    U64 cursor = get_int(params[1]);
    
    U32 codepoint = string_get_codepoint(str, &cursor);
    
    returns[0] = alloc_int(inter, codepoint);
    returns[1] = alloc_int(inter, cursor);
}

void Intrinsic_str_split(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String str = get_string(params[0]);
    String separator = get_string(params[1]);
    
    Array<String> result = string_split(context.arena, str, separator);
    
    Reference array = alloc_array(inter, VType_String, result.count);
    foreach(i, result.count) {
        ref_set_member(inter, array, i, alloc_string(inter, result[i]));
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

void Intrinsic_json_route(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String json = get_string(params[0]);
    String route = get_string(params[1]);
    
    Array<String> names = string_split(context.arena, route, "/");
    
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
    
    returns[0] = alloc_string(inter, json);
    returns[1] = ref_from_Result(inter, res);
}

//- YOV

void Intrinsic_yov_require(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    I64 major = get_int(params[0]);
    I64 minor = get_int(params[1]);
    
    Result res = RESULT_SUCCESS;
    
    B32 valid = major == YOV_MAJOR_VERSION && minor == YOV_MINOR_VERSION;
    if (!valid) {
        res = ResultMakeFailed(StrFormat(context.arena, "Require version: Yov v%u.%u", major, minor));
    }
    
    returns[0] = ref_from_Result(inter, res);
}

void Intrinsic_yov_require_min(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
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
    
    returns[0] = ref_from_Result(inter, res);
}

void Intrinsic_yov_require_max(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
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
    
    returns[0] = ref_from_Result(inter, res);
}

void Intrinsic_yov_parse(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
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
    
    Reference out = object_alloc(inter, VType_YovParseOutput);
    ref_assign_YovParseOutput(inter, out, temp_yov);
    
    yov = temp_yov;
    
    yov_shutdown();
    yov = last_yov;
    
    returns[0] = out;
#endif
}

//- MISC

void Intrinsic_ask_yesno(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String content = get_string(params[0]);
    B32 result = yov_ask_yesno("Ask", content);
    returns[0] = alloc_bool(inter, result);
}

void Intrinsic_exists(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String path = get_string(params[0]);
    B32 result = OsPathExists(path);
    returns[0] = alloc_bool(inter, result);
}

void Intrinsic_create_directory(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String path = path_absolute_to_cd(context.arena, inter, get_string(params[0]));
    B32 recursive = get_bool(params[1]);
    
    Result res = user_assertion(inter, StrFormat(context.arena, "Create directory:\n%S", path));
    
    if (!res.failed) {
        res = OsCreateDirectory(path, recursive);
    }
    
    returns[0] = ref_from_Result(inter, res);
}

void Intrinsic_delete_directory(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String path = path_absolute_to_cd(context.arena, inter, get_string(params[0]));
    
    Result res = user_assertion(inter, StrFormat(context.arena, "Delete directory:\n%S", path));
    
    if (!res.failed) {
        res = OsDeleteDirectory(path);
    }
    
    returns[0] = ref_from_Result(inter, res);
}

void Intrinsic_copy_directory(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String dst = path_absolute_to_cd(context.arena, inter, get_string(params[0]));
    String src = path_absolute_to_cd(context.arena, inter, get_string(params[1]));
    
    Result res = user_assertion(inter, StrFormat(context.arena, "Copy directory\n'%S'\nto\n'%S'", src, dst));
    
    if (!res.failed) {
        res = OsCopyDirectory(dst, src);
    }
    
    returns[0] = ref_from_Result(inter, res);
}

void Intrinsic_move_directory(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String dst = path_absolute_to_cd(context.arena, inter, get_string(params[0]));
    String src = path_absolute_to_cd(context.arena, inter, get_string(params[1]));
    
    Result res = user_assertion(inter, StrFormat(context.arena, "Move directory\n'%S'\nto\n'%S'", src, dst));
    
    if (!res.failed) {
        res = OsMoveDirectory(dst, src);
    }
    
    returns[0] = ref_from_Result(inter, res);
}

void Intrinsic_copy_file(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String dst = path_absolute_to_cd(context.arena, inter, get_string(params[0]));
    String src = path_absolute_to_cd(context.arena, inter, get_string(params[1]));
    CopyMode copy_mode = get_enum_CopyMode(params[2]);
    
    Result res = user_assertion(inter, StrFormat(context.arena, "Copy file\n'%S'\nto\n'%S'", src, dst));
    
    if (!res.failed) res = OsCopyFile(dst, src, copy_mode == CopyMode_Override);
    
    returns[0] = ref_from_Result(inter, res);
}

void Intrinsic_move_file(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String dst = path_absolute_to_cd(context.arena, inter, get_string(params[0]));
    String src = path_absolute_to_cd(context.arena, inter, get_string(params[1]));
    
    Result res = user_assertion(inter, StrFormat(context.arena, "Move file\n'%S'\nto\n'%S'", src, dst));
    
    if (!res.failed) res = OsMoveFile(dst, src);
    
    returns[0] = ref_from_Result(inter, res);
}

void Intrinsic_delete_file(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String path = path_absolute_to_cd(context.arena, inter, get_string(params[0]));
    
    Result res = user_assertion(inter, StrFormat(context.arena, "Delete file:\n'%S'", path));
    
    if (!res.failed) res = OsDeleteFile(path);
    
    returns[0] = ref_from_Result(inter, res);
}

void Intrinsic_write_entire_file(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String path = path_absolute_to_cd(context.arena, inter, get_string(params[0]));
    String content = get_string(params[1]);
    // TODO(Jose): B32 append = get_bool(params[2]);
    
    Result res = user_assertion(inter, StrFormat(context.arena, "Write entire file:\n'%S'", path));
    if (!res.failed) res = OsWriteEntireFile(path, { content.data, content.size });
    
    returns[0] = ref_from_Result(inter, res);
}

void Intrinsic_read_entire_file(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String path = path_absolute_to_cd(context.arena, inter, get_string(params[0]));
    
    Result res = user_assertion(inter, StrFormat(context.arena, "Read entire file:\n'%S'", path));
    RBuffer content{};
    if (!res.failed) res = OsReadEntireFile(context.arena, path, &content);
    String content_str = StrMake((char*)content.data, content.size);
    
    returns[0] = alloc_string(inter, content_str);
    returns[1] = ref_from_Result(inter, res);
}

void Intrinsic_file_get_info(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String path = path_absolute_to_cd(context.arena, inter, get_string(params[0]));
    
    FileInfo info;
    Result res = OsFileGetInfo(context.arena, path, &info);
    
    Reference ret = object_alloc(inter, VType_FileInfo);
    if (!res.failed) ref_assign_FileInfo(inter, ret, info);
    
    returns[0] = ret;
    returns[1] = ref_from_Result(inter, res);
}

void Intrinsic_dir_get_files_info(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    String path = path_absolute_to_cd(context.arena, inter, get_string(params[0]));
    
    Array<FileInfo> infos;
    Result res = OsDirGetFilesInfo(context.arena, path, &infos);
    
    Reference ret = alloc_array(inter, VType_FileInfo, infos.count);
    if (!res.failed) {
        foreach(i, infos.count) {
            Reference element = ref_get_member(inter, ret, i);
            ref_assign_FileInfo(inter, element, infos[i]);
        }
    }
    
    returns[0] = ret;
    returns[1] = ref_from_Result(inter, res);
}

//- MSVC

void Intrinsic_msvc_import_env_x64(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    Result res = MSVCImportEnv(MSVC_Env_x64);
    returns[0] = ref_from_Result(inter, res);
}

void Intrinsic_msvc_import_env_x86(Interpreter* inter, Array<Reference> params, Array<Reference> returns, Location location)
{
    Result res = MSVCImportEnv(MSVC_Env_x86);
    returns[0] = ref_from_Result(inter, res);
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