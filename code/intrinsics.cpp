#include "inc.h"

//- CORE 

internal_fn FunctionReturn intrinsic__print(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    print_info("%S", get_string(vars[0]));
    
    return { inter->void_obj, RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__println(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    print_info("%S\n", get_string(vars[0]));
    return { inter->void_obj, RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__call(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    
    i32 result = -1;
    String command_line = get_string(vars[0]);
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Call:\n%S", command_line));
    
    if (res.success) {
        result = os_call(get_string(inter->cd_obj), command_line);
    }
    
    return { obj_alloc_temp_int(inter, result), RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__call_exe(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    
    i32 result = -1;
    String exe_name = get_string(vars[0]);
    String params = get_string(vars[1]);
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Call Exe:\n%S %S", exe_name, params));
    
    if (res.success) {
        result = os_call_exe(get_string(inter->cd_obj), exe_name, params);
    }
    
    return { obj_alloc_temp_int(inter, result), RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__exit(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    interpreter_exit(inter);
    return { inter->void_obj, RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__set_cd(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    
    Object* obj = inter->cd_obj;
    String path = get_string(vars[0]);
    
    if (os_path_is_absolute(path)) {
        obj_copy(inter, obj, vars[0]);
        return { inter->void_obj, RESULT_SUCCESS };
    }
    
    String res = path_resolve(scratch.arena, path_append(scratch.arena, get_string(obj), path));
    set_string(inter, obj, res);
    
    return { inter->void_obj, RESULT_SUCCESS };;
}

//- UTILS 

internal_fn FunctionReturn intrinsic__path_resolve(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    String res = path_resolve(scratch.arena, get_string(vars[0]));
    return { obj_alloc_temp_string(inter, res), RESULT_SUCCESS };
}

//- YOV 

internal_fn FunctionReturn intrinsic__yov_require(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    i64 major = get_int(vars[0]);
    i64 minor = get_int(vars[1]);
    
    b32 res = major == YOV_MAJOR_VERSION && minor == YOV_MINOR_VERSION;
    
    if (!res) {
        report_error(code, "Require version: Yov v%u.%u", major, minor);
    }
    
    return { inter->void_obj, RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__yov_require_min(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    i64 major = get_int(vars[0]);
    i64 minor = get_int(vars[1]);
    
    b32 res = true;
    if (major > YOV_MAJOR_VERSION) res = false;
    else if (major == YOV_MAJOR_VERSION && minor > YOV_MINOR_VERSION) res = false;
    
    if (!res) {
        report_error(code, "Require minimum version: Yov v%u.%u", major, minor);
    }
    
    return { inter->void_obj, RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__yov_require_max(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    i64 major = get_int(vars[0]);
    i64 minor = get_int(vars[1]);
    
    b32 res = false;
    if (major > YOV_MAJOR_VERSION) res = true;
    else if (major == YOV_MAJOR_VERSION && minor >= YOV_MINOR_VERSION) res = true;
    
    if (!res) {
        report_error(code, "Require maximum version: Yov v%u.%u", major, minor);
    }
    
    return { inter->void_obj, RESULT_SUCCESS };
}

//- ARGS 

internal_fn ProgramArg* find_arg(Interpreter* inter, String name) {
    foreach(i, yov->args.count) {
        ProgramArg* arg = &yov->args[i];
        if (string_equals(arg->name, name)) return arg;
    }
    return NULL;
}

internal_fn b32 i64_from_arg(String arg, i64* v)
{
    *v = 0;
    
    if (string_equals(arg, STR("true"))) {
        *v = 1;
        return true;
    }
    
    if (string_equals(arg, STR("false"))) {
        *v = 0;
        return true;
    }
    
    i64 v0;
    if (i64_from_string(arg, &v0)) {
        *v = v0;
        return true;
    }
    
    return false;
}

internal_fn FunctionReturn intrinsic__arg_int(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    ProgramArg* arg = find_arg(inter, get_string(vars[0]));
    
    i64 value;
    
    if (arg == NULL) {
        value = get_int(vars[1]);
    }
    else {
        if (!i64_from_arg(arg->value, &value)) {
            // TODO(Jose): report_warning(inter->ctx, node->code, "Arg type missmatch, can't assign '%S' to a 'Int", arg->name);
        }
    }
    
    return { obj_alloc_temp_int(inter, value), RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__arg_bool(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    ProgramArg* arg = find_arg(inter, get_string(vars[0]));
    
    b32 value = false;
    
    if (arg == NULL) {
        value = get_bool(vars[1]);
    }
    else
    {
        i64 int_value = 0;
        if (!i64_from_arg(arg->value, &int_value)) {
            // TODO(Jose): report_warning(inter->ctx, node->code, "Arg type missmatch, can't assign '%S' to a 'Bool", arg->name);
        }
        value = int_value != 0;
    }
    
    return { obj_alloc_temp_bool(inter, (b8)value), RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__arg_string(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    ProgramArg* arg = find_arg(inter, get_string(vars[0]));
    
    String value;
    
    if (arg == NULL) value = get_string(vars[1]);
    else value = arg->value;
    
    return { obj_alloc_temp_string(inter, value), RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__arg_exists(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    ProgramArg* arg = find_arg(inter, get_string(vars[0]));
    return { obj_alloc_temp_bool(inter, arg != NULL), RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__arg_flag(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    ProgramArg* arg = find_arg(inter, get_string(vars[0]));
    
    b32 res = false;
    
    if (arg != NULL)
    {
        i64 int_value = 0;
        if (i64_from_arg(arg->value, &int_value)) res = int_value != 0;
        else res = true;
    }
    
    return { obj_alloc_temp_bool(inter, (b8)res), RESULT_SUCCESS };
}

//- MISC 

internal_fn FunctionReturn intrinsic__ask_yesno(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    String content = get_string(vars[0]);
    
    b32 result = os_ask_yesno(STR("Ask"), content);
    return { obj_alloc_temp_bool(inter, (b8)result), RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__exists(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    
    String path = get_string(vars[0]);
    b32 result = os_exists(path);
    
    return { obj_alloc_temp_bool(inter, result), RESULT_SUCCESS };
}

internal_fn FunctionReturn intrinsic__create_directory(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    b32 recursive = get_bool(vars[1]);
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Create directory:\n%S", path));
    
    if (res.success) {
        res = os_create_directory(path, recursive);
    }
    
    return { obj_alloc_temp_bool(inter, res.success), res };
}

internal_fn FunctionReturn intrinsic__delete_directory(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Delete directory:\n%S", path));
    
    if (res.success) {
        res = os_delete_directory(path);
    }
    
    return { obj_alloc_temp_bool(inter, res.success), res };
}

internal_fn FunctionReturn intrinsic__copy_directory(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    
    String dst = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    String src = path_absolute_to_cd(scratch.arena, inter, get_string(vars[1]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Copy directory\n'%S'\nto\n'%S'", src, dst));
    
    if (res.success) {
        res = os_copy_directory(dst, src);
    }
    
    return { obj_alloc_temp_bool(inter, res.success), res };
}

internal_fn FunctionReturn intrinsic__move_directory(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    
    String dst = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    String src = path_absolute_to_cd(scratch.arena, inter, get_string(vars[1]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Move directory\n'%S'\nto\n'%S'", src, dst));
    
    if (res.success) {
        res = os_move_directory(dst, src);
    }
    
    return { obj_alloc_temp_bool(inter, res.success), res };
}

internal_fn FunctionReturn intrinsic__copy_file(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    
    String dst = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    String src = path_absolute_to_cd(scratch.arena, inter, get_string(vars[1]));
    CopyMode copy_mode = get_enum_CopyMode(inter, vars[2]);
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Copy file\n'%S'\nto\n'%S'", src, dst));
    
    if (res.success) res = os_copy_file(dst, src, copy_mode == CopyMode_Override);
    
    return { obj_alloc_temp_bool(inter, res.success), res };
}

internal_fn FunctionReturn intrinsic__move_file(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    
    String dst = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    String src = path_absolute_to_cd(scratch.arena, inter, get_string(vars[1]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Move file\n'%S'\nto\n'%S'", src, dst));
    
    if (res.success) res = os_move_file(dst, src);
    
    return { obj_alloc_temp_bool(inter, res.success), res };
}

internal_fn FunctionReturn intrinsic__delete_file(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Delete file:\n'%S'", path));
    
    if (res.success) res = os_delete_file(path);
    
    return { obj_alloc_temp_bool(inter, res.success), res };
}

#define INTR(name, fn) { STR(name), fn }

Array<IntrinsicDefinition> get_intrinsics_table(Arena* arena)
{
    IntrinsicDefinition table[] = {
        // Core
        INTR("print", intrinsic__print),
        INTR("println", intrinsic__println),
        INTR("call", intrinsic__call),
        INTR("call_exe", intrinsic__call_exe),
        INTR("exit", intrinsic__exit),
        INTR("set_cd", intrinsic__set_cd),
        
        // Utils
        INTR("path_resolve", intrinsic__path_resolve),
        
        // Yov
        INTR("yov_require", intrinsic__yov_require),
        INTR("yov_require_min", intrinsic__yov_require_min),
        INTR("yov_require_max", intrinsic__yov_require_max),
        
        // Args
        INTR("arg_int", intrinsic__arg_int),
        INTR("arg_bool", intrinsic__arg_bool),
        INTR("arg_string", intrinsic__arg_string),
        INTR("arg_flag", intrinsic__arg_flag),
        INTR("arg_exists", intrinsic__arg_exists),
        
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
    };
    
    return array_copy(arena, array_make(table, array_count(table)));
}