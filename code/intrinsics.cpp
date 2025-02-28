#include "inc.h"

//- CORE 

internal_fn IntrinsicFunctionResult intrinsic__print(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    print_info("%S", get_string(vars[0]));
    
    return { inter->void_obj, RESULT_SUCCESS };
}

internal_fn IntrinsicFunctionResult intrinsic__println(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    print_info("%S\n", get_string(vars[0]));
    return { inter->void_obj, RESULT_SUCCESS };
}

internal_fn IntrinsicFunctionResult intrinsic__call(Interpreter* inter, Array<Object*> vars, CodeLocation code)
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

internal_fn IntrinsicFunctionResult intrinsic__call_exe(Interpreter* inter, Array<Object*> vars, CodeLocation code)
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

internal_fn IntrinsicFunctionResult intrinsic__exit(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    interpreter_exit(inter);
    return { inter->void_obj, RESULT_SUCCESS };
}

internal_fn IntrinsicFunctionResult intrinsic__set_cd(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    
    Object* obj = inter->cd_obj;
    String path = get_string(vars[0]);
    
    if (os_path_is_absolute(path)) {
        obj_copy(inter, obj, vars[0]);
        return { inter->void_obj, RESULT_SUCCESS };
    }
    
    String res = path_resolve(scratch.arena, path_append(scratch.arena, get_string(obj), path));
    obj_set_string(inter, obj, res);
    
    return { inter->void_obj, RESULT_SUCCESS };;
}

//- UTILS 

internal_fn IntrinsicFunctionResult intrinsic__path_resolve(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    String res = path_resolve(scratch.arena, get_string(vars[0]));
    return { obj_alloc_temp_string(inter, res), RESULT_SUCCESS };
}

//- YOV 

internal_fn IntrinsicFunctionResult intrinsic__yov_require(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    i64 major = get_int(vars[0]);
    i64 minor = get_int(vars[1]);
    
    b32 res = major == YOV_MAJOR_VERSION && minor == YOV_MINOR_VERSION;
    
    if (!res) {
        report_error(inter->ctx, code, "Require version: Yov v%u.%u", major, minor);
    }
    
    return { inter->void_obj, RESULT_SUCCESS };
}

internal_fn IntrinsicFunctionResult intrinsic__yov_require_min(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    i64 major = get_int(vars[0]);
    i64 minor = get_int(vars[1]);
    
    b32 res = true;
    if (major > YOV_MAJOR_VERSION) res = false;
    else if (major == YOV_MAJOR_VERSION && minor > YOV_MINOR_VERSION) res = false;
    
    if (!res) {
        report_error(inter->ctx, code, "Require minimum version: Yov v%u.%u", major, minor);
    }
    
    return { inter->void_obj, RESULT_SUCCESS };
}

internal_fn IntrinsicFunctionResult intrinsic__yov_require_max(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    i64 major = get_int(vars[0]);
    i64 minor = get_int(vars[1]);
    
    b32 res = false;
    if (major > YOV_MAJOR_VERSION) res = true;
    else if (major == YOV_MAJOR_VERSION && minor >= YOV_MINOR_VERSION) res = true;
    
    if (!res) {
        report_error(inter->ctx, code, "Require maximum version: Yov v%u.%u", major, minor);
    }
    
    return { inter->void_obj, RESULT_SUCCESS };
}

//- ARGS 

