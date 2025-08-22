#pragma once

internal_fn void define_core()
{
    // struct Result
    {
        ObjectDefinition m[3];
        m[0] = obj_def_make("failed", vtype_from_name("Bool"), false);
        m[1] = obj_def_make("message", vtype_from_name("String"), false);
        m[2] = obj_def_make("code", vtype_from_name("Int"), false);
        Array<ObjectDefinition> members = array_make(m, countof(m));
        define_struct("Result", members);
    }
    // struct YovInfo
    {
        ObjectDefinition m[5];
        m[0] = obj_def_make("path", vtype_from_name("String"), false);
        m[1] = obj_def_make("version", vtype_from_name("String"), false);
        m[2] = obj_def_make("major", vtype_from_name("Int"), false);
        m[3] = obj_def_make("minor", vtype_from_name("Int"), false);
        m[4] = obj_def_make("revision", vtype_from_name("Int"), false);
        Array<ObjectDefinition> members = array_make(m, countof(m));
        define_struct("YovInfo", members);
    }
    // struct Type
    {
        ObjectDefinition m[2];
        m[0] = obj_def_make("ID", vtype_from_name("Int"), false);
        m[1] = obj_def_make("name", vtype_from_name("String"), false);
        Array<ObjectDefinition> members = array_make(m, countof(m));
        define_struct("Type", members);
    }
    // struct Context
    {
        ObjectDefinition m[5];
        m[0] = obj_def_make("cd", vtype_from_name("String"), false);
        m[1] = obj_def_make("script_dir", vtype_from_name("String"), false);
        m[2] = obj_def_make("caller_dir", vtype_from_name("String"), false);
        m[3] = obj_def_make("args", vtype_from_dimension(vtype_from_name("String"), 1), false);
        m[4] = obj_def_make("types", vtype_from_dimension(vtype_from_name("Type"), 1), false);
        Array<ObjectDefinition> members = array_make(m, countof(m));
        define_struct("Context", members);
    }
    // enum OSKind
    {
        String n[2];
        n[0] = "Windows";
        n[1] = "Linux";
        Array<String> names = array_make(n, countof(n));
        define_enum("OSKind", names, {});
    }
    // struct OS
    {
        ObjectDefinition m[1];
        m[0] = obj_def_make("kind", vtype_from_name("OSKind"), false);
        Array<ObjectDefinition> members = array_make(m, countof(m));
        define_struct("OS", members);
    }
    // enum RedirectStdout
    {
        String n[4];
        n[0] = "Console";
        n[1] = "Ignore";
        n[2] = "Script";
        n[3] = "ImportEnv";
        Array<String> names = array_make(n, countof(n));
        define_enum("RedirectStdout", names, {});
    }
    // struct CallsContext
    {
        ObjectDefinition m[1];
        m[0] = obj_def_make("redirect_stdout", vtype_from_name("RedirectStdout"), false);
        Array<ObjectDefinition> members = array_make(m, countof(m));
        define_struct("CallsContext", members);
    }
    // global yov
    {
        define_global(obj_def_make("yov", vtype_from_name("YovInfo"), false));
    }
    // global context
    {
        define_global(obj_def_make("context", vtype_from_name("Context"), false));
    }
    // global os
    {
        define_global(obj_def_make("os", vtype_from_name("OS"), false));
    }
    // global calls
    {
        define_global(obj_def_make("calls", vtype_from_name("CallsContext"), false));
    }
    // function typeof
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("object", vtype_from_name("Any"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_name("Type"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "typeof", parameters, returns);
    }
    // function print
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("object", vtype_from_name("Any"), false);
        parameters = array_make(p, countof(p));
        define_intrinsic_function({}, "print", parameters, returns);
    }
    // function println
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("object", vtype_from_name("Any"), false);
        parameters = array_make(p, countof(p));
        define_intrinsic_function({}, "println", parameters, returns);
    }
    // function exit
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("exit_code", vtype_from_name("Int"), false);
        parameters = array_make(p, countof(p));
        define_intrinsic_function({}, "exit", parameters, returns);
    }
    // function set_cd
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("cd", vtype_from_name("String"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "set_cd", parameters, returns);
    }
    // function assert
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("result", vtype_from_name("Bool"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "assert", parameters, returns);
    }
    // function failed
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("message", vtype_from_name("String"), false);
        p[1] = obj_def_make("exit_code", vtype_from_name("Int"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "failed", parameters, returns);
    }
    // function env
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("name", vtype_from_name("String"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[2];
        r[0] = obj_def_make("value", vtype_from_name("String"), false);
        r[1] = obj_def_make("result", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "env", parameters, returns);
    }
    // function env_path
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("name", vtype_from_name("String"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[2];
        r[0] = obj_def_make("value", vtype_from_name("String"), false);
        r[1] = obj_def_make("result", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "env_path", parameters, returns);
    }
    // function env_path_array
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("name", vtype_from_name("String"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[2];
        r[0] = obj_def_make("value", vtype_from_dimension(vtype_from_name("String"), 1), false);
        r[1] = obj_def_make("result", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "env_path_array", parameters, returns);
    }
    // struct CallOutput
    {
        ObjectDefinition m[1];
        m[0] = obj_def_make("stdout", vtype_from_name("String"), false);
        Array<ObjectDefinition> members = array_make(m, countof(m));
        define_struct("CallOutput", members);
    }
    // function call
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("command", vtype_from_name("String"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[2];
        r[0] = obj_def_make("out", vtype_from_name("CallOutput"), false);
        r[1] = obj_def_make("result", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "call", parameters, returns);
    }
    // function call_exe
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("path", vtype_from_name("String"), false);
        p[1] = obj_def_make("arguments", vtype_from_name("String"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[2];
        r[0] = obj_def_make("out", vtype_from_name("CallOutput"), false);
        r[1] = obj_def_make("result", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "call_exe", parameters, returns);
    }
    // function call_script
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[3];
        p[0] = obj_def_make("path", vtype_from_name("String"), false);
        p[1] = obj_def_make("arguments", vtype_from_name("String"), false);
        p[2] = obj_def_make("yov_arguments", vtype_from_name("String"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[2];
        r[0] = obj_def_make("out", vtype_from_name("CallOutput"), false);
        r[1] = obj_def_make("result", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "call_script", parameters, returns);
    }
    // function path_resolve
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("path", vtype_from_name("String"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_name("String"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "path_resolve", parameters, returns);
    }
    // function str_get_codepoint
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("str", vtype_from_name("String"), false);
        p[1] = obj_def_make("cursor", vtype_from_name("Int"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[2];
        r[0] = obj_def_make("codepoint", vtype_from_name("Int"), false);
        r[1] = obj_def_make("next_cursor", vtype_from_name("Int"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "str_get_codepoint", parameters, returns);
    }
    // function str_split
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("str", vtype_from_name("String"), false);
        p[1] = obj_def_make("separator", vtype_from_name("String"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_dimension(vtype_from_name("String"), 1), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "str_split", parameters, returns);
    }
    // function yov_require
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("major", vtype_from_name("Int"), false);
        p[1] = obj_def_make("minor", vtype_from_name("Int"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "yov_require", parameters, returns);
    }
    // function yov_require_min
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("major", vtype_from_name("Int"), false);
        p[1] = obj_def_make("minor", vtype_from_name("Int"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "yov_require_min", parameters, returns);
    }
    // function yov_require_max
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("major", vtype_from_name("Int"), false);
        p[1] = obj_def_make("minor", vtype_from_name("Int"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "yov_require_max", parameters, returns);
    }
    // function ask_yesno
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("text", vtype_from_name("String"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_name("Bool"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "ask_yesno", parameters, returns);
    }
    // enum CopyMode
    {
        String n[2];
        n[0] = "NoOverride";
        n[1] = "Override";
        Array<String> names = array_make(n, countof(n));
        define_enum("CopyMode", names, {});
    }
    // struct FileInfo
    {
        ObjectDefinition m[2];
        m[0] = obj_def_make("path", vtype_from_name("String"), false);
        m[1] = obj_def_make("is_directory", vtype_from_name("Bool"), false);
        Array<ObjectDefinition> members = array_make(m, countof(m));
        define_struct("FileInfo", members);
    }
    // function exists
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("path", vtype_from_name("String"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_name("Bool"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "exists", parameters, returns);
    }
    // function create_directory
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("path", vtype_from_name("String"), false);
        p[1] = obj_def_make("recursive", vtype_from_name("Bool"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "create_directory", parameters, returns);
    }
    // function delete_directory
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("path", vtype_from_name("String"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "delete_directory", parameters, returns);
    }
    // function copy_directory
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("dst", vtype_from_name("String"), false);
        p[1] = obj_def_make("src", vtype_from_name("String"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "copy_directory", parameters, returns);
    }
    // function move_directory
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("dst", vtype_from_name("String"), false);
        p[1] = obj_def_make("src", vtype_from_name("String"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "move_directory", parameters, returns);
    }
    // function copy_file
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[3];
        p[0] = obj_def_make("dst", vtype_from_name("String"), false);
        p[1] = obj_def_make("src", vtype_from_name("String"), false);
        p[2] = obj_def_make("mode", vtype_from_name("CopyMode"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "copy_file", parameters, returns);
    }
    // function move_file
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("dst", vtype_from_name("String"), false);
        p[1] = obj_def_make("src", vtype_from_name("String"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "move_file", parameters, returns);
    }
    // function delete_file
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("path", vtype_from_name("String"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "delete_file", parameters, returns);
    }
    // function read_entire_file
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("path", vtype_from_name("String"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[2];
        r[0] = obj_def_make("content", vtype_from_name("String"), false);
        r[1] = obj_def_make("result", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "read_entire_file", parameters, returns);
    }
    // function write_entire_file
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[2];
        p[0] = obj_def_make("path", vtype_from_name("String"), false);
        p[1] = obj_def_make("content", vtype_from_name("String"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "write_entire_file", parameters, returns);
    }
    // function file_get_info
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("path", vtype_from_name("String"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[2];
        r[0] = obj_def_make("info", vtype_from_name("FileInfo"), false);
        r[1] = obj_def_make("result", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "file_get_info", parameters, returns);
    }
    // function dir_get_files_info
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition p[1];
        p[0] = obj_def_make("path", vtype_from_name("String"), false);
        parameters = array_make(p, countof(p));
        ObjectDefinition r[2];
        r[0] = obj_def_make("infos", vtype_from_dimension(vtype_from_name("FileInfo"), 1), false);
        r[1] = obj_def_make("result", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "dir_get_files_info", parameters, returns);
    }
    // function msvc_import_env_x64
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "msvc_import_env_x64", parameters, returns);
    }
    // function msvc_import_env_x86
    {
        Array<ObjectDefinition> parameters{};
        Array<ObjectDefinition> returns{};
        ObjectDefinition r[1];
        r[0] = obj_def_make("return", vtype_from_name("Result"), false);
        returns = array_make(r, countof(r));
        define_intrinsic_function({}, "msvc_import_env_x86", parameters, returns);
    }
}
