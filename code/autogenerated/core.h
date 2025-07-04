#pragma once

internal_fn void define_core(Interpreter* inter)
{
    // struct YovInfo
    {
        ObjectDefinition m[4];
        m[0] = obj_def_make("version", vtype_from_name(inter, "String"), false);
        m[1] = obj_def_make("major", vtype_from_name(inter, "Int"), false);
        m[2] = obj_def_make("minor", vtype_from_name(inter, "Int"), false);
        m[3] = obj_def_make("revision", vtype_from_name(inter, "Int"), false);
        Array<ObjectDefinition> members = array_make(m, array_count(m));
        define_struct(inter, "YovInfo", members);
    }
    // struct Context
    {
        ObjectDefinition m[4];
        m[0] = obj_def_make("cd", vtype_from_name(inter, "String"), false);
        m[1] = obj_def_make("script_dir", vtype_from_name(inter, "String"), false);
        m[2] = obj_def_make("caller_dir", vtype_from_name(inter, "String"), false);
        m[3] = obj_def_make("args", vtype_from_array_dimension(inter, vtype_from_name(inter, "String"), 1), false);
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
        m[0] = obj_def_make("kind", vtype_from_name(inter, "OSKind"), false);
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
        p[0] = obj_def_make("text", vtype_from_name(inter, "String"), false);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "print", parameters, VType_Void);
    }
    // function println
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("text", vtype_from_name(inter, "String"), false);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "println", parameters, VType_Void);
    }
    // function call
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("command", vtype_from_name(inter, "String"), false);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "call", parameters, vtype_from_name(inter, "Int"));
    }
    // function call_exe
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("exe_name", vtype_from_name(inter, "String"), false);
        p[1] = obj_def_make("arguments", vtype_from_array_dimension(inter, vtype_from_name(inter, "String"), 1), false);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "call_exe", parameters, vtype_from_name(inter, "Int"));
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
        p[0] = obj_def_make("cd", vtype_from_name(inter, "String"), false);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "set_cd", parameters, VType_Void);
    }
    // function path_resolve
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("path", vtype_from_name(inter, "String"), false);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "path_resolve", parameters, vtype_from_name(inter, "String"));
    }
    // function str_get_codepoint
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("str", vtype_from_name(inter, "String"), false);
        p[1] = obj_def_make("cursor", vtype_from_name(inter, "Int"), true);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "str_get_codepoint", parameters, vtype_from_name(inter, "Int"));
    }
    // function yov_require
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("major", vtype_from_name(inter, "Int"), false);
        p[1] = obj_def_make("minor", vtype_from_name(inter, "Int"), false);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "yov_require", parameters, VType_Void);
    }
    // function yov_require_min
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("major", vtype_from_name(inter, "Int"), false);
        p[1] = obj_def_make("minor", vtype_from_name(inter, "Int"), false);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "yov_require_min", parameters, VType_Void);
    }
    // function yov_require_max
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("major", vtype_from_name(inter, "Int"), false);
        p[1] = obj_def_make("minor", vtype_from_name(inter, "Int"), false);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "yov_require_max", parameters, VType_Void);
    }
    // function ask_yesno
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("text", vtype_from_name(inter, "String"), false);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "ask_yesno", parameters, vtype_from_name(inter, "Bool"));
    }
    // function exists
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("path", vtype_from_name(inter, "String"), false);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "exists", parameters, vtype_from_name(inter, "Bool"));
    }
    // function create_directory
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("path", vtype_from_name(inter, "String"), false);
        p[1] = obj_def_make("recursive", vtype_from_name(inter, "Bool"), false);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "create_directory", parameters, vtype_from_name(inter, "Bool"));
    }
    // function delete_directory
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("path", vtype_from_name(inter, "String"), false);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "delete_directory", parameters, vtype_from_name(inter, "Bool"));
    }
    // function copy_directory
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("dst", vtype_from_name(inter, "String"), false);
        p[1] = obj_def_make("src", vtype_from_name(inter, "String"), false);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "copy_directory", parameters, vtype_from_name(inter, "Bool"));
    }
    // function move_directory
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("dst", vtype_from_name(inter, "String"), false);
        p[1] = obj_def_make("src", vtype_from_name(inter, "String"), false);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "move_directory", parameters, vtype_from_name(inter, "Bool"));
    }
    // function copy_file
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[3];
        p[0] = obj_def_make("dst", vtype_from_name(inter, "String"), false);
        p[1] = obj_def_make("src", vtype_from_name(inter, "String"), false);
        p[2] = obj_def_make("mode", vtype_from_name(inter, "CopyMode"), false);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "copy_file", parameters, vtype_from_name(inter, "Bool"));
    }
    // function move_file
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("dst", vtype_from_name(inter, "String"), false);
        p[1] = obj_def_make("src", vtype_from_name(inter, "String"), false);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "move_file", parameters, vtype_from_name(inter, "Bool"));
    }
    // function delete_file
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("path", vtype_from_name(inter, "String"), false);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "delete_file", parameters, vtype_from_name(inter, "Bool"));
    }
    // function read_entire_file
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("path", vtype_from_name(inter, "String"), false);
        p[1] = obj_def_make("content", vtype_from_name(inter, "String"), true);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "read_entire_file", parameters, vtype_from_name(inter, "Bool"));
    }
    // function write_entire_file
    {
        Array<ObjectDefinition> parameters{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("path", vtype_from_name(inter, "String"), false);
        p[1] = obj_def_make("content", vtype_from_name(inter, "String"), false);
        parameters = array_make(p, array_count(p));
        define_intrinsic_function(inter, {}, "write_entire_file", parameters, vtype_from_name(inter, "Bool"));
    }
}
