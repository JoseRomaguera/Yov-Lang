#include "inc.h"

//- CORE 

internal_fn void intrinsic__typeof(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    
    Value value = params[0];
    VariableType* vtype = value_get_expected_vtype(value);
    
    if (vtype == nil_vtype) {
        invalid_codepath();
        vtype = void_vtype;
    }
    
    Value type = object_alloc(inter, VType_Type);
    value_assign_Type(inter, type, vtype);
    returns[0] = type;
}

internal_fn void intrinsic__print(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    String str = string_from_value(scratch.arena, inter, params[0]);
    print_info(str);
}

internal_fn void intrinsic__println(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    String str = string_from_value(scratch.arena, inter, params[0]);
    print_info("%S\n", str);
}

internal_fn void intrinsic__exit(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    i64 exit_code = get_int(params[0]);
    interpreter_exit(inter, (i32)exit_code);
}

internal_fn void intrinsic__set_cd(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    
    Value value = get_cd(inter);
    String path = get_string(params[0]);
    
    if (!os_path_is_absolute(path)) {
        path = path_resolve(scratch.arena, path_append(scratch.arena, get_string(value), path));
    }
    
    Result res;
    
    if (os_exists(path)) {
        res = RESULT_SUCCESS;
        set_string(inter, value, path);
    }
    else {
        res = result_failed_make("Path does not exists");
    }
    
    returns[0] = value_from_Result(inter, res);
}

internal_fn void intrinsic__assert(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    
    b32 result = get_bool(params[0]);
    
    Result res = RESULT_SUCCESS;
    if (!result) {
        String line_sample = yov_get_line_sample(scratch.arena, code);
        res = result_failed_make(string_format(yov->static_arena, "Assertion failed: %S", line_sample));
    }
    
    returns[0] = value_from_Result(inter, res);
}

internal_fn void intrinsic__failed(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    
    String message = get_string(params[0]);
    i64 exit_code = get_int(params[1]);
    
    Result res = result_failed_make(message, exit_code);
    returns[0] = value_from_Result(inter, res);
}

internal_fn void intrinsic__env(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    
    String name = get_string(params[0]);
    String value;
    Result res = os_env_get(scratch.arena, &value, name);
    
    returns[0] = alloc_string(inter, value);
    returns[1] = value_from_Result(inter, res);
}

internal_fn void intrinsic__env_path(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    
    String name = get_string(params[0]);
    String value;
    Result res = os_env_get(scratch.arena, &value, name);
    
    if (!res.failed) {
        value = path_resolve(scratch.arena, value);
    }
    
    returns[0] = alloc_string(inter, value);
    returns[1] = value_from_Result(inter, res);
}

internal_fn void intrinsic__env_path_array(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    
    String name = get_string(params[0]);
    String value;
    Result res = os_env_get(scratch.arena, &value, name);
    
    Value array;
    
    if (!res.failed)
    {
        Array<String> values = string_split(scratch.arena, value, ";");
        array = alloc_array(inter, VType_String, values.count, false);
        
        foreach(i, values.count) {
            Value element = value_get_element(inter, array, i);
            String path = values[i];
            path = path_resolve(scratch.arena, path);
            set_string(inter, element, path);
        }
    }
    else {
        array = alloc_array(inter, VType_String, 0, false);
    }
    
    returns[0] = array;
    returns[1] = value_from_Result(inter, res);
}

//- EXTERNAL CALLS 

internal_fn void return_from_external_call(Interpreter* inter, CallOutput res, Array<Value> returns)
{
    Value call_result = object_alloc(inter, VType_CallOutput);
    value_assign_CallOutput(inter, call_result, res);
    
    returns[0] = call_result;
    returns[1] = value_from_Result(inter, res.result);
}

