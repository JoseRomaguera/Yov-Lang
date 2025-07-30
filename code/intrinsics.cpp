#include "inc.h"

//- CORE 

internal_fn FunctionReturn intrinsic__typeof(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    Value value = vars[0];
    i32 vtype = value_get_expected_vtype(value);
    
    Value type = value_nil();
    Result res = result_failed_make("Unknown type");
    
    if (vtype >= 0) {
        res = RESULT_SUCCESS;
        type = object_alloc(inter, VType_Type);
        value_assign_Type(inter, type, vtype);
    }
    
    return { type, res };
}

internal_fn FunctionReturn intrinsic__print(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    String str = string_from_value(scratch.arena, inter, vars[0]);
    print_info(str);
    
    return { value_void(), RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__println(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    String str = string_from_value(scratch.arena, inter, vars[0]);
    print_info("%S\n", str);
    
    return { value_void(), RESULT_SUCCESS };
}

internal_fn FunctionReturn return_from_external_call(Interpreter* inter, CallResult res)
{
    Value ret = object_alloc(inter, VType_CallResult);
    Value ret_stdout = value_get_member(inter, ret, "stdout");
    Value ret_exit_code = value_get_member(inter, ret, "exit_code");
    
    set_string(inter, ret_stdout, res.stdout);
    set_int(ret_exit_code, res.result.exit_code);
    
    return { ret, res.result };
}

internal_fn FunctionReturn intrinsic__exit(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    i64 exit_code = get_int(vars[0]);
    interpreter_exit(inter, (i32)exit_code);
    return { value_void(), RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__set_cd(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    Value value = get_cd(inter);
    String path = get_string(vars[0]);
    
    if (os_path_is_absolute(path)) {
        value_assign(inter, &value, vars[0]);
        return { value_void(), RESULT_SUCCESS };
    }
    
    String res = path_resolve(scratch.arena, path_append(scratch.arena, get_string(value), path));
    set_string(inter, value, res);
    
    return { value_void(), RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__assert(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    b32 result = get_bool(vars[0]);
    
    Result res = RESULT_SUCCESS;
    if (!result) {
        String line_sample = yov_get_line_sample(scratch.arena, code);
        res = result_failed_make(string_format(yov->static_arena, "Assertion failed: %S", line_sample));
    }
    
    return { alloc_bool(inter, result), res };
}

internal_fn FunctionReturn intrinsic__env(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    String name = get_string(vars[0]);
    String value;
    Result res = os_env_get(scratch.arena, &value, name);
    
    return { alloc_string(inter, value), res };
}

internal_fn FunctionReturn intrinsic__env_path(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    String name = get_string(vars[0]);
    String value;
    Result res = os_env_get(scratch.arena, &value, name);
    if (!res.success) return { alloc_string(inter, ""), res };
    
    value = path_resolve(scratch.arena, value);
    
    return { alloc_string(inter, value), res };
}

internal_fn FunctionReturn intrinsic__env_array(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    String name = get_string(vars[0]);
    String value;
    Result res = os_env_get(scratch.arena, &value, name);
    if (!res.success) return { alloc_array(inter, VType_String, 0, false), res };
    
    Array<String> values = string_split(scratch.arena, value, ";");
    Value array = alloc_array(inter, VType_String, values.count, false);
    
    foreach(i, values.count) {
        Value element = value_get_element(inter, array, i);
        set_string(inter, element, values[i]);
    }
    
    return { array, res };
}

//- EXTERNAL CALLS 

internal_fn FunctionReturn intrinsic__call(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    CallResult res = {};
    
    String command_line = get_string(vars[0]);
    
    res.result = user_assertion(inter, string_format(scratch.arena, "Call:\n%S", command_line));
    
    RedirectStdout redirect_stdout = get_calls_redirect_stdout(inter);
    
    if (res.result.success) {
        res = os_call(scratch.arena, get_cd_value(inter), command_line, redirect_stdout);
    }
    
    return return_from_external_call(inter, res);
}

internal_fn FunctionReturn intrinsic__call_exe(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    String exe_name = get_string(vars[0]);
    String args = get_string(vars[1]);
    
    CallResult res{};
    
    res.result = user_assertion(inter, string_format(scratch.arena, "Call Exe:\n%S %S", exe_name, args));
    
    RedirectStdout redirect_stdout = get_calls_redirect_stdout(inter);
    
    if (res.result.success) {
        res = os_call_exe(scratch.arena, get_cd_value(inter), exe_name, args, redirect_stdout);
    }
    
    return return_from_external_call(inter, res);
}

internal_fn FunctionReturn intrinsic__call_script(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    String script_name = get_string(vars[0]);
    String args = get_string(vars[1]);
    String yov_args = get_string(vars[2]);
    
    CallResult res{};
    
    res.result = user_assertion(inter, string_format(scratch.arena, "Call Script:\n%S %S %S", yov_args, script_name, args));
    
    RedirectStdout redirect_stdout = get_calls_redirect_stdout(inter);
    
    if (res.result.success) {
        res = os_call_script(scratch.arena, get_cd_value(inter), script_name, args, yov_args, redirect_stdout);
    }
    
    return return_from_external_call(inter, res);
}

//- UTILS 

internal_fn FunctionReturn intrinsic__path_resolve(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    String res = path_resolve(scratch.arena, get_string(vars[0]));
    return { alloc_string(inter, res), RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__str_get_codepoint(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    Scope* last_scope = inter->current_scope;
    scope_push(inter, ScopeType_Block, VType_Void, false);
    DEFER(scope_pop(inter));
    
    Value cursor_value = lvalue_from_ref(scope_define_object_ref(inter, "dst", vars[1]));
    String str = get_string(vars[0]);
    
    u64 cursor = get_int(cursor_value);
    u32 codepoint = string_get_codepoint(str, &cursor);
    
    set_int(cursor_value, cursor);
    Value ret = alloc_int(inter, codepoint);
    scope_add_temporal(inter, last_scope, ret.obj);
    
    return { ret, RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__str_split(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    String str = get_string(vars[0]);
    String separator = get_string(vars[1]);
    Array<String> result = string_split(scratch.arena, str, separator);
    
    Value array = alloc_array(inter, VType_String, result.count, true);
    foreach(i, result.count) {
        Value member = value_get_element(inter, array, i);
        value_assign_ref(inter, &member, alloc_string(inter, result[i]));
    }
    
    return { array, RESULT_SUCCESS };
}

//- YOV 

internal_fn FunctionReturn intrinsic__yov_require(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    i64 major = get_int(vars[0]);
    i64 minor = get_int(vars[1]);
    
    b32 res = major == YOV_MAJOR_VERSION && minor == YOV_MINOR_VERSION;
    
    if (!res) {
        report_error(code, "Require version: Yov v%u.%u", major, minor);
    }
    
    return { value_void(), RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__yov_require_min(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    i64 major = get_int(vars[0]);
    i64 minor = get_int(vars[1]);
    
    b32 res = true;
    if (major > YOV_MAJOR_VERSION) res = false;
    else if (major == YOV_MAJOR_VERSION && minor > YOV_MINOR_VERSION) res = false;
    
    if (!res) {
        report_error(code, "Require minimum version: Yov v%u.%u", major, minor);
    }
    
    return { value_void(), RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__yov_require_max(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    i64 major = get_int(vars[0]);
    i64 minor = get_int(vars[1]);
    
    b32 res = false;
    if (major > YOV_MAJOR_VERSION) res = true;
    else if (major == YOV_MAJOR_VERSION && minor >= YOV_MINOR_VERSION) res = true;
    
    if (!res) {
        report_error(code, "Require maximum version: Yov v%u.%u", major, minor);
    }
    
    return { value_void(), RESULT_SUCCESS };
}

//- MISC 

internal_fn FunctionReturn intrinsic__ask_yesno(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    String content = get_string(vars[0]);
    
    b32 result = yov_ask_yesno("Ask", content);
    return { alloc_bool(inter, result), RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__exists(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    String path = get_string(vars[0]);
    b32 result = os_exists(path);
    
    return { alloc_bool(inter, result), RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__create_directory(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    b32 recursive = get_bool(vars[1]);
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Create directory:\n%S", path));
    
    if (res.success) {
        res = os_create_directory(path, recursive);
    }
    
    return { alloc_bool(inter, res.success), res };
}

internal_fn FunctionReturn intrinsic__delete_directory(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Delete directory:\n%S", path));
    
    if (res.success) {
        res = os_delete_directory(path);
    }
    
    return { alloc_bool(inter, res.success), res };
}

internal_fn FunctionReturn intrinsic__copy_directory(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    String dst = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    String src = path_absolute_to_cd(scratch.arena, inter, get_string(vars[1]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Copy directory\n'%S'\nto\n'%S'", src, dst));
    
    if (res.success) {
        res = os_copy_directory(dst, src);
    }
    
    return { alloc_bool(inter, res.success), res };
}

internal_fn FunctionReturn intrinsic__move_directory(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    String dst = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    String src = path_absolute_to_cd(scratch.arena, inter, get_string(vars[1]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Move directory\n'%S'\nto\n'%S'", src, dst));
    
    if (res.success) {
        res = os_move_directory(dst, src);
    }
    
    return { alloc_bool(inter, res.success), res };
}

internal_fn FunctionReturn intrinsic__copy_file(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    String dst = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    String src = path_absolute_to_cd(scratch.arena, inter, get_string(vars[1]));
    CopyMode copy_mode = get_enum_CopyMode(inter, vars[2]);
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Copy file\n'%S'\nto\n'%S'", src, dst));
    
    if (res.success) res = os_copy_file(dst, src, copy_mode == CopyMode_Override);
    
    return { alloc_bool(inter, res.success), res };
}

internal_fn FunctionReturn intrinsic__move_file(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    String dst = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    String src = path_absolute_to_cd(scratch.arena, inter, get_string(vars[1]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Move file\n'%S'\nto\n'%S'", src, dst));
    
    if (res.success) res = os_move_file(dst, src);
    
    return { alloc_bool(inter, res.success), res };
}

internal_fn FunctionReturn intrinsic__delete_file(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Delete file:\n'%S'", path));
    
    if (res.success) res = os_delete_file(path);
    
    return { alloc_bool(inter, res.success), res };
}

internal_fn FunctionReturn intrinsic__write_entire_file(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    String content = get_string(vars[1]);
    // TODO(Jose): b32 append = get_bool(vars[2]);
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Write entire file:\n'%S'", path));
    if (res.success) res = os_write_entire_file(path, { content.data, content.size });
    
    return { alloc_bool(inter, res.success), res };
}

internal_fn FunctionReturn intrinsic__read_entire_file(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    scope_push(inter, ScopeType_Block, VType_Void, false);
    DEFER(scope_pop(inter));
    
    Value dst = lvalue_from_ref(scope_define_object_ref(inter, "dst", vars[1]));
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Read entire file:\n'%S'", path));
    RawBuffer content{};
    if (res.success) res = os_read_entire_file(scratch.arena, path, &content);
    
    if (res.success) {
        String content_str = string_make((char*)content.data, content.size);
        value_assign(inter, &dst, alloc_string(inter, content_str));
    }
    
    return { alloc_bool(inter, res.success), res };
}

internal_fn FunctionReturn intrinsic__file_get_info(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    
    FileInfo info;
    Result res = os_file_get_info(scratch.arena, path, &info);
    
    Value ret = object_alloc(inter, VType_FileInfo);
    if (res.success) value_assign_FileInfo(inter, ret, info);
    
    return { ret, res };
}

internal_fn FunctionReturn intrinsic__dir_get_files_info(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    
    Array<FileInfo> infos;
    Result res = os_dir_get_files_info(scratch.arena, path, &infos);
    
    Value ret = alloc_array(inter, VType_FileInfo, infos.count, false);
    if (res.success) {
        foreach(i, infos.count) {
            Value element = value_get_element(inter, ret, i);
            value_assign_FileInfo(inter, element, infos[i]);
        }
    }
    
    return { ret, res };
}

//- MSVC 

internal_fn FunctionReturn intrinsic__msvc_import_env_x64(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    Result res = os_msvc_import_env(MSVC_Env_x64);
    return { alloc_bool(inter, res.success), res };
}

internal_fn FunctionReturn intrinsic__msvc_import_env_x86(Interpreter* inter, Array<Value> vars, CodeLocation code)
{
    SCRATCH();
    Result res = os_msvc_import_env(MSVC_Env_x86);
    return { alloc_bool(inter, res.success), res };
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
        
        INTR("env", intrinsic__env),
        INTR("env_path", intrinsic__env_path),
        INTR("env_array", intrinsic__env_array),
        
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