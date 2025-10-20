#include "inc.h"

//- CORE

void intrinsic__typeof(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    Reference ref = params[0];
    VariableType* vtype = ref.vtype;
    
    if (vtype == nil_vtype) {
        invalid_codepath();
        vtype = void_vtype;
    }
    
    Reference type = object_alloc(inter, VType_Type);
    ref_assign_Type(inter, type, vtype);
    returns[0] = type;
}

void intrinsic__print(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    String str = string_from_ref(scratch.arena, inter, params[0]);
    print_info(str);
}

void intrinsic__println(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    String str = string_from_ref(scratch.arena, inter, params[0]);
    print_info("%S\n", str);
}

void intrinsic__exit(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    i64 exit_code = get_int(params[0]);
    interpreter_exit(inter, (i32)exit_code);
}

void intrinsic__set_cd(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    Reference ref = get_cd(inter);
    String path = get_string(params[0]);
    
    if (!os_path_is_absolute(path)) {
        path = path_resolve(scratch.arena, path_append(scratch.arena, get_string(ref), path));
    }
    
    Result res;
    
    if (os_exists(path)) {
        res = RESULT_SUCCESS;
        ref_string_set(inter, ref, path);
    }
    else {
        res = result_failed_make("Path does not exists");
    }
    
    returns[0] = ref_from_Result(inter, res);
}

void intrinsic__assert(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    b32 result = get_bool(params[0]);
    
    Result res = RESULT_SUCCESS;
    if (!result) {
        String line_sample = yov_get_line_sample(scratch.arena, code);
        res = result_failed_make(string_format(yov->static_arena, "Assertion failed: %S", line_sample));
    }
    
    returns[0] = ref_from_Result(inter, res);
}

void intrinsic__failed(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    String message = get_string(params[0]);
    i64 exit_code = get_int(params[1]);
    
    Result res = result_failed_make(message, exit_code);
    returns[0] = ref_from_Result(inter, res);
}

void intrinsic__thread_sleep(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    i64 millis = get_int(params[0]);
    os_thread_sleep(millis);
}

void intrinsic__env(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    String name = get_string(params[0]);
    String value;
    Result res = os_env_get(scratch.arena, &value, name);
    
    returns[0] = alloc_string(inter, value);
    returns[1] = ref_from_Result(inter, res);
}

void intrinsic__env_path(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    String name = get_string(params[0]);
    String value;
    Result res = os_env_get(scratch.arena, &value, name);
    
    if (!res.failed) {
        value = path_resolve(scratch.arena, value);
    }
    
    returns[0] = alloc_string(inter, value);
    returns[1] = ref_from_Result(inter, res);
}

void intrinsic__env_path_array(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    String name = get_string(params[0]);
    String value;
    Result res = os_env_get(scratch.arena, &value, name);
    
    Reference array;
    
    if (!res.failed)
    {
        Array<String> values = string_split(scratch.arena, value, ";");
        array = alloc_array(inter, VType_String, values.count, false);
        
        foreach(i, values.count) {
            Reference element = ref_get_member(inter, array, i);
            String path = values[i];
            path = path_resolve(scratch.arena, path);
            ref_string_set(inter, element, path);
        }
    }
    else {
        array = alloc_array(inter, VType_String, 0, false);
    }
    
    returns[0] = array;
    returns[1] = ref_from_Result(inter, res);
}

//- CONSOLE 

void intrinsic__console_write(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    String str = get_string(params[0]);
    print_info(str);
}

void intrinsic__console_set_cursor(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    i64 x = get_int(params[0]);
    i64 y = get_int(params[1]);
    
    os_console_set_cursor(x, y);
}

void intrinsic__console_clear(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    os_console_clear();
}

//- EXTERNAL CALLS

internal_fn void return_from_external_call(Interpreter* inter, CallOutput res, Array<Reference> returns)
{
    Reference call_result = object_alloc(inter, VType_CallOutput);
    ref_assign_CallOutput(inter, call_result, res);
    
    returns[0] = call_result;
    returns[1] = ref_from_Result(inter, res.result);
}

void intrinsic__call(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    CallOutput res = {};
    
    String command_line = get_string(params[0]);
    
    res.result = user_assertion(inter, string_format(scratch.arena, "Call:\n%S", command_line));
    
    RedirectStdout redirect_stdout = get_calls_redirect_stdout(inter);
    
    if (!res.result.failed) {
        res = os_call(scratch.arena, get_cd_value(inter), command_line, redirect_stdout);
    }
    
    return_from_external_call(inter, res, returns);
}

void intrinsic__call_exe(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    String exe_name = get_string(params[0]);
    String args = get_string(params[1]);
    
    CallOutput res{};
    
    res.result = user_assertion(inter, string_format(scratch.arena, "Call Exe:\n%S %S", exe_name, args));
    
    RedirectStdout redirect_stdout = get_calls_redirect_stdout(inter);
    
    if (!res.result.failed) {
        res = os_call_exe(scratch.arena, get_cd_value(inter), exe_name, args, redirect_stdout);
    }
    
    return_from_external_call(inter, res, returns);
}

