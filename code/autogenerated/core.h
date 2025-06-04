#pragma once

internal_fn void define_core(Interpreter* inter)
{
    // struct YovInfo
    {
        ObjectDefinition m[4];
        m[0] = obj_def_make(STR("version"), vtype_from_name(inter, STR("String")));
        m[1] = obj_def_make(STR("major"), vtype_from_name(inter, STR("Int")));
        m[2] = obj_def_make(STR("minor"), vtype_from_name(inter, STR("Int")));
        m[3] = obj_def_make(STR("revision"), vtype_from_name(inter, STR("Int")));
        Array<ObjectDefinition> members = array_make(m, array_count(m));
        define_struct(inter, STR("YovInfo"), members);
    }
    // struct ParamInfo
    {
        ObjectDefinition m[2];
        m[0] = obj_def_make(STR("name"), vtype_from_name(inter, STR("String")));
        m[1] = obj_def_make(STR("type"), vtype_from_name(inter, STR("String")));
        Array<ObjectDefinition> members = array_make(m, array_count(m));
        define_struct(inter, STR("ParamInfo"), members);
    }
    // struct StructInfo
    {
        ObjectDefinition m[2];
        m[0] = obj_def_make(STR("name"), vtype_from_name(inter, STR("String")));
        m[1] = obj_def_make(STR("members"), vtype_from_array_dimension(inter, vtype_from_name(inter, STR("ParamInfo")), 1));
        Array<ObjectDefinition> members = array_make(m, array_count(m));
        define_struct(inter, STR("StructInfo"), members);
    }
    // struct Context
    {
        ObjectDefinition m[5];
        m[0] = obj_def_make(STR("cd"), vtype_from_name(inter, STR("String")));
        m[1] = obj_def_make(STR("script_dir"), vtype_from_name(inter, STR("String")));
        m[2] = obj_def_make(STR("caller_dir"), vtype_from_name(inter, STR("String")));
        m[3] = obj_def_make(STR("args"), vtype_from_array_dimension(inter, vtype_from_name(inter, STR("String")), 1));
        m[4] = obj_def_make(STR("structs"), vtype_from_array_dimension(inter, vtype_from_name(inter, STR("StructInfo")), 1));
        Array<ObjectDefinition> members = array_make(m, array_count(m));
        define_struct(inter, STR("Context"), members);
    }
    // enum OSKind
    {
        String n[1];
        n[0] = STR("Windows");
        Array<String> names = array_make(n, array_count(n));
        define_enum(inter, STR("OSKind"), names, {});
    }
    // struct OS
    {
        ObjectDefinition m[1];
        m[0] = obj_def_make(STR("kind"), vtype_from_name(inter, STR("OSKind")));
        Array<ObjectDefinition> members = array_make(m, array_count(m));
        define_struct(inter, STR("OS"), members);
    }
    // enum CopyMode
    {
        String n[2];
        n[0] = STR("NoOverride");
        n[1] = STR("Override");
        Array<String> names = array_make(n, array_count(n));
        define_enum(inter, STR("CopyMode"), names, {});
    }
    // global yov
    {
        Object* obj = define_object(inter, "yov", vtype_from_name(inter, "YovInfo"));
    }
    // global context
    {
        Object* obj = define_object(inter, "context", vtype_from_name(inter, "Context"));
    }
    // global os
    {
        Object* obj = define_object(inter, "os", vtype_from_name(inter, "OS"));
    }
    // function print
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make(STR("text"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("print"), parameters, VType_Void);
    }
    // function println
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make(STR("text"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("println"), parameters, VType_Void);
    }
    // function call
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make(STR("command"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("call"), parameters, vtype_from_name(inter, STR("Int")));
    }
    // function call_exe
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("exe_name"), vtype_from_name(inter, STR("String")));
        p[1] = obj_def_make(STR("arguments"), vtype_from_array_dimension(inter, vtype_from_name(inter, STR("String")), 1));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("call_exe"), parameters, vtype_from_name(inter, STR("Int")));
    }
    // function exit
    {
        Array<ObjectDefinition> parameters{};
        define_intrinsic_function(inter, {}, STR("exit"), parameters, VType_Void);
    }
    // function set_cd
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make(STR("cd"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("set_cd"), parameters, VType_Void);
    }
    // function path_resolve
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make(STR("path"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("path_resolve"), parameters, vtype_from_name(inter, STR("String")));
    }
    // function yov_require
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("major"), vtype_from_name(inter, STR("Int")));
        p[1] = obj_def_make(STR("minor"), vtype_from_name(inter, STR("Int")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("yov_require"), parameters, VType_Void);
    }
    // function yov_require_min
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("major"), vtype_from_name(inter, STR("Int")));
        p[1] = obj_def_make(STR("minor"), vtype_from_name(inter, STR("Int")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("yov_require_min"), parameters, VType_Void);
    }
    // function yov_require_max
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("major"), vtype_from_name(inter, STR("Int")));
        p[1] = obj_def_make(STR("minor"), vtype_from_name(inter, STR("Int")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("yov_require_max"), parameters, VType_Void);
    }
    // function arg_int
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("name"), vtype_from_name(inter, STR("String")));
        p[1] = obj_def_make(STR("default"), vtype_from_name(inter, STR("Int")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("arg_int"), parameters, vtype_from_name(inter, STR("Int")));
    }
    // function arg_bool
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("name"), vtype_from_name(inter, STR("String")));
        p[1] = obj_def_make(STR("default"), vtype_from_name(inter, STR("Bool")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("arg_bool"), parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function arg_string
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("name"), vtype_from_name(inter, STR("String")));
        p[1] = obj_def_make(STR("default"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("arg_string"), parameters, vtype_from_name(inter, STR("String")));
    }
    // function arg_flag
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make(STR("name"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("arg_flag"), parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function arg_exists
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make(STR("name"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("arg_exists"), parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function ask_yesno
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make(STR("text"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("ask_yesno"), parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function exists
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make(STR("path"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("exists"), parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function create_directory
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("path"), vtype_from_name(inter, STR("String")));
        p[1] = obj_def_make(STR("recursive"), vtype_from_name(inter, STR("Bool")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("create_directory"), parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function delete_directory
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make(STR("path"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("delete_directory"), parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function copy_directory
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("dst"), vtype_from_name(inter, STR("String")));
        p[1] = obj_def_make(STR("src"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("copy_directory"), parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function move_directory
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("dst"), vtype_from_name(inter, STR("String")));
        p[1] = obj_def_make(STR("src"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("move_directory"), parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function copy_file
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[3];
        p[0] = obj_def_make(STR("dst"), vtype_from_name(inter, STR("String")));
        p[1] = obj_def_make(STR("src"), vtype_from_name(inter, STR("String")));
        p[2] = obj_def_make(STR("mode"), vtype_from_name(inter, STR("CopyMode")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("copy_file"), parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function move_file
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("dst"), vtype_from_name(inter, STR("String")));
        p[1] = obj_def_make(STR("src"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("move_file"), parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function delete_file
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make(STR("path"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("delete_file"), parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function write_file
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("path"), vtype_from_name(inter, STR("String")));
        p[1] = obj_def_make(STR("content"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, STR("write_file"), parameters, vtype_from_name(inter, STR("Bool")));
    }
}