internal_fn ProgramArg* find_arg(Interpreter* inter, String name) {
    foreach(i, inter->ctx->args.count) {
        ProgramArg* arg = &inter->ctx->args[i];
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

internal_fn IntrinsicFunctionResult intrinsic__arg_int(Interpreter* inter, Array<Object*> vars, CodeLocation code)
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

internal_fn IntrinsicFunctionResult intrinsic__arg_bool(Interpreter* inter, Array<Object*> vars, CodeLocation code)
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

internal_fn IntrinsicFunctionResult intrinsic__arg_string(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    ProgramArg* arg = find_arg(inter, get_string(vars[0]));
    
    String value;
    
    if (arg == NULL) value = get_string(vars[1]);
    else value = arg->value;
    
    return { obj_alloc_temp_string(inter, value), RESULT_SUCCESS };
}

internal_fn IntrinsicFunctionResult intrinsic__arg_exists(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    ProgramArg* arg = find_arg(inter, get_string(vars[0]));
    return { obj_alloc_temp_bool(inter, arg != NULL), RESULT_SUCCESS };
}

internal_fn IntrinsicFunctionResult intrinsic__arg_flag(Interpreter* inter, Array<Object*> vars, CodeLocation code)
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

internal_fn IntrinsicFunctionResult intrinsic__ask_yesno(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    String content = get_string(vars[0]);
    
    b32 result = os_ask_yesno(STR("Ask"), content);
    return { obj_alloc_temp_bool(inter, (b8)result), RESULT_SUCCESS };
}

internal_fn IntrinsicFunctionResult intrinsic__exists(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    
    String path = get_string(vars[0]);
    b32 result = os_exists(path);
    
    return { obj_alloc_temp_bool(inter, result), RESULT_SUCCESS };
}

internal_fn IntrinsicFunctionResult intrinsic__create_directory(Interpreter* inter, Array<Object*> vars, CodeLocation code)
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

internal_fn IntrinsicFunctionResult intrinsic__delete_directory(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Delete directory:\n%S", path));
    
    if (res.success) {
        res = os_delete_directory(path);
    }
    
    return { obj_alloc_temp_bool(inter, res.success), res };
}

internal_fn IntrinsicFunctionResult intrinsic__copy_directory(Interpreter* inter, Array<Object*> vars, CodeLocation code)
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

internal_fn IntrinsicFunctionResult intrinsic__move_directory(Interpreter* inter, Array<Object*> vars, CodeLocation code)
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

internal_fn IntrinsicFunctionResult intrinsic__copy_file(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    
    String dst = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    String src = path_absolute_to_cd(scratch.arena, inter, get_string(vars[1]));
    b32 override = get_bool(vars[2]);
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Copy file\n'%S'\nto\n'%S'", src, dst));
    
    if (res.success) res = os_copy_file(dst, src, override);
    
    return { obj_alloc_temp_bool(inter, res.success), res };
}

internal_fn IntrinsicFunctionResult intrinsic__move_file(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    
    String dst = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    String src = path_absolute_to_cd(scratch.arena, inter, get_string(vars[1]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Move file\n'%S'\nto\n'%S'", src, dst));
    
    if (res.success) res = os_move_file(dst, src);
    
    return { obj_alloc_temp_bool(inter, res.success), res };
}

internal_fn IntrinsicFunctionResult intrinsic__delete_file(Interpreter* inter, Array<Object*> vars, CodeLocation code)
{
    SCRATCH();
    
    String path = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    
    Result res = user_assertion(inter, string_format(scratch.arena, "Delete file:\n'%S'", path));
    
    if (res.success) res = os_delete_file(path);
    
    return { obj_alloc_temp_bool(inter, res.success), res };
}

FunctionDefinition make_intrinsic_function(Arena* arena, String identifier, i32 return_vtype, Array<i32> parameters, IntrinsicFunction* intrinsic)
{
    if (parameters.count == 1 && parameters[0] == VType_Void) {
        parameters = {};
    }
    
    FunctionDefinition fn{};
    fn.identifier = identifier;
    fn.return_vtype = return_vtype;
    fn.parameter_vtypes = array_copy(arena, parameters);
    fn.intrinsic_fn = intrinsic;
    return fn;
}

#define define_instrinsic(_name, _fn, _return, ...) do { \
i32 params[] = { __VA_ARGS__ }; \
array_add(&list, make_intrinsic_function(arena, STR(_name), _return, array_make(params, array_count(params)), _fn)); \
} while (0)

Array<FunctionDefinition> get_intrinsic_functions(Arena* arena, Interpreter* inter)
{
    SCRATCH(arena);
    PooledArray<FunctionDefinition> list = pooled_array_make<FunctionDefinition>(scratch.arena, 32);
    
    // Core
    define_instrinsic("print", intrinsic__print, VType_Void, VType_String);
    define_instrinsic("println", intrinsic__println, VType_Void, VType_String);
    define_instrinsic("call", intrinsic__call, VType_Int, VType_String);
    define_instrinsic("call_exe", intrinsic__call_exe, VType_Int, VType_String, VType_String);
    // TODO(Jose): define_instrinsic("call_script", intrinsic__call_script, VType_Void, VType_String, VType_String);
    define_instrinsic("exit", intrinsic__exit, VType_Void, VType_Void);
    define_instrinsic("set_cd", intrinsic__set_cd, VType_Void, VType_String);
    
    // Utils
    define_instrinsic("path_resolve", intrinsic__path_resolve, VType_String, VType_String);
    
    // Yov
    define_instrinsic("yov_require", intrinsic__yov_require, VType_Void, VType_Int, VType_Int);
    define_instrinsic("yov_require_min", intrinsic__yov_require_min, VType_Void, VType_Int, VType_Int);
    define_instrinsic("yov_require_max", intrinsic__yov_require_max, VType_Void, VType_Int, VType_Int);
    
    // Args
    define_instrinsic("arg_int", intrinsic__arg_int, VType_Int, VType_String, VType_Int);
    define_instrinsic("arg_bool", intrinsic__arg_bool, VType_Bool, VType_String, VType_Bool);
    define_instrinsic("arg_string", intrinsic__arg_string, VType_String, VType_String, VType_String);
    define_instrinsic("arg_flag", intrinsic__arg_flag, VType_Bool, VType_String);
    define_instrinsic("arg_exists", intrinsic__arg_exists, VType_Bool, VType_String);
    
    // User
    define_instrinsic("ask_yesno", intrinsic__ask_yesno, VType_Bool, VType_String);
    
    // File System
    define_instrinsic("exists", intrinsic__exists, VType_Bool, VType_String);
    define_instrinsic("create_directory", intrinsic__create_directory, VType_Bool, VType_String, VType_Bool);
    define_instrinsic("delete_directory", intrinsic__delete_directory, VType_Bool, VType_String);
    define_instrinsic("copy_directory", intrinsic__copy_directory, VType_Bool, VType_String, VType_String);
    define_instrinsic("move_directory", intrinsic__move_directory, VType_Bool, VType_String, VType_String);
    define_instrinsic("copy_file", intrinsic__copy_file, VType_Bool, VType_String, VType_String, VType_Bool);
    define_instrinsic("move_file", intrinsic__move_file, VType_Bool, VType_String, VType_String);
    define_instrinsic("delete_file", intrinsic__delete_file, VType_Bool, VType_String);
    
    return array_from_pooled_array(arena, list);
}