void intrinsic__call_script(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    String script_name = get_string(params[0]);
    String args = get_string(params[1]);
    String yov_args = get_string(params[2]);
    
    CallOutput res{};
    
    res.result = user_assertion(inter, string_format(scratch.arena, "Call Script:\n%S %S %S", yov_args, script_name, args));
    
    RedirectStdout redirect_stdout = get_calls_redirect_stdout(inter);
    
    if (!res.result.failed) {
        res = os_call_script(scratch.arena, get_cd_value(inter), script_name, args, yov_args, redirect_stdout);
    }
    
    return_from_external_call(inter, res, returns);
}

//- UTILS

void intrinsic__path_resolve(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    String res = path_resolve(scratch.arena, get_string(params[0]));
    returns[0] = alloc_string(inter, res);
}

void intrinsic__str_get_codepoint(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    String str = get_string(params[0]);
    u64 cursor = get_int(params[1]);
    
    u32 codepoint = string_get_codepoint(str, &cursor);
    
    returns[0] = alloc_int(inter, codepoint);
    returns[1] = alloc_int(inter, cursor);
}

void intrinsic__str_split(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    String str = get_string(params[0]);
    String separator = get_string(params[1]);
    
    Array<String> result = string_split(scratch.arena, str, separator);
    
    Reference array = alloc_array(inter, VType_String, result.count, true);
    foreach(i, result.count) {
        ref_set_member(inter, array, i, alloc_string(inter, result[i]));
    }
    
    returns[0] = array;
}

struct JsonProperty {
    String name;
    String value;
};

internal_fn u64 json_skip(String json, u64 cursor)
{
    if (cursor != 0) {
        while (cursor < json.size && json[cursor] != ',') cursor++;
    }
    if (cursor >= json.size) return cursor;
    while (cursor < json.size && json[cursor] != '\"') cursor++;
    return cursor;
}

internal_fn JsonProperty json_get_property(String json, u64 cursor)
{
    while (cursor < json.size && json[cursor] != ':') cursor++;
    if (cursor >= json.size) return {};
    
    u64 separator = cursor;// TODO(Jose): 
}

internal_fn b32 json_access(String* dst, String json, String searching_name)
{
    *dst = {};
    
    u64 cursor = 0;
    
    while (cursor < json.size)
    {
        if (json[cursor] == '\"')
        {
            u64 name_begin = cursor + 1;
            u64 name_end = name_begin;
            
            while (name_end < json.size && json[name_end] != '\"')
                name_end++;
            
            String name = string_substring(json, name_begin, name_end - name_begin);
            
            if (string_equals(name, searching_name)) {
                // TODO(Jose): 
                return true;
            }
        }
        
        cursor = json_skip(json, cursor);
    }
    
    return false;
}

void intrinsic__json_route(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    String json = get_string(params[0]);
    String route = get_string(params[1]);
    
    Array<String> names = string_split(scratch.arena, route, "/");
    
    b32 success = true;
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
    if (!success) res = result_failed_make("Json route not found");
    
    returns[0] = alloc_string(inter, json);
    returns[1] = ref_from_Result(inter, res);
}

//- YOV

void intrinsic__yov_require(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    i64 major = get_int(params[0]);
    i64 minor = get_int(params[1]);
    
    Result res = RESULT_SUCCESS;
    
    b32 valid = major == YOV_MAJOR_VERSION && minor == YOV_MINOR_VERSION;
    if (!valid) {
        res = result_failed_make(string_format(scratch.arena, "Require version: Yov v%u.%u", major, minor));
    }
    
    returns[0] = ref_from_Result(inter, res);
}

void intrinsic__yov_require_min(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    i64 major = get_int(params[0]);
    i64 minor = get_int(params[1]);
    
    Result res = RESULT_SUCCESS;
    
    b32 valid = true;
    if (major > YOV_MAJOR_VERSION) valid = false;
    else if (major == YOV_MAJOR_VERSION && minor > YOV_MINOR_VERSION) valid = false;
    if (!valid) {
        res = result_failed_make(string_format(scratch.arena, "Require minimum version: Yov v%u.%u", major, minor));
    }
    
    returns[0] = ref_from_Result(inter, res);
}

void intrinsic__yov_require_max(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    i64 major = get_int(params[0]);
    i64 minor = get_int(params[1]);
    
    Result res = RESULT_SUCCESS;
    
    b32 valid = false;
    if (major > YOV_MAJOR_VERSION) valid = true;
    else if (major == YOV_MAJOR_VERSION && minor >= YOV_MINOR_VERSION) valid = true;
    if (!valid) {
        res = result_failed_make(string_format(scratch.arena, "Require maximum version: Yov v%u.%u", major, minor));
    }
    
    returns[0] = ref_from_Result(inter, res);
}