internal_fn void intrinsic__call(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
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

internal_fn void intrinsic__call_exe(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
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

internal_fn void intrinsic__call_script(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
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

internal_fn void intrinsic__path_resolve(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    String res = path_resolve(scratch.arena, get_string(params[0]));
    returns[0] = alloc_string(inter, res);
}

internal_fn void intrinsic__str_get_codepoint(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    
    String str = get_string(params[0]);
    u64 cursor = get_int(params[1]);
    
    u32 codepoint = string_get_codepoint(str, &cursor);
    
    returns[0] = alloc_int(inter, codepoint);
    returns[1] = alloc_int(inter, cursor);
}

internal_fn void intrinsic__str_split(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    
    String str = get_string(params[0]);
    String separator = get_string(params[1]);
    
    Array<String> result = string_split(scratch.arena, str, separator);
    
    Value array = alloc_array(inter, VType_String, result.count, true);
    foreach(i, result.count) {
        Value member = value_get_element(inter, array, i);
        value_assign_ref(inter, &member, alloc_string(inter, result[i]));
    }
    
    returns[0] = array;
}

//- YOV 

internal_fn void intrinsic__yov_require(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    i64 major = get_int(params[0]);
    i64 minor = get_int(params[1]);
    
    Result res = RESULT_SUCCESS;
    
    b32 valid = major == YOV_MAJOR_VERSION && minor == YOV_MINOR_VERSION;
    if (!valid) {
        res = result_failed_make(string_format(scratch.arena, "Require version: Yov v%u.%u", major, minor));
    }
    
    returns[0] = value_from_Result(inter, res);
}

internal_fn void intrinsic__yov_require_min(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
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
    
    returns[0] = value_from_Result(inter, res);
}

internal_fn void intrinsic__yov_require_max(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
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
    
    returns[0] = value_from_Result(inter, res);
}

//- MISC 

internal_fn void intrinsic__ask_yesno(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    String content = get_string(params[0]);
    b32 result = yov_ask_yesno("Ask", content);
    returns[0] = alloc_bool(inter, result);
}

internal_fn void intrinsic__exists(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    String path = get_string(params[0]);
    b32 result = os_exists(path);
    returns[0] = alloc_bool(inter, result);
}

internal_fn void intrinsic__create_directory(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    b32 recursive = get_bool(params[1]);
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Create directory:\n%S", path));
    
    if (!res.failed) {
        res = os_create_directory(path, recursive);
    }
    
    returns[0] = value_from_Result(inter, res);
}

internal_fn void intrinsic__delete_directory(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Delete directory:\n%S", path));
    
    if (!res.failed) {
        res = os_delete_directory(path);
    }
    
    returns[0] = value_from_Result(inter, res);
}

internal_fn void intrinsic__copy_directory(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    
    String dst = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    String src = path_absolute_to_cd(scratch.arena, inter, get_string(params[1]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Copy directory\n'%S'\nto\n'%S'", src, dst));
    
    if (!res.failed) {
        res = os_copy_directory(dst, src);
    }
    
    returns[0] = value_from_Result(inter, res);
}

internal_fn void intrinsic__move_directory(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    
    String dst = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    String src = path_absolute_to_cd(scratch.arena, inter, get_string(params[1]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Move directory\n'%S'\nto\n'%S'", src, dst));
    
    if (!res.failed) {
        res = os_move_directory(dst, src);
    }
    
    returns[0] = value_from_Result(inter, res);
}

internal_fn void intrinsic__copy_file(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    
    String dst = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    String src = path_absolute_to_cd(scratch.arena, inter, get_string(params[1]));
    CopyMode copy_mode = get_enum_CopyMode(params[2]);
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Copy file\n'%S'\nto\n'%S'", src, dst));
    
    if (!res.failed) res = os_copy_file(dst, src, copy_mode == CopyMode_Override);
    
    returns[0] = value_from_Result(inter, res);
}

internal_fn void intrinsic__move_file(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    
    String dst = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    String src = path_absolute_to_cd(scratch.arena, inter, get_string(params[1]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Move file\n'%S'\nto\n'%S'", src, dst));
    
    if (!res.failed) res = os_move_file(dst, src);
    
    returns[0] = value_from_Result(inter, res);
}

internal_fn void intrinsic__delete_file(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Delete file:\n'%S'", path));
    
    if (!res.failed) res = os_delete_file(path);
    
    returns[0] = value_from_Result(inter, res);
}

internal_fn void intrinsic__write_entire_file(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    String content = get_string(params[1]);
    // TODO(Jose): b32 append = get_bool(params[2]);
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Write entire file:\n'%S'", path));
    if (!res.failed) res = os_write_entire_file(path, { content.data, content.size });
    
    returns[0] = value_from_Result(inter, res);
}

internal_fn void intrinsic__read_entire_file(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Read entire file:\n'%S'", path));
    RawBuffer content{};
    if (!res.failed) res = os_read_entire_file(scratch.arena, path, &content);
    String content_str = string_make((char*)content.data, content.size);
    
    returns[0] = alloc_string(inter, content_str);
    returns[1] = value_from_Result(inter, res);
}

internal_fn void intrinsic__file_get_info(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    
    FileInfo info;
    Result res = os_file_get_info(scratch.arena, path, &info);
    
    Value ret = object_alloc(inter, VType_FileInfo);
    if (!res.failed) value_assign_FileInfo(inter, ret, info);
    
    returns[0] = ret;
    returns[1] = value_from_Result(inter, res);
}

internal_fn void intrinsic__dir_get_files_info(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(params[0]));
    
    Array<FileInfo> infos;
    Result res = os_dir_get_files_info(scratch.arena, path, &infos);
    
    Value ret = alloc_array(inter, VType_FileInfo, infos.count, false);
    if (!res.failed) {
        foreach(i, infos.count) {
            Value element = value_get_element(inter, ret, i);
            value_assign_FileInfo(inter, element, infos[i]);
        }
    }
    
    returns[0] = ret;
    returns[1] = value_from_Result(inter, res);
}

//- MSVC 

internal_fn void intrinsic__msvc_import_env_x64(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    Result res = os_msvc_import_env(MSVC_Env_x64);
    returns[0] = value_from_Result(inter, res);
}

internal_fn void intrinsic__msvc_import_env_x86(Interpreter* inter, Array<Value> params, Array<Value> returns, CodeLocation code)
{
    Result res = os_msvc_import_env(MSVC_Env_x86);
    returns[0] = value_from_Result(inter, res);
}

#define INTR(name, fn) { STR(name), fn }

Array<IntrinsicDefinition> get_intrinsics_table(Arena* arena)
{
    IntrinsicDefinition table[] = {
        // Core
        INTR("typeof", intrinsic__typeof),
        INTR("print", intrinsic__print),
        INTR("println", intrinsic__println),
        INTR("exit", intrinsic__exit),
        INTR("set_cd", intrinsic__set_cd),
        INTR("assert", intrinsic__assert),
        INTR("failed", intrinsic__failed),
        
        INTR("env", intrinsic__env),
        INTR("env_path", intrinsic__env_path),
        INTR("env_path_array", intrinsic__env_path_array),
        
        // External Calls
        INTR("call", intrinsic__call),
        INTR("call_exe", intrinsic__call_exe),
        INTR("call_script", intrinsic__call_script),
        
        // String Utils
        INTR("path_resolve", intrinsic__path_resolve),
        INTR("str_get_codepoint", intrinsic__str_get_codepoint),
        INTR("str_split", intrinsic__str_split),
        
        // Yov
        INTR("yov_require", intrinsic__yov_require),
        INTR("yov_require_min", intrinsic__yov_require_min),
        INTR("yov_require_max", intrinsic__yov_require_max),
        
        // User
        INTR("ask_yesno", intrinsic__ask_yesno),
        
        // File System
        INTR("exists", intrinsic__exists),
        INTR("create_directory", intrinsic__create_directory),
        INTR("delete_directory", intrinsic__delete_directory),
        INTR("copy_directory", intrinsic__copy_directory),
        INTR("move_directory", intrinsic__move_directory),
        INTR("copy_file", intrinsic__copy_file),
        INTR("move_file", intrinsic__move_file),
        INTR("delete_file", intrinsic__delete_file),
        INTR("read_entire_file", intrinsic__read_entire_file),
        INTR("write_entire_file", intrinsic__write_entire_file),
        INTR("file_get_info", intrinsic__file_get_info),
        INTR("dir_get_files_info", intrinsic__dir_get_files_info),
        
        // MSVC
        INTR("msvc_import_env_x64", intrinsic__msvc_import_env_x64),
        INTR("msvc_import_env_x86", intrinsic__msvc_import_env_x86),
    };
    
    return array_copy(arena, array_make(table, array_count(table)));
}