#include "inc.h"

//- CORE 

internal_fn Variable* intrinsic__print(Interpreter* inter, OpNode* node, Array<Variable*> vars)
{
    print_info("%S", get_string(vars[0]));
    return inter->void_var;
}

internal_fn Variable* intrinsic__println(Interpreter* inter, OpNode* node, Array<Variable*> vars)
{
    print_info("%S\n", get_string(vars[0]));
    return inter->void_var;
}

internal_fn Variable* intrinsic__call(Interpreter* inter, OpNode* node, Array<Variable*> vars)
{
    SCRATCH();
    
    i32 result = -1;
    String command_line = get_string(vars[0]);
    
    if (user_assertion(inter, string_format(scratch.arena, "Call:\n%S", command_line))) {
        result = os_call(get_string(inter->cd_obj->var), command_line);
    }
    
    return var_alloc_int(inter, result);
}

internal_fn Variable* intrinsic__exit(Interpreter* inter, OpNode* node, Array<Variable*> vars)
{
    inter->ctx->error_count++;// TODO(Jose): Weird
    return inter->void_var;
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

internal_fn Variable* intrinsic__arg_int(Interpreter* inter, OpNode* node, Array<Variable*> vars)
{
    SCRATCH();
    ProgramArg* arg = find_arg(inter, get_string(vars[0]));
    
    i64 value;
    
    if (arg == NULL) {
        value = get_int(vars[1]);
    }
    else {
        if (!i64_from_arg(arg->value, &value)) {
            report_warning(inter->ctx, node->code, "Arg type missmatch, can't assign '%S' to a 'Int", arg->name);
        }
    }
    
    return var_alloc_int(inter, value);
}

internal_fn Variable* intrinsic__arg_bool(Interpreter* inter, OpNode* node, Array<Variable*> vars)
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
            report_warning(inter->ctx, node->code, "Arg type missmatch, can't assign '%S' to a 'Bool", arg->name);
        }
        value = int_value != 0;
    }
    
    return var_alloc_bool(inter, (b8)value);
}

internal_fn Variable* intrinsic__arg_string(Interpreter* inter, OpNode* node, Array<Variable*> vars)
{
    SCRATCH();
    ProgramArg* arg = find_arg(inter, get_string(vars[0]));
    
    String value;
    
    if (arg == NULL) value = get_string(vars[1]);
    else value = arg->value;
    
    return var_alloc_string(inter, value);
}

internal_fn Variable* intrinsic__arg_exists(Interpreter* inter, OpNode* node, Array<Variable*> vars)
{
    SCRATCH();
    ProgramArg* arg = find_arg(inter, get_string(vars[0]));
    return var_alloc_bool(inter, arg != NULL);
}

internal_fn Variable* intrinsic__arg_flag(Interpreter* inter, OpNode* node, Array<Variable*> vars)
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
    
    return var_alloc_bool(inter, (b8)res);
}

//- MISC 

internal_fn Variable* intrinsic__ask_yesno(Interpreter* inter, OpNode* node, Array<Variable*> vars)
{
    String content = get_string(vars[0]);
    
    b32 result = os_ask_yesno(STR("Ask"), content);
    return var_alloc_bool(inter, (b8)result);
}

internal_fn Variable* intrinsic__set_cd(Interpreter* inter, OpNode* node, Array<Variable*> vars)
{
    SCRATCH();
    
    Object* obj = inter->cd_obj;
    String path = get_string(vars[0]);
    
    if (os_path_is_absolute(path)) {
        obj_assign(inter, obj, vars[0]);
        return inter->void_var;
    }
    
    String res = path_resolve(scratch.arena, path_append(scratch.arena, get_string(obj->var), path));
    Variable* new_cd = var_alloc_string(inter, res);
    obj_assign(inter, obj, new_cd);
    
    return inter->void_var;
}

internal_fn Variable* intrinsic__create_folder(Interpreter* inter, OpNode* node, Array<Variable*> vars)
{
    SCRATCH();
    
    String path = get_string(vars[0]);
    b32 recursive = get_bool(vars[1]);
    b32 result = false;
    
    if (user_assertion(inter, string_format(scratch.arena, "Create folder:\n%S", path))) {
        result = os_folder_create(path, recursive);
    }
    
    return var_alloc_bool(inter, (b8)result);
}

internal_fn Variable* intrinsic__copy_file(Interpreter* inter, OpNode* node, Array<Variable*> vars)
{
    SCRATCH();
    
    String dst = path_absolute_to_cd(scratch.arena, inter, get_string(vars[0]));
    String src = path_absolute_to_cd(scratch.arena, inter, get_string(vars[1]));
    
    if (user_assertion(inter, string_format(scratch.arena, "Copy file\n'%S'\ninto\n'%S'", src, dst))) {
        os_copy_file(dst, src, true);
    }
    
    return inter->void_var;
}

internal_fn Variable* intrinsic__path_resolve(Interpreter* inter, OpNode* node, Array<Variable*> vars)
{
    SCRATCH();
    String res = path_resolve(scratch.arena, get_string(vars[0]));
    return var_alloc_string(inter, res);
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
    
    define_instrinsic("print", intrinsic__print, VType_Void, VType_String);
    define_instrinsic("println", intrinsic__println, VType_Void, VType_String);
    define_instrinsic("call", intrinsic__call, VType_Int, VType_String);
    define_instrinsic("exit", intrinsic__exit, VType_Void, VType_Void);
    
    define_instrinsic("arg_int", intrinsic__arg_int, VType_Int, VType_String, VType_Int);
    define_instrinsic("arg_bool", intrinsic__arg_bool, VType_Bool, VType_String, VType_Bool);
    define_instrinsic("arg_string", intrinsic__arg_string, VType_String, VType_String, VType_String);
    define_instrinsic("arg_flag", intrinsic__arg_flag, VType_Bool, VType_String);
    define_instrinsic("arg_exists", intrinsic__arg_exists, VType_Bool, VType_String);
    
    define_instrinsic("ask_yesno", intrinsic__ask_yesno, VType_Bool, VType_String);
    define_instrinsic("set_cd", intrinsic__set_cd, VType_Void, VType_String);
    define_instrinsic("create_folder", intrinsic__create_folder, VType_Bool, VType_String, VType_Bool);
    define_instrinsic("copy_file", intrinsic__copy_file, VType_Void, VType_String, VType_String);
    define_instrinsic("path_resolve", intrinsic__path_resolve, VType_String, VType_String);
    
    return array_from_pooled_array(arena, list);
}
