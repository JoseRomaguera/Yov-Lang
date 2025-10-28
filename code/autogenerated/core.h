#pragma once

void intrinsic__typeof(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__print(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__println(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__exit(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__set_cd(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__assert(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__failed(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__thread_sleep(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__env(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__env_path(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__env_path_array(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__console_write(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__console_set_cursor(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__console_get_cursor(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__console_clear(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__call(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__call_exe(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__call_script(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__path_resolve(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__str_get_codepoint(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__str_split(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__json_route(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__yov_require(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__yov_require_min(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__yov_require_max(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__yov_parse(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__ask_yesno(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__exists(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__create_directory(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__delete_directory(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__copy_directory(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__move_directory(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__copy_file(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__move_file(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__delete_file(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__read_entire_file(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__write_entire_file(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__file_get_info(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__dir_get_files_info(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__msvc_import_env_x64(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);
void intrinsic__msvc_import_env_x86(Interpreter* inter, Array<Reference> params, Array<Reference> returns, CodeLocation code);

void define_core()
{
	// OSKind :: enum { Windows, Linux }
	{
		String names_[2];
		names_[0] = "Windows";
		names_[1] = "Linux";
		Array<String> names = array_make(names_, countof(names_));
		i64 values_[2];
		values_[0] = 0;
		values_[1] = 1;
		Array<i64> values = array_make(values_, countof(values_));
		define_enum("OSKind", names, values);
	}
	// RedirectStdout :: enum { Console, Ignore, Script, ImportEnv }
	{
		String names_[4];
		names_[0] = "Console";
		names_[1] = "Ignore";
		names_[2] = "Script";
		names_[3] = "ImportEnv";
		Array<String> names = array_make(names_, countof(names_));
		i64 values_[4];
		values_[0] = 0;
		values_[1] = 1;
		values_[2] = 2;
		values_[3] = 3;
		Array<i64> values = array_make(values_, countof(values_));
		define_enum("RedirectStdout", names, values);
	}
	// CopyMode :: enum { NoOverride, Override }
	{
		String names_[2];
		names_[0] = "NoOverride";
		names_[1] = "Override";
		Array<String> names = array_make(names_, countof(names_));
		i64 values_[2];
		values_[0] = 0;
		values_[1] = 1;
		Array<i64> values = array_make(values_, countof(values_));
		define_enum("CopyMode", names, values);
	}
	// Result :: struct { failed: Bool, message: String, code: Int }
	{
		ObjectDefinition members_[3];
		members_[0] = obj_def_make("failed", vtype_from_name("Bool"), false, {});
		members_[1] = obj_def_make("message", vtype_from_name("String"), false, {});
		members_[2] = obj_def_make("code", vtype_from_name("Int"), false, {});
		Array<ObjectDefinition> members = array_make(members_, countof(members_));
		VariableType* vtype = vtype_define_struct("Result");
		vtype_init_struct(vtype, members);
	}
	// YovInfo :: struct { path: String, version: String, major: Int, minor: Int, revision: Int }
	{
		ObjectDefinition members_[5];
		members_[0] = obj_def_make("path", vtype_from_name("String"), false, {});
		members_[1] = obj_def_make("version", vtype_from_name("String"), false, {});
		members_[2] = obj_def_make("major", vtype_from_name("Int"), false, {});
		members_[3] = obj_def_make("minor", vtype_from_name("Int"), false, {});
		members_[4] = obj_def_make("revision", vtype_from_name("Int"), false, {});
		Array<ObjectDefinition> members = array_make(members_, countof(members_));
		VariableType* vtype = vtype_define_struct("YovInfo");
		vtype_init_struct(vtype, members);
	}
	// Type :: struct { ID: Int, name: String }
	{
		ObjectDefinition members_[2];
		members_[0] = obj_def_make("ID", vtype_from_name("Int"), false, {});
		members_[1] = obj_def_make("name", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> members = array_make(members_, countof(members_));
		VariableType* vtype = vtype_define_struct("Type");
		vtype_init_struct(vtype, members);
	}
	// OS :: struct { kind: OSKind }
	{
		ObjectDefinition members_[1];
		members_[0] = obj_def_make("kind", vtype_from_name("OSKind"), false, {});
		Array<ObjectDefinition> members = array_make(members_, countof(members_));
		VariableType* vtype = vtype_define_struct("OS");
		vtype_init_struct(vtype, members);
	}
	// CallsContext :: struct { redirect_stdout: RedirectStdout }
	{
		ObjectDefinition members_[1];
		members_[0] = obj_def_make("redirect_stdout", vtype_from_name("RedirectStdout"), false, {});
		Array<ObjectDefinition> members = array_make(members_, countof(members_));
		VariableType* vtype = vtype_define_struct("CallsContext");
		vtype_init_struct(vtype, members);
	}
	// CallOutput :: struct { stdout: String }
	{
		ObjectDefinition members_[1];
		members_[0] = obj_def_make("stdout", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> members = array_make(members_, countof(members_));
		VariableType* vtype = vtype_define_struct("CallOutput");
		vtype_init_struct(vtype, members);
	}
	// EnumDefinition :: struct { identifier: String, elements: String[], values: Int[] }
	{
		ObjectDefinition members_[3];
		members_[0] = obj_def_make("identifier", vtype_from_name("String"), false, {});
		members_[1] = obj_def_make("elements", vtype_from_name("String[]"), false, {});
		members_[2] = obj_def_make("values", vtype_from_name("Int[]"), false, {});
		Array<ObjectDefinition> members = array_make(members_, countof(members_));
		VariableType* vtype = vtype_define_struct("EnumDefinition");
		vtype_init_struct(vtype, members);
	}
	// FileInfo :: struct { path: String, is_directory: Bool }
	{
		ObjectDefinition members_[2];
		members_[0] = obj_def_make("path", vtype_from_name("String"), false, {});
		members_[1] = obj_def_make("is_directory", vtype_from_name("Bool"), false, {});
		Array<ObjectDefinition> members = array_make(members_, countof(members_));
		VariableType* vtype = vtype_define_struct("FileInfo");
		vtype_init_struct(vtype, members);
	}
	// Context :: struct { cd: String, script_dir: String, caller_dir: String, args: String[], types: Type[] }
	{
		ObjectDefinition members_[5];
		members_[0] = obj_def_make("cd", vtype_from_name("String"), false, {});
		members_[1] = obj_def_make("script_dir", vtype_from_name("String"), false, {});
		members_[2] = obj_def_make("caller_dir", vtype_from_name("String"), false, {});
		members_[3] = obj_def_make("args", vtype_from_name("String[]"), false, {});
		members_[4] = obj_def_make("types", vtype_from_name("Type[]"), false, {});
		Array<ObjectDefinition> members = array_make(members_, countof(members_));
		VariableType* vtype = vtype_define_struct("Context");
		vtype_init_struct(vtype, members);
	}
	// ObjectDefinition :: struct { identifier: String, type: Type, is_constant: Bool }
	{
		ObjectDefinition members_[3];
		members_[0] = obj_def_make("identifier", vtype_from_name("String"), false, {});
		members_[1] = obj_def_make("type", vtype_from_name("Type"), false, {});
		members_[2] = obj_def_make("is_constant", vtype_from_name("Bool"), false, {});
		Array<ObjectDefinition> members = array_make(members_, countof(members_));
		VariableType* vtype = vtype_define_struct("ObjectDefinition");
		vtype_init_struct(vtype, members);
	}
	// FunctionDefinition :: struct { identifier: String, parameters: ObjectDefinition[], returns: ObjectDefinition[] }
	{
		ObjectDefinition members_[3];
		members_[0] = obj_def_make("identifier", vtype_from_name("String"), false, {});
		members_[1] = obj_def_make("parameters", vtype_from_name("ObjectDefinition[]"), false, {});
		members_[2] = obj_def_make("returns", vtype_from_name("ObjectDefinition[]"), false, {});
		Array<ObjectDefinition> members = array_make(members_, countof(members_));
		VariableType* vtype = vtype_define_struct("FunctionDefinition");
		vtype_init_struct(vtype, members);
	}
	// StructDefinition :: struct { identifier: String, members: ObjectDefinition[] }
	{
		ObjectDefinition members_[2];
		members_[0] = obj_def_make("identifier", vtype_from_name("String"), false, {});
		members_[1] = obj_def_make("members", vtype_from_name("ObjectDefinition[]"), false, {});
		Array<ObjectDefinition> members = array_make(members_, countof(members_));
		VariableType* vtype = vtype_define_struct("StructDefinition");
		vtype_init_struct(vtype, members);
	}
	// YovParseOutput :: struct { scripts: String[], functions: FunctionDefinition[], structs: StructDefinition[], enums: EnumDefinition[], globals: ObjectDefinition[], reports: String[] }
	{
		ObjectDefinition members_[6];
		members_[0] = obj_def_make("scripts", vtype_from_name("String[]"), false, {});
		members_[1] = obj_def_make("functions", vtype_from_name("FunctionDefinition[]"), false, {});
		members_[2] = obj_def_make("structs", vtype_from_name("StructDefinition[]"), false, {});
		members_[3] = obj_def_make("enums", vtype_from_name("EnumDefinition[]"), false, {});
		members_[4] = obj_def_make("globals", vtype_from_name("ObjectDefinition[]"), false, {});
		members_[5] = obj_def_make("reports", vtype_from_name("String[]"), false, {});
		Array<ObjectDefinition> members = array_make(members_, countof(members_));
		VariableType* vtype = vtype_define_struct("YovParseOutput");
		vtype_init_struct(vtype, members);
	}
	// typeof :: (object: Any) -> Type;
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("object", vtype_from_name("Any"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("Type"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__typeof, "typeof", parameters, returns);
	}
	// print :: (object: Any);
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("object", vtype_from_name("Any"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		Array<ObjectDefinition> returns = {};
		define_intrinsic_function(NO_CODE, intrinsic__print, "print", parameters, returns);
	}
	// println :: (object: Any);
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("object", vtype_from_name("Any"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		Array<ObjectDefinition> returns = {};
		define_intrinsic_function(NO_CODE, intrinsic__println, "println", parameters, returns);
	}
	// exit :: (exit_code: Int);
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("exit_code", vtype_from_name("Int"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		Array<ObjectDefinition> returns = {};
		define_intrinsic_function(NO_CODE, intrinsic__exit, "exit", parameters, returns);
	}
	// set_cd :: (cd: String) -> Result;
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("cd", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__set_cd, "set_cd", parameters, returns);
	}
	// assert :: (result: Bool) -> Result;
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("result", vtype_from_name("Bool"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__assert, "assert", parameters, returns);
	}
	// failed :: (message: String, exit_code: Int) -> Result;
	{
		ObjectDefinition parameters_[2];
		parameters_[0] = obj_def_make("message", vtype_from_name("String"), false, {});
		parameters_[1] = obj_def_make("exit_code", vtype_from_name("Int"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__failed, "failed", parameters, returns);
	}
	// thread_sleep :: (millis: Int);
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("millis", vtype_from_name("Int"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		Array<ObjectDefinition> returns = {};
		define_intrinsic_function(NO_CODE, intrinsic__thread_sleep, "thread_sleep", parameters, returns);
	}
	// env :: (name: String) -> (value: String, result: Result);
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("name", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[2];
		returns_[0] = obj_def_make("value", vtype_from_name("String"), false, {});
		returns_[1] = obj_def_make("result", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__env, "env", parameters, returns);
	}
	// env_path :: (name: String) -> (value: String, result: Result);
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("name", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[2];
		returns_[0] = obj_def_make("value", vtype_from_name("String"), false, {});
		returns_[1] = obj_def_make("result", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__env_path, "env_path", parameters, returns);
	}
	// env_path_array :: (name: String) -> (value: String[], result: Result);
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("name", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[2];
		returns_[0] = obj_def_make("value", vtype_from_name("String[]"), false, {});
		returns_[1] = obj_def_make("result", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__env_path_array, "env_path_array", parameters, returns);
	}
	// console_write :: (value: String);
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("value", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		Array<ObjectDefinition> returns = {};
		define_intrinsic_function(NO_CODE, intrinsic__console_write, "console_write", parameters, returns);
	}
	// console_set_cursor :: (x: Int, y: Int);
	{
		ObjectDefinition parameters_[2];
		parameters_[0] = obj_def_make("x", vtype_from_name("Int"), false, {});
		parameters_[1] = obj_def_make("y", vtype_from_name("Int"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		Array<ObjectDefinition> returns = {};
		define_intrinsic_function(NO_CODE, intrinsic__console_set_cursor, "console_set_cursor", parameters, returns);
	}
	// console_get_cursor :: () -> (x: Int, y: Int);
	{
		Array<ObjectDefinition> parameters = {};
		ObjectDefinition returns_[2];
		returns_[0] = obj_def_make("x", vtype_from_name("Int"), false, {});
		returns_[1] = obj_def_make("y", vtype_from_name("Int"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__console_get_cursor, "console_get_cursor", parameters, returns);
	}
	// console_clear :: ();
	{
		Array<ObjectDefinition> parameters = {};
		Array<ObjectDefinition> returns = {};
		define_intrinsic_function(NO_CODE, intrinsic__console_clear, "console_clear", parameters, returns);
	}
	// call :: (command: String) -> (out: CallOutput, result: Result);
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("command", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[2];
		returns_[0] = obj_def_make("out", vtype_from_name("CallOutput"), false, {});
		returns_[1] = obj_def_make("result", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__call, "call", parameters, returns);
	}
	// call_exe :: (path: String, arguments: String) -> (out: CallOutput, result: Result);
	{
		ObjectDefinition parameters_[2];
		parameters_[0] = obj_def_make("path", vtype_from_name("String"), false, {});
		parameters_[1] = obj_def_make("arguments", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[2];
		returns_[0] = obj_def_make("out", vtype_from_name("CallOutput"), false, {});
		returns_[1] = obj_def_make("result", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__call_exe, "call_exe", parameters, returns);
	}
	// call_script :: (path: String, arguments: String, yov_arguments: String) -> (out: CallOutput, result: Result);
	{
		ObjectDefinition parameters_[3];
		parameters_[0] = obj_def_make("path", vtype_from_name("String"), false, {});
		parameters_[1] = obj_def_make("arguments", vtype_from_name("String"), false, {});
		parameters_[2] = obj_def_make("yov_arguments", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[2];
		returns_[0] = obj_def_make("out", vtype_from_name("CallOutput"), false, {});
		returns_[1] = obj_def_make("result", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__call_script, "call_script", parameters, returns);
	}
	// path_resolve :: (path: String) -> String;
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("path", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__path_resolve, "path_resolve", parameters, returns);
	}
	// str_get_codepoint :: (str: String, cursor: Int) -> (codepoint: Int, next_cursor: Int);
	{
		ObjectDefinition parameters_[2];
		parameters_[0] = obj_def_make("str", vtype_from_name("String"), false, {});
		parameters_[1] = obj_def_make("cursor", vtype_from_name("Int"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[2];
		returns_[0] = obj_def_make("codepoint", vtype_from_name("Int"), false, {});
		returns_[1] = obj_def_make("next_cursor", vtype_from_name("Int"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__str_get_codepoint, "str_get_codepoint", parameters, returns);
	}
	// str_split :: (str: String, separator: String) -> String[];
	{
		ObjectDefinition parameters_[2];
		parameters_[0] = obj_def_make("str", vtype_from_name("String"), false, {});
		parameters_[1] = obj_def_make("separator", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("String[]"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__str_split, "str_split", parameters, returns);
	}
	// json_route :: (json: String, route: String) -> (out: String, res: Result);
	{
		ObjectDefinition parameters_[2];
		parameters_[0] = obj_def_make("json", vtype_from_name("String"), false, {});
		parameters_[1] = obj_def_make("route", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[2];
		returns_[0] = obj_def_make("out", vtype_from_name("String"), false, {});
		returns_[1] = obj_def_make("res", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__json_route, "json_route", parameters, returns);
	}
	// yov_require :: (major: Int, minor: Int) -> Result;
	{
		ObjectDefinition parameters_[2];
		parameters_[0] = obj_def_make("major", vtype_from_name("Int"), false, {});
		parameters_[1] = obj_def_make("minor", vtype_from_name("Int"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__yov_require, "yov_require", parameters, returns);
	}
	// yov_require_min :: (major: Int, minor: Int) -> Result;
	{
		ObjectDefinition parameters_[2];
		parameters_[0] = obj_def_make("major", vtype_from_name("Int"), false, {});
		parameters_[1] = obj_def_make("minor", vtype_from_name("Int"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__yov_require_min, "yov_require_min", parameters, returns);
	}
	// yov_require_max :: (major: Int, minor: Int) -> Result;
	{
		ObjectDefinition parameters_[2];
		parameters_[0] = obj_def_make("major", vtype_from_name("Int"), false, {});
		parameters_[1] = obj_def_make("minor", vtype_from_name("Int"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__yov_require_max, "yov_require_max", parameters, returns);
	}
	// yov_parse :: (path: String) -> YovParseOutput;
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("path", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("YovParseOutput"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__yov_parse, "yov_parse", parameters, returns);
	}
	// ask_yesno :: (text: String) -> Bool;
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("text", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("Bool"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__ask_yesno, "ask_yesno", parameters, returns);
	}
	// exists :: (path: String) -> Bool;
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("path", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("Bool"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__exists, "exists", parameters, returns);
	}
	// create_directory :: (path: String, recursive: Bool) -> Result;
	{
		ObjectDefinition parameters_[2];
		parameters_[0] = obj_def_make("path", vtype_from_name("String"), false, {});
		parameters_[1] = obj_def_make("recursive", vtype_from_name("Bool"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__create_directory, "create_directory", parameters, returns);
	}
	// delete_directory :: (path: String) -> Result;
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("path", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__delete_directory, "delete_directory", parameters, returns);
	}
	// copy_directory :: (dst: String, src: String) -> Result;
	{
		ObjectDefinition parameters_[2];
		parameters_[0] = obj_def_make("dst", vtype_from_name("String"), false, {});
		parameters_[1] = obj_def_make("src", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__copy_directory, "copy_directory", parameters, returns);
	}
	// move_directory :: (dst: String, src: String) -> Result;
	{
		ObjectDefinition parameters_[2];
		parameters_[0] = obj_def_make("dst", vtype_from_name("String"), false, {});
		parameters_[1] = obj_def_make("src", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__move_directory, "move_directory", parameters, returns);
	}
	// copy_file :: (dst: String, src: String, mode: CopyMode) -> Result;
	{
		ObjectDefinition parameters_[3];
		parameters_[0] = obj_def_make("dst", vtype_from_name("String"), false, {});
		parameters_[1] = obj_def_make("src", vtype_from_name("String"), false, {});
		parameters_[2] = obj_def_make("mode", vtype_from_name("CopyMode"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__copy_file, "copy_file", parameters, returns);
	}
	// move_file :: (dst: String, src: String) -> Result;
	{
		ObjectDefinition parameters_[2];
		parameters_[0] = obj_def_make("dst", vtype_from_name("String"), false, {});
		parameters_[1] = obj_def_make("src", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__move_file, "move_file", parameters, returns);
	}
	// delete_file :: (path: String) -> Result;
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("path", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__delete_file, "delete_file", parameters, returns);
	}
	// read_entire_file :: (path: String) -> (content: String, result: Result);
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("path", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[2];
		returns_[0] = obj_def_make("content", vtype_from_name("String"), false, {});
		returns_[1] = obj_def_make("result", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__read_entire_file, "read_entire_file", parameters, returns);
	}
	// write_entire_file :: (path: String, content: String) -> Result;
	{
		ObjectDefinition parameters_[2];
		parameters_[0] = obj_def_make("path", vtype_from_name("String"), false, {});
		parameters_[1] = obj_def_make("content", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__write_entire_file, "write_entire_file", parameters, returns);
	}
	// file_get_info :: (path: String) -> (info: FileInfo, result: Result);
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("path", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[2];
		returns_[0] = obj_def_make("info", vtype_from_name("FileInfo"), false, {});
		returns_[1] = obj_def_make("result", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__file_get_info, "file_get_info", parameters, returns);
	}
	// dir_get_files_info :: (path: String) -> (infos: FileInfo[], result: Result);
	{
		ObjectDefinition parameters_[1];
		parameters_[0] = obj_def_make("path", vtype_from_name("String"), false, {});
		Array<ObjectDefinition> parameters = array_make(parameters_, countof(parameters_));
		ObjectDefinition returns_[2];
		returns_[0] = obj_def_make("infos", vtype_from_name("FileInfo[]"), false, {});
		returns_[1] = obj_def_make("result", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__dir_get_files_info, "dir_get_files_info", parameters, returns);
	}
	// msvc_import_env_x64 :: () -> Result;
	{
		Array<ObjectDefinition> parameters = {};
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__msvc_import_env_x64, "msvc_import_env_x64", parameters, returns);
	}
	// msvc_import_env_x86 :: () -> Result;
	{
		Array<ObjectDefinition> parameters = {};
		ObjectDefinition returns_[1];
		returns_[0] = obj_def_make("return", vtype_from_name("Result"), false, {});
		Array<ObjectDefinition> returns = array_make(returns_, countof(returns_));
		define_intrinsic_function(NO_CODE, intrinsic__msvc_import_env_x86, "msvc_import_env_x86", parameters, returns);
	}
	// yov: YovInfo
	{
	ObjectDefinition def = obj_def_make("yov", vtype_from_name("YovInfo"), true, ir_generate_from_value(value_from_default(vtype_from_name("YovInfo"))));
		define_global(def);
	}
	// os: OS
	{
	ObjectDefinition def = obj_def_make("os", vtype_from_name("OS"), true, ir_generate_from_value(value_from_default(vtype_from_name("OS"))));
		define_global(def);
	}
	// context: Context
	{
	ObjectDefinition def = obj_def_make("context", vtype_from_name("Context"), false, ir_generate_from_value(value_from_default(vtype_from_name("Context"))));
		define_global(def);
	}
	// calls: CallsContext
	{
	ObjectDefinition def = obj_def_make("calls", vtype_from_name("CallsContext"), false, ir_generate_from_value(value_from_default(vtype_from_name("CallsContext"))));
		define_global(def);
	}
}
