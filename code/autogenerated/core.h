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
        define_struct(inter, "YovInfo", members);
    }
    // struct ParamInfo
    {
        ObjectDefinition m[2];
        m[0] = obj_def_make(STR("name"), vtype_from_name(inter, STR("String")));
        m[1] = obj_def_make(STR("type"), vtype_from_name(inter, STR("String")));
        Array<ObjectDefinition> members = array_make(m, array_count(m));
        define_struct(inter, "ParamInfo", members);
    }
    // struct StructInfo
    {
        ObjectDefinition m[2];
        m[0] = obj_def_make(STR("name"), vtype_from_name(inter, STR("String")));
        m[1] = obj_def_make(STR("members"), vtype_from_array_dimension(inter, vtype_from_name(inter, STR("ParamInfo")), 1));
        Array<ObjectDefinition> members = array_make(m, array_count(m));
        define_struct(inter, "StructInfo", members);
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
        define_struct(inter, "Context", members);
    }
    // enum OSKind
    {
        String n[2];
        n[0] = "Windows";
        n[1] = "Linux";
        Array<String> names = array_make(n, array_count(n));
        define_enum(inter, "OSKind", names, {});
    }
    // struct OS
    {
        ObjectDefinition m[1];
        m[0] = obj_def_make(STR("kind"), vtype_from_name(inter, STR("OSKind")));
        Array<ObjectDefinition> members = array_make(m, array_count(m));
        define_struct(inter, "OS", members);
    }
    // enum CopyMode
    {
        String n[2];
        n[0] = "NoOverride";
        n[1] = "Override";
        Array<String> names = array_make(n, array_count(n));
        define_enum(inter, "CopyMode", names, {});
    }
    // global yov
    {
        ObjectRef* ref = scope_define_object_ref(inter, "yov", value_def(inter, vtype_from_name(inter, "YovInfo")));
    }
    // global context
    {
        ObjectRef* ref = scope_define_object_ref(inter, "context", value_def(inter, vtype_from_name(inter, "Context")));
    }
    // global os
    {
        ObjectRef* ref = scope_define_object_ref(inter, "os", value_def(inter, vtype_from_name(inter, "OS")));
    }
    // function print
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make(STR("text"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "print", parameters, VType_Void);
    }
    // function println
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make(STR("text"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "println", parameters, VType_Void);
    }
    // function call
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make(STR("command"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "call", parameters, vtype_from_name(inter, STR("Int")));
    }
    // function call_exe
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("exe_name"), vtype_from_name(inter, STR("String")));
        p[1] = obj_def_make(STR("arguments"), vtype_from_array_dimension(inter, vtype_from_name(inter, STR("String")), 1));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "call_exe", parameters, vtype_from_name(inter, STR("Int")));
    }
    // function exit
    {
        Array<ObjectDefinition> parameters{};
        define_intrinsic_function(inter, {}, "exit", parameters, VType_Void);
    }
    // function set_cd
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make(STR("cd"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "set_cd", parameters, VType_Void);
    }
    // function path_resolve
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make(STR("path"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "path_resolve", parameters, vtype_from_name(inter, STR("String")));
    }
    // function yov_require
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("major"), vtype_from_name(inter, STR("Int")));
        p[1] = obj_def_make(STR("minor"), vtype_from_name(inter, STR("Int")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "yov_require", parameters, VType_Void);
    }
    // function yov_require_min
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("major"), vtype_from_name(inter, STR("Int")));
        p[1] = obj_def_make(STR("minor"), vtype_from_name(inter, STR("Int")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "yov_require_min", parameters, VType_Void);
    }
    // function yov_require_max
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("major"), vtype_from_name(inter, STR("Int")));
        p[1] = obj_def_make(STR("minor"), vtype_from_name(inter, STR("Int")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "yov_require_max", parameters, VType_Void);
    }
    // function ask_yesno
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make(STR("text"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "ask_yesno", parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function exists
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make(STR("path"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "exists", parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function create_directory
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("path"), vtype_from_name(inter, STR("String")));
        p[1] = obj_def_make(STR("recursive"), vtype_from_name(inter, STR("Bool")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "create_directory", parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function delete_directory
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make(STR("path"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "delete_directory", parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function copy_directory
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("dst"), vtype_from_name(inter, STR("String")));
        p[1] = obj_def_make(STR("src"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "copy_directory", parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function move_directory
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("dst"), vtype_from_name(inter, STR("String")));
        p[1] = obj_def_make(STR("src"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "move_directory", parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function copy_file
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[3];
        p[0] = obj_def_make(STR("dst"), vtype_from_name(inter, STR("String")));
        p[1] = obj_def_make(STR("src"), vtype_from_name(inter, STR("String")));
        p[2] = obj_def_make(STR("mode"), vtype_from_name(inter, STR("CopyMode")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "copy_file", parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function move_file
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("dst"), vtype_from_name(inter, STR("String")));
        p[1] = obj_def_make(STR("src"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "move_file", parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function delete_file
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make(STR("path"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "delete_file", parameters, vtype_from_name(inter, STR("Bool")));
    }
    // function write_file
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make(STR("path"), vtype_from_name(inter, STR("String")));
        p[1] = obj_def_make(STR("content"), vtype_from_name(inter, STR("String")));
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "write_file", parameters, vtype_from_name(inter, STR("Bool")));
    }
}