void intrinsic__yov_parse(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    String path = get_string(params[0]);
    
    path = path_absolute_to_cd(scratch.arena, inter, path);
    
    b32 file_is_core = string_ends(path, "code/core.yov");
    
    Yov* last_yov = yov;
    yov_initialize(!file_is_core);
    yov_config(path, {});
    
    InterpreterSettings settings = {};
    Interpreter* inter0 = yov_compile(settings, false, !file_is_core);
    
    Yov* temp_yov = yov;
    yov = last_yov;
    
    Reference out = object_alloc(inter, VType_YovParseOutput);
    ref_assign_YovParseOutput(inter, out, temp_yov);
    
    yov = temp_yov;
    
    interpreter_shutdown(inter0);
    yov_shutdown();
    yov = last_yov;
    
    returns[0] = out;
}

//- MISC

void intrinsic__ask_yesno(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    String content = get_string(params[0]);
    b32 result = yov_ask_yesno("Ask", content);
    returns[0] = alloc_bool(inter, result);
}

void intrinsic__exists(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    String path = get_string(params[0]);
    b32 result = os_exists(path);
    returns[0] = alloc_bool(inter, result);
}

void intrinsic__create_directory(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    b32 recursive = get_bool(params[1]);
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Create directory:\n%S", path));
    
    if (!res.failed) {
        res = os_create_directory(path, recursive);
    }
    
    returns[0] = ref_from_Result(inter, res);
}

void intrinsic__delete_directory(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Delete directory:\n%S", path));
    
    if (!res.failed) {
        res = os_delete_directory(path);
    }
    
    returns[0] = ref_from_Result(inter, res);
}

void intrinsic__copy_directory(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    String dst = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    String src = path_absolute_to_cd(scratch.arena, inter, get_string(params[1]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Copy directory\n'%S'\nto\n'%S'", src, dst));
    
    if (!res.failed) {
        res = os_copy_directory(dst, src);
    }
    
    returns[0] = ref_from_Result(inter, res);
}

void intrinsic__move_directory(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    String dst = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    String src = path_absolute_to_cd(scratch.arena, inter, get_string(params[1]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Move directory\n'%S'\nto\n'%S'", src, dst));
    
    if (!res.failed) {
        res = os_move_directory(dst, src);
    }
    
    returns[0] = ref_from_Result(inter, res);
}

void intrinsic__copy_file(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    String dst = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    String src = path_absolute_to_cd(scratch.arena, inter, get_string(params[1]));
    CopyMode copy_mode = get_enum_CopyMode(params[2]);
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Copy file\n'%S'\nto\n'%S'", src, dst));
    
    if (!res.failed) res = os_copy_file(dst, src, copy_mode == CopyMode_Override);
    
    returns[0] = ref_from_Result(inter, res);
}

void intrinsic__move_file(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    String dst = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    String src = path_absolute_to_cd(scratch.arena, inter, get_string(params[1]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Move file\n'%S'\nto\n'%S'", src, dst));
    
    if (!res.failed) res = os_move_file(dst, src);
    
    returns[0] = ref_from_Result(inter, res);
}

void intrinsic__delete_file(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Delete file:\n'%S'", path));
    
    if (!res.failed) res = os_delete_file(path);
    
    returns[0] = ref_from_Result(inter, res);
}

void intrinsic__write_entire_file(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    String content = get_string(params[1]);
    // TODO(Jose): b32 append = get_bool(params[2]);
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Write entire file:\n'%S'", path));
    if (!res.failed) res = os_write_entire_file(path, { content.data, content.size });
    
    returns[0] = ref_from_Result(inter, res);
}

void intrinsic__read_entire_file(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Read entire file:\n'%S'", path));
    RawBuffer content{};
    if (!res.failed) res = os_read_entire_file(scratch.arena, path, &content);
    String content_str = string_make((char*)content.data, content.size);
    
    returns[0] = alloc_string(inter, content_str);
    returns[1] = ref_from_Result(inter, res);
}

void intrinsic__file_get_info(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    
    FileInfo info;
    Result res = os_file_get_info(scratch.arena, path, &info);
    
    Reference ret = object_alloc(inter, VType_FileInfo);
    if (!res.failed) ref_assign_FileInfo(inter, ret, info);
    
    returns[0] = ret;
    returns[1] = ref_from_Result(inter, res);
}

void intrinsic__dir_get_files_info(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    
    Array<FileInfo> infos;
    Result res = os_dir_get_files_info(scratch.arena, path, &infos);
    
    Reference ret = alloc_array(inter, VType_FileInfo, infos.count, false);
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

void intrinsic__msvc_import_env_x64(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    Result res = os_msvc_import_env(MSVC_Env_x64);
    returns[0] = ref_from_Result(inter, res);
}

void intrinsic__msvc_import_env_x86(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code)
{
    Result res = os_msvc_import_env(MSVC_Env_x86);
    returns[0] = ref_from_Result(inter, res);
}
